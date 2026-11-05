#include "GGLBotOpponent.h"
#include "PPO/PPOLearner.h"

#include <RLGymCPP/ActionParsers/DefaultAction.h>
#include <RLGymCPP/CommonValues.h>
#include <RLGymCPP/Gamestates/StateUtil.h>
#include <RLGymCPP/ThreadPool.h>

#include <array>
#include <cstdlib>
#include <string>

using namespace GGL;
using namespace RLGC;

namespace {
	// Both bots' obs coefficients (AdvancedObs / GalactusOBS)
	constexpr float POS_COEF = 1 / 5000.f, VEL_COEF = 1 / 2300.f, ANG_VEL_COEF = 1 / 3.f;
	constexpr int MAX_PLAYERS = 3;          // Both are built with 3 players per team
	constexpr int PLAYER_OBS_SIZE = 29;

	// GalactusOBS: RLBot BallPrediction samples at 0.25, 0.5, 1, 1.5 and 2s
	constexpr std::array<int, 5> GALACTUS_PRED_TICKS = { 30, 60, 120, 180, 240 };
	constexpr std::array<int, 6> GALACTUS_BIG_PADS = { 3, 4, 15, 18, 29, 30 };
	constexpr float GALACTUS_BIG_PAD_GATE = 1500.f;

	inline void AddVec(float*& out, const Vec& v) {
		*out++ = v.x;
		*out++ = v.y;
		*out++ = v.z;
	}

	inline void AddZeros(float*& out, int n) {
		for (int i = 0; i < n; i++)
			*out++ = 0.f;
	}

	// AdvancedObs::AddPlayerToObs, with the x-mirror Chronos's AdvancedObsPadded adds (mirrorX false
	// for Galactus)
	void AddPlayer(float*& out, const Player& player, bool inv, bool mirrorX, const PhysState& ball) {
		auto phys = MirrorPhysX(InvertPhys(player, inv), mirrorX);
		AddVec(out, phys.pos * POS_COEF);
		AddVec(out, phys.rotMat.forward);
		AddVec(out, phys.rotMat.up);
		AddVec(out, phys.vel * VEL_COEF);
		AddVec(out, phys.angVel * ANG_VEL_COEF);
		AddVec(out, phys.rotMat.Dot(phys.angVel) * ANG_VEL_COEF);
		AddVec(out, phys.rotMat.Dot(ball.pos - phys.pos) * POS_COEF);
		AddVec(out, phys.rotMat.Dot(ball.vel - phys.vel) * VEL_COEF);
		*out++ = player.boost / 100;
		*out++ = player.isOnGround;
		*out++ = player.HasFlipOrJump();
		*out++ = player.isDemoed;
		*out++ = player.hasJumped;
	}

	// GGL_BOT_SHUFFLE=0 fills the slots in arena order instead of shuffling them (read once). The shuffle is what the
	// bots' own AdvancedObsPadded does by default (the training build); whether the LIVE builds of GalactusV2 and
	// ChronosV2 shuffle is not established (their source is not in this repo), so the sim can run either way and the two
	// can be compared (docs/EXTERNAL_OPPONENTS.md). Default: shuffle, as before.
	bool ShuffleSlots() {
		static const bool on = [] {
			const char* v = std::getenv("GGL_BOT_SHUFFLE");
			return !(v && std::string(v) == "0");
		}();
		return on;
	}

