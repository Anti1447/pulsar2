#include "TestFramework.h"
#include "RewardTestUtil.h"

#include <RLGymCPP/Rewards/DribbleRewards.h>
#include <RLGymCPP/Telemetry/CarryCatchTelemetry.h>

#include <cmath>

using namespace RLGC;
using namespace RewardTest;

// Audit of DribbleMasteryReward, 2026-11-08. Rev 5.0 (this file's expectations) = Rev 4.0 + fix 1 (no 0.5 direction floor)
// + fix 2 (field-position factor 0.3..1.0). The audit-time Rev 4.0 findings are quoted in each test where the value changed. Scenario ladder and hand derivations:
// docs/reward_scenarios/DribbleMasteryReward_2026-11-08.md (D1..D7 + findings 1-4).
// Fixture: fabricated states, no engine. A "carry" frame puts the ball at car-frame (lr, lf, lu) on a flat grounded car
// moving at `speed` along `fwd`, ball velocity = car velocity + relVel. Frame i (0-based) has carry age t = (i+1) * STEP_DT.
// Default carry sits near the opponent goal (y = 4800, field factor 1.0) so the Rev 4.0 shape numbers below still hold;
// the field-position factor is tested separately (Dribble_fix2_*).
// Hand anchors (all raw, before weight 0.5 and the wrapper): entry 0.03; steady maximum 0.4 + 0.0667 = 0.4667 per step
// from t = 4 s; taper over 6..10 s; cumulative 24.6 at 6 s and 38.9 at 10 s.

namespace {
	struct Carry {
		Vec carPos = Vec(0, 4800, 17);
		Vec fwd = Vec(0, 1, 0);
		float speed = 1800;
		float lr = 0, lf = 40, lu = 124;       // ball position in the car frame
		Vec relVel = Vec(0, 0, 0);             // ball velocity minus car velocity
		bool ground = true;
		bool oppTouch = false, mateTouch = false;
		bool hasOpp = false;
		Vec oppPos = Vec(3000, -4000, 17), oppVel = Vec(0, 0, 0), oppFwd = Vec(0, 1, 0);
	};

	void Fill(GameState& g, const Carry& c) {
		g.deltaTime = STEP_DT;
		Vec right(c.fwd.y, -c.fwd.x, 0);
		Vec up(0, 0, 1);
		Player p = MakePlayer(1, 0, Team::BLUE, c.carPos);
		p.rotMat.forward = c.fwd; p.rotMat.right = right; p.rotMat.up = up;
		p.vel = c.fwd * c.speed;
		p.isOnGround = c.ground;
		g.players.push_back(p);
		Player o = MakePlayer(2, 1, Team::ORANGE, c.hasOpp ? c.oppPos : Vec(3000, -4000, 17));
		o.rotMat.forward = c.oppFwd; o.rotMat.right = Vec(c.oppFwd.y, -c.oppFwd.x, 0); o.rotMat.up = up;
		o.vel = c.hasOpp ? c.oppVel : Vec(0, 0, 0);
		o.ballTouchedStep = c.oppTouch;
		g.players.push_back(o);
		if (c.mateTouch) {
			Player m = MakePlayer(3, 2, Team::BLUE, Vec(-3000, -4000, 17));
			m.ballTouchedStep = true;
			g.players.push_back(m);
		}
		g.ball.pos = c.carPos + right * c.lr + c.fwd * c.lf + up * c.lu;
		g.ball.vel = p.vel + c.relVel;
	}

	std::vector<float> Run(DribbleMasteryReward& r, const std::vector<Carry>& fr) {
		std::vector<GameState> st(fr.size());
		for (size_t i = 0; i < fr.size(); i++) Fill(st[i], fr[i]);
		r.Reset(st[0]);
		std::vector<float> out;
		for (auto& s : st) { r.PreStep(s); out.push_back(r.GetReward(s.players[0], s, false)); }
		return out;
	}
	std::vector<Carry> Steady(int n, const Carry& c = Carry()) { return std::vector<Carry>(n, c); }
	float Sum(const std::vector<float>& v, size_t a, size_t b) { float s = 0; for (size_t i = a; i < b && i < v.size(); i++) s += v[i]; return s; }
	// reward at carry age 4 s (the 60th step) of a steady carry described by c
	float At4s(const Carry& c) { DribbleMasteryReward r; return Run(r, Steady(60, c)).back(); }
}

