#include "TestFramework.h"
#include "RewardTestUtil.h"

#include <RLGymCPP/Rewards/KickoffOutcomeReward.h>
#include <RLGymCPP/Rewards/CommonRewards.h>

#include <cmath>

using namespace RLGC;
using namespace RewardTest;

// Audit of KickoffOutcomeReward (ported from RLGymBot_v3; in BuildRewards it is a COMMENTED-OUT candidate, weight 12 in v3).
// Updated 2026-11-03 for the three audit fixes: possession = team(s) on the latest touch step (0 on a double), a contested gate, standoff 3.5 s.
// Hand-computed from the header's rules (defaults): horizon 2.0 s after the first touch, standoff 3.0 s with -0.6, step = 8/120 s.
//   armed        : at Reset, |ball| < 300 (3D) and ball speed < 200
//   first touch  : the step on which any player touches starts the 2.0 s window; both teams are flagged if both touch that step;
//                  if no opponent is within 1500 uu of the ball (or closing >500 uu/s) at that step the kickoff is closed with NO payout
//   score(T)     : clip( 0.40*field + 0.20*possess + 0.15*momentum + 0.15*firstTouch + 0.10*coverage ), dir = +1 blue / -1 orange
//                  field = clip(ball.y*dir/2500), possess = +1/-1 by the last toucher's team, momentum = clip(ball.vel.y*dir/2300),
//                  firstTouch = +1 / -1 / 0 (0 if both or neither), coverage = +/-1 if a teammate is goalside (team size >= 2; 0 in 1v1)
//   goal in window: +1 for the scorer, -1 for the conceder (overrides all terms)
//   payment      : once per episode, to every player: takers (closest to the ball at spawn, ties all count) 100%, others 50%
//   standoff     : no touch for 3.5 s -> everyone gets -0.6 once (takers 100%, others 50%) and the kickoff is closed
// Fixture: fabricated states (no engine). The engine side is in tools/reward_lab/packs/kickoff_outcome and
// tests/test_kickoff_outcome_permutations.cpp.

namespace {
	constexpr float DT = 8.0f / 120.0f;

	struct KO { Vec pos; Team team; bool touch = false; };

	GameState Make(const std::vector<KO>& ps, Vec ball = Vec(0, 0, 93), Vec ballVel = Vec(0, 0, 0), int lastTouchCarId = -1, bool goal = false) {
		GameState gs; gs.deltaTime = DT;
		for (size_t i = 0; i < ps.size(); i++) {
			Player p = MakePlayer(1 + (uint32_t)i, (int)i, ps[i].team, ps[i].pos);
			p.ballTouchedStep = ps[i].touch;
			gs.players.push_back(p);
		}
		gs.ball.pos = ball; gs.ball.vel = ballVel; gs.lastTouchCarID = lastTouchCarId; gs.goalScored = goal;
		return gs;
	}

	// Drives the reward the way EnvSet does: PreStep then GetAllRewards, once per step
	struct Drive {
		KickoffOutcomeReward r;
		explicit Drive(const GameState& init) { r.Reset(init); }
		std::vector<float> Step(GameState& gs, bool isFinal = false) { r.PreStep(gs); return r.GetAllRewards(gs, isFinal); }
	};

	// 1v1 spawn: blue 0 at (-1500,0), orange 1 at (1500,0)
	GameState Spawn1v1() { return Make({ KO{ Vec(-1500, 0, 17), Team::BLUE }, KO{ Vec(1500, 0, 17), Team::ORANGE } }); }
}

TEST(KickoffOutcome_arming_is_3D_distance_under_300_and_speed_under_200) {
	auto armed = [](Vec ball, Vec vel) {
		KickoffOutcomeReward::Metrics m0 = KickoffOutcomeReward::GetAndResetMetrics(); (void)m0;
		Drive d(Make({ KO{ Vec(-1500, 0, 17), Team::BLUE }, KO{ Vec(800, 0, 17), Team::ORANGE } }, ball, vel));
		return KickoffOutcomeReward::GetAndResetMetrics().armed == 1;
	};
	CHECK(armed(Vec(0, 0, 93), Vec(0, 0, 0)));
	CHECK(armed(Vec(285, 0, 93), Vec(0, 0, 0)));       // |(285, 0, 93)| = 299.8
	CHECK(!armed(Vec(290, 0, 93), Vec(0, 0, 0)));      // 304.5
	CHECK(armed(Vec(0, 0, 93), Vec(199, 0, 0)));
	CHECK(!armed(Vec(0, 0, 93), Vec(200, 0, 0)));
	CHECK(!armed(Vec(1000, 0, 93), Vec(0, 0, 0)));     // a random-state ball
}

