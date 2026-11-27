#include "TestFramework.h"
#include "RewardTestUtil.h"

#include <RLGymCPP/Rewards/WavedashGainReward.h>

using namespace RLGC;

// WavedashGainReward on fabricated tick-4 state sequences (dt = 1/30 s). The ball is behind the car unless a case says otherwise,
// so the base value is read without the aim bonus.

namespace {
	constexpr float DT = 4 / 120.f;

	struct Frame { Vec pos, vel; bool ground, flipping; };

	Frame Ground(float speed, float z = 17) { return { Vec(0, 0, z), Vec(0, speed, 0), true, false }; }
	Frame Dodge(float speed, float z = 40) { return { Vec(0, 0, z), Vec(0, speed, 0), false, true }; }

	// Feeds the frames through the reward as EnvSet does; returns the value of every step
	std::vector<float> Play(const std::vector<Frame>& frames, Vec ball = Vec(0, -3000, 93)) {
		WavedashGainReward reward;
		std::vector<Player> players;
		for (const Frame& f : frames) {
			Player p = RewardTest::MakePlayer(1, 0, Team::BLUE, f.pos);
			p.vel = f.vel;
			p.isOnGround = f.ground;
			p.isFlipping = f.flipping;
			p.isDemoed = false;
			players.push_back(p);
		}
		std::vector<float> out;
		for (size_t i = 0; i < players.size(); i++) {
			players[i].prev = i ? &players[i - 1] : nullptr;
			GameState s = {};
			s.deltaTime = DT;
			s.ball.pos = ball;
			s.players = { players[i] };
			if (i == 0) reward.Reset(s);
			reward.PreStep(s);
			out.push_back(reward.GetReward(s.players[0], s, false));
		}
		return out;
	}
	float Total(const std::vector<float>& v) { float t = 0; for (float x : v) t += x; return t; }
	void Repeat(std::vector<Frame>& v, Frame f, int n) { for (int i = 0; i < n; i++) v.push_back(f); }
}

TEST(WavedashGain_pays_the_speed_gained_on_the_landing_step) {
	std::vector<Frame> f;
	Repeat(f, Ground(1200), 5);
	f.push_back(Dodge(1650));
	f.push_back(Ground(1600)); // lands 400 above the 1200 it had
	Repeat(f, Ground(1600), 5);
	auto v = Play(f);
	CHECK_NEAR(v[6], 400.f / 500.f, 0.03f); // z 17 -> 17: no height term
	CHECK_NEAR(Total(v), v[6], 1e-6f);
}

TEST(WavedashGain_caps_at_one_and_pays_nothing_without_a_gain) {
	std::vector<Frame> big;
	Repeat(big, Ground(500), 5);
	big.push_back(Dodge(1500));
	big.push_back(Ground(1500));
	CHECK_NEAR(Total(Play(big)), 1.f, 1e-6f);
	std::vector<Frame> none; // at the speed limit: nothing left to gain
	Repeat(none, Ground(2300), 5);
	none.push_back(Dodge(2300));
	none.push_back(Ground(2295));
	CHECK_NEAR(Total(Play(none)), 0.f, 0.f);
	std::vector<Frame> loses;
	Repeat(loses, Ground(1500), 5);
	loses.push_back(Dodge(1300));
	loses.push_back(Ground(1100));
	CHECK_NEAR(Total(Play(loses)), 0.f, 0.f);
}

TEST(WavedashGain_braking_then_dashing_back_pays_nothing) {
	std::vector<Frame> f;
	for (int cycle = 0; cycle < 10; cycle++) {
		Repeat(f, Ground(1600), 2);
		Repeat(f, Ground(1100), 4); // brake
		f.push_back(Dodge(1600));
		f.push_back(Ground(1600)); // only back to where it was 0.2 s ago
	}
	CHECK_NEAR(Total(Play(f)), 0.f, 1e-6f);
	// after more than the window at the low speed, getting the speed back counts again
	std::vector<Frame> slow;
	Repeat(slow, Ground(1600), 3);
	Repeat(slow, Ground(1100), 40);
	slow.push_back(Dodge(1600));
	slow.push_back(Ground(1600));
	CHECK_NEAR(Total(Play(slow)), 1.f, 1e-6f);
}

TEST(WavedashGain_chained_dashes_are_each_paid_while_they_add_speed) {
	std::vector<Frame> f;
	Repeat(f, Ground(800), 5);
	for (float speed : { 1100.f, 1400.f, 1700.f }) {
		f.push_back(Dodge(speed));
		Repeat(f, Ground(speed), 3);
	}
	CHECK_NEAR(Total(Play(f)), 3 * 300.f / 500.f, 0.05f);
}

TEST(WavedashGain_falling_is_not_a_gain_and_climbing_is_not_a_loss) {
	// dodge from height: the speed picked up by falling is exactly what gravity gives
	float v0 = 1000, z0 = 300, z1 = 20;
	float fallen = sqrtf(v0 * v0 + 2 * 650 * (z0 - z1));
	std::vector<Frame> fall;
	Repeat(fall, Ground(v0, z0), 5);
	fall.push_back(Dodge(fallen, 150));
	fall.push_back(Ground(fallen, z1));
	CHECK_NEAR(Total(Play(fall)), 0.f, 0.01f);
	// up a wall: same speed 150 uu higher is a real gain
	std::vector<Frame> climb;
	Repeat(climb, Ground(1400, 500), 5);
	climb.push_back(Dodge(1400, 580));
	climb.push_back(Ground(1400, 650));
	float expected = (1400 - sqrtf(1400.f * 1400 - 2 * 650 * 150)) / 500;
	CHECK_NEAR(Total(Play(climb)), expected, 0.01f);
}

TEST(WavedashGain_ball_aim_adds_up_to_a_quarter) {
	std::vector<Frame> f;
	Repeat(f, Ground(1200), 5);
	f.push_back(Dodge(1450));
	f.push_back(Ground(1450));
	float away = Total(Play(f, Vec(0, -3000, 17)));
	float toward = Total(Play(f, Vec(0, 3000, 17)));
	float sideways = Total(Play(f, Vec(3000, 0, 17)));
	CHECK_NEAR(away, 0.5f, 0.01f);
	CHECK_NEAR(toward, 0.5f * 1.25f, 0.01f);
	CHECK_NEAR(sideways, away, 0.01f);
}

TEST(WavedashGain_no_pay_without_a_dodge_or_after_reset) {
	std::vector<Frame> boost; // speeding up and bouncing without a dodge
	Repeat(boost, Ground(800), 3);
	boost.push_back({ Vec(0, 0, 60), Vec(0, 1300, 0), false, false });
	boost.push_back(Ground(1700));
	CHECK_NEAR(Total(Play(boost)), 0.f, 0.f);
	std::vector<Frame> first = { Dodge(900), Ground(1500) }; // the very first state has nothing before it
	CHECK_NEAR(Total(Play(first)), 0.f, 0.f);
}