TEST(Dribble_D1_entry_then_quiet_then_ramp) {
	DribbleMasteryReward r;
	auto v = Run(r, Steady(90));
	CHECK_NEAR(v[0], 0.03f, 1e-5f);                       // entry bonus
	for (int i = 1; i <= 4; i++) CHECK_NEAR(v[i], 0.0f, 1e-6f);   // t < 0.4 s
	CHECK(v[6] > 0.0f);                                   // shaping is on by step 7 at the latest (float rounding on the 0.4 gate)
	CHECK(v[6] < 0.01f);                                  // and still tiny: durationFactor = (t/2)^2 is 0.04 at 0.4 s
}

TEST(Dribble_D1b_steady_maximum_at_4s) {
	// base 1, durationFactor 4, 0.08 * 4 * 1.25 (flick-ready) = 0.4, plus boost 1.0 * dt = 0.0667
	DribbleMasteryReward r;
	auto v = Run(r, Steady(60));
	CHECK_NEAR(v.back(), 0.4667f, 0.004f);
}

TEST(Dribble_cumulative_pay_of_one_carry_matches_the_hand_integral) {
	DribbleMasteryReward r;
	auto v = Run(r, Steady(170));
	CHECK_NEAR(Sum(v, 0, 90), 24.6f, 1.5f);               // t = 6 s: 24.6
	CHECK_NEAR(Sum(v, 0, 150), 38.9f, 1.5f);              // t = 10 s: 38.9 (6..10 s taper averages x0.51)
	CHECK(v[169] < 0.0095f);                              // after the taper: 0.4667 * 0.02 = 0.0093 per step
	CHECK(Sum(v, 150, 170) < 0.2f);
}

TEST(Dribble_taper_midpoint) {
	DribbleMasteryReward r;
	auto v = Run(r, Steady(130));
	CHECK_NEAR(v[119], 0.4667f * 0.51f, 0.01f);           // t = 8 s: u = 0.5 -> factor 0.51
}

TEST(Dribble_D2_speed_gate_and_ramp) {
	Carry slow; slow.speed = 799;
	DribbleMasteryReward r;
	auto v = Run(r, Steady(60, slow));
	for (float x : v) CHECK_NEAR(x, 0.0f, 1e-6f);         // 799 < 800: no carry, not even the entry bonus
	Carry c880; c880.speed = 880;
	CHECK_NEAR(At4s(c880), 0.248f, 0.004f);               // speedMult 0.453: 0.181 + 0.0667
}

TEST(Dribble_D3_geometry_gates_at_0p9x_and_1p1x) {
	Carry in; in.lr = 67.5f;                              // 0.9 x the 75 uu lateral gate
	CHECK_NEAR(At4s(in), 0.1067f, 0.003f);                // centering 0.10: 0.1 * 0.08 * 4 = 0.032, x1.25 flick-ready = 0.04, + boost 0.0667
	Carry out; out.lr = 82.5f;
	DribbleMasteryReward r;
	for (float x : Run(r, Steady(30, out))) CHECK_NEAR(x, 0.0f, 1e-6f);
	Carry lo; lo.lu = 90;                                 // 0.9 x 100 below the height band
	DribbleMasteryReward r2;
	for (float x : Run(r2, Steady(30, lo))) CHECK_NEAR(x, 0.0f, 1e-6f);
	Carry hi; hi.lu = 253;                                // 1.1 x 230 above it
	DribbleMasteryReward r3;
	for (float x : Run(r3, Steady(30, hi))) CHECK_NEAR(x, 0.0f, 1e-6f);
	Carry ok; ok.lu = 110;
	CHECK(At4s(ok) > 0.0f);
}

