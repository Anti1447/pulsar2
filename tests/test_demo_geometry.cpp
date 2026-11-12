#include "TestFramework.h"
#include "RewardTestUtil.h"

#include <RLGymCPP/Rewards/Events/DemoReward.h>
#include <RLGymCPP/Telemetry/DemoTelemetry.h>

#include <cmath>
#include <vector>

using namespace RLGC;
using namespace RewardTest;
namespace DG = RLGC::DemoGeometry;

// DemoGeometry f, OffensiveDemoReward and DemoTelemetry (2026-11-09). Derivations: docs/reward_scenarios/OffensiveDemo_2026-11-09.md.
// Fabricated states, no engine. Geometry: BLUE attacks the ORANGE goal, posts at (+-892.755, 5120). Ball at the origin unless stated.
// The shot lane is the triangle ball-leftPost-rightPost (inside = distance 0), outside = distance to the nearer ball->post segment.

namespace {
	struct Victim { Vec pos; Vec posNow; bool demoed = true; Team team = Team::ORANGE; };

	// prev + cur state; player 0 = blue bumper, then the victims. Pointers are linked after all vectors are final.
	struct Fix {
		GameState prev, cur;
		Fix(Vec ball, std::vector<Victim> victims, bool bumperDemo = true, int extraBlue = 0) {
			prev.ball.pos = ball; cur.ball.pos = ball;
			auto add = [&](Team t, Vec pPrev, Vec pNow, bool demo, bool demoed) {
				uint32_t id = (uint32_t)prev.players.size();
				Player a = MakePlayer(id, (int)id, t, pPrev);
				Player b = MakePlayer(id, (int)id, t, pNow);
				b.eventState.demo = demo; b.eventState.demoed = demoed;
				prev.players.push_back(a); cur.players.push_back(b);
			};
			add(Team::BLUE, Vec(0, -1000, 17), Vec(0, -900, 17), bumperDemo, false);
			for (int i = 0; i < extraBlue; i++) add(Team::BLUE, Vec(300, -2000, 17), Vec(300, -2000, 17), false, false);
			for (auto& v : victims) add(v.team, v.pos, v.posNow, false, v.demoed);
			cur.prev = &prev;
			for (size_t i = 0; i < cur.players.size(); i++) cur.players[i].prev = &prev.players[i];
			cur.deltaTime = STEP_DT;
		}
	};
	DG::Sample Eval(Vec victimPos, int extraBlue = 1) {
		Fix f(Vec(0, 0, 93), { { victimPos, Vec(0, -4000, 17) } }, true, extraBlue);
		return DG::Evaluate(f.cur.players[0], f.cur);
	}
	float PayFor(Vec victimPos, int extraBlue) {
		Fix f(Vec(0, 0, 93), { { victimPos, Vec(0, -4000, 17) } }, true, extraBlue);
		OffensiveDemoReward r; r.Reset(f.cur);
		return r.GetReward(f.cur.players[0], f.cur, false);
	}
}

// ---- f: easy ----
TEST(demo_f_on_segment_near_ball_is_one) {
	auto s = Eval(Vec(0, 500, 17));
	CHECK(s.valid); CHECK_NEAR(s.f, 1.0f, 1e-5f); CHECK_NEAR(s.mult, 1.0f, 1e-5f);
}
TEST(demo_f_far_behind_ball_is_zero) {
	auto s = Eval(Vec(0, -3500, 17)); // segment end is the ball: 3500 away -> lineProx 0
	CHECK_NEAR(s.f, 0.0f, 1e-5f); CHECK_NEAR(s.mult, DG::MULT_FLOOR, 1e-5f);
}

// ---- f: hand-computed middles ----
TEST(demo_f_beside_ball_1000) { // nearest lane point is on the ball->right-post segment at t=0.0330: (29.5,169.2) ; lineDist 985.14 -> (1600-985.14)/1200 = 0.5124 ; ballProx 1
	auto s = Eval(Vec(1000, 0, 17));
	CHECK_NEAR(s.f, 0.51239f, 1e-3f); CHECK_NEAR(s.mult, 0.40f + 0.60f * 0.51239f, 1e-3f);
}
TEST(demo_f_down_the_lane_2250_is_half) { // inside the lane (half-width 392 at y=2250) -> lineProx 1 ; ballDist 2250 -> (3500-2250)/2500 = 0.5
	auto s = Eval(Vec(0, 2250, 17));
	CHECK_NEAR(s.f, 0.5f, 1e-4f);
}
TEST(demo_f_beyond_goal_is_zero) { // (0,6000): outside the lane, 1253.6 from a post ; ballDist 6000 -> 0
	CHECK_NEAR(Eval(Vec(0, 6000, 17)).f, 0.0f, 1e-5f);
}

