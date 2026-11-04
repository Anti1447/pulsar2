#include "TestFramework.h"
#include "RewardTestUtil.h"

#include "../GigaLearnCPP/src/private/GigaLearnCPP/ExternalPlayMonitor.h"

using namespace RLGC;
using namespace GGL;
using S = ExternalPlayStats;

// Fabricated-state tests for the per-bot play statistics (ExternalPlayMonitor): the counters add exactly what
// the states say. Bot = ORANGE unless stated; 1v1 arenas; tickSkip 8 (15 steps per second).

namespace {
	GameState Arena1v1(Vec ball = Vec(0, 0, 93), Vec ballVel = Vec(0, 0, 0)) {
		GameState gs = {};
		gs.ball.pos = ball;
		gs.ball.vel = ballVel;
		gs.players.push_back(RewardTest::MakePlayer(1, 0, Team::BLUE, Vec(-500, -2000, 17)));
		gs.players.push_back(RewardTest::MakePlayer(2, 1, Team::ORANGE, Vec(500, 2000, 17)));
		return gs;
	}
	const std::vector<uint8_t> NO = { 0 }, YES = { 1 };
}

TEST(PlayMonitor_kickoff_first_touch_by_bot_and_time) {
	ExternalPlayStats st;
	ExternalPlayMonitor m;
	m.BeginServe(1);
	GameState gs = Arena1v1();
	m.Step({ gs }, YES, NO, Team::ORANGE, 8, st);       // step 1: episode starts, ball at rest in the centre
	m.Step({ gs }, NO, NO, Team::ORANGE, 8, st);        // step 2: nobody
	gs.players[1].ballTouchedStep = true;
	m.Step({ gs }, NO, NO, Team::ORANGE, 8, st);        // step 3: the bot touches
	gs.players[0].ballTouchedStep = true;
	m.Step({ gs }, NO, NO, Team::ORANGE, 8, st);        // later touch by us must not count as a kickoff touch
	CHECK_EQ(st.Get(S::KICKOFFS), (int64_t)1);
	CHECK_EQ(st.Get(S::KICKOFF_BOT_FIRST), (int64_t)1);
	CHECK_EQ(st.Get(S::KICKOFF_US_FIRST), (int64_t)0);
	CHECK_EQ(st.Get(S::KICKOFF_TOUCH_STEPS), (int64_t)3);
	// 3 steps x 8 ticks / 120 = 0.2 s
	for (auto& kv : st.Report(8))
		if (kv.first == "Kickoff Mean Touch Time s")
			CHECK_NEAR(kv.second, 0.2f, 1e-5f);
}

TEST(PlayMonitor_kickoff_us_first_contested_and_no_touch) {
	ExternalPlayStats st;
	ExternalPlayMonitor m;
	// Us first
	m.BeginServe(1);
	GameState gs = Arena1v1();
	m.Step({ gs }, YES, NO, Team::ORANGE, 8, st);
	gs.players[0].ballTouchedStep = true;
	m.Step({ gs }, NO, NO, Team::ORANGE, 8, st);
	CHECK_EQ(st.Get(S::KICKOFF_US_FIRST), (int64_t)1);
	CHECK_EQ(st.Get(S::KICKOFF_BOT_FIRST), (int64_t)0);
	// A fresh kickoff touched by both in the same step: counted for both (a contested ball)
	GameState c = Arena1v1();
	m.Step({ c }, YES, YES, Team::ORANGE, 8, st);       // the previous episode ends, a new one starts in the same call
	// (terminalNow closes the episode that just started: one step, no touch)
	CHECK_EQ(st.Get(S::KICKOFFS), (int64_t)2);
	CHECK_EQ(st.Get(S::KICKOFF_NO_TOUCH), (int64_t)1);
	m.Step({ c }, YES, NO, Team::ORANGE, 8, st);
	c.players[0].ballTouchedStep = c.players[1].ballTouchedStep = true;
	m.Step({ c }, NO, NO, Team::ORANGE, 8, st);
	CHECK_EQ(st.Get(S::KICKOFF_BOT_FIRST), (int64_t)1);
	CHECK_EQ(st.Get(S::KICKOFF_US_FIRST), (int64_t)2);
}

TEST(PlayMonitor_episode_joined_mid_way_is_not_a_kickoff) {
	ExternalPlayStats st;
	ExternalPlayMonitor m;
	m.BeginServe(1);
	GameState gs = Arena1v1(Vec(1000, 1000, 400), Vec(0, 800, 0)); // serve began mid-play
	gs.players[1].ballTouchedStep = true;
	m.Step({ gs }, NO, NO, Team::ORANGE, 8, st);
	CHECK_EQ(st.Get(S::KICKOFFS), (int64_t)0);
	CHECK_EQ(st.Get(S::KICKOFF_BOT_FIRST), (int64_t)0);
	CHECK_EQ(st.Get(S::TOUCH_STEPS_BOT), (int64_t)1); // still counted as play
	// A fresh episode whose ball is NOT at rest in the centre (a random state) is not a kickoff either
	GameState r = Arena1v1(Vec(1500, -800, 200), Vec(300, 0, 0));
	m.Step({ r }, YES, NO, Team::ORANGE, 8, st);
	CHECK_EQ(st.Get(S::KICKOFFS), (int64_t)0);
}