TEST(Dribble_gates_ground_and_ball_height) {
	Carry air; air.ground = false;
	DribbleMasteryReward r;
	for (float x : Run(r, Steady(20, air))) CHECK_NEAR(x, 0.0f, 1e-6f);
	Carry tall; tall.carPos = Vec(0, 0, 600); // ball centre at 600 + 124 = 724 > 600
	DribbleMasteryReward r2;
	for (float x : Run(r2, Steady(20, tall))) CHECK_NEAR(x, 0.0f, 1e-6f);
}

TEST(Dribble_D4_opponent_touch_kills_the_carry_for_12_steps) {
	std::vector<Carry> fr = Steady(60);
	Carry touch; touch.oppTouch = true;
	fr.push_back(touch);                                  // step 61: opponent touch
	for (int i = 0; i < 11; i++) fr.push_back(Carry());   // steps 62..72: cooldown still running (pre-decrement gate)
	for (int i = 0; i < 3; i++) fr.push_back(Carry());    // step 73 on: carry restarts
	DribbleMasteryReward r;
	auto v = Run(r, fr);
	CHECK_NEAR(v[59], 0.4667f, 0.004f);
	for (int i = 60; i <= 71; i++) CHECK_NEAR(v[i], 0.0f, 1e-6f);   // 12 blocked steps including the touch step
	CHECK_NEAR(v[72], 0.03f, 1e-5f);                      // fresh entry, durationFactor back to zero
	CHECK_NEAR(v[73], 0.0f, 1e-6f);
}

TEST(Dribble_D4b_teammate_touch_does_nothing) {
	Carry mate; mate.mateTouch = true;
	CHECK_NEAR(At4s(mate), 0.4667f, 0.004f);
}

TEST(Dribble_D5_opponent_pressure_nets_negative) {
	// opponent 300 uu ahead of the ball, stationary, facing it: closing 1.0 (ball approaches), facing 1.0, closeness 0.5
	Carry c; c.hasOpp = true; c.oppPos = Vec(0, 4800 + 340, 141); c.oppFwd = Vec(0, -1, 0);
	CHECK_NEAR(At4s(c), 0.4667f - 0.5f * (0.5f + 2.0f * 0.4667f), 0.005f);   // -0.25
	// D5b: chaser 300 uu behind at 1500 vs ball 1800: closing 0, facing 1 -> threat 0.4, scale 0.76, threat 0.38
	Carry b; b.hasOpp = true; b.oppPos = Vec(0, 4800 - 260, 141); b.oppVel = Vec(0, 1500, 0); b.oppFwd = Vec(0, 1, 0);
	CHECK_NEAR(At4s(b), 0.4667f - 0.38f * (0.5f + 2.0f * 0.4667f), 0.006f);  // -0.078
	// 590 uu away the penalty is about 0
	Carry far = b; far.oppPos = Vec(0, 4800 - 550, 141);
	CHECK_NEAR(At4s(far), 0.4485f, 0.006f);
}

TEST(Dribble_fix2_pay_rises_toward_the_opponent_net) {
	// Rev 4.0 (audit D6): 0.4667 everywhere. Rev 5.0: x (0.3 + 0.7 * clamp((12000 - d) / 9500, 0, 1)), d = ball to opponent goal back.
	Carry own; own.carPos = Vec(0, -4000, 17);     // d = 9961.6 -> 0.3 + 0.7 * 0.2146 = 0.4502
	Carry mid; mid.carPos = Vec(0, 0, 17);         // d = 5960.8 -> 0.3 + 0.7 * 0.6357 = 0.7450
	Carry edge; edge.carPos = Vec(0, 3500 - 40, 17); // ball at y = 3500, d = 2500.5 -> 0.9999
	Carry near; near.carPos = Vec(0, 4800, 17);    // inside 2500: 1.0
	float a = At4s(own), m = At4s(mid), e = At4s(edge), n = At4s(near);
	CHECK_NEAR(a, 0.4667f * 0.4502f, 0.004f);
	CHECK_NEAR(m, 0.4667f * 0.7450f, 0.004f);
	CHECK_NEAR(e, 0.4667f, 0.005f);
	CHECK_NEAR(n, 0.4667f, 0.004f);
	CHECK(a < m && m < e + 1e-4f && e <= n + 1e-4f);   // monotone toward the net
	CHECK(n > 1.9f * a);                               // a carry at the net pays about 2.2x one at our own goal
}

