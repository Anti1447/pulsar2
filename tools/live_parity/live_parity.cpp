// Offline TRAIN-path vs LIVE-path parity check for a Pulsar checkpoint (no RLBot, no Rocket League, CPU only).
//
// Why: the viewer/trainer play a checkpoint through EnvSet (full engine CarState, tickSkip 8, actionDelay 7), while
// the RLBot bot (src/RLBotClient.cpp) rebuilds each Player from the v5 GamePacket and runs its own tick bookkeeping.
// A checkpoint can look fine in one and broken in the other. This tool plays the SAME kickoffs (same seeds) through
// each path, mirror self-play, argmax actions (the live bot is deterministic), and reports what differs.
//
// Paths:
//   TRAIN       full engine state at every 8-tick boundary; 7 ticks on the old controls, then 1 on the new (EnvSet).
//   LIVE        what RLBotClient.cpp does today: per-tick update() with ctx.ticks/updateAction, Player built ONLY from
//               the fields ToPlayer() copies (pos/rot/vel/angVel, boost, isOnGround, hasJumped, hasDoubleJumped,
//               isDemoed). hasFlipped, airTimeSinceJump and the rest stay zero, as in the client.
//   LIVE_FIXED  LIVE plus the flip-state fields the v5 packet does carry but the client ignores
//               (has_dodged -> hasFlipped, dodge_timeout -> airTimeSinceJump).
//
// The packet is SIMULATED from the engine, so this checks the client's logic and the fields it drops, not RLBot's
// own timing jitter or its exact field semantics. The same BallPredictor feeds all three paths so only the player
// state and the timing differ.
//
// Usage (repo root):  ./build/LivePathParity <checkpoint dir> [games=40] [seed=20261115] [ballpred=1|0]
#include <GigaLearnCPP/Util/InferUnit.h>
#include <RLGymCPP/ObsBuilders/AdvancedObsPadded.h>
#include <RLGymCPP/ActionParsers/DefaultAction.h>
#include <RLGymCPP/Gamestates/GameState.h>
#include <RLGymCPP/Gamestates/BallPredictor.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

using namespace RLGC;
using namespace GGL;

static constexpr int MAX_PLAYERS_PER_TEAM = 3;
static constexpr int TICK_SKIP = 8, ACTION_DELAY = 7;
static constexpr int GAME_TICKS = 120 * 8; // 8 s of kickoff

enum class Path { TRAIN, LIVE, LIVE_FIXED };
static const char* PathName(Path p) { return p == Path::TRAIN ? "TRAIN" : p == Path::LIVE ? "LIVE" : "LIVE_FIXED"; }

// What the live client's ToPlayer() keeps of an engine player.
static Player AsPacketPlayer(const Player& e, bool withFlipFields) {
	Player p = {};
	static_cast<PhysState&>(p) = static_cast<const PhysState&>(e);
	p.carId = e.carId; p.team = e.team; p.index = e.index;
	p.boost = e.boost;
	p.isOnGround = e.isOnGround;
	p.hasJumped = e.hasJumped;
	p.hasDoubleJumped = e.hasDoubleJumped;
	p.isDemoed = e.isDemoed;
	if (withFlipFields) {
		p.hasFlipped = e.hasFlipped;                 // v5 has_dodged
		p.airTimeSinceJump = e.airTimeSinceJump;     // v5 dodge_timeout (= DOUBLEJUMP_MAX_DELAY - airTime while it runs)
	}
	return p;
}

struct DecisionRow { float t; Action a; bool trueFlip, seenFlip, hasFlipped; };

struct CarLog {
	std::vector<DecisionRow> rows;
	int dodgeStep = -1;           // first decision step on which the engine's hasFlipped is true
	int pitchDownAfterDodge = 0;  // decision steps with pitch <= -0.99 at/after the dodge
	int flipFlagMismatch = 0;     // decision steps where the obs flag HasFlipOrJump differs from the engine's
	int jumpAfterDodge = 0;       // decision steps pressing jump at/after the dodge (engine says no flip left)
	int steps = 0;
};

struct GameResult {
	float firstTouchT = -1; int firstTouchCar = -1;
	float ballY3s = 0; // ball y at 3 s (blue attacks +y), mirror play: 0 = a contested/neutral kickoff
	CarLog car[2];
};

