#include "TestFramework.h"
#include "RewardTestUtil.h"

#include <RLGymCPP/Rewards/CommonRewards.h>
#include <RLGymCPP/Rewards/ZeroSumReward.h>

using namespace RLGC;
using namespace RewardTest;

// Audit of TouchAccelReward (weight 10, wrapped ZeroSum(TEAM_SPIRIT 0.3)), re-audited after the 2026-11-03 rework (findings 2 and 3).
// On a step where the player's ballTouchedStep is set, with S = min(1, |ball vel| / M), M = KPHToVel(110) = 110 * 250/9 = 3055.56 uu/s:
//   pay = min( |v_now - v_prev| / M ,  max(0, S_now - H) )
// H = the player's high-water mark: the highest S its touches have left the ball at since the last reset; raised to S_now on each of its
// touch steps, ZEROED for a player when an opponent touches the ball (and at Reset). 0 with no previous state, no touch, or no PreStep.
// Hand values at M = 3055.556: 500 uu/s -> 0.163636;  1000 -> 0.327273;  1500 -> 0.490909;  2000 -> 0.654545;  3000 -> 0.981818.
// Fixture: fabricated states (no engine). The engine side is in tools/reward_lab/packs/touch_accel.

namespace {
	constexpr float M = 110.f * 250.f / 9.f;

	struct F { Vec vel; int touchers = 0; };          // touchers: bit i = player i touched (1v1: bit0 blue, bit1 orange)

	GameState Make(const F& f, int perTeam) {
		GameState gs; gs.deltaTime = STEP_DT;
		for (int i = 0; i < perTeam; i++) {
			gs.players.push_back(MakePlayer(1 + 2 * i, 2 * i, Team::BLUE, Vec(-500.f, 300.f * i, 17)));
			gs.players.push_back(MakePlayer(2 + 2 * i, 2 * i + 1, Team::ORANGE, Vec(500.f, 300.f * i, 17)));
		}
		gs.ball.pos = Vec(0, 0, 93); gs.ball.vel = f.vel;
		for (size_t i = 0; i < gs.players.size(); i++) gs.players[i].ballTouchedStep = (f.touchers >> i) & 1;
		return gs;
	}

	// Drives a fresh reward through the frames (frame 0 is the starting state, never paid) and returns every player's reward per frame
	std::vector<std::vector<float>> Run(const std::vector<F>& frames, int perTeam = 1, TouchAccelReward* use = nullptr) {
		TouchAccelReward local; TouchAccelReward& r = use ? *use : local;
		std::vector<GameState> st;
		for (auto& f : frames) st.push_back(Make(f, perTeam));
		r.Reset(st[0]);
		std::vector<std::vector<float>> out;
		for (size_t i = 0; i < st.size(); i++) {
			if (i > 0) st[i].prev = &st[i - 1];
			r.PreStep(st[i]);
			out.push_back(r.GetAllRewards(st[i], false));
		}
		return out;
	}

	// pay for player 0 for one (prev velocity -> cur velocity) transition, touched by player 0 (or by nobody)
	float Step(Vec prev, Vec cur, bool touched = true) {
		return Run({ F{ prev, 0 }, F{ cur, touched ? 1 : 0 } })[1][0];
	}
	Vec Y(float s) { return Vec(0, s, 0); }
}

TEST(TouchAccel_the_cap_is_110_kph_in_uu_per_s) {
	CHECK_NEAR(TouchAccelReward::MAX_REWARDED_BALL_SPEED, 3055.556f, 0.01f);
}

TEST(TouchAccel_a_touch_that_speeds_up_the_ball_pays_the_gain_as_a_fraction_of_the_cap) {
	CHECK_NEAR(Step(Y(0), Y(1000)), 0.327273f, 1e-5f);
	CHECK_NEAR(Step(Y(1000), Y(2000)), 0.327273f, 1e-5f);      // impulse 1000 -> same gain, same pay (S_now - H is 0.65, the impulse is the limit)
	CHECK_NEAR(Step(Y(500), Y(1500)), 0.327273f, 1e-5f);
	CHECK_NEAR(Step(Y(0), Y(1500)), 0.490909f, 1e-5f);
}