TEST(Dribble_fix2_field_factor_is_per_team_canonical) {
	// An ORANGE car carrying at y = -4800 (orange attacks -y) gets the same factor as a blue car at +4800: the scene is mirrored in y
	Carry blue; blue.carPos = Vec(0, 4800, 17);
	DribbleMasteryReward r;
	auto fr = Steady(60, blue);
	std::vector<GameState> st(fr.size());
	for (size_t i = 0; i < fr.size(); i++) {
		Fill(st[i], fr[i]);
		// mirror the whole scene in y and swap teams: orange attacking -y
		for (auto& p : st[i].players) { p.pos.y = -p.pos.y; p.vel.y = -p.vel.y; p.rotMat.forward.y = -p.rotMat.forward.y; p.rotMat.right.x = -p.rotMat.right.x;
			p.team = (p.team == Team::BLUE) ? Team::ORANGE : Team::BLUE; }
		st[i].ball.pos.y = -st[i].ball.pos.y; st[i].ball.vel.y = -st[i].ball.vel.y;
	}
	r.Reset(st[0]);
	float last = 0;
	for (auto& s : st) { r.PreStep(s); last = r.GetReward(s.players[0], s, false); }
	CHECK_NEAR(last, 0.4667f, 0.004f);
}

TEST(Dribble_fix1_away_from_the_net_pays_nothing) {
	// Rev 4.0 (audit D6c): 0.2333 (floor 0.5). Rev 5.0: 0.
	Carry away; away.fwd = Vec(0, -1, 0);
	CHECK_NEAR(At4s(away), 0.0f, 1e-6f);
	// sideways too
	Carry side; side.fwd = Vec(1, 0, 0);
	CHECK_NEAR(At4s(side), 0.0f, 0.02f);
	// and an opponent in range still costs the fixed penalty: carrying away under pressure is net negative
	Carry press = away; press.hasOpp = true; press.oppPos = Vec(0, 4800 - 340, 141); press.oppFwd = Vec(0, 1, 0); press.oppVel = Vec(0, 1500, 0);
	CHECK(At4s(press) < -0.1f);
}

TEST(Dribble_fix1_direction_levels_and_circling_mean) {
	// Midfield so the toward-goal direction is ~ +y for every heading; ratios to the straight-ahead pay.
	auto at = [](float deg) { Carry c; c.carPos = Vec(0, 0, 17); float a = deg * 3.14159265f / 180.0f; c.fwd = Vec(sinf(a), cosf(a), 0); return At4s(c); };
	float straight = at(0);
	CHECK_NEAR(at(66.4f) / straight, 1.0f, 0.02f);        // cos 0.4: the full-pay edge
	CHECK_NEAR(at(78.46f) / straight, 0.5f, 0.03f);       // cos 0.2: 0.2 / 0.4 = 0.5 (Rev 4.0: 0.75)
	CHECK_NEAR(at(90.0f) / straight, 0.0f, 0.02f);        // sideways (Rev 4.0: 0.5)
	CHECK_NEAR(at(120.0f) / straight, 0.0f, 1e-3f);       // away (Rev 4.0: 0.5)
	// A bot driving circles at constant speed now keeps 0.43 of the straight-line pay (Rev 4.0: 0.72)
	float sum = 0; int n = 0;
	for (int d = 0; d < 360; d += 3) { sum += at((float)d); n++; }
	float frac = (sum / n) / straight;
	CHECK(frac > 0.41f && frac < 0.46f);
}

