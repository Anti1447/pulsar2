#include "TestFramework.h"
#include "RewardTestUtil.h"

#include <RLGymCPP/Rewards/CommonRewards.h>
#include <RLGymCPP/Rewards/ZeroSumReward.h>

#include <cmath>

using namespace RLGC;
using namespace RewardTest;

// Audit of KickoffRaceReward (checklist item 2, weight 60, wrapped ZeroSum(TEAM_SPIRIT 0.3)).
//   active    : only if the state passed to Reset has the ball at rest at the centre (|x| < 200, |y| < 200, speed < 50)
//   steps     : bumped once per step (by player index 0); a touch on step k pays 1 - k/45; steps > 45 resolve the kickoff at 0
//   first touch of the kickoff, by ANY team, resolves it: every player touching on that SAME step is paid, only if CONTESTED, else 0.
//   Fires once per kickoff. (Window 30 -> 45 and the same-step rule are the 2026-11-02 audit fixes.)
//   contested : some alive opponent of the toucher is within 1500 uu of the ball, or closing on it faster than 500 uu/s
// All values below were computed by hand from those rules. Fixture: fabricated states; the engine side (when does a real
// kickoff touch happen, who is paid in a 50/50) is in tools/reward_lab/packs/kickoff_race.

namespace {
	struct KP { Vec pos; Vec vel = Vec(0, 0, 0); Team team = Team::BLUE; bool touch = false, demoed = false; };

	GameState Make(const std::vector<KP>& ps, Vec ball = Vec(0, 0, 93), Vec ballVel = Vec(0, 0, 0)) {
		GameState gs; gs.deltaTime = STEP_DT;
		for (size_t i = 0; i < ps.size(); i++) {
			Player p = MakePlayer(1 + (uint32_t)i, (int)i, ps[i].team, ps[i].pos);
			p.vel = ps[i].vel; p.ballTouchedStep = ps[i].touch; p.isDemoed = ps[i].demoed;
			gs.players.push_back(p);
		}
		gs.ball.pos = ball; gs.ball.vel = ballVel;
		return gs;
	}

	// Run `step` calls (1-based): the touch (if any) is set on step `touchStep`; returns the rewards of the step `touchStep`
	// 1v1: blue 0 at the left, orange 1 at distance `oppDist` from the ball.
	std::vector<float> Race(int touchStep, float oppDist, Vec oppVel = Vec(0, 0, 0), int toucher = 0, bool oppDemoed = false) {
		KickoffRaceReward r;
		GameState init = Make({ KP{ Vec(-2000, 0, 17) }, KP{ Vec(oppDist, 0, 17), oppVel, Team::ORANGE } });
		r.Reset(init);
		std::vector<float> out;
		for (int k = 1; k <= touchStep; k++) {
			std::vector<KP> ps = { KP{ Vec(-1000, 0, 17) }, KP{ Vec(oppDist, 0, 17), oppVel, Team::ORANGE, false, oppDemoed } };
			if (k == touchStep) ps[toucher].touch = true;
			GameState gs = Make(ps);
			out = r.GetAllRewards(gs, false);
		}
		return out;
	}
}

TEST(KickoffRace_a_touch_on_step_k_pays_one_minus_k_over_45) {
	CHECK_NEAR(Race(1, 800)[0], 1.0f - 1.0f / 45.f, 1e-6f);    // 0.97778
	CHECK_NEAR(Race(15, 800)[0], 2.0f / 3.0f, 1e-6f);
	CHECK_NEAR(Race(31, 800)[0], 14.0f / 45.f, 1e-6f);         // the fastest real kickoff touch (diagonal, step 31): 0.31111
	CHECK_NEAR(Race(40, 800)[0], 5.0f / 45.f, 1e-6f);          // back kickoff, step 40: 0.11111
	CHECK_EQ(Race(45, 800)[0], 0.0f);                           // 1 - 45/45
}

TEST(KickoffRace_a_touch_after_the_45_step_window_pays_nothing) {
	CHECK_EQ(Race(46, 800)[0], 0.0f);
	CHECK_EQ(Race(60, 800)[0], 0.0f);
}

TEST(KickoffRace_only_a_ball_at_rest_at_the_centre_arms_it) {
	auto paid = [](Vec ball, Vec ballVel) {
		KickoffRaceReward r; r.Reset(Make({ KP{ Vec(-2000, 0, 17) }, KP{ Vec(800, 0, 17), Vec(), Team::ORANGE } }, ball, ballVel));
		GameState gs = Make({ KP{ Vec(-1000, 0, 17), Vec(), Team::BLUE, true }, KP{ Vec(800, 0, 17), Vec(), Team::ORANGE } });
		return r.GetAllRewards(gs, false)[0];
	};
	CHECK(paid(Vec(0, 0, 93), Vec(0, 0, 0)) > 0.9f);
	CHECK(paid(Vec(199, 199, 93), Vec(0, 0, 0)) > 0.9f);       // inside the centre box
	CHECK_EQ(paid(Vec(200, 0, 93), Vec(0, 0, 0)), 0.0f);       // |x| < 200 is strict
	CHECK_EQ(paid(Vec(0, 350, 93), Vec(0, 0, 0)), 0.0f);
	CHECK_EQ(paid(Vec(0, 0, 93), Vec(0, 60, 0)), 0.0f);        // moving ball: not a kickoff
	CHECK(paid(Vec(0, 0, 93), Vec(0, 49, 0)) > 0.9f);          // speed < 50
}