TEST(TouchAccel_one_hit_is_capped_at_1_and_the_cap_binds_on_the_speed_fraction) {
	CHECK_NEAR(Step(Y(0), Y(9999)), 1.0f, 1e-6f);               // a huge hit on a resting ball: 1.0, not more
	CHECK_NEAR(Step(Y(0), Y(4000)), 1.0f, 1e-6f);               // over the cap: S_now = 1, impulse 1.31 -> capped at 1
	CHECK_NEAR(Step(Y(3000), Y(4000)), 1000.f / M, 1e-5f);      // a fast ball hit harder: limited by the impulse (0.327), not by the 0.018 headroom
}

// Finding 3 (fixed): the impulse, not the net speed. A return or reversal of a fast ball pays.
TEST(TouchAccel_returning_or_redirecting_a_fast_ball_pays_the_speed_it_leaves_with) {
	CHECK_NEAR(Step(Y(1000), Y(-1000)), 1000.f / M, 1e-5f);     // pure reversal: impulse 2000/M = 0.65, limited by S_now = 0.327
	CHECK_NEAR(Step(Y(2300), Y(-2300)), 2300.f / M, 1e-5f);     // 0.7527 (it paid 0 before)
	CHECK_NEAR(Step(Y(1000), Vec(1000, 0, 0)), 1000.f / M, 1e-5f);   // a sideways redirect at the same speed: impulse 1414, S_now 0.327
	CHECK_NEAR(Step(Y(1000), Y(-1500)), 1500.f / M, 1e-5f);     // reversed and faster: S_now = 0.4909 (impulse 2500 is larger)
}

// No "must speed up" gate (removed after the engine's head-on return left the ball SLOWER than it came and was paid 0): a touch that slows the ball
// pays min(impulse, exit speed fraction), so a cushion that kills the ball pays almost nothing and a hard return pays its exit speed.
TEST(TouchAccel_a_touch_that_slows_the_ball_pays_the_smaller_of_the_impulse_and_the_exit_speed) {
	CHECK_NEAR(Step(Y(1500), Y(1000)), 500.f / M, 1e-5f);      // impulse 500 < exit 1000
	CHECK_NEAR(Step(Y(2300), Y(300)), 300.f / M, 1e-5f);       // a dead stop of a fast ball: impulse 2000, exit speed 300 -> 0.098
	CHECK_NEAR(Step(Y(2300), Y(-1578)), 1578.f / M, 1e-5f);    // the engine's head-on case: 0.516
	CHECK_EQ(Step(Y(1000), Y(1000)), 0.0f);                    // unchanged speed and direction: impulse 0
	CHECK_EQ(Step(Y(0), Y(0)), 0.0f);
}

TEST(TouchAccel_pays_nothing_without_a_touch_or_a_previous_state_or_a_PreStep) {
	CHECK_EQ(Step(Y(0), Y(2000), /*touched=*/false), 0.0f);
	{
		TouchAccelReward r; GameState b = Make(F{ Y(2000), 1 }, 1);
		r.Reset(b); r.PreStep(b);                               // no state.prev
		CHECK_EQ(r.GetReward(b.players[0], b, false), 0.0f);
	}
	{
		TouchAccelReward r; GameState a = Make(F{ Y(0), 0 }, 1), b = Make(F{ Y(2000), 1 }, 1); b.prev = &a;
		CHECK_EQ(r.GetReward(b.players[0], b, false), 0.0f);   // PreStep never called: nothing precomputed
	}
}

// Direction is still ignored: a hit into your own goal is paid like any other (BallToGoal carries direction)
TEST(TouchAccel_ignores_the_direction_of_the_ball) {
	CHECK_NEAR(Step(Y(0), Y(-1000)), 1000.f / M, 1e-5f);
	CHECK_NEAR(Step(Y(0), Vec(0, 0, 1000)), 1000.f / M, 1e-5f);   // a pop straight up: z counts in the speed
}

// A chain of touch steps with the speed rising each step telescopes to the final S (the "total is 1.0" claim in the source comment)
TEST(TouchAccel_a_monotone_chain_telescopes_to_the_final_speed_fraction) {
	float speeds[] = { 0, 400, 900, 1700, 2600, 3400, 5000 };
	std::vector<F> fr; for (float s : speeds) fr.push_back(F{ Y(s), 1 });
	auto v = Run(fr);
	float total = 0; for (size_t i = 1; i < v.size(); i++) total += v[i][0];
	CHECK_NEAR(total, 1.0f, 1e-5f);
	CHECK_NEAR(v[5][0], 1.f - 2600.f / M, 1e-5f);              // 3400 is over the cap: only the headroom is left (0.149), not the 0.26 impulse
	CHECK_EQ(v[6][0], 0.0f);                                   // already at the mark 1.0: nothing more to earn
}

