#include "TestFramework.h"

#include <GigaLearnCPP/Util/PolicyLRRamp.h>

using GGL::PolicyLRRampScale;

// PHASE B: the policy LR starts at 1/10 of its PHASE A value and gains 1/10 every 50M steps, reaching 1.0 at +450M.
TEST(PolicyLRRamp_starts_at_a_tenth_and_gains_a_tenth_every_50M) {
	const int64_t start = 3'000'000'000;
	CHECK_NEAR(PolicyLRRampScale(start, start), 0.1f, 1e-6f);
	CHECK_NEAR(PolicyLRRampScale(start + 49'999'999, start), 0.1f, 1e-6f);
	CHECK_NEAR(PolicyLRRampScale(start + 50'000'000, start), 0.2f, 1e-6f);
	CHECK_NEAR(PolicyLRRampScale(start + 149'999'999, start), 0.3f, 1e-6f);
	CHECK_NEAR(PolicyLRRampScale(start + 400'000'000, start), 0.9f, 1e-6f);
	CHECK_NEAR(PolicyLRRampScale(start + 449'999'999, start), 0.9f, 1e-6f);
	CHECK_EQ(PolicyLRRampScale(start + 450'000'000, start), 1.f); // exactly the PHASE A value
	CHECK_EQ(PolicyLRRampScale(start + 10'000'000'000, start), 1.f);
}

TEST(PolicyLRRamp_is_monotonic_and_inert_without_a_start) {
	const int64_t start = 100;
	float prev = 0;
	for (int64_t ts = start; ts < start + 600'000'000; ts += 7'000'000) {
		float s = PolicyLRRampScale(ts, start);
		CHECK(s >= prev);
		CHECK(s >= 0.1f - 1e-6f && s <= 1.f);
		prev = s;
	}
	CHECK_EQ(PolicyLRRampScale(5, -1), 1.f);                 // no marker timestep: no ramp
	CHECK_NEAR(PolicyLRRampScale(0, 1000), 0.1f, 1e-6f);     // a checkpoint older than the marker clamps to the first stage
}