TEST(KickoffRace_contest_gate_is_1500_uu_or_500_uu_per_s_closing) {
	CHECK(Race(5, 1490)[0] > 0.8f);                             // 1490 uu out (1492 in 3D, the ball is 76 uu higher): contests
	CHECK_EQ(Race(5, 1500)[0], 0.0f);                           // 1500 out is 1502 in 3D and standing still: a delay
	CHECK(Race(5, 3000, Vec(-501, 0, 0))[0] > 0.8f);            // far but closing at 501 uu/s contests
	CHECK_EQ(Race(5, 3000, Vec(-500, 0, 0))[0], 0.0f);          // exactly 500 does not (strict >)
	CHECK_EQ(Race(5, 3000, Vec(501, 0, 0))[0], 0.0f);           // moving away does not
	CHECK_EQ(Race(5, 1000, Vec(0, 0, 0), 0, /*oppDemoed=*/true)[0], 0.0f); // a demolished opponent does not contest
}

TEST(KickoffRace_it_fires_once_even_if_the_first_touch_was_uncontested) {
	KickoffRaceReward r; r.Reset(Make({ KP{ Vec(-2000, 0, 17) }, KP{ Vec(3000, 0, 17), Vec(), Team::ORANGE } }));
	GameState a = Make({ KP{ Vec(-1000, 0, 17), Vec(), Team::BLUE, true }, KP{ Vec(3000, 0, 17), Vec(), Team::ORANGE } });
	CHECK_EQ(r.GetAllRewards(a, false)[0], 0.0f);               // uncontested first touch: 0, and the kickoff is resolved
	GameState b = Make({ KP{ Vec(-500, 0, 17), Vec(), Team::BLUE, true }, KP{ Vec(600, 0, 17), Vec(), Team::ORANGE } });
	auto v = r.GetAllRewards(b, false);
	CHECK_EQ(v[0], 0.0f); CHECK_EQ(v[1], 0.0f);                 // a later contested touch pays nothing
}

TEST(KickoffRace_a_second_touch_after_a_paid_one_pays_nothing) {
	KickoffRaceReward r; r.Reset(Make({ KP{ Vec(-2000, 0, 17) }, KP{ Vec(800, 0, 17), Vec(), Team::ORANGE } }));
	GameState a = Make({ KP{ Vec(-1000, 0, 17), Vec(), Team::BLUE, true }, KP{ Vec(800, 0, 17), Vec(), Team::ORANGE } });
	CHECK(r.GetAllRewards(a, false)[0] > 0.9f);
	GameState b = Make({ KP{ Vec(-900, 0, 17), Vec(), Team::BLUE, true }, KP{ Vec(700, 0, 17), Vec(), Team::ORANGE, true } });
	auto v = r.GetAllRewards(b, false);
	CHECK_EQ(v[0], 0.0f); CHECK_EQ(v[1], 0.0f);
}

TEST(KickoffRace_a_new_episode_rearms_it) {
	KickoffRaceReward r; r.Reset(Make({ KP{ Vec(-2000, 0, 17) }, KP{ Vec(800, 0, 17), Vec(), Team::ORANGE } }));
	GameState a = Make({ KP{ Vec(-1000, 0, 17), Vec(), Team::BLUE, true }, KP{ Vec(800, 0, 17), Vec(), Team::ORANGE } });
	CHECK(r.GetAllRewards(a, false)[0] > 0.9f);
	r.Reset(Make({ KP{ Vec(-2000, 0, 17) }, KP{ Vec(800, 0, 17), Vec(), Team::ORANGE } }));   // next kickoff
	CHECK(r.GetAllRewards(a, false)[0] > 0.9f);                                          // step counter restarted
}

TEST(KickoffRace_the_other_team_can_win_the_race_too) {
	auto v = Race(10, 800, Vec(0, 0, 0), /*toucher=*/1);        // orange (index 1) touches first, blue is the opponent 1000 uu away
	CHECK_NEAR(v[1], 1.0f - 10.0f / 45.f, 1e-6f);
	CHECK_EQ(v[0], 0.0f);
}