	// Teammate then opponent slots, each group shuffled over its padded slot count with empty
	// slots all-zero (the order both bots' builders produce)
	void AddOthers(float*& out, const Player& self, const GameState& state, bool inv, bool mirrorX, const PhysState& ball) {
		std::array<const Player*, MAX_PLAYERS> teammates = {}, opponents = {};
		int numTeammates = 0, numOpponents = 0;
		for (auto& other : state.players) {
			if (other.carId == self.carId)
				continue;
			if (other.team == self.team) {
				if (numTeammates >= MAX_PLAYERS - 1)
					RG_ERR_CLOSE("GGLBotOpponent: too many teammates for a " << MAX_PLAYERS << "-player obs");
				teammates[numTeammates++] = &other;
			} else {
				if (numOpponents >= MAX_PLAYERS)
					RG_ERR_CLOSE("GGLBotOpponent: too many opponents for a " << MAX_PLAYERS << "-player obs");
				opponents[numOpponents++] = &other;
			}
		}

		if (ShuffleSlots()) {
			std::shuffle(teammates.begin(), teammates.begin() + (MAX_PLAYERS - 1), ::Math::GetRandEngine());
			std::shuffle(opponents.begin(), opponents.begin() + MAX_PLAYERS, ::Math::GetRandEngine());
		}

		for (int i = 0; i < MAX_PLAYERS - 1; i++) {
			if (teammates[i])
				AddPlayer(out, *teammates[i], inv, mirrorX, ball);
			else
				AddZeros(out, PLAYER_OBS_SIZE);
		}
		for (int i = 0; i < MAX_PLAYERS; i++) {
			if (opponents[i])
				AddPlayer(out, *opponents[i], inv, mirrorX, ball);
			else
				AddZeros(out, PLAYER_OBS_SIZE);
		}
	}

	bool ShouldMirrorX(const Player& player) {
		return InvertPhys(player, player.team == Team::ORANGE).pos.x > 0;
	}

	Action MirrorActionX(Action action) {
		action.steer *= -1;
		action.yaw *= -1;
		action.roll *= -1;
		return action;
	}

	bool SameAction(const Action& a, const Action& b) {
		for (int i = 0; i < Action::ELEM_AMOUNT; i++)
			if (a[i] != b[i])
				return false;
		return true;
	}
}