// Compare with KickoffRace's tighter box (|x|,|y| < 200, speed < 50): a ball 280 uu from the centre rolling at 150 uu/s is a kickoff here
TEST(KickoffOutcome_arms_on_a_ball_that_KickoffRace_would_not) {
	GameState s = Make({ KO{ Vec(-1500, 0, 17), Team::BLUE }, KO{ Vec(800, 0, 17), Team::ORANGE } }, Vec(250, 120, 93), Vec(0, 150, 0));
	CHECK(KickoffRaceReward::IsKickoffBall(s) == false);
	KickoffOutcomeReward::GetAndResetMetrics();
	Drive d(s);
	CHECK_EQ(KickoffOutcomeReward::GetAndResetMetrics().armed, 1LL);
}

TEST(KickoffOutcome_a_state_that_is_not_a_kickoff_never_pays) {
	Drive d(Make({ KO{ Vec(-1500, 0, 17), Team::BLUE }, KO{ Vec(800, 0, 17), Team::ORANGE } }, Vec(2000, 1000, 93)));
	for (int k = 1; k <= 120; k++) {
		GameState gs = Make({ KO{ Vec(-1000, 0, 17), Team::BLUE, k == 5 }, KO{ Vec(800, 0, 17), Team::ORANGE } }, Vec(2000, 1000, 93));
		auto v = d.Step(gs);
		CHECK_EQ(v[0], 0.0f); CHECK_EQ(v[1], 0.0f);
	}
}

// 1v1, blue touches on step 4 and nobody else does. Ball at the resolution: (0, 1000, 93) moving (0, 1150, 0), last touch blue.
//   blue: 0.40 * (1000/2500) + 0.20 * 1 + 0.15 * (1150/2300) + 0.15 * 1 + 0 = 0.16 + 0.2 + 0.075 + 0.15 = 0.585 ; orange = -0.585 exactly
TEST(KickoffOutcome_a_blue_win_scores_0p585_and_orange_exactly_minus_that_in_1v1) {
	KickoffOutcomeReward::GetAndResetMetrics();
	Drive d(Spawn1v1());
	std::vector<float> v; int paidAt = -1;
	for (int k = 1; k <= 60; k++) {
		GameState gs = Make({ KO{ Vec(-1000, 0, 17), Team::BLUE, k == 4 }, KO{ Vec(800, 0, 17), Team::ORANGE } }, Vec(0, 1000, 93), Vec(0, 1150, 0), k >= 4 ? 1 : -1);
		v = d.Step(gs);
		if (paidAt < 0 && (v[0] != 0 || v[1] != 0)) { paidAt = k; break; }
	}
	CHECK(paidAt > 0);
	CHECK_NEAR(v[0], 0.585f, 1e-5f);
	CHECK_NEAR(v[1], -0.585f, 1e-5f);
	CHECK_EQ(v[0] + v[1], 0.0f);                       // antisymmetric in 1v1: exactly zero-sum
	auto m = KickoffOutcomeReward::GetAndResetMetrics();
	CHECK_EQ(m.resolved, 1LL); CHECK_EQ(m.standoffs, 0LL); CHECK_EQ(m.goals, 0LL);
	CHECK_EQ(m.modeResolved[0], 1LL);
}

// The window: touch on step 4, clock advances 8/120 per step starting that step; payment when the clock passes touch + 2.0 s.
TEST(KickoffOutcome_it_pays_30_or_31_steps_after_the_first_touch) {
	Drive d(Spawn1v1());
	int paidAt = -1;
	for (int k = 1; k <= 80; k++) {
		GameState gs = Make({ KO{ Vec(-1000, 0, 17), Team::BLUE, k == 4 }, KO{ Vec(800, 0, 17), Team::ORANGE } }, Vec(0, 500, 93), Vec(0, 300, 0), k >= 4 ? 1 : -1);
		auto v = d.Step(gs);
		if (paidAt < 0 && (v[0] != 0 || v[1] != 0)) paidAt = k;
	}
	// 2.0 s / (8/120 s) = 30 steps exactly; float accumulation of 0.0666667 can leave the sum a hair under 2.0, which pays one step later
	std::cout << "    paid on step " << paidAt << " (touch on step 4, so " << paidAt - 4 << " steps later)" << std::endl;
	CHECK(paidAt - 4 == 30 || paidAt - 4 == 31);
}