// FIXED 2026-11-02: on a dead-even 50/50 where both cars touch on the SAME step, both are paid the same (it used to pay only the lower
// player index and charge the other as the "loser"). A touch one step LATER than the first pays nothing.
TEST(KickoffRace_simultaneous_touches_are_both_paid_equally) {
	KickoffRaceReward r; r.Reset(Make({ KP{ Vec(-2000, 0, 17) }, KP{ Vec(2000, 0, 17), Vec(), Team::ORANGE } }));
	GameState gs = Make({ KP{ Vec(-300, 0, 17), Vec(), Team::BLUE, true }, KP{ Vec(300, 0, 17), Vec(), Team::ORANGE, true } });
	auto v = r.GetAllRewards(gs, false);
	CHECK_NEAR(v[0], 1.0f - 1.0f / 45.f, 1e-6f);
	CHECK_EQ(v[0], v[1]);
}

TEST(KickoffRace_a_touch_one_step_after_the_first_pays_nothing) {
	KickoffRaceReward r; r.Reset(Make({ KP{ Vec(-2000, 0, 17) }, KP{ Vec(2000, 0, 17), Vec(), Team::ORANGE } }));
	GameState a = Make({ KP{ Vec(-300, 0, 17), Vec(), Team::BLUE, true }, KP{ Vec(300, 0, 17), Vec(), Team::ORANGE, false } });
	CHECK(r.GetAllRewards(a, false)[0] > 0.9f);
	GameState b = Make({ KP{ Vec(-200, 0, 17), Vec(), Team::BLUE, false }, KP{ Vec(200, 0, 17), Vec(), Team::ORANGE, true } });
	auto v = r.GetAllRewards(b, false);
	CHECK_EQ(v[0], 0.0f); CHECK_EQ(v[1], 0.0f);
}

TEST(KickoffRace_a_same_step_touch_by_an_uncontested_pair_member_still_pays_nothing) {
	// a delay kickoff (orange far and still): blue touches on step 4 and so does a blue teammate; neither is contested
	KickoffRaceReward r; r.Reset(Make({ KP{ Vec(-2000, 300, 17) }, KP{ Vec(-2000, -300, 17) }, KP{ Vec(3500, 0, 17), Vec(), Team::ORANGE } }));
	std::vector<float> v;
	for (int k = 1; k <= 4; k++) {
		GameState gs = Make({ KP{ Vec(-200, 300, 17), Vec(), Team::BLUE, k == 4 }, KP{ Vec(-200, -300, 17), Vec(), Team::BLUE, k == 4 }, KP{ Vec(3500, 0, 17), Vec(), Team::ORANGE } });
		v = r.GetAllRewards(gs, false);
	}
	CHECK_EQ(v[0], 0.0f); CHECK_EQ(v[1], 0.0f);
}

TEST(KickoffRace_steps_are_counted_once_per_step_in_2v2) {
	KickoffRaceReward r;
	auto mk = [](bool touch3) {
		return Make({ KP{ Vec(-1000, 300, 17) }, KP{ Vec(-1000, -300, 17) }, KP{ Vec(900, 300, 17), Vec(), Team::ORANGE }, KP{ Vec(900, -300, 17), Vec(), Team::ORANGE, touch3 } });
	};
	r.Reset(mk(false));
	std::vector<float> v;
	for (int k = 1; k <= 12; k++) { GameState gs = mk(k == 12); v = r.GetAllRewards(gs, false); }
	CHECK_NEAR(v[3], 1.0f - 12.0f / 45.f, 1e-6f);                // orange car 3 touches on step 12: 0.73333
}

// Wiring as in training: ZeroSum(KickoffRace, 0.3), 2v2. Blue car 0 wins a contested kickoff on step 15 (r = 1 - 15/45 = 2/3).
//   car 0: 2/3 * 0.7 + 0.3 * 1/3 = 0.56667; teammate 0.3 * 1/3 = 0.1; each orange car -1/3
TEST(KickoffRace_zero_sum_2v2_values_at_team_spirit_0p3) {
	auto mk = [](bool touch) {
		return Make({ KP{ Vec(-1000, 300, 17), Vec(), Team::BLUE, touch }, KP{ Vec(-1000, -300, 17) },
			KP{ Vec(900, 300, 17), Vec(), Team::ORANGE }, KP{ Vec(900, -300, 17), Vec(), Team::ORANGE } });
	};
	Reward* zs = new ZeroSumReward(new KickoffRaceReward(), 0.3f);
	zs->Reset(mk(false));
	std::vector<float> v;
	for (int k = 1; k <= 15; k++) { GameState gs = mk(k == 15); zs->PreStep(gs); v = zs->GetAllRewards(gs, false); }
	CHECK_NEAR(v[0], 0.56667f, 1e-4f);
	CHECK_NEAR(v[1], 0.1f, 1e-5f);
	CHECK_NEAR(v[2], -0.33333f, 1e-4f);
	CHECK_NEAR(v[3], -0.33333f, 1e-4f);
	CHECK_NEAR(v[0] + v[1] + v[2] + v[3], 0.0f, 1e-5f);
	delete zs;
}
