#include "TestFramework.h"
#include "RewardTestUtil.h"

#include <RLGymCPP/Rewards/CommonRewards.h>
#include <RLGymCPP/Rewards/ZeroSumReward.h>

using namespace RLGC;
using namespace RewardTest;

// Audit of AerialTouchReward (checklist item 2, weight 120 in BuildRewards, wrapped ZeroSum(TEAM_SPIRIT 0.3)).
// Formula under test, from the class constants:
//   ballFrac    = clamp((ball.z - 150) / 1300, 0, 1)
//   airFrac     = min(1, player.airTime / 1.75)
//   heightFrac  = min(ballFrac, airFrac)            // a MIN, not a product
//   impulseFrac = min(1, |ball.vel - prev.ball.vel| / 500)
//   reward      = heightFrac * impulseFrac          // 0 if not a touch, on the ground, no prev state, or in cooldown
//   cooldown    : pays set sincePay = 0; the next 12 steps pay 0; the 13th step after a pay can pay again
// All expected values below were computed by hand from these constants before the test was run.
// Fixture: fabricated states (no engine), deltaTime = 8/120. The engine side (does a real aerial touch look like this?)
// is in tools/reward_lab/packs/aerial_touch.

namespace {
	struct AT {
		float ballZ = 800, airTime = 1.75f, dv = 500;
		bool touch = true, ground = false;
	};

	// One player (index 0). Frame i has ball velocity prev + dv so |delta v| = dv exactly.
	std::vector<float> RunAT(AerialTouchReward& r, const std::vector<AT>& frames, bool firstHasPrev = false) {
		std::vector<GameState> st; st.reserve(frames.size());
		Vec bv(0, 0, 0);
		for (size_t i = 0; i < frames.size(); i++) {
			const AT& f = frames[i];
			GameState gs; gs.deltaTime = STEP_DT;
			Player p = MakePlayer(1, 0, Team::BLUE, Vec(0, 0, f.ballZ));
			p.isOnGround = f.ground; p.ballTouchedStep = f.touch; p.airTime = f.airTime;
			gs.players.push_back(p);
			gs.ball.pos = Vec(0, 0, f.ballZ);
			bv = bv + Vec(f.dv, 0, 0); // ball velocity changes by exactly dv along x each frame
			gs.ball.vel = bv;
			st.push_back(std::move(gs));
		}
		for (size_t i = 1; i < st.size(); i++) st[i].prev = &st[i - 1];
		(void)firstHasPrev;
		r.Reset(st[0]);
		std::vector<float> out;
		for (auto& s : st) { r.PreStep(s); out.push_back(r.GetReward(s.players[0], s, false)); }
		return out;
	}
	float Pay(const AT& f) { AerialTouchReward r; return RunAT(r, { AT{ 800, 0, 0, false }, f })[1]; }
}

TEST(AerialTouch_full_credit_is_exactly_one) {
	// z 1450 -> ballFrac 1; airTime 1.75 -> airFrac 1; dv 500 -> impulse 1
	CHECK_NEAR(Pay(AT{ 1450, 1.75f, 500 }), 1.0f, 1e-5f);
}

TEST(AerialTouch_every_factor_clamps_at_one) {
	CHECK_NEAR(Pay(AT{ 2000, 5.0f, 2000 }), 1.0f, 1e-5f); // ceiling-high, long flight, huge hit: still 1, not more
}

TEST(AerialTouch_half_height_half_impulse_is_a_quarter) {
	// z 800 -> (800-150)/1300 = 0.5; airTime 1.75 -> 1; min = 0.5; dv 250 -> 0.5; 0.5 * 0.5
	CHECK_NEAR(Pay(AT{ 800, 1.75f, 250 }), 0.25f, 1e-5f);
}

TEST(AerialTouch_short_flight_limits_credit) {
	// z 1450 -> 1; airTime 0.35 -> 0.35/1.75 = 0.2; dv 500 -> 1
	CHECK_NEAR(Pay(AT{ 1450, 0.35f, 500 }), 0.2f, 1e-5f);
}

TEST(AerialTouch_height_is_the_smaller_of_ball_height_and_air_time_not_their_product) {
	// ballFrac 0.5 and airFrac 0.5: min = 0.5 (a product would be 0.25); dv 500 -> 1
	CHECK_NEAR(Pay(AT{ 800, 0.875f, 500 }), 0.5f, 1e-5f);
}

TEST(AerialTouch_ball_at_or_below_150_pays_nothing) {
	CHECK_EQ(Pay(AT{ 150, 1.75f, 500 }), 0.0f);
	CHECK_EQ(Pay(AT{ 140, 1.75f, 500 }), 0.0f);
	CHECK(Pay(AT{ 151, 1.75f, 500 }) > 0.0f); // the threshold is exactly 150
}

TEST(AerialTouch_a_grounded_car_is_never_paid_even_for_a_high_ball) {
	AT f{ 1450, 1.75f, 500 }; f.ground = true;
	CHECK_EQ(Pay(f), 0.0f);
}

TEST(AerialTouch_no_touch_no_pay) {
	AT f{ 1450, 1.75f, 500 }; f.touch = false;
	CHECK_EQ(Pay(f), 0.0f);
}