// ---- boundaries at the real constants, 0.9x / 1.1x ----
TEST(demo_f_line_boundaries) {
	// Probe the lane edge: a point d outward (normal to the ball->right-post edge) from that edge's midpoint (the closest lane point is the midpoint,
	// since the projection stays interior). lineDist = d by construction; ballProx from the point's own distance to the ball.
	const float px = 892.755f, py = 5120.0f, L = sqrtf(px * px + py * py);
	const float nx = py / L, ny = -px / L; // outward normal (toward +x)
	auto at = [&](float d) { return Vec(px * 0.5f + nx * d, py * 0.5f + ny * d, 17); };
	auto expect = [&](Vec p, float d) {
		float bd = sqrtf(p.x * p.x + p.y * p.y);
		return DG::Ramp(d, DG::LINE_FULL, DG::LINE_ZERO) * DG::Ramp(bd, DG::BALL_FULL, DG::BALL_ZERO);
	};
	for (float d : { 0.0f, DG::LINE_FULL * 0.9f, DG::LINE_FULL * 1.1f, DG::LINE_ZERO * 0.9f, DG::LINE_ZERO * 1.1f }) {
		Vec p = at(d);
		CHECK_NEAR(Eval(p).f, expect(p, d), 1e-4f);
	}
	// and the lineProx part alone moves across the boundaries (ballProx is the same factor for both reads here up to the shift of the point)
	CHECK(Eval(at(DG::LINE_FULL * 0.9f)).f > Eval(at(DG::LINE_FULL * 1.1f)).f);
	CHECK(Eval(at(DG::LINE_ZERO * 1.1f)).f == 0.0f);
}
TEST(demo_f_inside_the_lane_has_zero_line_distance) {
	CHECK_NEAR(DG::LaneDistXY(Vec(0, 3000, 17), Vec(0, 0, 93), Team::BLUE), 0.0f, 1e-6f);   // goalie-side centre
	CHECK_NEAR(DG::LaneDistXY(Vec(300, 3000, 17), Vec(0, 0, 93), Team::BLUE), 0.0f, 1e-6f); // half-width at y=3000 is 523
	CHECK_NEAR(DG::LaneDistXY(Vec(0, 4900, 17), Vec(0, 0, 93), Team::BLUE), 0.0f, 1e-6f);   // in front of the net
}
TEST(demo_f_post_corridor_two_lines_vs_one) {
	// (800,3000) is 272.8 outside the right edge -> lineProx 1 (<= 400) ; the old single centre line would have given (1600-800)/1200 = 0.667.
	CHECK_NEAR(DG::LaneDistXY(Vec(800, 3000, 17), Vec(0, 0, 93), Team::BLUE), 272.79f, 0.05f);
	// (2000,3000): 1454.9 outside -> lineProx (1600-1454.9)/1200 = 0.1209
	CHECK_NEAR(DG::LaneDistXY(Vec(2000, 3000, 17), Vec(0, 0, 93), Team::BLUE), 1454.95f, 0.05f);
	// left/right symmetric
	CHECK_NEAR(DG::LaneDistXY(Vec(-800, 3000, 17), Vec(0, 0, 93), Team::BLUE), 272.79f, 0.05f);
}
TEST(demo_f_lane_degenerate_ball_has_no_nan) { // ball on / beyond the goal line: zero-area triangle -> falls back to the two segments
	for (float by : { 5120.0f, 5500.0f, -5500.0f }) {
		float d = DG::LaneDistXY(Vec(100, 4000, 17), Vec(0, by, 93), Team::BLUE);
		CHECK(std::isfinite(d)); CHECK(d >= 0.0f);
	}
}
TEST(demo_f_off_centre_ball_moves_the_lane) { // ball at (3000,2000): the lane leans toward the right-hand posts; a car on the straight line to the centre of goal is inside it
	Vec ball(3000, 2000, 93);
	CHECK_NEAR(DG::LaneDistXY(Vec(1500, 3560, 17), ball, Team::BLUE), 0.0f, 1e-6f); // midpoint of ball -> goal centre (0,5120)
	CHECK(DG::LaneDistXY(Vec(3000, 4000, 17), ball, Team::BLUE) > 1000.0f);         // straight up from the ball, away from the goal
}
TEST(demo_f_ball_boundaries) { // victim on the segment, so lineProx = 1
	CHECK_NEAR(Eval(Vec(0, DG::BALL_FULL * 0.9f, 17)).f, 1.0f, 1e-5f);
	CHECK_NEAR(Eval(Vec(0, DG::BALL_ZERO * 0.9f, 17)).f, (DG::BALL_ZERO - DG::BALL_ZERO * 0.9f) / (DG::BALL_ZERO - DG::BALL_FULL), 1e-4f);
	CHECK_NEAR(Eval(Vec(0, DG::BALL_ZERO * 1.1f, 17)).f, 0.0f, 1e-5f);
}

