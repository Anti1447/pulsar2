#pragma once
#include "../Reward.h"
#include "../RewardTrace.h"
#include "../../Math.h"
#include "../Events/PlayerDataEventReward.h"
#include "DemoGeometry.h"

namespace RLGC {

	// A real class, not a typedef (2026-11-03 audit): the typedef's readable name was just "false>" (shared by every non-negated event
	// reward), so the Lab could not name it and the trainer's reward key was the mangled template string. 1.0 for the bumper of a demo.
	class DemoReward : public PlayerDataEventReward<&PlayerEventState::demo, false> {};

	// 2026-11-09: DemoReward x where the victim was (DemoGeometry.h). Pays the bumper  demo x (0.4 + 0.6 f)  in 2v2/3v3, f = how close the demoed
	// car was to the ball->opponent-goal segment and to the ball. f = 1 pays EXACTLY the flat DemoReward (1.0, so the weight's full value is kept),
	// f = 0 pays 0.4. 1v1, or a demo whose victim cannot be identified, pays 1.0 (the old behaviour). Revert: use DemoReward in BuildRewards().
	class OffensiveDemoReward : public Reward {
	public:
		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			if (!player.eventState.demo) return 0.0f;
			return DemoGeometry::Evaluate(player, state).mult; // 1.0 when unresolved or 1v1
		}
	};
}
