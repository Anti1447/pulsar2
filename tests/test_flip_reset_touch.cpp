#include "TestFramework.h"
#include "RewardTestUtil.h"

#include <RLGymCPP/Rewards/PlayerBall/FlipResetTouchReward.h>
#include <RLGymCPP/Telemetry/FlipResetTelemetry.h>

#include <vector>

using namespace RLGC;
using namespace RewardTest;

// FlipResetTouchReward + FlipResetTelemetry (2026-11-11). Report: docs/reward_scenarios/FlipResetTouch_2026-11-11.md.
// Engine rule used by the fixtures (RocketSim compat): HasFlipReset() = !isOnGround && HasFlipOrJump() && !hasJumped,
// HasFlipOrJump() = isOnGround || (!hasFlipped && !hasDoubleJumped && airTimeSinceJump < DOUBLEJUMP_MAX_DELAY (1.25 s)).
// Fabricated states, no engine: this proves the rule, not that the engine raises the flags the way the fixture assumes.

namespace {
	struct Fr {
		bool ground = false, hasJumped = true, hasFlipped = false, touch = false;
		float airTimeSinceJump = 0.2f;
		float carZ = 900, ballZ = 1000;
	};
	Player Mk(const Fr& f, uint32_t id = 1, int idx = 0, Team t = Team::BLUE) {
		Player p = MakePlayer(id, idx, t, Vec(0, 0, f.carZ));
		p.isOnGround = f.ground; p.hasJumped = f.hasJumped; p.hasFlipped = f.hasFlipped; p.hasDoubleJumped = false;
		p.airTimeSinceJump = f.airTimeSinceJump; p.ballTouchedStep = f.touch;
		return p;
	}
	// Chain of single-player states; returns the reward on each.
	std::vector<float> Run(const std::vector<Fr>& frames) {
		std::vector<GameState> st(frames.size());
		for (size_t i = 0; i < frames.size(); i++) { st[i].deltaTime = STEP_DT; st[i].ball.pos = Vec(0, 0, frames[i].ballZ); st[i].players.push_back(Mk(frames[i])); }
		for (size_t i = 1; i < st.size(); i++) { st[i].prev = &st[i - 1]; st[i].players[0].prev = &st[i - 1].players[0]; }
		FlipResetTouchReward r; std::vector<float> out;
		for (auto& s : st) out.push_back(r.GetReward(s.players[0], s, false));
		return out;
	}
	Fr Spent(bool touch) { Fr f; f.touch = touch; return f; }                       // airborne, jumped: no reset
	Fr Reset(bool touch = false) { Fr f; f.hasJumped = false; f.touch = touch; f.airTimeSinceJump = 0; return f; } // airborne, did not jump: reset available
}

