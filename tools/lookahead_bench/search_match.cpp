// docs/GCO_LOOKAHEAD_TEST.md, Phase 1/2a harness. Offline, CPU only: never touches the trainer or the GPU.
//
// Modes
//   selftest  fidelity check: snapshot a live game, record the next H raw-play decisions, restore the snapshot into a
//             DIFFERENT arena (the rollout arena), replay the same decisions through the same split-step protocol,
//             and require identical obs at every step and an identical end state.
//   calib     raw-vs-raw games (sides alternate). Reports the blue win share (sanity, expect ~0.5) and fits
//             G ~ a*V + b between the critic's value of a state and the realised discounted outcome, used as the
//             leaf scale for search.
//   search    the SEARCH side (alternating blue/orange) vs the RAW side, same checkpoint: at every decision, the K
//             likeliest legal first actions are each rolled out H steps on the real sim (snapshot/restore into
//             rollout arenas), the opponent modeled as the same raw policy, with common random numbers across
//             candidates; the best-scoring first action is played.
//
// Game: kickoff, ends at the first goal or after CAP_SECONDS (a draw). Both sides sample from the policy (temperature 1).
//
// Usage (from build/):  taskset -c 0-3 nice -n 19 ./LookaheadSearch <ckpt dir> <selftest|calib|argmax|search> [games=200]
//                       [slots=16] [H=30] [K=8] [M=1] [leaf=1] [a=1] [b=0] [seed=12345] [pick=0 best|1 worst|2 random] [oppArgmax=0]
//
// Frozen external opponent (docs/GCO_LOOKAHEAD_TEST.md section 14): set EXT_BOT=galactus|chronos|dice|nexto. In modes `raw` (the
// side samples the policy, no search) and `search`, the OTHER side in the real game is then that bot (argmax, as shipped), while the
// rollouts still model it as our own policy. Run from build/ (bot weights at ../rlbot-run/).
#include <GigaLearnCPP/Util/Models.h>
#include <GigaLearnCPP/PPO/PPOLearner.h>
#include <GigaLearnCPP/GGLBotOpponent.h>
#include <GigaLearnCPP/NextoOpponent.h>

#include <RLGymCPP/EnvSet/EnvSet.h>
#include <RLGymCPP/ObsBuilders/AdvancedObsPadded.h>
#include <RLGymCPP/StateSetters/KickoffState.h>
#include <RLGymCPP/ActionParsers/DefaultAction.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <iostream>

using namespace RLGC;
using namespace GGL;

namespace {
	constexpr int TICK_SKIP = 8, ACTION_DELAY = 7;
	constexpr float GAMMA = 0.9969f;      // TRAIN_GAMMA
	constexpr float CAP_SECONDS = 30.f;
	constexpr int CAP_STEPS = (int)(CAP_SECONDS * 120.f / TICK_SKIP);
	const int NUM_ACTIONS = DefaultAction().GetActionAmount();

	double NowS() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

	uint64_t Mix(uint64_t x) {
		x += 0x9E3779B97F4A7C15ULL;
		x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
		x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
		return x ^ (x >> 31);
	}
	// Deterministic uniform in (0,1) from (seed, a, b, c, d): the common random numbers.
	float Uni(uint64_t seed, uint64_t a, uint64_t b, uint64_t c, uint64_t d) {
		uint64_t h = Mix(seed ^ Mix(a + 0x1000 * Mix(b + 0x1000 * Mix(c + 0x1000 * Mix(d)))));
		return ((float)((h >> 40) & 0xFFFFFF) + 0.5f) / 16777216.f;
	}