TEST(KickoffOutcome_it_pays_once_per_episode) {
	Drive d(Spawn1v1());
	int payments = 0;
	for (int k = 1; k <= 200; k++) {
		GameState gs = Make({ KO{ Vec(-1000, 0, 17), Team::BLUE, k == 4 || k == 60 }, KO{ Vec(800, 0, 17), Team::ORANGE, k == 70 } }, Vec(0, 800, 93), Vec(0, 500, 0), 1);
		auto v = d.Step(gs);
		if (v[0] != 0 || v[1] != 0) payments++;
	}
	CHECK_EQ(payments, 1);
}

TEST(KickoffOutcome_first_touch_term_is_plus_minus_and_zero_on_a_same_step_double) {
	auto scoreBlue = [](bool blueTouch, bool orangeTouch) {
		Drive d(Spawn1v1());
		std::vector<float> v;
		for (int k = 1; k <= 60; k++) {
			// the ball rests 500 uu on blue's attacking side (field = 0.4 * 0.2 = 0.08 for blue), so the other terms are possess and firstTouch;
			// GameState::lastTouchCarID is set to the OTHER team on purpose: the reward must not read it
			GameState gs = Make({ KO{ Vec(-1000, 0, 17), Team::BLUE, k == 3 && blueTouch }, KO{ Vec(1000, 0, 17), Team::ORANGE, k == 3 && orangeTouch } }, Vec(0, 500, 93), Vec(0, 0, 0), blueTouch ? 2 : 1);
			v = d.Step(gs);
			if (v[0] != 0 || v[1] != 0) return v[0];
		}
		return -99.f;
	};
	CHECK_NEAR(scoreBlue(true, false), 0.08f + 0.35f, 1e-5f);    // blue alone first and last: field 0.08 + possess 0.2 + first 0.15
	CHECK_NEAR(scoreBlue(false, true), 0.08f - 0.35f, 1e-5f);    // orange alone: 0.08 - 0.2 - 0.15
	CHECK_NEAR(scoreBlue(true, true), 0.08f, 1e-5f);             // a same-step double touch: no first touch, no possession (it used to be +/-0.2 by player order)
}

// POSSESSION (fixed 2026-11-03): the team(s) on the most recent touch step, not GameState::lastTouchCarID
TEST(KickoffOutcome_possession_follows_the_latest_touch_step_and_is_zero_when_both_teams_touch_it) {
	auto scoreBlue = [](int blueSteps0, int orangeSteps0, int blueStep, int orangeStep) {
		(void)blueSteps0; (void)orangeSteps0;
		Drive d(Spawn1v1());
		std::vector<float> v;
		for (int k = 1; k <= 60; k++) {
			GameState gs = Make({ KO{ Vec(-1000, 0, 17), Team::BLUE, k == blueStep || k == 10 && blueStep == 10 }, KO{ Vec(1000, 0, 17), Team::ORANGE, k == orangeStep } }, Vec(0, 0, 93), Vec(0, 0, 0), 1);
			v = d.Step(gs);
			if (v[0] != 0 || v[1] != 0) return v[0];
		}
		return -99.f;
	};
	// blue touches first (step 3), orange touches alone later (step 6): the latest touch step is orange's -> possession orange: blue = +0.15 - 0.2
	CHECK_NEAR(scoreBlue(0, 0, 3, 6), 0.15f - 0.2f, 1e-5f);
	// orange first (step 3), blue alone later (step 6): firstTouch -0.15, possession blue +0.2
	CHECK_NEAR(scoreBlue(0, 0, 6, 3), -0.15f + 0.2f, 1e-5f);
	// blue first (step 3), then BOTH touch together on step 6: the latest touch step is shared -> possession 0: blue = +0.15
	{
		Drive d(Spawn1v1());
		std::vector<float> v;
		for (int k = 1; k <= 60; k++) {
			GameState gs = Make({ KO{ Vec(-1000, 0, 17), Team::BLUE, k == 3 || k == 6 }, KO{ Vec(1000, 0, 17), Team::ORANGE, k == 6 } }, Vec(0, 0, 93), Vec(0, 0, 0), 2);
			v = d.Step(gs);
			if (v[0] != 0 || v[1] != 0) break;
		}
		CHECK_NEAR(v[0], 0.15f, 1e-5f);
		CHECK_NEAR(v[1], -0.15f, 1e-5f);
	}
}

