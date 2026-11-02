#include "TestFramework.h"
#include "RewardTestUtil.h"

#include <RLGymCPP/Rewards/ZeroSumReward.h>
#include <RLGymCPP/Rewards/SupersonicReward.h>
#include <RLGymCPP/Rewards/RolvWavedashReward.h>
#include <RLGymCPP/Rewards/PitchControlAerialReward.h>

using namespace RLGC;
using namespace RewardTest;

// Audit scenarios for the three ported movement rewards (docs/reward_scenarios/*_2026-11-01.md).
// All expected values are hand-computed from the class constants before running. Fixture: one car,
// deltaTime = 8/120 (tickSkip 8). "Under this fixture" - scripted states, not engine physics.

namespace {
	struct Fr {
		Vec pos = Vec(0, 0, 17), vel = Vec(0, 0, 0);
		bool ground = true, flipping = false, doubleJumped = false;
		float pitchDeg = 0.f, boostAction = 0.f;
	};

	// Runs frames through `r` (Reset on frame 0, which sees a null prev like the live loop).
	std::vector<float> Run(Reward& r, const std::vector<Fr>& frames) {
		std::vector<GameState> st; st.reserve(frames.size());
		for (const Fr& f : frames) {
			GameState gs; gs.deltaTime = STEP_DT;
			Player p = MakePlayer(1, 0, Team::BLUE, f.pos);
			p.vel = f.vel; p.isOnGround = f.ground; p.isFlipping = f.flipping; p.hasDoubleJumped = f.doubleJumped;
			float a = f.pitchDeg * 3.14159265f / 180.f;
			p.rotMat.forward = Vec(cosf(a), 0, sinf(a));
			p.prevAction.boost = f.boostAction;
			gs.players.push_back(p);
			gs.ball.pos = Vec(0, 0, 93);
			st.push_back(std::move(gs));
		}
		for (size_t i = 1; i < st.size(); i++) {
			st[i].prev = &st[i - 1];
			st[i].players[0].prev = &st[i - 1].players[0];
		}
		r.Reset(st[0]);
		std::vector<float> out;
		for (auto& s : st) { r.PreStep(s); out.push_back(r.GetReward(s.players[0], s, false)); }
		return out;
	}
	float Sum(const std::vector<float>& v) { float s = 0; for (float x : v) s += x; return s; }

	Fr Cruise(float speed) { Fr f; f.vel = Vec(speed, 0, 0); return f; }
}

// =====================================================================================================
// SupersonicReward  (per step: 0.5*dt + min(dur/5,1)*2*dt  while |vel| >= 2200; dur resets below)
// =====================================================================================================

TEST(Supersonic_threshold_is_2200_not_2300) {
	SupersonicReward r; auto v = Run(r, { Cruise(2199), Cruise(2199) });
	CHECK_EQ(v[1], 0.0f);
	SupersonicReward r2; auto w = Run(r2, { Cruise(2200), Cruise(2200) });
	CHECK(w[1] > 0.0f);
}

TEST(Supersonic_first_step_value) {
	// dt = 1/15. dur = dt. base = 0.5*dt = 0.033333. bonus = min(dt/5,1)*2 = 0.026667; sustain = bonus*dt = 0.0017778.
	SupersonicReward r; auto v = Run(r, { Cruise(2200) });
	CHECK_NEAR(v[0], 0.035111f, 1e-4f);
}

TEST(Supersonic_ramps_to_2p5_per_second_after_5s) {
	// After 5 s the bonus term is 2.0: per step (0.5 + 2.0) * dt = 0.16667 = 2.5 per second.
	std::vector<Fr> fr(90, Cruise(2300)); SupersonicReward r; auto v = Run(r, fr);
	CHECK_NEAR(v[85], 2.5f * STEP_DT, 1e-4f);
}