	struct Nets {
		ModelSet models;
		void Build(int obsSize, const std::filesystem::path& ckpt) {
			// MUST mirror ExampleMain.cpp for the 6.1 lineage: GELU, LayerNorm, residual on.
			auto mk = [](std::vector<int> sizes) {
				PartialModelConfig c; c.layerSizes = sizes; c.addResiduals = true;
				c.activationType = ModelActivationType::GELU; c.addLayerNorm = true; return c;
			};
			PartialModelConfig sh = mk({ 1152, 1152 }); sh.addOutputLayer = false;
			PartialModelConfig pol = mk({ 1024, 1024, 1024, 1024 });
			PartialModelConfig crit = mk({ 1280, 1280 });
			PartialModelConfig ctrunk = mk({ 1280, 1280 }); ctrunk.addOutputLayer = false;
			PPOLearner::MakeModels(true, obsSize, NUM_ACTIONS, sh, pol, crit, ctrunk, torch::Device(torch::kCPU), models);
			models.Load(ckpt, false, false);
		}
		torch::Tensor Probs(torch::Tensor obs, torch::Tensor masks) {
			torch::NoGradGuard ng;
			return PPOLearner::InferPolicyProbsFromModels(models, obs, masks, 1.f, false);
		}
		torch::Tensor Value(torch::Tensor obs) {
			torch::NoGradGuard ng;
			torch::Tensor h = models["shared_head"]->Forward(obs, false);
			h = models["critic_trunk"]->Forward(h, false);
			return models["critic"]->Forward(h, false).flatten().to(torch::kFloat32).contiguous();
		}
	};

	// Inverse-CDF sampling with explicit uniforms (so the same u gives the same action for the same probabilities).
	torch::Tensor SampleFromProbs(const torch::Tensor& probs, const std::vector<float>& u) {
		torch::Tensor tu = torch::from_blob((void*)u.data(), { (int64_t)u.size(), 1 }, torch::kFloat32);
		torch::Tensor cdf = probs.cumsum(1);
		return (cdf < tu).sum(1).clamp_(0, NUM_ACTIONS - 1).to(torch::kInt64);
	}

	std::unique_ptr<EnvSet> MakeEnv(int numArenas) {
		EnvSetConfig cfg = {};
		cfg.numArenas = numArenas;
		cfg.tickSkip = TICK_SKIP;
		cfg.actionDelay = ACTION_DELAY;
		cfg.saveRewards = false;
		cfg.envCreateFn = [](int) {
			auto arena = Arena::Create(GameMode::SOCCAR);
			arena->AddCar(Team::BLUE); arena->AddCar(Team::ORANGE);
			EnvCreateResult r = {};
			r.arena = arena;
			r.actionParser = new DefaultAction();
			// shuffleSlots OFF: the viewer note in AdvancedObsPadded.h (the shuffle is clock-seeded; restore-and-replay needs determinism)
			r.obsBuilder = new AdvancedObsPadded(3, /*shuffleSlots=*/false, 8);
			r.stateSetter = new KickoffState();
			return r;
		};
		return std::make_unique<EnvSet>(cfg);
	}

	torch::Tensor ObsTensor(EnvSet& e) { return torch::from_blob(e.state.obs.data.data(), { (int64_t)e.state.numPlayers, (int64_t)e.obsSize }, torch::kFloat32); }
	torch::Tensor MaskTensor(EnvSet& e) { return torch::from_blob(e.state.actionMasks.data.data(), { (int64_t)e.state.numPlayers, (int64_t)NUM_ACTIONS }, torch::kUInt8); }

	// Everything needed to put another arena (and its EnvSet slot) into this arena's current state.
	struct Snap {
		RsfArenaSnap* engine = nullptr;
		std::vector<CarControls> controls;
		std::vector<BallHitInfo> hit;
		GameState gs, prevGs;
		~Snap() { if (engine) rsf_arena_snapshot_free(engine); }
	};
	void Capture(EnvSet& real, int gi, Snap& s) {
		if (s.engine) rsf_arena_snapshot_free(s.engine);
		Arena* a = real.arenas[gi];
		s.engine = rsf_arena_snapshot(a->_rs);
		s.controls.clear(); s.hit.clear();
		for (Car* c : a->_cars) { s.controls.push_back(c->controls); s.hit.push_back(c->_ballHitInfo); }
		s.gs = real.state.gameStates[gi];
		s.prevGs = real.state.prevGameStates[gi];
	}
	void Load(EnvSet& roll, int ri, const Snap& s) {
		Arena* a = roll.arenas[ri];
		rsf_arena_restore(a->_rs, s.engine);
		a->tickCount = rsf_arena_tick_count(a->_rs);
		int i = 0;
		for (Car* c : a->_cars) { c->controls = s.controls[i]; c->_ballHitInfo = s.hit[i]; i++; }
		roll.state.gameStates[ri] = s.gs;
		roll.state.gameStates[ri].lastArena = a;
		roll.state.gameStates[ri].userInfo = roll.userInfos[ri];
		roll.state.prevGameStates[ri] = s.prevGs;
		roll.state.terminals[ri] = 0;
	}

