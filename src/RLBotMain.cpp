// Entry point for the RLBot v5 bot executable (GigaLearnRLBot).
//
// Builds an InferUnit from a trained checkpoint using the SAME obs builder, action
// parser and model shape the bot was trained with (see src/ExampleMain.cpp's
// cfg.ppo.* / cfg.gapSensor.* setup), then connects to the RLBotServer / RLBotSim
// and plays until the match ends.

#include "RLBotClient.h"

#include <RLGymCPP/ObsBuilders/AdvancedObsPadded.h>
#include <RLGymCPP/ActionParsers/DefaultAction.h>
#include <RLGymCPP/Gamestates/GameState.h>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

using namespace RLGC;
using namespace GGL;

// Must match ExampleMain.cpp's MAX_PLAYERS_PER_TEAM (not exported from that file).
static constexpr int MAX_PLAYERS_PER_TEAM = 3;

// Determine the policy's obs size the way the framework does during environment
// setup: build a representative (1v1) state and measure a single obs vector. This
// must match the obs size the checkpoint was trained with, or InferUnit will abort
// with a clear size-mismatch message.
static int ComputeObsSize(AdvancedObsPadded* obs, int ballPredSlices) {
	// A state with no arena needs external ball-prediction slices (placeholders: only the SIZE matters here).
	if (ballPredSlices > 0)
		obs->SetExternalBallPred(std::vector<PhysState>(ballPredSlices));
	GameState gs; // default ctor sizes the boost-pad arrays
	Player a = {}; a.carId = 1; a.team = Team::BLUE;   a.index = 0;
	Player b = {}; b.carId = 2; b.team = Team::ORANGE; b.index = 1;
	gs.players = { a, b };
	return (int)obs->BuildObs(gs.players[0], gs).size();
}

static bool EnvFlag(const char* name) {
	const char* v = std::getenv(name);
	return v && (std::string(v) == "1" || std::string(v) == "true");
}