TEST(Supersonic_one_slow_step_resets_the_ramp) {
	std::vector<Fr> fr(80, Cruise(2300)); fr.push_back(Cruise(2100)); fr.push_back(Cruise(2300));
	SupersonicReward r; auto v = Run(r, fr);
	CHECK_EQ(v[80], 0.0f);
	CHECK_NEAR(v[81], 0.035111f, 1e-4f); // back to the first-step value
}

TEST(Supersonic_episode_income_at_v3_weight_is_about_one_goal) {
	// 30 s at supersonic = 450 steps at 15 Hz. Hand integral: 0.5*30 + (5 + 50) = 70 raw. At the v3 Gold
	// weight 2.0 that is ~140 per episode: nearly Goal (150) from speed alone, unwrapped.
	std::vector<Fr> fr(450, Cruise(2300)); SupersonicReward r; float total = Sum(Run(r, fr));
	CHECK(total > 68.0f && total < 72.0f);
	CHECK(total * 2.0f > 130.0f);
}

TEST(Supersonic_no_demo_gate) {
	// A demoed car that still carries velocity keeps being paid (the class never reads isDemoed).
	GameState gs; gs.deltaTime = STEP_DT;
	Player p = MakePlayer(1, 0, Team::BLUE, Vec(0, 0, 17)); p.vel = Vec(2300, 0, 0); p.isDemoed = true;
	gs.players.push_back(p);
	SupersonicReward r; r.Reset(gs); CHECK(r.GetReward(gs.players[0], gs, false) > 0.0f);
}

TEST(Supersonic_unwrapped_pays_both_players_wrapped_cancels) {
	// ZeroSumReward's Reset/GetAllRewards are protected: drive it through the Reward base like EnvSet does.
	auto All = [](Reward& r, const GameState& g) { r.Reset(g); return r.GetAllRewards(g, false); };
	GameState gs; gs.deltaTime = STEP_DT;
	gs.players = { MakePlayer(1, 0, Team::BLUE, Vec(0, 0, 17)), MakePlayer(2, 1, Team::ORANGE, Vec(0, 3000, 17)) };
	gs.players[0].vel = Vec(2300, 0, 0); gs.players[1].vel = Vec(-2300, 0, 0);
	SupersonicReward bare;
	auto b = All(bare, gs);
	CHECK(b[0] > 0.0f && b[1] > 0.0f);                 // both paid: the stack no longer sums to 0
	ZeroSumReward wrapped(new SupersonicReward(), 0.3f);
	auto w = All(wrapped, gs);
	CHECK_NEAR(w[0] + w[1], 0.0f, 1e-6f);               // symmetric speed: nothing left
	gs.players[1].vel = Vec(-1000, 0, 0);               // only one supersonic: a pure differential
	ZeroSumReward w2(new SupersonicReward(), 0.3f);
	auto d = All(w2, gs);
	CHECK(d[0] > 0.0f && d[1] < 0.0f);
	CHECK_NEAR(d[0] + d[1], 0.0f, 1e-6f);
}

// =====================================================================================================
// RolvWavedashReward  (pays |dv at flip start| / 2300 on a landing step where the car is or was flipping)
// =====================================================================================================

namespace {
	// ground, jump (air), flip starts with a 500 uu/s kick, flipping..., landing (prev flipping).
	std::vector<Fr> HopFlipLand(float kick, int flippingSteps = 6) {
		std::vector<Fr> v;
		Fr g; v.push_back(g);
		Fr jump; jump.ground = false; jump.pos = Vec(0, 0, 60); v.push_back(jump);
		Fr start = jump; start.flipping = true; start.vel = Vec(kick, 0, 0); start.pos.z = 100; v.push_back(start);
		for (int i = 0; i < flippingSteps; i++) { Fr m = start; m.pos.z = 120; v.push_back(m); }
		Fr land; land.ground = true; land.flipping = false; land.vel = Vec(kick, 0, 0); v.push_back(land);
		return v;
	}
}