	int PlayerIdx(const GameState& gs, int team) { for (int i = 0; i < (int)gs.players.size(); i++) if ((int)gs.players[i].team == team) return i; return 0; }

	// +1 blue scored, -1 orange scored, 0 none (goal side from the ball's y, as ResetDiag does)
	int GoalSign(const GameState& gs) {
		if (!gs.goalScored) return 0;
		return RS_TEAM_FROM_Y(gs.ball.pos.y) == Team::ORANGE ? +1 : -1;
	}

	// ---------------------------------------------------------------- selftest
	int SelfTest(Nets& nets, int H) {
		auto real = MakeEnv(1); auto roll = MakeEnv(1);
		if (real->obsSize != 278) { printf("obs size %d != 278\n", real->obsSize); return 1; }
		int bad = 0;
		for (int trial = 0; trial < 6; trial++) {
			real->ResetArena(0);
			uint64_t seed = 100 + trial;
			int warm = 20 + 25 * trial;
			auto stepReal = [&](std::vector<int>* recordActs, int cyc) {
				torch::Tensor probs = nets.Probs(ObsTensor(*real), MaskTensor(*real));
				std::vector<float> u = { Uni(seed, cyc, 0, 0, 0), Uni(seed, cyc, 1, 0, 0) };
				torch::Tensor act = SampleFromProbs(probs, u);
				IList acts(2); for (int i = 0; i < 2; i++) acts[i] = (int)act[i].item<int64_t>();
				if (recordActs) *recordActs = acts;
				real->StepFirstHalf(false);
				real->StepSecondHalf(acts, false);
			};
			for (int c = 0; c < warm; c++) stepReal(nullptr, c);
			Snap snap; Capture(*real, 0, snap);
			std::vector<std::vector<int>> recorded;
			std::vector<std::vector<float>> realObs;
			for (int h = 0; h < H; h++) {
				std::vector<int> acts; stepReal(&acts, 1000 + h);
				recorded.push_back(acts);
				realObs.emplace_back(real->state.obs.data.begin(), real->state.obs.data.begin() + 2 * real->obsSize);
			}
			Load(*roll, 0, snap);
			double maxObs = 0; int firstBad = -1;
			for (int h = 0; h < H; h++) {
				roll->StepFirstHalf(false);
				IList acts = { recorded[h][0], recorded[h][1] };
				roll->StepSecondHalf(acts, false);
				for (int j = 0; j < 2 * real->obsSize; j++) {
					double d = fabs((double)roll->state.obs.data[j] - (double)realObs[h][j]);
					if (d > maxObs) maxObs = d;
					if (d > 0 && firstBad < 0) firstBad = h;
				}
			}
			const GameState& a = real->state.gameStates[0]; const GameState& b = roll->state.gameStates[0];
			bool endSame = a.ball.pos.x == b.ball.pos.x && a.ball.pos.y == b.ball.pos.y && a.ball.pos.z == b.ball.pos.z && a.ball.vel.x == b.ball.vel.x;
			for (int i = 0; i < 2; i++) endSame &= a.players[i].pos.x == b.players[i].pos.x && a.players[i].vel.z == b.players[i].vel.z && a.players[i].boost == b.players[i].boost;
			printf("  trial %d (warm %3d cycles, replay %d): max |obs diff| = %.3g%s, end state %s\n", trial, warm, H, maxObs,
				firstBad >= 0 ? (std::string(" (first differing step ") + std::to_string(firstBad) + ")").c_str() : "", endSame ? "IDENTICAL" : "DIFFERS");
			// Pass = end state bit-identical and obs within 1e-4 (observed once at 2e-6 on step 0 of one trial: float round-off in
			// the obs path, e.g. a ball prediction rebuilt from the restored ball vs one carried incrementally; physics is exact).
			if (maxObs > 1e-4 || !endSame) bad++;
		}
		printf("selftest: %s\n", bad ? "FAIL" : "PASS");
		return bad ? 1 : 0;
	}
}

