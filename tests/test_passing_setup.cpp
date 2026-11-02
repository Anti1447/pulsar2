#include "TestFramework.h"
#include "RewardTestUtil.h"

#include <RLGymCPP/Rewards/PassingSetupRewards.h>

using namespace RLGC;
using namespace RewardTest;

// PassingSetupReward on engine-style ball prediction. Blue attacks +y. The passer touches at
// (0,-1000,300) and the ball goes out toward the right wing; the teammate stands at the ball's
// 1.5 s landing spot (read from the same path the reward sees, not hard-coded).

namespace {
	struct Scn {
		std::vector<BallState> path; GameState gs; PassingSetupReward r;
		Scn(Vec ballPos, Vec ballVel, Vec teammatePos, Vec oppPos, bool withPred = true) {
			path = BallPath(ballPos, ballVel);
			gs.deltaTime = STEP_DT;
			gs.ball.pos = ballPos; gs.ball.vel = ballVel;
			gs.players = { MakePlayer(1, 0, Team::BLUE, Vec(ballPos.x - 100, ballPos.y - 100, 17)),
			               MakePlayer(2, 1, Team::BLUE, teammatePos),
			               MakePlayer(3, 2, Team::ORANGE, oppPos) };
			gs.players[0].ballTouchedStep = true; gs.lastTouchCarID = 1;
			gs.ballPred = withPred ? &path : nullptr;
			r.Reset(gs);
		}
		float Pay() { r.PreStep(gs); return r.GetReward(gs.players[0], gs, false); }
	};
	const Vec BALL(0, -1000, 300), VEL(900, 700, 350), FAR_OPP(-3000, 3000, 17);
	Vec Landing() { return BallPath(BALL, VEL)[180].pos; } // 1.5 s
	Vec Mate() { Vec l = Landing(); return l + Vec(150, 0, -l.z + 17); }
}

TEST(PassingSetup_golden_open_teammate_at_landing_spot) {
	Scn s(BALL, VEL, Mate(), FAR_OPP);
	float pay = s.Pay();
	CHECK(pay > 2.5f); // primary 2.0 + clean-lob 1.5 minus nothing; measured 2.80
}

TEST(PassingSetup_cooldown_blocks_immediate_second_touch) {
	Scn s(BALL, VEL, Mate(), FAR_OPP);
	CHECK(s.Pay() > 0.0f);
	CHECK_EQ(s.r.GetReward(s.gs.players[0], s.gs, false), 0.0f);
}

TEST(PassingSetup_counterfactual_opponent_on_landing_spot) {
	// Only the opponent moves. The primary term (oppDist - tmDist margin) must collapse.
	Scn open(BALL, VEL, Mate(), FAR_OPP);
	Vec l = Landing();
	Scn blocked(BALL, VEL, Mate(), l + Vec(100, 0, -l.z + 17));
	CHECK(open.Pay() - blocked.Pay() > 1.0f);
}

TEST(PassingSetup_aimless_backward_touch_is_penalized) {
	Scn s(BALL, Vec(0, -900, 350), Vec(2500, 2500, 17), FAR_OPP);
	CHECK_EQ(s.Pay(), -0.2f);
}

TEST(PassingSetup_gates_pay_zero) {
	{ Scn s(BALL, Vec(100, 100, 0), Mate(), FAR_OPP);                      CHECK_EQ(s.Pay(), 0.0f); } // < 400 uu/s
	{ Scn s(BALL, VEL, Vec(BALL.x - 50, BALL.y - 50, 17), FAR_OPP);        CHECK_EQ(s.Pay(), 0.0f); } // teammate within 800 uu
	{ Scn s(BALL, VEL, Mate(), FAR_OPP); s.gs.players[0].ballTouchedStep = false; CHECK_EQ(s.Pay(), 0.0f); } // no touch
}

TEST(PassingSetup_missing_prediction_pays_zero) {
	// Without the port guard the stationary-ball fallback paid MORE (3.5) than the correct 2.8.
	Scn s(BALL, VEL, Mate(), FAR_OPP, /*withPred=*/false);
	CHECK_EQ(s.Pay(), 0.0f);
}