GGL::GGLBotOpponent::GGLBotOpponent(GGLBotKind kind, const std::filesystem::path& modelFolder, torch::Device device)
	: kind(kind), device(device) {

	PartialModelConfig sharedHeadCfg, policyCfg;
	sharedHeadCfg.addLayerNorm = policyCfg.addLayerNorm = true;
	sharedHeadCfg.addOutputLayer = false;
	if (kind == GGLBotKind::GALACTUS_V2) {
		name = "GalactusV2";
		obsSize = 258;
		sharedHeadCfg.layerSizes = { 1024, 1024 };
		policyCfg.layerSizes = { 1024, 1024, 1024, 1024 };
		sharedHeadCfg.activationType = policyCfg.activationType = ModelActivationType::LEAKY_RELU;
	} else {
		name = "ChronosV2";
		obsSize = 225;
		sharedHeadCfg.layerSizes = { 960, 960 };
		policyCfg.layerSizes = { 960, 800, 640, 640 };
		sharedHeadCfg.activationType = policyCfg.activationType = ModelActivationType::RELU;
	}

	// Their DefaultAction, verbatim: the same table as ours, built the same way, but its air mask
	// starts at numGroundActions + 1
	{
		constexpr float R_B[] = { 0, 1 }, R_F[] = { -1, 0, 1 };
		for (float throttle : R_F)
			for (float steer : R_F)
				for (float boost : R_B)
					for (float handbrake : R_B) {
						if (boost == 1 && throttle != 1)
							continue;
						actions.push_back({ throttle, steer, 0, steer, 0, 0, boost, handbrake });
					}
		int numGroundActions = (int)actions.size();
		for (float pitch : R_F)
			for (float yaw : R_F)
				for (float roll : R_F)
					for (float jump : R_B)
						for (float boost : R_B) {
							if (jump == 1 && yaw != 0)
								continue;
							if (pitch == roll && roll == jump && jump == 0)
								continue;
							float handbrake = (jump == 1) && (pitch != 0 || yaw != 0 || roll != 0);
							actions.push_back({ boost, yaw, pitch, yaw, roll, jump, boost, handbrake });
						}

		size_t n = actions.size();
		groundMask.assign(n, 0); airMask.assign(n, 0); jumpMask.assign(n, 0); boostMask.assign(n, 0);
		for (int i = 0; i < (int)n; i++) {
			const Action& a = actions[i];
			if (a.jump)
				jumpMask[i] = true;
			if (a.boost)
				boostMask[i] = true;
			if (i < numGroundActions)
				groundMask[i] = true;
			if (i > numGroundActions && !a.jump)
				airMask[i] = true;
			if (i < numGroundActions && a.throttle == a.boost && (a.yaw != 0) == a.handbrake)
				airMask[i] = true;
		}
	}

	// Their rows onto ours, and each row onto its x-mirror, both by exact equality. Hard-fail on
	// a miss so table drift can never scramble their controls.
	DefaultAction ours = {};
	if (ours.actions.size() != actions.size())
		RG_ERR_CLOSE("GGLBotOpponent(" << name << "): action table has " << actions.size() << " rows, ours " << ours.actions.size());
	actionMap.assign(actions.size(), -1);
	mirrorMap.assign(actions.size(), -1);
	for (size_t i = 0; i < actions.size(); i++) {
		Action mirrored = MirrorActionX(actions[i]);
		for (size_t j = 0; j < actions.size(); j++) {
			if (SameAction(actions[i], ours.actions[j]))
				actionMap[i] = (int)j;
			if (SameAction(mirrored, actions[j]))
				mirrorMap[i] = (int)j;
		}
		if (actionMap[i] < 0 || mirrorMap[i] < 0)
			RG_ERR_CLOSE("GGLBotOpponent(" << name << "): action row " << i << " has no match in our table or no x-mirror");
	}

	for (int i = 0; i < CommonValues::BOOST_LOCATIONS_AMOUNT; i++) {
		const Vec loc = CommonValues::BOOST_LOCATIONS[i];
		int found = -1;
		for (int j = 0; j < CommonValues::BOOST_LOCATIONS_AMOUNT; j++) {
			const Vec other = CommonValues::BOOST_LOCATIONS[j];
			if (fabsf(loc.x + other.x) < 3.f && fabsf(loc.y - other.y) < 3.f) {
				found = j;
				break;
			}
		}
		if (found < 0)
			RG_ERR_CLOSE("GGLBotOpponent: no x-mirrored boost pad for index " << i);
		mirrorPadIndex[i] = found;
	}

	try {
		PPOLearner::MakeModels(false, obsSize, (int)actions.size(), sharedHeadCfg, policyCfg, {}, {}, device, models);
		models.Load(modelFolder, false, false);
	} catch (const std::exception& e) {
		RG_ERR_CLOSE("GGLBotOpponent(" << name << "): failed to load SHARED_HEAD.lt/POLICY.lt from " << modelFolder << ": " << e.what());
	}

	RG_LOG("GGLBotOpponent: " << name << " loaded from " << modelFolder << " (obs " << obsSize << ", "
		<< actions.size() << " actions mapped onto DefaultAction)");
}

GGL::GGLBotOpponent::~GGLBotOpponent() {
	for (Model* model : models)
		delete model;
}

void GGL::GGLBotOpponent::BuildMask(uint8_t* out, const Player& player) {
	size_t n = actions.size();
	for (size_t i = 0; i < n; i++)
		out[i] = player.isOnGround ? groundMask[i] : airMask[i];
	if (player.boost == 0)
		for (size_t i = 0; i < n; i++)
			out[i] &= ~boostMask[i];
	bool isTurtled = player.worldContact.hasContact && player.worldContact.contactNormal.z > 0.9f;
	if (player.HasFlipOrJump() || isTurtled)
		for (size_t i = 0; i < n; i++)
			out[i] |= jumpMask[i];
}

