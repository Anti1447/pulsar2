#pragma once
#include "../Gamestates/GameState.h"
#include "../BasicTypes/Action.h"
#include "../BasicTypes/Lists.h"

// https://github.com/AechPro/rocket-league-gym-sim/blob/main/rlgym_sim/utils/obs_builders/obs_builder.py
namespace RLGC {
	class ObsBuilder {
	public:
		// Virtual so a builder that owns resources (AdvancedObsPadded's ball-prediction arena) is freed
		// when deleted through this base
		virtual ~ObsBuilder() = default;

		virtual void Reset(const GameState& initialState) {}

		// Ball prediction for `state`, if this builder maintains one (see GameState::ballPred). Called by EnvSet
		// once per step before rewards; must be cheap when called again for the same tick from BuildObs.
		virtual const std::vector<BallState>* GetBallPrediction(const GameState& state) { return nullptr; }

		// NOTE: May be called once during environment initialization to determine policy neuron size
		virtual FList BuildObs(const Player& player, const GameState& state) = 0;
	};
}