namespace {
	// Guard for ORACLE: snapshot a real game against the bot, replay our recorded actions in a rollout arena while the BOT is
	// asked for its action on the ROLLOUT arena's own state, and require the same bot action at every step plus an identical end
	// state. Run with GGL_BOT_SHUFFLE=0 (the shuffle draws from a random engine, so replay is not deterministic with it on).
	int OracleTest(Nets& nets, ExternalOpponent& bot, int H) {
		if (!(std::getenv("GGL_BOT_SHUFFLE") && std::string(std::getenv("GGL_BOT_SHUFFLE")) == "0"))
			printf("WARNING: GGL_BOT_SHUFFLE=0 is not set; mismatches below may be the slot shuffle, not a bug\n");
		auto real = MakeEnv(1); auto roll = MakeEnv(1);
		bot.BeginServe(2);
		int bad = 0;
		for (int trial = 0; trial < 6; trial++) {
			real->ResetArena(0);
			std::vector<uint8_t> fresh = { 1 };
			uint64_t seed = 300 + trial;
			int warm = 20 + 25 * trial;
			std::vector<bool> botMask = { false, true };
			auto stepReal = [&](int cyc, std::vector<int>* rec) {
				torch::Tensor probs = nets.Probs(ObsTensor(*real), MaskTensor(*real));
				std::vector<float> u = { Uni(seed, cyc, 0, 0, 0), Uni(seed, cyc, 1, 0, 0) };
				torch::Tensor act = SampleFromProbs(probs, u);
				IList acts(2); for (int i = 0; i < 2; i++) acts[i] = (int)act[i].item<int64_t>();
				real->StepFirstHalf(false);
				std::vector<int> ext(2, 0);
				bot.Act(real->state.gameStates, botMask, fresh, ext);
				fresh[0] = 0;
				acts[1] = ext[1];
				if (rec) *rec = acts;
				real->StepSecondHalf(acts, false);
			};
			for (int c = 0; c < warm; c++) stepReal(c, nullptr);
			Snap snap; Capture(*real, 0, snap);
			std::vector<std::vector<int>> recorded;
			for (int h = 0; h < H; h++) { std::vector<int> acts; stepReal(1000 + h, &acts); recorded.push_back(acts); }
			Load(*roll, 0, snap);
			std::vector<uint8_t> zero = { 0 };
			int mism = 0;
			for (int h = 0; h < H; h++) {
				roll->StepFirstHalf(false);
				std::vector<int> ext(2, 0);
				bot.Act(roll->state.gameStates, botMask, zero, ext);
				if (ext[1] != recorded[h][1]) mism++;
				IList acts = { recorded[h][0], ext[1] };
				roll->StepSecondHalf(acts, false);
			}
			const GameState& a = real->state.gameStates[0]; const GameState& b = roll->state.gameStates[0];
			bool endSame = a.ball.pos.x == b.ball.pos.x && a.ball.pos.y == b.ball.pos.y && a.ball.pos.z == b.ball.pos.z;
			for (int i = 0; i < 2; i++) endSame &= a.players[i].pos.x == b.players[i].pos.x && a.players[i].boost == b.players[i].boost;
			printf("  trial %d (warm %3d, replay %d): bot-action mismatches %d/%d, end state %s\n", trial, warm, H, mism, H, endSame ? "IDENTICAL" : "DIFFERS");
			if (mism || !endSame) bad++;
		}
		printf("oracletest: %s\n", bad ? "FAIL" : "PASS");
		return bad ? 1 : 0;
	}
}