// GalactusOBS::BuildObsInto, with the external (RLBot) ball-prediction path
void GGL::GGLBotOpponent::BuildGalactusObs(float* out, const Player& player, const GameState& state, const BallPredictor& pred) {
	float* start = out;
	bool inv = player.team == Team::ORANGE;
	auto ball = InvertPhys(state.ball, inv);
	auto& pads = state.GetBoostPads(inv);
	auto& padTimers = state.GetBoostPadTimers(inv);

	AddVec(out, ball.pos * POS_COEF);
	AddVec(out, ball.vel * VEL_COEF);
	AddVec(out, ball.angVel * ANG_VEL_COEF);

	for (int i = 0; i < Action::ELEM_AMOUNT; i++)
		*out++ = player.prevAction[i];

	for (int i = 0; i < CommonValues::BOOST_LOCATIONS_AMOUNT; i++)
		*out++ = pads[i] ? 1.f : 1.f / (1.f + padTimers[i]);

	// Nearest active big pad within the gate, relative to the car (zero if none)
	{
		PhysState playerPhys = InvertPhys(player, inv);
		Vec nearestRelative = {};
		float nearestDistSq = GALACTUS_BIG_PAD_GATE * GALACTUS_BIG_PAD_GATE;
		for (int padIdx : GALACTUS_BIG_PADS) {
			if (!pads[padIdx])
				continue;
			Vec padPos = CommonValues::BOOST_LOCATIONS[padIdx];
			float distSq = playerPhys.pos.DistSq2D(padPos);
			if (distSq <= nearestDistSq) {
				nearestDistSq = distSq;
				nearestRelative = padPos - playerPhys.pos;
			}
		}
		AddVec(out, nearestRelative * POS_COEF);
	}

	AddPlayer(out, player, inv, false, ball);
	AddOthers(out, player, state, inv, false, ball);

	for (int ticks : GALACTUS_PRED_TICKS) {
		auto predBall = InvertPhys(pred.pred[ticks], inv);
		AddVec(out, predBall.pos * POS_COEF);
		AddVec(out, predBall.vel * VEL_COEF);
	}

	RG_ASSERT(out - start == obsSize);
}

// Chronos's AdvancedObsPadded::BuildObs (maxPlayers 3, ball prediction off)
void GGL::GGLBotOpponent::BuildChronosObs(float* out, const Player& player, const GameState& state) {
	float* start = out;
	bool inv = player.team == Team::ORANGE;
	bool mirrorX = InvertPhys(player, inv).pos.x > 0;

	auto ball = MirrorPhysX(InvertPhys(state.ball, inv), mirrorX);
	auto& pads = state.GetBoostPads(inv);
	// Chronos's GameState::GetBoostPadTimers has the original typo: the opposite frame's timers
	auto& padTimers = inv ? state.boostPadTimers : state.boostPadTimersInv;

	AddVec(out, ball.pos * POS_COEF);
	AddVec(out, ball.vel * VEL_COEF);
	AddVec(out, ball.angVel * ANG_VEL_COEF);

	Action prevAction = mirrorX ? MirrorActionX(player.prevAction) : player.prevAction;
	for (int i = 0; i < Action::ELEM_AMOUNT; i++)
		*out++ = prevAction[i];

	for (int i = 0; i < CommonValues::BOOST_LOCATIONS_AMOUNT; i++) {
		int padIndex = mirrorX ? mirrorPadIndex[i] : i;
		*out++ = pads[padIndex] ? 1.f : 1.f / (1.f + padTimers[padIndex]);
	}

	AddPlayer(out, player, inv, mirrorX, ball);
	AddOthers(out, player, state, inv, mirrorX, ball);

	RG_ASSERT(out - start == obsSize);
}