TEST(RolvWavedash_golden_landing_in_a_flip_pays_dv_over_2300) {
	RolvWavedashReward r; auto v = Run(r, HopFlipLand(500.f));
	CHECK_NEAR(Sum(v), 500.f / 2300.f, 1e-5f); // 0.21739, paid on the landing step only
	CHECK_NEAR(v.back(), 500.f / 2300.f, 1e-5f);
}

TEST(RolvWavedash_counterfactual_gates) {
	{ RolvWavedashReward r; auto v = Run(r, HopFlipLand(0.f));  CHECK_EQ(Sum(v), 0.0f); }   // flip with no velocity change: 0
	{ // lands without flipping at all
	  std::vector<Fr> fr; Fr g; fr.push_back(g); Fr a = g; a.ground = false; a.pos.z = 80; fr.push_back(a); fr.push_back(g);
	  RolvWavedashReward r; CHECK_EQ(Sum(Run(r, fr)), 0.0f); }
	{ // flip ends in the air (isFlipping false) 2 steps before landing: stored value is cleared, landing pays 0
	  auto fr = HopFlipLand(500.f); fr[fr.size() - 2].flipping = false; fr[fr.size() - 3].flipping = false;
	  RolvWavedashReward r; CHECK_EQ(Sum(Run(r, fr)), 0.0f); }
	{ RolvWavedashReward r(false); auto v = Run(r, HopFlipLand(0.f)); CHECK_EQ(Sum(v), 1.0f); } // basic mode: flat 1.0
	{ RolvWavedashReward r; auto v = Run(r, { Fr() }); CHECK_EQ(v[0], 0.0f); }               // null prev on frame 0
}

TEST(RolvWavedash_no_cooldown_every_landing_in_a_flip_repays) {
	// There is no handbrake, ground-proximity, ball or cooldown condition: every landing step with isFlipping set pays.
	// CAUTION (corrected after the engine probe, tests/test_wavedash_engine.cpp): in the real engine an ordinary jump-flip
	// hop does NOT reach this state - its flip is over long before touchdown. Only a flip pressed within ~3 ticks of
	// landing does. This fixture scripts that state directly; it tests the missing cooldown, not how often it occurs.
	std::vector<Fr> loop;
	for (int i = 0; i < 10; i++) { auto h = HopFlipLand(500.f, 5); loop.insert(loop.end(), h.begin(), h.end()); }
	RolvWavedashReward r; float total = Sum(Run(r, loop));
	CHECK_NEAR(total, 10.f * 500.f / 2300.f, 1e-4f); // every hop paid in full: no anti-farm state
	// 0.2174 * 20 = 4.35 per event at weight 20, unwrapped. The engine pays only ~35-60% of true last-moment flips at 15 Hz.
}

// =====================================================================================================
// PitchControlAerialReward  (jump -> pitch 25..65 deg -> hasDoubleJumped within 0.55 s; paid the step after)
// =====================================================================================================

namespace {
	// ground, liftoff z=30, pitched 30 deg (+boost) z=80, double jump z=150, payout step z=400.
	std::vector<Fr> FastAerial(float pitch, bool boost, bool doubleJump = true) {
		std::vector<Fr> v;
		Fr g; v.push_back(g);
		Fr lift; lift.ground = false; lift.pos = Vec(0, 0, 30); v.push_back(lift);
		Fr p = lift; p.pos.z = 80; p.pitchDeg = pitch; p.boostAction = boost ? 1.f : 0.f; v.push_back(p);
		Fr dj = p; dj.pos.z = 150; dj.doubleJumped = doubleJump; dj.boostAction = 0.f; v.push_back(dj);
		Fr pay = dj; pay.pos.z = 400; v.push_back(pay);
		return v;
	}
}

