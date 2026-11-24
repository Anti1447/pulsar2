#pragma once

#include "ExternalOpponent.h"
#include "MatchLoop.h"
#include <GigaLearnCPP/LearnerConfig.h>
#include <RLGymCPP/EnvSet/EnvSet.h>
#include <map>

namespace GGL {

	// GAUNTLET (docs/Pre_Run_Checklist.md item 8): fixed-seed 1v1 kickoff games of OUR current policy, deterministic
	// (argmax) actions, against one external bot at a time. The yardstick for "is the policy getting better" that
	// does not move with the training: the same kickoff layouts every run (config.gauntlet.seed), the same three bots,
	// the same rules as the sim round robin (MatchLoop.h; the bots answer through ExternalOpponent::Act exactly as in
	// training). Owns its own arenas, so it never disturbs the training fleet, the skill tracker or the exploiter eval.
	//
	// Call only where no collection worker is running (the barrier zone), because it reads the live weights, steps
	// arenas on the shared thread pool and borrows the Learner's bot instances.
	struct GauntletEval {
		RLGC::EnvSet* envSet = nullptr;                // 1v1 arenas
		std::map<int, RLGC::EnvSet*> extraEnvSets;     // team size (2, 3) -> arenas of that size
		GauntletConfig config;

		// baseConfig: the training EnvSet's config (tickSkip, actionDelay...). obsSize: the trainer's, checked against the arenas'.
		GauntletEval(const RLGC::EnvSetConfig& baseConfig, const GauntletConfig& config, int obsSize);
		~GauntletEval();

		// `ours` plays blue in even arenas and orange in odd ones against `bot`; results are from our side.
		// teamSize 1 = the 1v1 arenas; 2/3 need config.extraTeamSizes. The bot must be able to field that team size.
		MatchResult Run(class PPOLearner* ppo, ExternalOpponent& bot, int teamSize = 1);

		// Bot against bot (docs/GCO_IMITATION_CHRONOS.md E0, the teacher's own noise floor): `a` plays in the role `ours` has in Run. Both play argmax. `a` and `b` may be the same
		// object (self-play: Act is called with disjoint player rows, the bots keep no per-arena state that two calls could mix up).
		MatchResult RunBots(ExternalOpponent& a, ExternalOpponent& b, int teamSize = 1);

		RG_NO_COPY(GauntletEval);
	};
}