static GameResult PlayKickoff(InferUnit& iu, AdvancedObsPadded* obsBuilder, int ballPredSlices, Path path, unsigned seed) {
	Arena* arena = Arena::Create(GameMode::SOCCAR);
	arena->AddCar(Team::BLUE);
	arena->AddCar(Team::ORANGE);
	arena->ResetToRandomKickoff((int)seed);
	for (Car* c : arena->_cars) c->controls = {};

	BallPredictor predictor(GameMode::SOCCAR, std::max(1, ballPredSlices) * AdvancedObsPadded::BALL_PRED_SLICE_TICKS + 1);
	auto feedBallPred = [&](const GameState& gs) {
		if (ballPredSlices <= 0) return;
		constexpr int S = AdvancedObsPadded::BALL_PRED_SLICE_TICKS;
		predictor.Update(gs.ball, arena->tickCount);
		std::vector<PhysState> slices;
		for (int i = 1; i <= ballPredSlices; i++) slices.push_back(predictor.pred[(size_t)i * S]);
		obsBuilder->SetExternalBallPred(slices);
	};

	GameResult res;
	Action lastAct[2] = {};
	Action pending[2] = {};           // TRAIN/LIVE: the chosen action waiting to be applied
	bool prevBallMoving = false;

	// ---- per-decision bookkeeping (both paths call this with the state the policy SAW and the engine's truth) ----
	auto record = [&](int ci, const GameState& seenGs, const GameState& trueGs, const Action& a) {
		const Player& seen = seenGs.players[ci];
		const Player& tru = trueGs.players[ci];
		CarLog& L = res.car[ci];
		DecisionRow r{ arena->tickCount / 120.f, a, tru.HasFlipOrJump(), seen.HasFlipOrJump(), tru.hasFlipped };
		L.rows.push_back(r);
		L.steps++;
		if (r.trueFlip != r.seenFlip) L.flipFlagMismatch++;
		if (tru.hasFlipped && L.dodgeStep < 0) L.dodgeStep = L.steps;
		if (L.dodgeStep >= 0) {
			if (a.pitch <= -0.99f) L.pitchDownAfterDodge++;
			if (a.jump) L.jumpAfterDodge++;
		}
	};

	auto trackTouch = [&]() {
		BallState b = arena->ball->GetState();
		bool moving = b.vel.Length() > 20.f;
		if (moving && !prevBallMoving && res.firstTouchT < 0) {
			res.firstTouchT = arena->tickCount / 120.f;
			float best = 1e9f;
			for (int i = 0; i < 2; i++) {
				float d = (arena->_cars[i]->GetState().pos - b.pos).Length();
				if (d < best) { best = d; res.firstTouchCar = i; }
			}
		}
		prevBallMoving = prevBallMoving || moving;
	};

	if (path == Path::TRAIN) {
		for (int step = 0; step < GAME_TICKS / TICK_SKIP; step++) {
			GameState gs(arena);
			gs.lastTickCount = arena->tickCount;
			for (int i = 0; i < 2; i++) gs.players[i].prevAction = lastAct[i];
			feedBallPred(gs);
			for (int i = 0; i < 2; i++) {
				pending[i] = iu.InferAction(gs.players[i], gs, true);
				record(i, gs, gs, pending[i]);
			}
			arena->Step(ACTION_DELAY);                       // old controls
			for (int i = 0; i < 2; i++) { arena->_cars[i]->controls = (CarControls)pending[i]; lastAct[i] = pending[i]; }
			arena->Step(TICK_SKIP - ACTION_DELAY);           // new controls
			trackTouch();
			if (arena->tickCount == 3 * 120) res.ballY3s = arena->ball->GetState().pos.y;
		}
	} else {
		// RLBotBot::update() verbatim, per car, one engine tick per packet.
		struct Ctx { bool updateAction = true; int ticks = -1; Action action = {}, controls = {}; } ctx[2];
		for (int tick = 0; tick < GAME_TICKS; tick++) {
			GameState truth(arena);
			truth.lastTickCount = arena->tickCount;
			GameState seen = truth;
			for (int i = 0; i < 2; i++) seen.players[i] = AsPacketPlayer(truth.players[i], path == Path::LIVE_FIXED);
			const int ticksElapsed = 1;
			for (int i = 0; i < 2; i++) {
				Ctx& c = ctx[i];
				c.ticks += ticksElapsed;
				seen.players[i].prevAction = c.controls;
				if (c.updateAction) {
					c.updateAction = false;
					feedBallPred(seen);
					c.action = iu.InferAction(seen.players[i], seen, true);
					record(i, seen, truth, c.action);
				}
				if (c.ticks >= (ACTION_DELAY - 1) || c.ticks == -1) c.controls = c.action;
				if (c.ticks >= TICK_SKIP || c.ticks == -1) { c.ticks = 0; c.updateAction = true; }
				arena->_cars[i]->controls = (CarControls)c.controls;
			}
			arena->Step(1);
			trackTouch();
			if (arena->tickCount == 3 * 120) res.ballY3s = arena->ball->GetState().pos.y;
		}
	}
	delete arena;
	return res;
}

static bool EnvFlag(const char* n, bool def) {
	const char* v = std::getenv(n);
	return v ? (std::string(v) == "1" || std::string(v) == "true") : def;
}