// Finding 2 (fixed): speed lost between your own touches is NOT paid again. 0 -> 1000 -> (500, no touch) -> 1500 pays 0.3273 + 0.1636 = 0.4909,
// exactly the final fraction (the ratchet used to pay 0.6545).
TEST(TouchAccel_speed_lost_between_touches_is_not_paid_again) {
	auto v = Run({ F{ Y(0), 0 }, F{ Y(1000), 1 }, F{ Y(500), 0 }, F{ Y(1500), 1 } });
	CHECK_NEAR(v[1][0], 0.327273f, 1e-5f);
	CHECK_EQ(v[2][0], 0.0f);
	CHECK_NEAR(v[3][0], 500.f / M, 1e-5f);                     // only the part above the 1000 uu/s already paid: 0.1636 (impulse was 1000/M = 0.327)
	CHECK_NEAR(v[1][0] + v[3][0], 1500.f / M, 1e-5f);
	// a repeated nudge to the same speed is worth nothing the second time
	auto w = Run({ F{ Y(0), 0 }, F{ Y(1000), 1 }, F{ Y(300), 0 }, F{ Y(1000), 1 }, F{ Y(300), 0 }, F{ Y(1000), 1 } });
	CHECK_NEAR(w[1][0], 0.327273f, 1e-5f);
	CHECK_EQ(w[3][0], 0.0f); CHECK_EQ(w[5][0], 0.0f);
}

// An OPPONENT's touch resets the mark: the same speed becomes payable again. Blue 0->1000 (paid), ball slows to 500, orange touches (500 -> 600,
// orange is paid its own), then blue 600 -> 1500 pays min(impulse 900/M, S_now 0.4909) = 0.2945, where without the orange touch it would pay 0.1636.
TEST(TouchAccel_an_opponent_touch_resets_the_mark) {
	auto v = Run({ F{ Y(0), 0 }, F{ Y(1000), 0b01 }, F{ Y(500), 0 }, F{ Y(600), 0b10 }, F{ Y(1500), 0b01 } });
	CHECK_NEAR(v[1][0], 0.327273f, 1e-5f);
	CHECK_NEAR(v[3][1], 100.f / M, 1e-5f);                     // orange's own touch: impulse 100, orange's mark was 0
	CHECK_NEAR(v[4][0], 900.f / M, 1e-5f);                     // 0.2945
	auto w = Run({ F{ Y(0), 0 }, F{ Y(1000), 0b01 }, F{ Y(500), 0 }, F{ Y(600), 0 }, F{ Y(1500), 0b01 } });
	CHECK_NEAR(w[4][0], 500.f / M, 1e-5f);                     // no orange touch: only the 500 above the mark
}

// A TEAMMATE's touch does not reset a mark; the per-player marks are independent
TEST(TouchAccel_marks_are_per_player_and_a_teammates_touch_does_not_reset_them) {
	// 2v2 order: blue0, orange0, blue1, orange1.  blue0 pays at 1000; blue1 touches at 1000 -> 1200 (its own mark 0: impulse 200); blue0 again 1200 -> 1500
	auto v = Run({ F{ Y(0), 0 }, F{ Y(1000), 0b0001 }, F{ Y(1200), 0b0100 }, F{ Y(1500), 0b0001 } }, 2);
	CHECK_NEAR(v[1][0], 1000.f / M, 1e-5f);
	CHECK_NEAR(v[2][2], 200.f / M, 1e-5f);
	CHECK_NEAR(v[3][0], 300.f / M, 1e-5f);                     // blue0's mark is still 1000/M: 500 above it is free but the impulse is only 300
}

// The mark follows every touch of the player: a cushion that leaves the ball at 1200 uu/s pays its impulse (300) and raises the mark to 1200, so a
// later hit to 1700 after the ball drifts to 1000 pays the 500 above the mark (the impulse is 700): 0.1636, not the 0.229 a mark that ignored the
// cushion would allow.
TEST(TouchAccel_a_cushion_raises_the_mark) {
	auto v = Run({ F{ Y(1500), 0 }, F{ Y(1200), 1 }, F{ Y(1000), 0 }, F{ Y(1700), 1 } });
	CHECK_NEAR(v[1][0], 300.f / M, 1e-5f);
	CHECK_NEAR(v[3][0], 500.f / M, 1e-5f);
}