// ---- hard: a challenger just behind the ball scores high (segment, not line) ----
TEST(demo_f_challenger_just_behind_ball_scores_high) { // (0,-500): nearest segment point is the ball: 500 -> lineProx (1600-500)/1200 = 0.9167 ; ballProx 1
	CHECK_NEAR(Eval(Vec(0, -500, 17)).f, 0.916667f, 1e-4f);
}
TEST(demo_f_mirror_symmetry_in_x_and_team) {
	CHECK_NEAR(Eval(Vec(900, 700, 17)).f, Eval(Vec(-900, 700, 17)).f, 1e-6f);
	// ORANGE bumper attacks the other way: the mirrored victim gives the same f
	GameState prev, cur; prev.ball.pos = Vec(0, 0, 93); cur.ball.pos = prev.ball.pos;
	Player b0 = MakePlayer(0, 0, Team::ORANGE, Vec(0, 1000, 17)), v0 = MakePlayer(1, 1, Team::BLUE, Vec(900, -700, 17));
	prev.players = { b0, v0 };
	Player b1 = b0, v1 = v0; b1.eventState.demo = true; v1.eventState.demoed = true;
	cur.players = { b1, v1 }; cur.prev = &prev; cur.players[0].prev = &prev.players[0]; cur.players[1].prev = &prev.players[1];
	CHECK_NEAR(DG::Evaluate(cur.players[0], cur).f, Eval(Vec(900, 700, 17)).f, 1e-5f);
}

// ---- multiplier ----
TEST(demo_multiplier_endpoints_and_monotone) {
	CHECK_EQ(DG::Multiplier(1.0f), 1.0f);              // the best demo pays EXACTLY what the flat reward paid
	CHECK(DG::Multiplier(0.0f) < 1.0f);                // the worst pays less
	CHECK_NEAR(DG::Multiplier(0.0f), DG::MULT_FLOOR, 1e-6f);
	float last = -1.0f;
	for (float f = 0.0f; f <= 1.0f; f += 0.05f) { float m = DG::Multiplier(f); CHECK(m >= last); last = m; }
	CHECK_EQ(DG::Multiplier(2.0f), 1.0f); CHECK_NEAR(DG::Multiplier(-1.0f), DG::MULT_FLOOR, 1e-6f);
}
TEST(demo_multiplier_stays_in_band_over_a_position_grid) {
	for (int x = -4000; x <= 4000; x += 500) for (int y = -5000; y <= 5000; y += 500) {
		float m = DG::Multiplier(DG::FactorAt(Vec(0, 0, 93), Vec((float)x, (float)y, 17), Team::BLUE));
		CHECK(m >= DG::MULT_FLOOR - 1e-6f); CHECK(m <= 1.0f + 1e-6f);
	}
}