int main(int argc, char** argv) {
	std::cout << std::unitbuf;
	if (argc < 2) { std::cerr << "usage: LivePathParity <checkpoint dir> [games=40] [seed=20261115] [ballpred=1|0]\n"; return 2; }
	const std::string ckpt = argv[1];
	const int games = argc > 2 ? std::atoi(argv[2]) : 40;
	const unsigned seed0 = argc > 3 ? (unsigned)std::atoll(argv[3]) : 20261115u;
	const bool ballPred = argc > 4 ? std::atoi(argv[4]) != 0 : EnvFlag("GGL_BALLPRED", true);
	const int ballPredSlices = ballPred ? 8 : 0;

	bool inited = false;
	for (const char* d : { "collision_meshes", "build/collision_meshes", "../collision_meshes" })
		if (std::filesystem::exists(std::filesystem::path(d) / "soccar")) { RocketSim::Init(d); inited = true; break; }
	if (!inited) { std::cerr << "collision_meshes not found (run from the repo root or build/)\n"; return 2; }

	auto* obsBuilder = new AdvancedObsPadded(MAX_PLAYERS_PER_TEAM, /*shuffleSlots=*/true, ballPredSlices);
	auto* actionParser = new DefaultAction();
	int obsSize;
	{
		if (ballPredSlices > 0) obsBuilder->SetExternalBallPred(std::vector<PhysState>(ballPredSlices));
		GameState gs; Player a = {}; a.carId = 1; a.team = Team::BLUE; a.index = 0;
		Player b = {}; b.carId = 2; b.team = Team::ORANGE; b.index = 1;
		gs.players = { a, b };
		obsSize = (int)obsBuilder->BuildObs(gs.players[0], gs).size();
	}

	// MUST mirror src/RLBotMain.cpp (which mirrors ExampleMain.cpp).
	PartialModelConfig sh;
	sh.layerSizes = ballPred ? std::vector<int>{ 1152, 1152 } : std::vector<int>{ 1152, 1152, 1152 };
	sh.addResiduals = true;
	sh.activationType = ballPred ? ModelActivationType::GELU : ModelActivationType::LEAKY_RELU;
	sh.addLayerNorm = true;
	sh.addOutputLayer = false;
	PartialModelConfig pol;
	pol.layerSizes = ballPred ? std::vector<int>{ 1024, 1024, 1024, 1024 } : std::vector<int>{ 768, 768, 768 };
	pol.addResiduals = true;
	pol.activationType = sh.activationType;
	pol.addLayerNorm = true;
	InferUnit iu(obsBuilder, obsSize, actionParser, sh, pol, ckpt, /*useGPU=*/false);

	std::printf("LivePathParity: %s | obs %d | %d kickoffs x 2 cars, mirror, argmax | seed %u\n\n", ckpt.c_str(), obsSize, games, seed0);

	const Path paths[3] = { Path::TRAIN, Path::LIVE, Path::LIVE_FIXED };
	std::vector<GameResult> all[3];
	for (int p = 0; p < 3; p++)
		for (int g = 0; g < games; g++)
			all[p].push_back(PlayKickoff(iu, obsBuilder, ballPredSlices, paths[p], seed0 + g));

	std::printf("%-11s %9s %12s %10s %12s %13s %14s %12s\n", "path", "touch<=8s", "median touch", "no-touch", "|ball y|@3s", "flag mismatch", "pitch-1 post", "jump post");
	std::printf("%-11s %9s %12s %10s %12s %13s %14s %12s\n", "", "", "(s)", "", "(uu)", "(steps/game)", "(steps/game)", "(steps/game)");
	for (int p = 0; p < 3; p++) {
		std::vector<float> touchT; int touched = 0; double aby = 0, mism = 0, pitch = 0, jump = 0; int dodgers = 0;
		for (auto& r : all[p]) {
			if (r.firstTouchT >= 0) { touched++; touchT.push_back(r.firstTouchT); }
			aby += std::fabs(r.ballY3s);
			for (auto& c : r.car) { mism += c.flipFlagMismatch; if (c.dodgeStep >= 0) { dodgers++; pitch += c.pitchDownAfterDodge; jump += c.jumpAfterDodge; } }
		}
		std::sort(touchT.begin(), touchT.end());
		float med = touchT.empty() ? -1.f : touchT[touchT.size() / 2];
		std::printf("%-11s %6d/%-3d %12.2f %10d %12.0f %13.2f %14.2f %12.2f\n", PathName(paths[p]), touched, games, med, games - touched,
			aby / games, mism / (2.0 * games), dodgers ? pitch / dodgers : 0.0, dodgers ? jump / dodgers : 0.0);
	}

	// One car's decision trace on kickoff 0, so the symptom is visible: jump/pitch/boost per step and the two flip flags.
	for (int p = 0; p < 3; p++) {
		std::printf("\n%s, kickoff 0, blue car, first 28 decisions (t | jump pitch boost | engine HasFlipOrJump / seen by policy | hasFlipped)\n", PathName(paths[p]));
		const auto& rows = all[p][0].car[0].rows;
		for (int i = 0; i < (int)rows.size() && i < 28; i++) {
			const auto& r = rows[i];
			std::printf("  %4.2f | %d %+.0f %d | %d / %d | %d%s\n", r.t, (int)(r.a.jump != 0), r.a.pitch, (int)(r.a.boost != 0),
				(int)r.trueFlip, (int)r.seenFlip, (int)r.hasFlipped, r.trueFlip != r.seenFlip ? "   <-- policy sees the wrong flip flag" : "");
		}
	}
	return 0;
}