TEST(AerialTouch_first_frame_without_a_previous_state_pays_nothing) {
	AerialTouchReward r;
	GameState gs; gs.deltaTime = STEP_DT;
	Player p = MakePlayer(1, 0, Team::BLUE, Vec(0, 0, 1000)); p.isOnGround = false; p.ballTouchedStep = true; p.airTime = 1.75f;
	gs.players.push_back(p); gs.ball.pos = Vec(0, 0, 1000); gs.ball.vel = Vec(500, 0, 0);
	r.Reset(gs);
	CHECK_EQ(r.GetReward(gs.players[0], gs, false), 0.0f);
}

TEST(AerialTouch_a_touch_that_changes_nothing_pays_nothing_and_does_not_start_the_cooldown) {
	AerialTouchReward r;
	// frame 1: touch with dv 0 -> reward 0, no cooldown; frame 2: a real touch pays at once
	auto v = RunAT(r, { AT{ 1000, 1.75f, 0, false }, AT{ 1000, 1.75f, 0 }, AT{ 1450, 1.75f, 500 } });
	CHECK_EQ(v[1], 0.0f);
	CHECK(v[2] > 0.0f);
}

// Farming check: a hover-and-tap loop. A touch every step must pay once, then nothing for 12 steps.
TEST(AerialTouch_cooldown_blocks_twelve_steps_then_pays_again) {
	AerialTouchReward r;
	std::vector<AT> frames = { AT{ 1450, 1.75f, 500, false } };           // frame 0: no touch (no prev)
	for (int i = 0; i < 30; i++) frames.push_back(AT{ 1450, 1.75f, 500 }); // frames 1..30 all touch with a full hit
	auto v = RunAT(r, frames);
	CHECK(v[1] > 0.99f);                         // frame 1 pays (first ever)
	for (int k = 2; k <= 13; k++) CHECK_EQ(v[k], 0.0f); // frames 2..13: the 12 blocked steps
	CHECK(v[14] > 0.99f);                        // frame 14: the 13th step after the pay pays again
	for (int k = 15; k <= 26; k++) CHECK_EQ(v[k], 0.0f);
	CHECK(v[27] > 0.99f);
	// over 30 touching steps: exactly 3 payments, so about 3.0 not 30.0
	float sum = 0; for (float x : v) sum += x;
	CHECK_NEAR(sum, 3.0f, 1e-3f);
}

TEST(AerialTouch_cooldown_counts_steps_not_touches) {
	AerialTouchReward r;
	// pay on frame 1, then 12 steps with no touch, then a touch on frame 14 pays
	std::vector<AT> frames = { AT{ 1450, 1.75f, 500, false }, AT{ 1450, 1.75f, 500 } };
	for (int i = 0; i < 12; i++) { AT f{ 1450, 1.75f, 500 }; f.touch = false; frames.push_back(f); }
	frames.push_back(AT{ 1450, 1.75f, 500 });
	auto v = RunAT(r, frames);
	CHECK(v[1] > 0.99f);
	CHECK(v[14] > 0.99f);
}

TEST(AerialTouch_cooldown_is_per_player) {
	AerialTouchReward r;
	GameState a, b; a.deltaTime = b.deltaTime = STEP_DT;
	for (GameState* g : { &a, &b }) {
		for (int i = 0; i < 2; i++) {
			Player p = MakePlayer(1 + i, i, i == 0 ? Team::BLUE : Team::ORANGE, Vec(0, 0, 1450));
			p.isOnGround = false; p.airTime = 1.75f; p.ballTouchedStep = (i == 0 || g == &b);
			g->players.push_back(p);
		}
		g->ball.pos = Vec(0, 0, 1450);
	}
	a.ball.vel = Vec(0, 0, 0); b.ball.vel = Vec(500, 0, 0);
	b.prev = &a;
	r.Reset(a);
	// step b: both players touch for the first time. Both pay: one player's pay does not start the other's cooldown
	auto v = r.GetAllRewards(b, false);
	CHECK(v[0] > 0.99f && v[1] > 0.99f);
}

// Team wiring as it is in training: ZeroSum(AerialTouch, 0.3), 2v2, blue car 0 takes a full-credit aerial touch.
//   car 0: 1*0.7 + 0.3*(1/2) - 0 = 0.85; its teammate: 0*0.7 + 0.3*0.5 = 0.15; each orange car: 0 + 0 - 0.5 = -0.5
TEST(AerialTouch_zero_sum_2v2_values_at_team_spirit_0p3) {
	Reward* zs = new ZeroSumReward(new AerialTouchReward(), 0.3f);
	GameState a, b; a.deltaTime = b.deltaTime = STEP_DT;
	for (GameState* g : { &a, &b }) {
		for (int i = 0; i < 4; i++) {
			Player p = MakePlayer(1 + i, i, i < 2 ? Team::BLUE : Team::ORANGE, Vec(0, 0, 1450));
			p.isOnGround = false; p.airTime = 1.75f; p.ballTouchedStep = (g == &b && i == 0);
			g->players.push_back(p);
		}
		g->ball.pos = Vec(0, 0, 1450);
	}
	a.ball.vel = Vec(0, 0, 0); b.ball.vel = Vec(500, 0, 0);
	b.prev = &a;
	zs->Reset(a);
	zs->PreStep(b);
	auto v = zs->GetAllRewards(b, false);
	CHECK_NEAR(v[0], 0.85f, 1e-5f);
	CHECK_NEAR(v[1], 0.15f, 1e-5f);
	CHECK_NEAR(v[2], -0.5f, 1e-5f);
	CHECK_NEAR(v[3], -0.5f, 1e-5f);
	CHECK_NEAR(v[0] + v[1] + v[2] + v[3], 0.0f, 1e-5f); // zero-sum across the arena
	delete zs;
}