TEST(PlayMonitor_goals_by_mode_side_and_kickoff_window) {
	ExternalPlayStats st;
	ExternalPlayMonitor m;
	m.BeginServe(1);
	// Bot ORANGE. Ball in the orange goal (y > 0) = orange conceded = we scored.
	GameState gs = Arena1v1();
	m.Step({ gs }, YES, NO, Team::ORANGE, 8, st);
	for (int i = 0; i < 20; i++) // 21 steps in all = 1.4 s
		m.Step({ gs }, NO, NO, Team::ORANGE, 8, st);
	GameState g = Arena1v1(Vec(0, 5200, 100));
	g.goalScored = true;
	m.Step({ g }, NO, YES, Team::ORANGE, 8, st);
	CHECK_EQ(st.Get(S::GF_1V1), (int64_t)1);
	CHECK_EQ(st.Get(S::GA_1V1), (int64_t)0);
	CHECK_EQ(st.Get(S::GF_BOT_ORANGE), (int64_t)1);
	CHECK_EQ(st.Get(S::KICKOFF_GF_5S), (int64_t)1);
	// Next kickoff, the bot scores after 6 s (outside the window): counted as a goal, not as a 5 s goal
	m.Step({ gs }, YES, NO, Team::ORANGE, 8, st);
	for (int i = 0; i < 90; i++)
		m.Step({ gs }, NO, NO, Team::ORANGE, 8, st);
	GameState ga = Arena1v1(Vec(0, -5200, 100));
	ga.goalScored = true;
	m.Step({ ga }, NO, YES, Team::ORANGE, 8, st);
	CHECK_EQ(st.Get(S::GA_1V1), (int64_t)1);
	CHECK_EQ(st.Get(S::GA_BOT_ORANGE), (int64_t)1);
	CHECK_EQ(st.Get(S::KICKOFF_GA_5S), (int64_t)0);
	// Report: GD per 1k steps in 1v1 = (1 - 1) = 0; steps counted every call
	CHECK_EQ(st.Get(S::STEPS_1V1), (int64_t)(1 + 20 + 1 + 1 + 90 + 1));
}

TEST(PlayMonitor_team_sizes_go_to_their_own_mode_counters) {
	ExternalPlayStats st;
	ExternalPlayMonitor m;
	m.BeginServe(2);
	GameState a = Arena1v1();
	GameState b = {};
	b.ball.pos = Vec(0, 0, 93);
	for (int i = 0; i < 3; i++) {
		b.players.push_back(RewardTest::MakePlayer(10 + i, i, Team::BLUE, Vec(-300.f * i, -2000, 17)));
		b.players.push_back(RewardTest::MakePlayer(20 + i, 3 + i, Team::ORANGE, Vec(300.f * i, 2000, 17)));
	}
	b.goalScored = true;
	b.ball.pos = Vec(0, -5200, 100);
	m.Step({ a, b }, { 0, 0 }, { 0, 1 }, Team::BLUE, 8, st);
	CHECK_EQ(st.Get(S::STEPS_1V1), (int64_t)1);
	CHECK_EQ(st.Get(S::STEPS_3V3), (int64_t)1);
	CHECK_EQ(st.Get(S::STEPS_2V2), (int64_t)0);
	// ball in y<0 = blue's goal; the bot is BLUE, so the bot conceded: we scored in 3v3
	CHECK_EQ(st.Get(S::GF_3V3), (int64_t)1);
	CHECK_EQ(st.Get(S::GF_BOT_BLUE), (int64_t)1);
}

TEST(PlayMonitor_touch_half_shots_demos_by_side) {
	ExternalPlayStats st;
	ExternalPlayMonitor m;
	m.BeginServe(1);
	GameState gs = Arena1v1(Vec(0, 2500, 93)); // orange half
	gs.players[0].ballTouchedStep = true;
	gs.players[1].eventState.shot = true;
	gs.players[0].eventState.demo = true;
	m.Step({ gs }, NO, NO, Team::ORANGE, 8, st);
	GameState h = Arena1v1(Vec(0, -2500, 93)); // blue half
	h.players[1].ballTouchedStep = true;
	m.Step({ h }, NO, NO, Team::ORANGE, 8, st);
	CHECK_EQ(st.Get(S::BALL_STEPS_BOT_HALF), (int64_t)1);
	CHECK_EQ(st.Get(S::TOUCH_STEPS_US), (int64_t)1);
	CHECK_EQ(st.Get(S::TOUCH_STEPS_BOT), (int64_t)1);
	CHECK_EQ(st.Get(S::SHOTS_BOT), (int64_t)1);
	CHECK_EQ(st.Get(S::SHOTS_US), (int64_t)0);
	CHECK_EQ(st.Get(S::DEMOS_US), (int64_t)1);
	bool sawShare = false;
	for (auto& kv : st.Report(8))
		if (kv.first == "Touch Share Bot %") {
			sawShare = true;
			CHECK_NEAR(kv.second, 50.f, 1e-4f);
		}
	CHECK(sawShare);
}

TEST(PlayMonitor_report_omits_ratios_with_empty_denominator) {
	ExternalPlayStats st;
	CHECK(st.Report(8).empty());
}