TEST(FRT_pays_when_touched_last_step_and_the_reset_just_appeared) {
	auto r = Run({ Spent(true), Reset(false) });
	CHECK_EQ(r[0], 0.0f); CHECK_EQ(r[1], 1.0f);
}
TEST(FRT_no_touch_on_the_previous_step_pays_nothing) {
	auto r = Run({ Spent(false), Reset(false) });
	CHECK_EQ(r[1], 0.0f);
}
TEST(FRT_a_touch_on_the_current_step_only_does_not_count) {
	auto r = Run({ Spent(false), Reset(true) });
	CHECK_EQ(r[1], 0.0f);
}
TEST(FRT_needs_the_edge_a_reset_that_was_already_available_pays_nothing) {
	auto r = Run({ Reset(true), Reset(true), Reset(true) });   // available the whole time, touching every step
	CHECK_EQ(r[1], 0.0f); CHECK_EQ(r[2], 0.0f);
}
TEST(FRT_a_held_window_pays_once) { // edge on step 1, then 8 more steps of touches with the reset still available
	std::vector<Fr> fr = { Spent(true), Reset(true) };
	for (int i = 0; i < 8; i++) fr.push_back(Reset(true));
	float sum = 0; for (float x : Run(fr)) sum += x;
	CHECK_NEAR(sum, 1.0f, 1e-6f);
}
TEST(FRT_grounded_spent_or_expired_cars_have_no_reset) {
	Fr onGround = Reset(false); onGround.ground = true;
	CHECK_EQ(Run({ Spent(true), onGround })[1], 0.0f);                       // isOnGround => HasFlipReset false
	Fr flipped = Reset(false); flipped.hasFlipped = true;
	CHECK_EQ(Run({ Spent(true), flipped })[1], 0.0f);                        // flip already used
	Fr expired = Reset(false); expired.airTimeSinceJump = 1.3f;
	CHECK_EQ(Run({ Spent(true), expired })[1], 0.0f);                        // past the 1.25 s window
	Fr edge = Reset(false); edge.airTimeSinceJump = 1.2f;
	CHECK_EQ(Run({ Spent(true), edge })[1], 1.0f);                           // just inside it
}
TEST(FRT_first_step_of_an_episode_pays_nothing) {
	GameState s; s.players.push_back(Mk(Reset(true)));
	FlipResetTouchReward r;
	CHECK_EQ(r.GetReward(s.players[0], s, false), 0.0f);                    // no prev
}
TEST(FRT_ignores_ball_and_car_height) {
	for (float bz : { 100.f, 600.f, 1800.f }) for (float cz : { 100.f, 900.f, 1800.f }) {
		Fr a = Spent(true), b = Reset(false); a.ballZ = b.ballZ = bz; a.carZ = b.carZ = cz;
		CHECK_EQ(Run({ a, b })[1], 1.0f);
	}
}
// DOCUMENTS the coarseness: a car riding a wall (touching the ball, isOnGround true) that then leaves the wall without jumping gets HasFlipReset
// true on its first airborne step, so a touch the step before pays 1.0 with no wheel contact in the air at all.
TEST(FRT_documents_the_wall_leave_false_positive) {
	Fr wall = Spent(true); wall.ground = true; wall.hasJumped = false; wall.airTimeSinceJump = 0;   // on the wall, touching the ball
	Fr off = Reset(false);                                                                          // airborne next step, never jumped
	CHECK_EQ(Run({ wall, off })[1], 1.0f);
	// the same exit with no touch pays nothing (but still counts in the "any" telemetry)
	Fr wallNoTouch = wall; wallNoTouch.touch = false;
	CHECK_EQ(Run({ wallNoTouch, off })[1], 0.0f);
}
TEST(FRT_another_players_touch_or_reset_does_not_pay_this_player) {
	GameState prev, cur; prev.deltaTime = cur.deltaTime = STEP_DT;
	prev.players.push_back(Mk(Spent(false), 1, 0, Team::BLUE));  prev.players.push_back(Mk(Spent(true), 2, 1, Team::ORANGE));
	cur.players.push_back(Mk(Reset(false), 1, 0, Team::BLUE));   cur.players.push_back(Mk(Reset(false), 2, 1, Team::ORANGE));
	cur.prev = &prev; for (int i = 0; i < 2; i++) cur.players[i].prev = &prev.players[i];
	FlipResetTouchReward r;
	CHECK_EQ(r.GetReward(cur.players[0], cur, false), 0.0f);   // blue did not touch last step
	CHECK_EQ(r.GetReward(cur.players[1], cur, false), 1.0f);   // orange did
}
// A real chain (jump, wheel-touch reset, flip, wheel-touch reset) pays once per reset: the engine's jump/flip is what re-arms the edge.
TEST(FRT_a_repeated_reset_chain_pays_once_per_reset) {
	Fr flipped = Spent(false); flipped.hasFlipped = true;
	auto r = Run({ Spent(true), Reset(false), Reset(false), flipped, Spent(true), Reset(false) });
	float sum = 0; for (float x : r) sum += x;
	CHECK_NEAR(sum, 2.0f, 1e-6f);
}

// ---- telemetry ----
TEST(FRT_telemetry_counts_any_and_after_touch_separately_and_per_player) {
	using namespace FlipResetTelemetry;
	GameState prev, cur; prev.deltaTime = cur.deltaTime = STEP_DT;
	Fr wallOff = Spent(false); wallOff.ground = true; wallOff.hasJumped = false; wallOff.airTimeSinceJump = 0;
	prev.players.push_back(Mk(Spent(true), 1, 0));  prev.players.push_back(Mk(wallOff, 2, 1, Team::ORANGE));
	cur.players.push_back(Mk(Reset(false), 1, 0));  cur.players.push_back(Mk(Reset(false), 2, 1, Team::ORANGE));
	cur.prev = &prev; for (int i = 0; i < 2; i++) cur.players[i].prev = &prev.players[i];
	EpisodeMem m;
	StepEvents ev = Observe(m, cur);
	CHECK_EQ(ev.any, 2); CHECK_EQ(ev.afterTouch, 1);                        // both left without a jump; only player 0 touched first
	CHECK_EQ(m.any[0], 1); CHECK_EQ(m.any[1], 1); CHECK_EQ(m.afterTouch[0], 1); CHECK_EQ(m.afterTouch[1], 0);
	CHECK(HasEpisode(m)); CHECK_EQ(m.nPlayers, 2);
}
TEST(FRT_telemetry_first_step_counts_nothing_and_the_reward_matches_the_predicate) {
	using namespace FlipResetTelemetry;
	GameState s; s.players.push_back(Mk(Reset(true)));
	EpisodeMem m; StepEvents ev = Observe(m, s);
	CHECK_EQ(ev.any, 0); CHECK_EQ(ev.afterTouch, 0);
	// predicate == reward on a grid of flag combinations
	for (int g = 0; g < 2; g++) for (int hj = 0; hj < 2; hj++) for (int tp = 0; tp < 2; tp++) for (int tn = 0; tn < 2; tn++) {
		Fr a = Spent(tp); a.hasJumped = true; Fr b; b.ground = g; b.hasJumped = hj; b.touch = tn; b.airTimeSinceJump = 0;
		GameState p0, c0; p0.players.push_back(Mk(a)); c0.players.push_back(Mk(b));
		c0.prev = &p0; c0.players[0].prev = &p0.players[0];
		FlipResetTouchReward r;
		CHECK_EQ(r.GetReward(c0.players[0], c0, false), FlipResetTouchReward::ResetAfterTouch(c0.players[0]) ? 1.0f : 0.0f);
	}
}