int main(int argc, char** argv) {
	std::cout << std::unitbuf; // flush every insertion (log files block-buffer otherwise)

	// Checkpoint folder (a saved checkpoints_5.0v3/<step> dir, containing SHARED_HEAD.lt +
	// POLICY.lt): argv[1], else $GGL_CHECKPOINT, else "checkpoint" relative to the
	// executable's working dir.
	std::string checkpoint = "checkpoint";
	if (argc > 1) {
		checkpoint = argv[1];
	} else if (const char* env = std::getenv("GGL_CHECKPOINT")) {
		checkpoint = env;
	}

	bool useGPU = EnvFlag("GGL_USE_GPU");

	// Team-canonical, fixed-width padded obs (4.0/5.0 team-play lineage) - same builder
	// AdvancedObsPadded(3) ExampleMain.cpp's MakeEnv() uses, so 1v1/2v2/3v3 checkpoints
	// all load through the same obs shape.
	// GGL_BALLPRED=1 (set by the 6.0 launcher, tools/package_bot.sh): the 6.0 lineage, 8 ball-prediction slices (278 floats), GELU,
	// shared head 2 deep and policy 4 deep (ExampleMain.cpp's g_BallPredObs branch). Unset: the 5.3 shapes below.
	const bool ballPred = EnvFlag("GGL_BALLPRED");
	const int ballPredSlices = ballPred ? 8 : 0; // BALL_PRED_SLICES in ExampleMain.cpp
	if (ballPred) {
		// BallPredictor builds a car-less arena, so RocketSim needs its collision meshes.
		bool inited = false;
		std::vector<std::string> dirs;
		if (const char* e = std::getenv("GGL_COLLISION_MESHES")) dirs.push_back(e);
		for (const char* d : { "collision_meshes", "../collision_meshes", "build/collision_meshes" }) dirs.push_back(d);
		for (auto& d : dirs)
			if (std::filesystem::exists(std::filesystem::path(d) / "soccar")) { RocketSim::Init(d); inited = true; break; }
		if (!inited)
			RG_ERR_CLOSE("RLBot bot: collision_meshes not found (set GGL_COLLISION_MESHES); the ball prediction needs them");
	}
	auto* obsBuilder = new AdvancedObsPadded(MAX_PLAYERS_PER_TEAM, /*shuffleSlots=*/true, ballPredSlices);
	auto* actionParser = new DefaultAction();

	int obsSize = ComputeObsSize(obsBuilder, ballPredSlices);
	RG_LOG("RLBot bot obs size: " << obsSize);

	// Must mirror ExampleMain.cpp's cfg.ppo.sharedHead / cfg.ppo.policy exactly, or the
	// saved weights won't load into a matching architecture. Current lineage: the
	// residual/asymmetric net (trunk 1152 kept wide, policy SHRUNK to 768).
	//
	// addResiduals IS LOAD-INVISIBLE AND BEHAVIOUR-CRITICAL: the module list stays flat,
	// so a residual checkpoint loads shape-clean into a non-residual model and then plays
	// WRONG silently (Model::Forward applies the skips from recorded index spans). A
	// mismatch here produces no error at all - only a bot that looks lobotomized.
	bool addResiduals = true;

	PartialModelConfig sharedHeadConfig;
	sharedHeadConfig.layerSizes = { 1152, 1152, 1152 };  // stem + 1 residual block
	sharedHeadConfig.addResiduals = addResiduals;
	sharedHeadConfig.activationType = ballPred ? ModelActivationType::GELU : ModelActivationType::LEAKY_RELU;
	if (ballPred) sharedHeadConfig.layerSizes = { 1152, 1152 };
	sharedHeadConfig.addLayerNorm = true;
	// The shared head is a pure feature trunk; the policy head owns the output layer
	// (matches PPOLearnerConfig's own default - InferUnit builds models directly from
	// PartialModelConfig, bypassing that default, so it must be set explicitly here).
	sharedHeadConfig.addOutputLayer = false;

	PartialModelConfig policyConfig;
	policyConfig.layerSizes = { 768, 768, 768 };         // stem + 1 residual block
	policyConfig.addResiduals = addResiduals;
	policyConfig.activationType = ballPred ? ModelActivationType::GELU : ModelActivationType::LEAKY_RELU;
	if (ballPred) policyConfig.layerSizes = { 1024, 1024, 1024, 1024 };
	policyConfig.addLayerNorm = true;

	// NOTE: the policy head is at plain trunk width. The Optimistic-Critic Ladder's
	// 5-column policy wire (which made this head's input 512+5=517 on the 5.0v3 lineage
	// from 18.88B on) was REMOVED 2026-07-25 with the rest of the Ladder actuation; the
	// composition critic that replaced it never feeds H to the policy. Ladder-era 5.0v3
	// checkpoints therefore DO NOT load here - they abort with 264704 vs 262144 on
	// POLICY.lt layer 0. Restore tag for that architecture: pre-strip-20260725.
	RG_LOG("Loading GigaLearn checkpoint from \"" << checkpoint << "\" (useGPU=" << useGPU << ")...");
	auto* inferUnit = new InferUnit(
		obsBuilder, obsSize, actionParser,
		sharedHeadConfig, policyConfig,
		checkpoint, useGPU);

	RLBotParams params = {};
	params.tickSkip = 8;      // matches cfg.tickSkip in ExampleMain.cpp
	params.actionDelay = 7;   // live play uses 7: with 0 the bot behaved erratically in RLBot (observed 2026-11-02).
	                          // NOTE: the trainer's cfg.actionDelay is still 0 (ExampleMain.cpp), so live and training differ until that is decided.
	                          // Chronos and Galactus were trained with 7 (docs/6.0.md).
	params.inferUnit = inferUnit;
	params.obsBuilder = obsBuilder;
	params.ballPredSlices = ballPredSlices;

	RLBotClient::Run(params);
	return EXIT_SUCCESS;
}
