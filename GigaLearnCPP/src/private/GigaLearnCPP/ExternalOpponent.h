#pragma once
#include "FrameworkTorch.h"

#include <RLGymCPP/Gamestates/GameState.h>

namespace GGL {

	// A frozen bot from outside this run that plays the non-self team on the external opponents'
	// share of collection iterations (LearnerConfig::ExternalOpponentConfig). Each one reads
	// GameStates with its own obs builder and answers in OUR DefaultAction indices; its rows are
	// excluded from training like every other opponent's. Implementations: NextoOpponent,
	// GGLBotOpponent (GalactusV2, ChronosV2).
	//
	// Threading: owned by the Learner and used only from the collection path (the pipelined
	// worker). The weights are frozen, so there is nothing to snapshot.
	class ExternalOpponent {
	public:
		std::string name;     // Metric prefix, e.g. "Nexto" -> Nexto/Goals For

		// How the bot picks among its actions. 0 = argmax of its net (the bot as shipped; the default and what the
		// yardsticks assume). > 0 = sample from softmax(logits / temperature): 1 is the net's own distribution, lower
		// is closer to argmax. Set by the Learner from ExternalOpponentConfig::sampleTemperature for training serves;
		// the gauntlet forces 0. Sampling uses torch's global generator (ExternalSampling.h).
		float temperature = 0.f;

		virtual ~ExternalOpponent() = default;

		// Largest team size (players per team) of an arena this bot can play in. The Learner leaves an arena that exceeds it
		// out of the serve: both of its teams play as the live policy that iteration, and the arena is skipped by the
		// yardstick counters. Default: no limit.
		virtual int MaxTeamSize() const { return 1 << 20; }

		// Called once when a serve iteration begins (it picks up arenas mid-episode)
		virtual void BeginServe(int numPlayers) {}

		// Fill outActionIdx (OUR action-table indices) for every player marked in isOld.
		// prevArenaTerminals = envSet->state.terminals as the PREVIOUS step left them, copied BEFORE EnvSet::Reset()
		// (which zeroes them):
		// a nonzero entry means that arena's current obs is a fresh episode.
		virtual void Act(const std::vector<RLGC::GameState>& states,
			const std::vector<bool>& isOld,
			const std::vector<uint8_t>& prevArenaTerminals,
			std::vector<int>& outActionIdx) = 0;
	};
}