void GGL::GGLBotOpponent::Act(const std::vector<GameState>& states,
	const std::vector<bool>& isOld,
	const std::vector<uint8_t>& prevArenaTerminals,
	std::vector<int>& outActionIdx) {

	RG_NO_GRAD;
	int numArenas = (int)states.size();
	int numActions = (int)actions.size();

	// Row of each served player, laid out in arena order so arenas can be built in parallel
	std::vector<int> arenaFirstPlayer(numArenas + 1, 0), arenaFirstRow(numArenas + 1, 0);
	std::vector<int> rowPlayer; // Global player index per row
	for (int a = 0; a < numArenas; a++) {
		arenaFirstPlayer[a + 1] = arenaFirstPlayer[a] + (int)states[a].players.size();
		for (int g = arenaFirstPlayer[a]; g < arenaFirstPlayer[a + 1]; g++)
			if (isOld[g])
				rowPlayer.push_back(g);
		arenaFirstRow[a + 1] = (int)rowPlayer.size();
	}
	int numRows = (int)rowPlayer.size();
	if (numRows == 0)
		return;

	if (kind == GGLBotKind::GALACTUS_V2 && (int)predictors.size() < numArenas) {
		predictors.resize(numArenas);
		predictorArenas.resize(numArenas, nullptr);
	}

	std::vector<float> obsBuf((size_t)numRows * obsSize);
	std::vector<uint8_t> maskBuf((size_t)numRows * numActions);
	std::vector<uint8_t> rowMirrored(numRows, 0);

	auto fnBuildArena = [&](int a) {
		int row = arenaFirstRow[a];
		if (row == arenaFirstRow[a + 1])
			return;
		const GameState& gs = states[a];

		BallPredictor* pred = nullptr;
		if (kind == GGLBotKind::GALACTUS_V2) {
			if (!gs.lastArena)
				RG_ERR_CLOSE("GGLBotOpponent(GalactusV2): game state has no arena to predict the ball from");
			if (predictorArenas[a] != gs.lastArena) {
				predictors[a] = std::make_unique<BallPredictor>(gs.lastArena->gameMode, GALACTUS_PRED_TICKS.back() + 1);
				predictorArenas[a] = gs.lastArena;
			}
			pred = predictors[a].get();
			pred->Update(gs.ball, gs.lastTickCount);
		}

		for (int i = 0; i < (int)gs.players.size(); i++) {
			if (!isOld[arenaFirstPlayer[a] + i])
				continue;
			const Player& player = gs.players[i];
			float* obs = obsBuf.data() + (size_t)row * obsSize;
			if (kind == GGLBotKind::GALACTUS_V2) {
				BuildGalactusObs(obs, player, gs, *pred);
			} else {
				BuildChronosObs(obs, player, gs);
				rowMirrored[row] = ShouldMirrorX(player);
			}
			BuildMask(maskBuf.data() + (size_t)row * numActions, player);
			row++;
		}
	};
	g_ThreadPool.StartBatchedJobsChunked(fnBuildArena, numArenas, false);

	std::vector<int> pickVec = Pick(obsBuf, maskBuf, numRows);

	for (int row = 0; row < numRows; row++)
		outActionIdx[rowPlayer[row]] = ToOurAction(pickVec[row], rowMirrored[row]);
}

std::vector<int> GGL::GGLBotOpponent::Pick(const std::vector<float>& obs, const std::vector<uint8_t>& masks, int numRows) {
	RG_NO_GRAD;
	int numActions = (int)actions.size();
	torch::Tensor picks;
	try {
		auto tObs = torch::from_blob((void*)obs.data(), { numRows, obsSize }, torch::kFloat32).to(device);
		auto tMasks = torch::from_blob((void*)masks.data(), { numRows, numActions }, torch::kUInt8).to(device);
		PPOLearner::InferActionsFromModels(models, tObs, tMasks, /*deterministic=*/true, 1.f, /*halfPrec=*/false, &picks, nullptr);
	} catch (const std::exception& e) {
		RG_ERR_CLOSE("GGLBotOpponent(" << name << ")::Pick: inference failed (" << numRows << " rows): " << e.what());
	}
	return TENSOR_TO_VEC<int>(picks.cpu());
}

int GGL::GGLBotOpponent::ToOurAction(int theirs, bool mirrored) const {
	if (mirrored)
		theirs = mirrorMap[theirs];
	return actionMap[theirs];
}

bool GGL::GGLBotOpponent::MirroredForChronos(const RLGC::Player& player) {
	return ShouldMirrorX(player);
}