int main(int argc, char** argv) {
	std::cout << std::unitbuf;
	setvbuf(stdout, NULL, _IOLBF, 0); // progress lines must appear when redirected to a file
	if (argc < 3) { printf("usage: %s <ckpt dir> <selftest|calib|argmax|search> [games] [slots] [H] [K] [M] [leaf] [a] [b]\n", argv[0]); return 2; }
	const std::filesystem::path ckpt = argv[1];
	const std::string mode = argv[2];
	const int N = argc > 3 ? atoi(argv[3]) : 200;
	const int G = argc > 4 ? std::min(atoi(argv[4]), N) : std::min(16, N);
	const int H = argc > 5 ? atoi(argv[5]) : 30;
	const int K = argc > 6 ? atoi(argv[6]) : 8;
	const int M = argc > 7 ? atoi(argv[7]) : 1;
	const int leaf = argc > 8 ? atoi(argv[8]) : 1;
	const float calA = argc > 9 ? (float)atof(argv[9]) : 1.f;
	const float calB = argc > 10 ? (float)atof(argv[10]) : 0.f;
	const int R = K * M;
	// leak/integrity controls (docs/GCO_LOOKAHEAD_TEST.md section 13): pick 0 = best candidate (the test), 1 = WORST, 2 = RANDOM candidate;
	// oppArgmax 1 = the real opponent plays its argmax while the rollouts still model it as sampling.
	const int pick = argc > 12 ? atoi(argv[12]) : 0;
	const int oppArgmax = argc > 13 ? atoi(argv[13]) : 0;

	bool inited = false;
	for (const char* d : { "collision_meshes", "build/collision_meshes", "../collision_meshes" })
		if (std::filesystem::exists(std::filesystem::path(d) / "soccar")) { RocketSim::Init(d, true); inited = true; break; }
	if (!inited) { printf("collision_meshes not found\n"); return 2; }
	at::set_num_threads(4);

	Nets nets; nets.Build(278, ckpt);
	const bool oracle = std::getenv("ORACLE") && std::string(std::getenv("ORACLE")) == "1"; // rollouts use the BOT's policy for the opponent
	if (mode == "selftest") return SelfTest(nets, std::max(H, 10));

	const bool searching = mode == "search";
	const bool rawCtl = mode == "raw"; // control: the "search" side samples the raw policy (same as the other side, or vs EXT_BOT)
	const bool argmaxCtl = mode == "argmax"; // control: the "search" side plays the policy's deterministic argmax, the other side samples
	// Frozen external opponent
	std::shared_ptr<ExternalOpponent> bot;
	std::string botName = "none";
	if (const char* eb = std::getenv("EXT_BOT"); eb && eb[0]) {
		botName = eb;
		torch::Device cpu(torch::kCPU);
		if (botName == "nexto") bot = std::make_shared<NextoOpponent>("../rlbot-run/nexto/nexto-model.pt", cpu);
		else if (botName == "galactus") bot = std::make_shared<GGLBotOpponent>(GGLBotKind::GALACTUS_V2, "../rlbot-run/galactus_v2", cpu);
		else if (botName == "chronos") bot = std::make_shared<GGLBotOpponent>(GGLBotKind::CHRONOS_V2, "../rlbot-run/chronos_v2", cpu);
		else if (botName == "dice") bot = std::make_shared<GGLBotOpponent>(GGLBotKind::DICE, "../rlbot-run/dice", cpu);
		else { printf("unknown EXT_BOT %s\n", eb); return 2; }
		bot->temperature = 0.f; // argmax: the bot as shipped
	}
	if (mode == "oracletest") {
		if (!bot) { printf("oracletest needs EXT_BOT\n"); return 2; }
		return OracleTest(nets, *bot, std::max(H, 10));
	}
	if (oracle && !bot) { printf("ORACLE=1 needs EXT_BOT\n"); return 2; }
	auto real = MakeEnv(G);
	if (oracle) printf("ORACLE: rollout opponent = the real bot's policy (%s)\n", botName.c_str());
	if (bot) { bot->BeginServe(real->state.numPlayers); printf("opponent in the real game: %s (argmax)\n", bot->name.c_str()); }
	std::vector<uint8_t> botFresh(G, 1);
	std::unique_ptr<EnvSet> roll;
	if (searching) roll = MakeEnv(G * R);
	printf("mode %s: %d games, %d slots%s\n", mode.c_str(), N, G, searching ? "" : " (raw vs raw)");
	if (searching) printf("pick=%d oppArgmax=%d\n", pick, oppArgmax);
	if (searching) printf("search: H=%d steps (%.1f s), K=%d candidates, M=%d rollouts each, leaf=%d (a=%.4f b=%.4f)\n", H, H * TICK_SKIP / 120.f, K, M, leaf, calA, calB);

	struct Slot { int game = -1, searchTeam = 0, steps = 0; bool active = false; std::vector<std::array<float, 2>> v; };
	std::vector<Slot> slots(G);
	int started = 0, finished = 0;
	double blueScore = 0; int blueN = 0;
	double searchScore = 0; int sw = 0, sl = 0, sd = 0;
	double sumSteps = 0;
	// calibration accumulators for G ~ a*V + b
	double cn = 0, cV = 0, cG = 0, cVV = 0, cGG = 0, cVG = 0;
	std::vector<Snap> snaps(G);
	uint64_t seed = argc > 11 ? (uint64_t)atoll(argv[11]) : 12345;
	double tSearch = 0, tReal = 0, tLoad = 0, tRoll = 0, tInf = 0; long long decisions = 0;
	auto startGame = [&](int g) {
		if (started >= N) { slots[g].active = false; return; }
		slots[g].game = started++; slots[g].searchTeam = slots[g].game % 2; slots[g].steps = 0; slots[g].active = true; slots[g].v.clear();
		real->ResetArena(g);
	};
	for (int g = 0; g < G; g++) startGame(g);

	const double t0 = NowS();
	long long cycle = 0;
	while (finished < N) {
		cycle++;
		const int D = real->obsSize;
		torch::Tensor obsT = ObsTensor(*real), maskT = MaskTensor(*real);
		double ts = NowS();
		torch::Tensor probs = nets.Probs(obsT, maskT);
		std::vector<float> u(2 * G);
		for (int g = 0; g < G; g++) for (int i = 0; i < 2; i++) u[2 * g + i] = Uni(seed, cycle, 7, g, i);
		torch::Tensor rawAct = SampleFromProbs(probs, u);
		if (mode == "calib") {
			torch::Tensor v = nets.Value(obsT);
			for (int g = 0; g < G; g++) if (slots[g].active) slots[g].v.push_back({ v[2 * g].item<float>(), v[2 * g + 1].item<float>() });
		}
		tInf += NowS() - ts;

		IList actions(real->state.numPlayers, 0);
		for (int i = 0; i < real->state.numPlayers; i++) actions[i] = (int)rawAct[i].item<int64_t>();

		if (argmaxCtl) {
			torch::Tensor am = probs.argmax(1);
			for (int g = 0; g < G; g++) if (slots[g].active) actions[2 * g + PlayerIdx(real->state.gameStates[g], slots[g].searchTeam)] = (int)am[2 * g + PlayerIdx(real->state.gameStates[g], slots[g].searchTeam)].item<int64_t>();
		}
		if (searching && oppArgmax) {
			torch::Tensor am = probs.argmax(1);
			for (int g = 0; g < G; g++) if (slots[g].active) { int o = 1 - PlayerIdx(real->state.gameStates[g], slots[g].searchTeam); actions[2 * g + o] = (int)am[2 * g + o].item<int64_t>(); }
		}
		if (searching) {
			double tsearch = NowS();
			// candidates + snapshots
			std::vector<std::vector<int>> cand(G);
			std::vector<std::array<std::vector<int>, 1>> dummy;
			for (int g = 0; g < G; g++) {
				if (!slots[g].active) continue;
				const GameState& gs = real->state.gameStates[g];
				int ours = PlayerIdx(gs, slots[g].searchTeam);
				auto topk = probs[2 * g + ours].topk(K);
				torch::Tensor idx = std::get<1>(topk), val = std::get<0>(topk);
				for (int k = 0; k < K; k++) cand[g].push_back(val[k].item<float>() > 0 ? (int)idx[k].item<int64_t>() : (int)idx[0].item<int64_t>());
				Capture(*real, g, snaps[g]);
			}
			double tl = NowS();
			for (int g = 0; g < G; g++) if (slots[g].active) for (int r = 0; r < R; r++) Load(*roll, g * R + r, snaps[g]);
			tLoad += NowS() - tl;

			std::vector<int> scoredAt(G * R, -1), scoredSign(G * R, 0);
			// first-step opponent action: sampled from the real obs row with CRN over the M rollout copies
			std::vector<std::vector<int>> oppFirst(G, std::vector<int>(M, 0));
			for (int g = 0; g < G; g++) {
				if (!slots[g].active) continue;
				int opp = 1 - PlayerIdx(real->state.gameStates[g], slots[g].searchTeam);
				std::vector<float> uu(M); torch::Tensor row = probs[2 * g + opp].unsqueeze(0).expand({ M, NUM_ACTIONS }).contiguous();
				for (int m = 0; m < M; m++) uu[m] = Uni(seed, cycle, 11, g, m);
				torch::Tensor a = SampleFromProbs(row, uu);
				for (int m = 0; m < M; m++) oppFirst[g][m] = (int)a[m].item<int64_t>();
			}
			const int RP = roll->state.numPlayers;
			std::vector<bool> isBotRoll(RP, false);
			if (oracle) for (int g = 0; g < G; g++) if (slots[g].active) {
				int ours = PlayerIdx(real->state.gameStates[g], slots[g].searchTeam);
				for (int r = 0; r < R; r++) isBotRoll[roll->state.arenaPlayerStartIdx[g * R + r] + 1 - ours] = true;
			}
			std::vector<uint8_t> rollFresh(G * R, 0);
			for (int h = 0; h < H; h++) {
				double tr = NowS();
				roll->StepFirstHalf(false);
				IList racts(RP, 0);
				if (h == 0) {
					for (int g = 0; g < G; g++) {
						if (!slots[g].active) continue;
						int ours = PlayerIdx(real->state.gameStates[g], slots[g].searchTeam);
						for (int r = 0; r < R; r++) {
							int base = roll->state.arenaPlayerStartIdx[g * R + r];
							racts[base + ours] = cand[g][r / M];
							racts[base + 1 - ours] = oppFirst[g][r % M];
						}
					}
				} else {
					double ti = NowS();
					torch::Tensor rp = nets.Probs(ObsTensor(*roll), MaskTensor(*roll));
					std::vector<float> ru(RP);
					for (int g = 0; g < G; g++) for (int r = 0; r < R; r++) {
						int base = roll->state.arenaPlayerStartIdx[g * R + r];
						for (int i = 0; i < 2; i++) ru[base + i] = Uni(seed, cycle, 100 + h, g * 64 + (r % M), i); // CRN: independent of the candidate k
					}
					torch::Tensor ra = SampleFromProbs(rp, ru);
					for (int i = 0; i < RP; i++) racts[i] = (int)ra[i].item<int64_t>();
					tInf += NowS() - ti;
				}
				if (oracle) {
					std::vector<int> ext(RP, 0);
					bot->Act(roll->state.gameStates, isBotRoll, rollFresh, ext);
					for (int i = 0; i < RP; i++) if (isBotRoll[i]) racts[i] = ext[i];
				}
				roll->StepSecondHalf(racts, false);
				for (int g = 0; g < G; g++) if (slots[g].active) for (int r = 0; r < R; r++) {
					int ri = g * R + r;
					if (scoredAt[ri] < 0) { int sgn = GoalSign(roll->state.gameStates[ri]); if (sgn) { scoredAt[ri] = h; scoredSign[ri] = sgn; } }
				}
				tRoll += NowS() - tr;
			}
			// leaf value of OUR player at the end of each rollout
			torch::Tensor leafV = leaf ? nets.Value(ObsTensor(*roll)) : torch::zeros({ RP });
			for (int g = 0; g < G; g++) {
				if (!slots[g].active) continue;
				const GameState& gs = real->state.gameStates[g];
				int ours = PlayerIdx(gs, slots[g].searchTeam);
				int ourSign = slots[g].searchTeam == 0 ? +1 : -1; // goal sign from our side: blue scoring is +1 for blue
				std::vector<double> score(K, 0.0);
				for (int r = 0; r < R; r++) {
					int ri = g * R + r, k = r / M;
					double s;
					if (scoredAt[ri] >= 0) s = (scoredSign[ri] == ourSign ? 1.0 : -1.0) * std::pow((double)GAMMA, scoredAt[ri] + 1);
					else s = std::pow((double)GAMMA, H) * (leaf ? calA * (double)leafV[roll->state.arenaPlayerStartIdx[ri] + ours].item<float>() + calB : 0.0);
					score[k] += s / M;
				}
				int best = 0;
				if (pick == 0) { for (int k = 1; k < K; k++) if (score[k] > score[best]) best = k; }
				else if (pick == 1) { for (int k = 1; k < K; k++) if (score[k] < score[best]) best = k; }
				else best = std::min(K - 1, (int)(Uni(seed, cycle, 13, g, 0) * K));
				actions[2 * g + ours] = cand[g][best];
				decisions++;
			}
			tSearch += NowS() - tsearch;
		}

		double tr0 = NowS();
		real->StepFirstHalf(false);
		if (bot) {
			std::vector<bool> isBot(real->state.numPlayers, false);
			for (int g = 0; g < G; g++) if (slots[g].active) isBot[2 * g + 1 - PlayerIdx(real->state.gameStates[g], slots[g].searchTeam)] = true;
			std::vector<int> ext(real->state.numPlayers, 0);
			bot->Act(real->state.gameStates, isBot, botFresh, ext);
			for (int i = 0; i < real->state.numPlayers; i++) if (isBot[i]) actions[i] = ext[i];
			std::fill(botFresh.begin(), botFresh.end(), 0);
		}
		real->StepSecondHalf(actions, false);
		tReal += NowS() - tr0;

		for (int g = 0; g < G; g++) {
			Slot& sl_ = slots[g];
			if (!sl_.active) continue;
			sl_.steps++;
			const GameState& gs = real->state.gameStates[g];
			int sgn = GoalSign(gs);
			if (!sgn && sl_.steps < CAP_STEPS) continue;
			// game over
			finished++; sumSteps += sl_.steps;
			if (mode == "calib") {
				blueScore += sgn > 0 ? 1.0 : (sgn == 0 ? 0.5 : 0.0); blueN++;
				for (int t = 0; t < (int)sl_.v.size(); t++) for (int i = 0; i < 2; i++) {
					double Gt = std::pow((double)GAMMA, (int)sl_.v.size() - t) * (i == 0 ? +1 : -1) * sgn; // player 0 = blue
					double V = sl_.v[t][i];
					cn++; cV += V; cG += Gt; cVV += V * V; cGG += Gt * Gt; cVG += V * Gt;
				}
			}
			if (searching || argmaxCtl || rawCtl) {
				int ourSign = sl_.searchTeam == 0 ? +1 : -1;
				if (sgn == 0) { sd++; searchScore += 0.5; } else if (sgn == ourSign) { sw++; searchScore += 1; } else sl++;
			}
			if (finished % 20 == 0 || finished == N) {
				double el = NowS() - t0;
				if (searching || argmaxCtl || rawCtl) printf("[%.0fs] %d/%d games: search W/L/D %d/%d/%d, score %.3f\n", el, finished, N, sw, sl, sd, searchScore / finished);
				else printf("[%.0fs] %d/%d games\n", el, finished, N);
			}
			startGame(g);
		}
	}

	const double el = NowS() - t0;
	printf("\n== result (%s) ==  games %d, mean length %.1f s, wall %.0f s\n", mode.c_str(), N, sumSteps / N * TICK_SKIP / 120.0, el);
	if (mode == "calib") {
		double mV = cV / cn, mG = cG / cn, vV = cVV / cn - mV * mV, vG = cGG / cn - mG * mG, cov = cVG / cn - mV * mG;
		double a = cov / vV, b = mG - a * mV, r2 = cov * cov / (vV * vG);
		printf("blue score %.3f over %d games (sanity, expect about 0.5)\n", blueScore / blueN, blueN);
		printf("critic vs realised discounted outcome over %.0f rows: mean V %.4f, mean G %.4f, var V %.5f, var G %.5f, corr^2 %.3f\n", cn, mV, mG, vV, vG, r2);
		printf("FIT  G ~ a*V + b:  a = %.4f  b = %.4f    (use: ... search ... %d %d %d %d 1 %.4f %.4f)\n", a, b, N, G, H, K, a, b);
	}
	if (rawCtl) printf("RAW side (vs %s): W %d  L %d  D %d  score %.3f  (se about %.3f)\n", botName.c_str(), sw, sl, sd, searchScore / N, std::sqrt(0.25 / N));
	if (argmaxCtl) printf("ARGMAX side (control): W %d  L %d  D %d  score %.3f\n", sw, sl, sd, searchScore / N);
	if (searching) {
		double p = searchScore / N, se = std::sqrt(0.25 / N);
		printf("SEARCH side (vs %s): W %d  L %d  D %d  score %.3f  (se about %.3f; 0.5 = parity)\n", botName.c_str(), sw, sl, sd, p, se);
		printf("timing: %lld decisions; per decision: search %.1f ms (load %.1f, rollout steps %.1f, of which inference %.1f ms), real step %.1f ms\n",
			decisions, tSearch / decisions * 1e3, tLoad / decisions * 1e3, tRoll / decisions * 1e3, tInf / decisions * 1e3, tReal / (double)cycle * 1e3);
	}
	return 0;
}