// The mark only ever rises on the player's own touches: a later, softer touch (600 after paying at 1000) does not lower it, so regaining 1000 after
// the ball drifts to 300 pays nothing the second time.
TEST(TouchAccel_a_softer_later_touch_does_not_lower_the_mark) {
	auto v = Run({ F{ Y(0), 0 }, F{ Y(1000), 1 }, F{ Y(600), 1 }, F{ Y(300), 0 }, F{ Y(1000), 1 } });
	CHECK_NEAR(v[1][0], 1000.f / M, 1e-5f);
	CHECK_EQ(v[2][0], 0.0f);                                   // 600 is below the mark 1000: no headroom
	CHECK_EQ(v[4][0], 0.0f);                                   // and the mark is still 1000
}

// 2v2: one touch by blue0 resets the marks of BOTH orange cars (order blue0, orange0, blue1, orange1). orange0 pays at 1000, orange1 at 1200,
// blue0 touches (ball 800), then orange1 hits 800 -> 1200 again and is paid the full 400 impulse instead of the 0 a surviving mark would give.
TEST(TouchAccel_an_opponent_touch_resets_every_opposing_players_mark) {
	auto v = Run({ F{ Y(0), 0 }, F{ Y(1000), 0b0010 }, F{ Y(1200), 0b1000 }, F{ Y(800), 0b0001 }, F{ Y(800), 0 }, F{ Y(1200), 0b1000 } }, 2);
	CHECK_NEAR(v[2][3], 200.f / M, 1e-5f);                     // orange1 first pays at 1200 (its mark was 0): impulse 200
	CHECK_NEAR(v[5][3], 400.f / M, 1e-5f);                     // reset by blue0's touch: pays the 400 impulse again
}

// FINDING 5, left: two players touching on the same step are both paid in full (opponents reset each other's marks first)
TEST(TouchAccel_a_simultaneous_touch_pays_every_toucher_the_full_gain) {
	auto v = Run({ F{ Y(0), 0 }, F{ Y(1000), 0b11 } });
	CHECK_NEAR(v[1][0], 0.327273f, 1e-5f); CHECK_NEAR(v[1][1], 0.327273f, 1e-5f);
}

TEST(TouchAccel_reset_clears_the_marks) {
	TouchAccelReward r;
	auto v = Run({ F{ Y(0), 0 }, F{ Y(1000), 1 } }, 1, &r);
	CHECK_NEAR(v[1][0], 0.327273f, 1e-5f);
	CHECK(r.highWater[1] > 0.3f);
	auto w = Run({ F{ Y(0), 0 }, F{ Y(1000), 1 } }, 1, &r);    // Run() starts with Reset
	CHECK_NEAR(w[1][0], 0.327273f, 1e-5f);                     // a new episode pays the same hit again
}

// Wiring as in training: ZeroSum(TouchAccel, 0.3), weight 10.  1v1: toucher +x, opponent -x.  2v2: 0.85x, 0.15x, -0.5x, -0.5x
TEST(TouchAccel_zero_sum_values_1v1_and_2v2_at_team_spirit_0p3_weight_10) {
	const float x = 1000.f / M;                     // 0.327273
	for (int perTeam = 1; perTeam <= 2; perTeam++) {
		Reward* zs = new ZeroSumReward(new TouchAccelReward(), 0.3f);
		GameState a = Make(F{ Y(0), 0 }, perTeam); zs->Reset(a);
		GameState b = Make(F{ Y(1000), 1 }, perTeam); b.prev = &a; zs->PreStep(b);
		auto v = zs->GetAllRewards(b, false);
		if (perTeam == 1) {
			CHECK_NEAR(v[0], x, 1e-6f); CHECK_NEAR(v[1], -x, 1e-6f);
			CHECK_NEAR(10.f * v[0], 3.27273f, 1e-4f);
		} else {
			CHECK_NEAR(v[0], 0.85f * x, 1e-6f); CHECK_NEAR(v[2], 0.15f * x, 1e-6f);
			CHECK_NEAR(v[1], -0.5f * x, 1e-6f); CHECK_NEAR(v[3], -0.5f * x, 1e-6f);
			CHECK_NEAR(v[0] + v[1] + v[2] + v[3], 0.0f, 1e-6f);
		}
		delete zs;
	}
}