// CONTESTED GATE (added 2026-11-03, same rule as KickoffRace): no payout when nobody contests the first touch
TEST(KickoffOutcome_a_delay_kickoff_is_closed_without_a_payout) {
	KickoffOutcomeReward::GetAndResetMetrics();
	Drive d(Spawn1v1());
	float total = 0;
	for (int k = 1; k <= 100; k++) {
		// blue touches on step 4 and drives the ball far upfield; orange stays at its spawn (1500 uu from the centre, +ball y 1000: 1803 uu away)
		GameState gs = Make({ KO{ Vec(-1000, 0, 17), Team::BLUE, k == 4 }, KO{ Vec(1500, 0, 17), Team::ORANGE } }, Vec(0, 1000, 93), Vec(0, 1150, 0), 1);
		auto v = d.Step(gs);
		total += fabsf(v[0]) + fabsf(v[1]);
	}
	CHECK_EQ(total, 0.0f);
	auto m = KickoffOutcomeReward::GetAndResetMetrics();
	CHECK_EQ(m.uncontested, 1LL); CHECK_EQ(m.resolved, 0LL); CHECK_EQ(m.armed, 1LL);
}

TEST(KickoffOutcome_contest_gate_is_1500_uu_or_500_uu_per_s_closing_and_ignores_a_demolished_opponent) {
	auto pays = [](Vec orangePos, Vec orangeVel, bool demoed = false) {
		Drive d(Spawn1v1());
		for (int k = 1; k <= 60; k++) {
			GameState gs = Make({ KO{ Vec(-1000, 0, 17), Team::BLUE, k == 3 }, KO{ orangePos, Team::ORANGE } }, Vec(0, 0, 93), Vec(0, 0, 0), 1);
			gs.players[1].vel = orangeVel; gs.players[1].isDemoed = demoed;
			auto v = d.Step(gs);
			if (v[0] != 0 || v[1] != 0) return true;
		}
		return false;
	};
	CHECK(pays(Vec(1490, 0, 17), Vec(0, 0, 0)));                    // 3D distance sqrt(1490^2 + 76^2) = 1492 < 1500
	CHECK(!pays(Vec(1500, 0, 17), Vec(0, 0, 0)));                   // 1502: a delay
	CHECK(pays(Vec(3000, 0, 17), Vec(-501, 0, 0)));                 // far but closing at 501 uu/s
	CHECK(!pays(Vec(3000, 0, 17), Vec(-500, 0, 0)));                // exactly 500 does not
	CHECK(!pays(Vec(3000, 0, 17), Vec(501, 0, 0)));                 // moving away
	CHECK(!pays(Vec(800, 0, 17), Vec(0, 0, 0), /*demoed=*/true));   // a demolished opponent does not contest
}

TEST(KickoffOutcome_a_same_step_double_touch_is_contested_by_definition) {
	KickoffOutcomeReward::GetAndResetMetrics();
	Drive d(Spawn1v1());
	bool paid = false;
	for (int k = 1; k <= 60; k++) {
		GameState gs = Make({ KO{ Vec(-200, 0, 17), Team::BLUE, k == 3 }, KO{ Vec(200, 0, 17), Team::ORANGE, k == 3 } }, Vec(0, 0, 93), Vec(0, 0, 0), 1);
		auto v = d.Step(gs);
		if (v[0] != 0 || v[1] != 0) paid = true;
	}
	// payment is 0 here (possession 0, first touch 0, ball at rest at the centre): the check is that the kickoff was resolved, not closed as a delay
	auto m = KickoffOutcomeReward::GetAndResetMetrics();
	CHECK_EQ(m.uncontested, 0LL);
	CHECK(m.resolved >= 1LL);
	(void)paid;
}

