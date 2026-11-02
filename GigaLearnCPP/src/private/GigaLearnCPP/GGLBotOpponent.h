#pragma once
#include "ExternalOpponent.h"
#include "Util/Models.h"

#include <RLGymCPP/BasicTypes/Action.h>
#include <RLGymCPP/Gamestates/BallPredictor.h>

namespace GGL {

	enum class GGLBotKind {
		GALACTUS_V2,  // SoldierDoge/GalactusV2, checkpoint 201.35B
		CHRONOS_V2,   // KingJake3113/Chronos
	};

	// EXTERNAL TRAINING OPPONENTS from the RLBot v5 botpack (2026-11-01, user-directed: stronger
	// than Nexto, which this lineage was beating ~10:1). Both are GigaLearnCPP bots, so their
	// weights (rlbot/SHARED_HEAD.lt + rlbot/POLICY.lt in each repo) load into our own Model
	// classes unchanged: an LN-MLP shared head and policy, no residuals, tick skip 8, 90 actions.
	//
	// What is ported, verbatim from each repo's inference code (fidelity to what the net was
	// trained on beats local notions of correctness, same rule as NextoOpponent):
	//   - Obs. GalactusV2: GalactusOBS(3), 258 floats, with 5 ball-prediction samples at 0.25,
	//     0.5, 1, 1.5 and 2s. They come from our BallPredictor every step, which matches its RLBot
	//     deployment (RLBot's fresh BallPrediction) rather than its training tracker (refreshed
	//     every 32 ticks). ChronosV2: its AdvancedObsPadded(3), 225 floats, no ball prediction,
	//     every player x-mirrored onto the left half of the field, and its GameState's original
	//     GetBoostPadTimers, which returns the OPPOSITE frame's timers (our copy fixed that typo).
	//   - Action masks: their DefaultAction mask, whose air mask starts one row later than ours
	//     (`i > numGroundActions`).
	//   - Actions: their table is ours row for row (checked at construction). Chronos parses an
	//     x-mirrored car's action with steer/yaw/roll negated, mapped back onto our table.
	// Inference is deterministic (argmax), as both bots play in RLBot.
	//
	// Not ported: both were trained with actionDelay 7 (the new action takes effect on the last
	// tick of the 8); this run uses 0, so they act ~58 ms sooner than they were trained to.
	class GGLBotOpponent : public ExternalOpponent {
	public:
		GGLBotKind kind;
		int obsSize;
		torch::Device device;
		ModelSet models;

		// Their DefaultAction table and masks
		std::vector<RLGC::Action> actions;
		std::vector<uint8_t> groundMask, airMask, jumpMask, boostMask;
		std::vector<int> actionMap;   // Their row -> our DefaultAction index
		std::vector<int> mirrorMap;   // Their row -> their row with steer/yaw/roll negated
		int mirrorPadIndex[RLGC::CommonValues::BOOST_LOCATIONS_AMOUNT] = {}; // Chronos: x-mirrored pad

		// GalactusV2: one ball predictor per arena index (made on first serve)
		std::vector<std::unique_ptr<RLGC::BallPredictor>> predictors;
		std::vector<Arena*> predictorArenas;

		// modelFolder holds SHARED_HEAD.lt and POLICY.lt
		GGLBotOpponent(GGLBotKind kind, const std::filesystem::path& modelFolder, torch::Device device);
		~GGLBotOpponent();

		void Act(const std::vector<RLGC::GameState>& states,
			const std::vector<bool>& isOld,
			const std::vector<uint8_t>& prevArenaTerminals,
			std::vector<int>& outActionIdx) override;

	private:
		void BuildGalactusObs(float* out, const RLGC::Player& player, const RLGC::GameState& state,
			const RLGC::BallPredictor& pred);
		void BuildChronosObs(float* out, const RLGC::Player& player, const RLGC::GameState& state);
		void BuildMask(uint8_t* out, const RLGC::Player& player);
	};
}
