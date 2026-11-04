#pragma once
#include "../Reward.h"
#include "../RewardTrace.h"
#include "../../Math.h"
#include "../Events/PlayerDataEventReward.h"

namespace RLGC {

	// A real class, not a typedef (2026-11-03 audit): the typedef's readable name was just "false>" (shared by every non-negated event
	// reward), so the Lab could not name it and the trainer's reward key was the mangled template string. 1.0 for the bumper of a demo.
	class DemoReward : public PlayerDataEventReward<&PlayerEventState::demo, false> {};
}