// 2v2 with team coverage: blue 0 touches. Blue has a teammate nearer its own goal than the ball (+0.1); orange does not (-0.1).
//   ball resolved at (0, 600, 93), vel 0, last touch blue: field 0.4*0.24 = 0.096, possess 0.2, first 0.15
//   blue = 0.096 + 0.2 + 0 + 0.15 + 0.1 = 0.546 ; orange = -0.096 - 0.2 - 0.15 - 0.1 = -0.546?  (orange coverage: its cars are at y > ball y for dir -1?)
// Orange cars at y = +2000: for orange dir = -1, "goalside" means p.y*dir < ball.y*dir, i.e. -2000 < -600: true, so orange also gets +0.1.
//   orange = -0.096 - 0.2 - 0.15 + 0.1 = -0.346. Blue + orange = 0.2: NOT zero-sum (both teams are credited for coverage)
TEST(KickoffOutcome_coverage_is_credited_to_both_teams_so_2v2_is_not_zero_sum) {
	GameState init = Make({ KO{ Vec(-500, -2000, 17), Team::BLUE }, KO{ Vec(500, 2000, 17), Team::ORANGE }, KO{ Vec(-1500, -2500, 17), Team::BLUE }, KO{ Vec(1500, 2500, 17), Team::ORANGE } });
	Drive d(init);
	std::vector<float> v;
	for (int k = 1; k <= 60; k++) {
		GameState gs = Make({ KO{ Vec(-200, -400, 17), Team::BLUE, k == 3 }, KO{ Vec(500, 2000, 17), Team::ORANGE }, KO{ Vec(-1500, -2500, 17), Team::BLUE }, KO{ Vec(1500, 2000, 17), Team::ORANGE } },
			Vec(0, 600, 93), Vec(0, 0, 0), k >= 3 ? 1 : -1);
		v = d.Step(gs);
		if (v[0] != 0) break;
	}
	// takers: blue 0 and orange 1 (each the closest on its own team at spawn, 2062 uu): 100%; the other two cars are support: 50%
	float blueScore = 0.096f + 0.2f + 0.15f + 0.1f, orangeScore = -0.096f - 0.2f - 0.15f + 0.1f;
	CHECK_NEAR(v[0], blueScore, 1e-4f);              // blue taker
	CHECK_NEAR(v[1], orangeScore, 1e-4f);            // orange taker
	CHECK_NEAR(v[2], 0.5f * blueScore, 1e-4f);       // blue support
	CHECK_NEAR(v[3], 0.5f * orangeScore, 1e-4f);
	CHECK(fabsf(blueScore + orangeScore) > 0.19f);   // 0.2 of net credit across the two teams
}

TEST(KickoffOutcome_takers_are_the_closest_to_the_ball_at_spawn_and_ties_all_count) {
	// 3v3 with spots 0,1,4 (the two diagonals tie at 3277, the back car is 4608)
	GameState init = Make({
		KO{ Vec(-2048, -2560, 17), Team::BLUE }, KO{ Vec(2048, 2560, 17), Team::ORANGE },
		KO{ Vec(2048, -2560, 17), Team::BLUE },  KO{ Vec(-2048, 2560, 17), Team::ORANGE },
		KO{ Vec(0, -4608, 17), Team::BLUE },     KO{ Vec(0, 4608, 17), Team::ORANGE } });
	CHECK(KickoffUtil::IsKickoffTaker(init.players[0], init));
	CHECK(KickoffUtil::IsKickoffTaker(init.players[2], init));    // the tie: both diagonal blue cars are takers
	CHECK(!KickoffUtil::IsKickoffTaker(init.players[4], init));   // the back car is support
	CHECK(KickoffUtil::IsKickoffTaker(init.players[1], init));
	CHECK(KickoffUtil::IsKickoffTaker(init.players[3], init));
	CHECK(!KickoffUtil::IsKickoffTaker(init.players[5], init));
	// the offset spots (-256,-3840) and (256,-3840) are equidistant too
	GameState off = Make({ KO{ Vec(-256, -3840, 17), Team::BLUE }, KO{ Vec(256, -3840, 17), Team::BLUE }, KO{ Vec(0, -4608, 17), Team::BLUE },
		KO{ Vec(256, 3840, 17), Team::ORANGE }, KO{ Vec(-256, 3840, 17), Team::ORANGE }, KO{ Vec(0, 4608, 17), Team::ORANGE } });
	CHECK(KickoffUtil::IsKickoffTaker(off.players[0], off) && KickoffUtil::IsKickoffTaker(off.players[1], off) && !KickoffUtil::IsKickoffTaker(off.players[2], off));
}