TEST(PitchControlAerial_golden_value_with_and_without_boost) {
	// first jump z = 30, second jump z = 150 -> heightGained 120 -> 0.005*120 = 0.6.
	// pitch 30 deg: acc = 1 - ((30-45)/20)^2 = 0.4375 -> pitchBonus = min(1, 0.4375*2) = 0.875.
	// base 2.0 + 0.6 + 0.875 = 3.475; x1.5 with boost seen during the ascent = 5.2125.
	{ PitchControlAerialReward r; auto v = Run(r, FastAerial(30.f, false)); CHECK_NEAR(Sum(v), 3.475f, 2e-3f); CHECK_NEAR(v.back(), 3.475f, 2e-3f); }
	{ PitchControlAerialReward r; auto v = Run(r, FastAerial(30.f, true));  CHECK_NEAR(Sum(v), 5.2125f, 3e-3f); }
}

TEST(PitchControlAerial_gates) {
	{ PitchControlAerialReward r; CHECK_EQ(Sum(Run(r, FastAerial(10.f, true))), 0.0f); }          // pitch below 25 deg: never PITCHED_UP
	{ PitchControlAerialReward r; CHECK_EQ(Sum(Run(r, FastAerial(80.f, true))), 0.0f); }          // above 65 deg
	{ PitchControlAerialReward r; CHECK_EQ(Sum(Run(r, FastAerial(30.f, true, false))), 0.0f); }   // a flip (no hasDoubleJumped)
	{ // second jump after 0.6 s (window 0.55 s): liftoff, pitch, 7 idle air steps, then the double jump
	  auto fr = FastAerial(30.f, true); Fr idle = fr[2]; idle.boostAction = 0.f;
	  Fr dj = fr[3]; Fr pay = fr[4]; fr.resize(3);
	  for (int i = 0; i < 7; i++) fr.push_back(idle);
	  fr.push_back(dj); fr.push_back(pay);
	  PitchControlAerialReward r; CHECK_EQ(Sum(Run(r, fr)), 0.0f); }
	{ // first step of an episode has no prev
	  PitchControlAerialReward r; auto v = Run(r, { Fr() }); CHECK_EQ(v[0], 0.0f); }
}

TEST(PitchControlAerial_height_bonus_ignores_climb_after_the_double_jump) {
	// The payout fires at the top of the NEXT call, before the "track max height while SECOND_JUMP" block runs,
	// so maxHeightAfterSecondJump is just z at the double-jump step. Raising z on the payout step changes nothing.
	auto lo = FastAerial(30.f, true); auto hi = FastAerial(30.f, true); hi.back().pos.z = 1500.f;
	PitchControlAerialReward a, b;
	CHECK_NEAR(Sum(Run(a, lo)), Sum(Run(b, hi)), 1e-6f);
	// and the bonus is the climb BEFORE the double jump: raise z at the double-jump step and it grows.
	auto taller = FastAerial(30.f, true); taller[3].pos.z = 450.f; taller[4].pos.z = 450.f;
	PitchControlAerialReward c; CHECK(Sum(Run(c, taller)) > Sum(Run(*new PitchControlAerialReward(), lo)));
}

TEST(PitchControlAerial_no_cooldown_each_landing_repays) {
	// After a payout the state is WAITING; a second double jump in the same air time pays nothing, but a fresh
	// liftoff (needs a landing) pays in full. Ten hop-cycles of 15 steps (1.0 s, an ASSUMED minimum real cycle).
	std::vector<Fr> loop;
	for (int i = 0; i < 10; i++) {
		auto c = FastAerial(30.f, true);
		for (int k = 0; k < 9; k++) { Fr a = c.back(); a.doubleJumped = true; c.push_back(a); } // hang in the air
		Fr g; c.push_back(g);                                                                       // land (frame 0 of next cycle is also ground)
		loop.insert(loop.end(), c.begin() + (i ? 1 : 0), c.end());
	}
	PitchControlAerialReward r; float total = Sum(Run(r, loop));
	CHECK_NEAR(total, 10.f * 5.2125f, 0.05f);
	// ~5.2 raw per ~1 s cycle. At the v3 Gold weight 12 that is ~63/s if unwrapped; ZeroSum cancels it only against a mirror farmer.
}
