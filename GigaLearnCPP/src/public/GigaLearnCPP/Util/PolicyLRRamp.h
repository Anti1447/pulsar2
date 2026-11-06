#pragma once
// Stepwise policy-LR ramp: the scale starts at 1/10 and rises by 1/10 of the base value every stepSize timesteps until it is 1.
// Torch-free so it is unit-tested (tests/test_policy_lr_ramp.cpp). Used for PHASE B (src/ExampleMain.cpp): when the 2v2/3v3 mix
// engages, the policy LR drops to 1/10 of its PHASE A value and climbs back in 1/10 steps every 50M steps (450M to full).
#include <algorithm>
#include <cstdint>

namespace GGL {
	// startTs: the timestep the ramp began (the PHASE B marker's). Before it, and for a negative startTs, the scale is 1 (no ramp).
	inline float PolicyLRRampScale(int64_t totalTimesteps, int64_t startTs, int64_t stepSize = 50'000'000, int numStages = 10) {
		if (startTs < 0 || stepSize <= 0 || numStages <= 1)
			return 1.f;
		int64_t k = totalTimesteps < startTs ? 0 : (totalTimesteps - startTs) / stepSize;
		int stage = (int)std::min<int64_t>(k, numStages - 1); // 0 .. numStages-1
		return (float)(stage + 1) / (float)numStages;         // 0.1, 0.2 ... 1.0 (exact at the end)
	}
}