// ---- reward: pays, gates, fallbacks ----
TEST(demo_reward_best_pays_one_worst_pays_floor) {
	CHECK_NEAR(PayFor(Vec(0, 500, 17), 1), 1.0f, 1e-5f);
	CHECK_NEAR(PayFor(Vec(0, -3500, 17), 1), DG::MULT_FLOOR, 1e-5f);
}
TEST(demo_reward_no_demo_pays_zero) {
	Fix f(Vec(0, 0, 93), { { Vec(0, 500, 17), Vec(0, 500, 17) } }, /*bumperDemo=*/false, 1);
	OffensiveDemoReward r; r.Reset(f.cur);
	CHECK_EQ(r.GetReward(f.cur.players[0], f.cur, false), 0.0f);
}
TEST(demo_reward_1v1_is_unchanged) { // one blue, one orange: flat 1.0 even for the worst position
	CHECK_NEAR(PayFor(Vec(0, -3500, 17), 0), 1.0f, 1e-6f);
}
TEST(demo_reward_unresolved_victim_pays_full) {
	Fix f(Vec(0, 0, 93), { { Vec(0, -3500, 17), Vec(0, -3500, 17), /*demoed=*/false } }, true, 1);
	OffensiveDemoReward r; r.Reset(f.cur);
	CHECK_EQ(r.GetReward(f.cur.players[0], f.cur, false), 1.0f);
	CHECK(!DG::Evaluate(f.cur.players[0], f.cur).valid);
}
TEST(demo_reward_uses_position_before_the_step) { // the engine has moved the victim to a respawn point (0,-4000); it was at (0,500)
	Fix f(Vec(0, 0, 93), { { Vec(0, 500, 17), Vec(0, -4000, 17) } }, true, 1);
	CHECK_NEAR(DG::Evaluate(f.cur.players[0], f.cur).f, 1.0f, 1e-5f);
}
TEST(demo_reward_ignores_a_demoed_teammate_and_picks_nearest_opponent) {
	// A flagged BLUE teammate far away is not a victim. Two flagged opponents: the one nearest the bumper (prev pos (0,-1000)) is the victim.
	Fix f(Vec(0, 0, 93), { { Vec(0, -3500, 17), Vec(0, -3500, 17), true, Team::BLUE },
	                       { Vec(0, 500, 17), Vec(0, 500, 17) }, { Vec(0, 4000, 17), Vec(0, 4000, 17) } }, true, 1);
	auto s = DG::Evaluate(f.cur.players[0], f.cur);
	CHECK(s.valid); CHECK_NEAR(s.f, 1.0f, 1e-5f); // (0,500) is nearer to the bumper than (0,4000)
}

// ---- farming probe: the multiplier can only LOWER a payout ----
TEST(demo_reward_never_exceeds_the_flat_demo) {
	for (int x = -4000; x <= 4000; x += 800) for (int y = -5000; y <= 5000; y += 800)
		CHECK(PayFor(Vec((float)x, (float)y, 17), 1) <= 1.0f + 1e-6f);
}

// ---- telemetry window ----
TEST(demo_telemetry_window_opens_expires_and_resets) {
	GameState s0; s0.players.push_back(MakePlayer(0, 0, Team::BLUE, Vec(0, 0, 17)));
	DemoTelemetry::ArenaMem m;
	GameState prev = s0;
	s0.players[0].eventState.demo = true;
	DemoTelemetry::Advance(m, s0);                                   // first step of an episode (prev == null): demo this step
	CHECK(DemoTelemetry::WindowOpen(m, Team::BLUE)); CHECK(!DemoTelemetry::WindowOpen(m, Team::ORANGE));
	GameState idle; idle.prev = &prev; idle.players.push_back(MakePlayer(0, 0, Team::BLUE, Vec(0, 0, 17)));
	for (int i = 0; i < DemoTelemetry::WINDOW_STEPS; i++) DemoTelemetry::Advance(m, idle);
	CHECK(DemoTelemetry::WindowOpen(m, Team::BLUE));                 // 30 steps later: still open (4.0 s)
	DemoTelemetry::Advance(m, idle);
	CHECK(!DemoTelemetry::WindowOpen(m, Team::BLUE));                // step 31: closed
	s0.prev = nullptr; DemoTelemetry::Advance(m, s0);
	CHECK(DemoTelemetry::WindowOpen(m, Team::BLUE));
	GameState fresh = idle; fresh.prev = nullptr; DemoTelemetry::Advance(m, fresh); // new episode forgets
	CHECK(!DemoTelemetry::WindowOpen(m, Team::BLUE));
}
TEST(demo_telemetry_scorer_and_baseline) {
	GameState s; s.ball.pos = Vec(0, 5200, 93); CHECK(DemoTelemetry::Scorer(s) == Team::BLUE);
	s.ball.pos = Vec(0, -5200, 93); CHECK(DemoTelemetry::Scorer(s) == Team::ORANGE);
	Fix f(Vec(0, 0, 93), { { Vec(1000, 0, 17), Vec(1000, 0, 17) } }, true, 1);
	CHECK_NEAR(DemoTelemetry::OppFactor(f.cur.players[2], Team::BLUE, f.cur), 0.51239f, 1e-3f); // same number the reward uses
}