TEST(Dribble_fix1_2_combined_circling_at_midfield_vs_straight_at_the_net) {
	// What the pair buys: a circling carry at midfield now earns about 0.43 * 0.745 = 0.32 of a straight carry at the net.
	Carry net; net.carPos = Vec(0, 4800, 17);
	float atNet = At4s(net);
	float sum = 0; int n = 0;
	for (int d = 0; d < 360; d += 3) {
		Carry c; c.carPos = Vec(0, 0, 17); float a = d * 3.14159265f / 180.0f; c.fwd = Vec(sinf(a), cosf(a), 0);
		sum += At4s(c); n++;
	}
	float circling = sum / n;
	CHECK(circling / atNet > 0.29f && circling / atNet < 0.35f);
}

TEST(Dribble_F2_a_broken_carry_restarts_the_clock) {
	// Finding 2: one step outside the height band zeroes the accumulated carry age; the pay falls from 0.467 to 0.03.
	std::vector<Carry> fr = Steady(60);
	Carry pop; pop.lu = 240;
	fr.push_back(pop);
	for (int i = 0; i < 3; i++) fr.push_back(Carry());
	DribbleMasteryReward r;
	auto v = Run(r, fr);
	CHECK_NEAR(v[59], 0.4667f, 0.004f);
	CHECK_NEAR(v[60], 0.0f, 1e-6f);
	CHECK_NEAR(v[61], 0.03f, 1e-5f);                      // re-entry, no memory of the 4 s
	CHECK_NEAR(v[62], 0.0f, 1e-6f);
	// a smaller pop that stays inside the band does not reset it
	std::vector<Carry> fr2 = Steady(60);
	Carry soft; soft.lu = 229;
	fr2.push_back(soft);
	DribbleMasteryReward r2;
	CHECK(Run(r2, fr2).back() > 0.3f);
}

TEST(Dribble_F4_boost_term_ignores_quality) {
	// lateral 74/75, relative speed 499/500, speed 801/800: every shaping factor is near zero, the boost term still pays 1.0 per second
	Carry c; c.lr = 74; c.speed = 801; c.relVel = Vec(0, 0, 499);
	float p = At4s(c);
	CHECK(p > 0.0667f * 0.98f);
	CHECK_NEAR(p, 0.0675f, 0.003f);
	// a clean carry pays 7x that
	CHECK(At4s(Carry()) > 6.5f * p);
}

TEST(Dribble_telemetry_carry_predicate_agrees_with_the_reward_gates) {
	// CarryCatchTelemetry::InCarry is a copy of the reward's gates. On a grid, the reward's first-step entry bonus (0.03) appears
	// exactly when the predicate says "carry". (Fresh reward per point, no opponents, so the entry bonus is the whole first-step pay.)
	int agree = 0, total = 0, carries = 0;
	float lrs[] = { 0, 70, 80 }, lfs[] = { -80, -60, 40, 140, 160 }, lus[] = { 95, 105, 124, 225, 235 };
	float speeds[] = { 700, 790, 810, 1800 }, rels[] = { 0, 450, 550 };
	bool grounds[] = { true, false }; float zlift[] = { 0, 520 };
	for (float lr : lrs) for (float lf : lfs) for (float lu : lus) for (float sp : speeds) for (float rel : rels)
	for (bool gr : grounds) for (float zl : zlift) {
		Carry c; c.lr = lr; c.lf = lf; c.lu = lu; c.speed = sp; c.relVel = Vec(0, 0, rel); c.ground = gr; c.carPos = Vec(0, 4800, 17 + zl);
		GameState g; Fill(g, c);
		DribbleMasteryReward r; r.Reset(g); r.PreStep(g);
		bool paid = r.GetReward(g.players[0], g, false) > 0.0f;
		bool pred = CarryCatchTelemetry::InCarry(g.players[0], g);
		total++; if (paid == pred) agree++; if (pred) carries++;
	}
	CHECK_EQ(agree, total);
	CHECK(carries > 20);   // the grid actually exercises both outcomes
	CHECK(carries < total - 20);
}
