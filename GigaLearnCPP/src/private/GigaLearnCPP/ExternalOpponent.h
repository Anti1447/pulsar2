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

		virtual ~ExternalOpponent() = default;

		// Called once when a serve iteration begins (it picks up arenas mid-episode)
		virtual void BeginServe(int numPlayers) {}

		// Fill outActionIdx (OUR action-table indices) for every player marked in isOld.
		// prevArenaTerminals = envSet->state.terminals at the loop top (the PREVIOUS step's flags):
		// a nonzero entry means that arena's current obs is a fresh episode.
		virtual void Act(const std::vector<RLGC::GameState>& states,
			const std::vector<bool>& isOld,
			const std::vector<uint8_t>& prevArenaTerminals,
			std::vector<int>& outActionIdx) = 0;
	};
}