// ---- credit checks (2026-11-11): DemoPlausible and StateEventCheck ----
namespace {
	// bumper (blue) at (0,-1000), victim (orange) `gap` uu ahead; the bumper's supersonic flag on the previous / current step is set by the args.
	struct CC {
		GameState prev, cur;
		CC(float gap, bool ssPrev, bool ssCur, bool flagDemo, bool flagDemoed, bool victimRose, bool sameTeamVictim = false) {
			auto mk = [&](Vec pos, Team t) { return MakePlayer(t == Team::BLUE ? 1 : 2, 0, t, pos); };
			Player b0 = mk(Vec(0, -1000, 17), Team::BLUE), v0 = mk(Vec(0, -1000 + gap, 17), sameTeamVictim ? Team::BLUE : Team::ORANGE);
			b0.isSupersonic = ssPrev;
			prev.players = { b0, v0 };
			Player b1 = b0, v1 = v0;
			b1.isSupersonic = ssCur; b1.eventState.demo = flagDemo;
			v1.eventState.demoed = flagDemoed; v1.isDemoed = victimRose;
			cur.players = { b1, v1 };
			cur.prev = &prev;
			for (int i = 0; i < 2; i++) cur.players[i].prev = &prev.players[i];
		}
	};
}
TEST(DemoCredit_plausible_needs_supersonic_and_closeness) {
	using namespace DemoTelemetry;
	{ CC c(150, true, true, true, true, true);   CHECK(DemoPlausible(c.cur.players[0], c.cur.players[1])); }
	{ CC c(150, false, true, true, true, true);  CHECK(DemoPlausible(c.cur.players[0], c.cur.players[1])); }  // flag flipped inside the step
	{ CC c(150, true, false, true, true, true);  CHECK(DemoPlausible(c.cur.players[0], c.cur.players[1])); }
	{ CC c(150, false, false, true, true, true); CHECK(!DemoPlausible(c.cur.players[0], c.cur.players[1])); } // never supersonic
	{ CC c(PLAUSIBLE_MAX_DIST * 0.9f, true, true, true, true, true); CHECK(DemoPlausible(c.cur.players[0], c.cur.players[1])); }
	{ CC c(PLAUSIBLE_MAX_DIST * 1.1f, true, true, true, true, true); CHECK(!DemoPlausible(c.cur.players[0], c.cur.players[1])); } // 3000 uu apart is the misrouted case
	{ CC c(3000, true, true, true, true, true);  CHECK(!DemoPlausible(c.cur.players[0], c.cur.players[1])); }
}
TEST(DemoCredit_plausible_reads_positions_before_the_step) { // the engine has moved the demoed car to its respawn point
	using namespace DemoTelemetry;
	CC c(150, true, true, true, true, true);
	c.cur.players[1].pos = Vec(0, -4500, 17);
	CHECK(DemoPlausible(c.cur.players[0], c.cur.players[1]));
}
TEST(DemoCredit_state_event_check_consistent_cases_do_not_mismatch) {
	using namespace DemoTelemetry;
	CC ok(150, true, true, true, true, true);   auto a = StateEventCheck(ok.cur);
	CHECK(a.evaluable); CHECK(a.anything); CHECK(!a.mismatch);                      // flags and the rise agree
	CC quiet(150, true, true, false, false, false); auto b = StateEventCheck(quiet.cur);
	CHECK(b.evaluable); CHECK(!b.anything); CHECK(!b.mismatch);                     // nothing happened, nothing flagged
}
TEST(DemoCredit_state_event_check_catches_misrouted_and_missing_events) {
	using namespace DemoTelemetry;
	CC flagNoRise(150, true, true, true, true, false);   CHECK(StateEventCheck(flagNoRise.cur).mismatch);   // flags in an arena where nobody was demoed
	CC riseNoFlag(150, true, true, false, false, true);  CHECK(StateEventCheck(riseNoFlag.cur).mismatch);   // a demo whose event never arrived
	CC wrongVictim(150, true, true, true, true, true);
	wrongVictim.cur.players[1].eventState.demoed = false; wrongVictim.cur.players[0].eventState.demoed = true; // victim flag on the bumper, not the demoed car
	CHECK(StateEventCheck(wrongVictim.cur).mismatch);
	CC bumperOnly(150, true, true, true, false, true);   CHECK(StateEventCheck(bumperOnly.cur).mismatch);    // demo flag without a demoed flag
	CC sameTeam(150, true, true, true, true, true, /*sameTeamVictim=*/true); CHECK(StateEventCheck(sameTeam.cur).mismatch); // no opposite-team pair
}
TEST(DemoCredit_state_event_check_is_not_evaluable_on_the_first_step) {
	using namespace DemoTelemetry;
	GameState s; s.players.push_back(MakePlayer(1, 0, Team::BLUE, Vec(0, 0, 17))); s.players[0].eventState.demo = true;
	CHECK(!StateEventCheck(s).evaluable);
}