TEST(KickoffOutcome_a_goal_in_the_window_is_plus_one_for_the_scorer_and_minus_one_for_the_conceder) {
	Drive d(Spawn1v1());
	std::vector<float> v;
	for (int k = 1; k <= 20; k++) {
		bool goal = (k == 12);
		// blue touched on step 3, the ball ends in the orange goal (y > 0) on step 12
		GameState gs = Make({ KO{ Vec(-1000, 0, 17), Team::BLUE, k == 3 }, KO{ Vec(800, 0, 17), Team::ORANGE } }, Vec(0, goal ? 5300.f : 1000.f, 93), Vec(0, 3000, 0), 1, goal);
		v = d.Step(gs);
		if (v[0] != 0 || v[1] != 0) { CHECK_EQ(k, 12); break; }
	}
	CHECK_EQ(v[0], 1.0f); CHECK_EQ(v[1], -1.0f);
	// and the other way: the ball in the BLUE goal (y < 0) -> orange +1
	Drive d2(Spawn1v1());
	for (int k = 1; k <= 20; k++) {
		bool goal = (k == 12);
		GameState gs = Make({ KO{ Vec(-1000, 0, 17), Team::BLUE }, KO{ Vec(1000, 0, 17), Team::ORANGE, k == 3 } }, Vec(0, goal ? -5300.f : -1000.f, 93), Vec(0, -3000, 0), 2, goal);
		v = d2.Step(gs);
		if (v[0] != 0 || v[1] != 0) break;
	}
	CHECK_EQ(v[0], -1.0f); CHECK_EQ(v[1], 1.0f);
}

TEST(KickoffOutcome_the_score_is_clipped_to_minus_one_one) {
	// every term maxed: field 1 (ball.y >= 2500), momentum 1, possess 1, first 1 -> 0.4 + 0.2 + 0.15 + 0.15 = 0.9 (+0.1 coverage with 2 cars) = 1.0
	GameState init = Make({ KO{ Vec(-1000, -2000, 17), Team::BLUE }, KO{ Vec(1000, 2000, 17), Team::ORANGE }, KO{ Vec(-1500, -2500, 17), Team::BLUE }, KO{ Vec(1500, 2500, 17), Team::ORANGE } });
	Drive d(init);
	std::vector<float> v;
	for (int k = 1; k <= 60; k++) {
		GameState gs = Make({ KO{ Vec(0, 2400, 17), Team::BLUE, k == 3 }, KO{ Vec(1000, 4000, 17), Team::ORANGE }, KO{ Vec(-1500, -2500, 17), Team::BLUE }, KO{ Vec(1500, 4500, 17), Team::ORANGE } },
			Vec(0, 4000, 93), Vec(0, 6000, 0), 1);
		v = d.Step(gs);
		if (v[0] != 0) break;
	}
	CHECK_NEAR(v[0], 1.0f, 1e-6f);                      // field clipped to 1: 0.4 + 0.2 + 0.15 + 0.15 + 0.1 = 1.0
	CHECK(v[1] >= -1.0f);
}

// Standoff: nobody touches for 3.5 s: everyone is charged 0.6 once (takers 100%, others 50%) and the kickoff closes.
TEST(KickoffOutcome_a_standoff_charges_everyone_minus_0p6_once_and_closes_the_kickoff) {
	KickoffOutcomeReward::GetAndResetMetrics();
	GameState init = Make({ KO{ Vec(-2048, -2560, 17), Team::BLUE }, KO{ Vec(2048, 2560, 17), Team::ORANGE }, KO{ Vec(0, -4608, 17), Team::BLUE }, KO{ Vec(0, 4608, 17), Team::ORANGE } });
	Drive d(init);
	int at = -1; std::vector<float> v;
	for (int k = 1; k <= 100; k++) {
		bool lateTouch = (k == 70);
		GameState gs = Make({ KO{ Vec(-2048, -2560, 17), Team::BLUE, lateTouch }, KO{ Vec(2048, 2560, 17), Team::ORANGE }, KO{ Vec(0, -4608, 17), Team::BLUE }, KO{ Vec(0, 4608, 17), Team::ORANGE } });
		auto w = d.Step(gs);
		if (at < 0 && (w[0] != 0 || w[1] != 0)) { at = k; v = w; }
		else if (at >= 0) { for (float x : w) CHECK_EQ(x, 0.0f); }   // closed: nothing later, not even for the touch on step 70
	}
	std::cout << "    standoff charged on step " << at << " (3.5 s = 52.5 steps)" << std::endl;
	CHECK(at == 53 || at == 54);
	CHECK_NEAR(v[0], -0.6f, 1e-6f); CHECK_NEAR(v[1], -0.6f, 1e-6f);   // takers
	CHECK_NEAR(v[2], -0.3f, 1e-6f); CHECK_NEAR(v[3], -0.3f, 1e-6f);   // support share
	auto m = KickoffOutcomeReward::GetAndResetMetrics();
	CHECK_EQ(m.standoffs, 1LL); CHECK_EQ(m.resolved, 0LL);
}

// A touch just after the standoff window still closes nothing: a slow-but-contesting kickoff is charged -0.6 even though a touch follows
TEST(KickoffOutcome_a_touch_after_the_standoff_is_not_scored) {
	Drive d(Spawn1v1());
	float total = 0;
	for (int k = 1; k <= 100; k++) {
		GameState gs = Make({ KO{ Vec(-1000, 0, 17), Team::BLUE, k == 58 }, KO{ Vec(800, 0, 17), Team::ORANGE } }, Vec(0, 800, 93), Vec(0, 500, 0), 1);
		auto v = d.Step(gs);
		total += v[0] + v[1];
	}
	CHECK_NEAR(total, -1.2f, 1e-5f);   // only the standoff: -0.6 each (1v1: both takers)
}

TEST(KickoffOutcome_an_episode_ending_inside_the_window_pays_at_the_terminal_step) {
	Drive d(Spawn1v1());
	std::vector<float> at8;
	for (int k = 1; k <= 10; k++) {
		GameState gs = Make({ KO{ Vec(-1000, 0, 17), Team::BLUE, k == 3 }, KO{ Vec(800, 0, 17), Team::ORANGE } }, Vec(0, 1000, 93), Vec(0, 1150, 0), 1);
		auto v = d.Step(gs, /*isFinal=*/k == 8);
		if (k < 8) { CHECK_EQ(v[0], 0.0f); }
		if (k == 8) at8 = v;
		if (k > 8) { CHECK_EQ(v[0], 0.0f); }
	}
	CHECK_NEAR(at8[0], 0.585f, 1e-5f);   // paid on step 8, well before the 2 s horizon
}

TEST(KickoffOutcome_an_episode_ending_before_any_touch_pays_nothing) {
	Drive d(Spawn1v1());
	for (int k = 1; k <= 10; k++) {
		GameState gs = Make({ KO{ Vec(-1000, 0, 17), Team::BLUE }, KO{ Vec(800, 0, 17), Team::ORANGE } });
		auto v = d.Step(gs, /*isFinal=*/k == 10);
		CHECK_EQ(v[0], 0.0f); CHECK_EQ(v[1], 0.0f);
	}
}

TEST(KickoffOutcome_metrics_count_modes_and_reset) {
	KickoffOutcomeReward::GetAndResetMetrics();
	for (int n = 1; n <= 3; n++) {
		std::vector<KO> ps; for (int i = 0; i < n; i++) { ps.push_back(KO{ Vec(-1000.f - 100 * i, 0, 17), Team::BLUE }); ps.push_back(KO{ Vec(1000.f + 100 * i, 0, 17), Team::ORANGE }); }
		Drive d(Make(ps));
		for (int k = 1; k <= 60; k++) {
			std::vector<KO> q = ps; q[0].touch = (k == 2);
			GameState gs = Make(q, Vec(0, 800, 93), Vec(0, 400, 0), 1);
			d.Step(gs);
		}
	}
	auto m = KickoffOutcomeReward::GetAndResetMetrics();
	CHECK_EQ(m.armed, 3LL); CHECK_EQ(m.resolved, 3LL);
	CHECK_EQ(m.modeResolved[0], 1LL); CHECK_EQ(m.modeResolved[1], 1LL); CHECK_EQ(m.modeResolved[2], 1LL);
	auto m2 = KickoffOutcomeReward::GetAndResetMetrics();
	CHECK_EQ(m2.armed, 0LL); CHECK_EQ(m2.resolved, 0LL);   // reading resets
}
