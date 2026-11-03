#include "TestFramework.h"
#include "RewardTestUtil.h"

#include <RLGymCPP/Rewards/CommonRewards.h>
#include <RLGymCPP/Rewards/ZeroSumReward.h>

using namespace RLGC;
using namespace RewardTest;

// Audit of OpposedSaveReward (weight 25, wrapped ZeroSum(TEAM_SPIRIT 0.3)). The engine's GameEventTracker raises a `save` event (EnvSet sets
// eventState.save on the saver) when a SHOT (ball speed >= 1750 uu/s, predicted to score within 2.0 s, a shooter-team touch about 0.3 s earlier)
// stops being "probably going in" and a car of the threatened team touched the ball in the step. This reward pays that event only when the LAST BALL
// TOUCH STRICTLY BEFORE THE SAVE STEP came from the OPPONENT (the guard against self-served phantom shots). Touch history is tracked in PreStep
// from the per-step touch flags: curTouchTeam = team of the latest touch up to and including this step (-1 none, -2 both teams on the same step),
// prevTouchTeam = the value before this step's update.
//   pays   : saver's eventState.save && prevTouchTeam == the opposite team
//   blocked: the saver's team touched on an earlier step (a self-served shot, or a save that took two touch steps), a same-step double touch
//            right before, or no touch at all yet this episode
// Fixture: fabricated states with the save flag set directly (no engine). The engine side (what a real shot and save look like) is in
// tools/reward_lab/packs/opposed_save.

namespace {
	struct OS { int touchers = 0; int saver = -1; };        // touchers: bit0 blue0, bit1 orange1, bit2 blue2, bit3 orange3; saver: player index or -1

	GameState Make(int perTeam, const OS& f) {
		GameState gs; gs.deltaTime = STEP_DT;
		for (int i = 0; i < perTeam; i++) {
			gs.players.push_back(MakePlayer(1 + 2 * i, 2 * i, Team::BLUE, Vec(-500.f, 300.f * i, 17)));
			gs.players.push_back(MakePlayer(2 + 2 * i, 2 * i + 1, Team::ORANGE, Vec(500.f, 300.f * i, 17)));
		}
		for (size_t i = 0; i < gs.players.size(); i++) gs.players[i].ballTouchedStep = (f.touchers >> i) & 1;
		if (f.saver >= 0) gs.players[f.saver].eventState.save = true;
		return gs;
	}

	// per-step reward vectors (one entry per player) for a sequence of frames
	std::vector<std::vector<float>> Run(int perTeam, const std::vector<OS>& frames, bool resetBetween = false) {
		OpposedSaveReward r;
		GameState init = Make(perTeam, OS{});
		r.Reset(init);
		std::vector<std::vector<float>> out;
		for (auto& f : frames) { GameState gs = Make(perTeam, f); r.PreStep(gs); out.push_back(r.GetAllRewards(gs, false)); (void)resetBetween; }
		return out;
	}
}

// Orange shoots (touch on step 3); nobody touches; blue saves on step 10: the last touch strictly before is orange's: paid
TEST(OpposedSave_a_save_after_an_opponent_shot_pays_the_saver) {
	std::vector<OS> fr(12);
	fr[2].touchers = 0b10;           // orange touches on step 3
	fr[9].touchers = 0b01;           // blue touches on step 10 ...
	fr[9].saver = 0;                 // ... and the engine reports the save for blue 0
	auto v = Run(1, fr);
	CHECK_EQ(v[9][0], 1.0f);
	CHECK_EQ(v[9][1], 0.0f);         // the shooter is paid nothing raw
	for (int k = 0; k < 9; k++) { CHECK_EQ(v[k][0], 0.0f); CHECK_EQ(v[k][1], 0.0f); }
}

// The guard: a shot armed by the saver's own team is not paid, however the opponent touched before
TEST(OpposedSave_a_self_served_shot_pays_nothing) {
	std::vector<OS> fr(14);
	fr[1].touchers = 0b10;           // orange touches (any touch)
	fr[4].touchers = 0b01;           // blue then hits it at its own net (the arming touch)
	fr[10].touchers = 0b01; fr[10].saver = 0;    // and blue saves its own shot
	auto v = Run(1, fr);
	CHECK_EQ(v[10][0], 0.0f);
}

TEST(OpposedSave_a_teammates_arming_touch_also_blocks_the_pay) {
	std::vector<OS> fr(14);
	fr[1].touchers = 0b0010;         // orange touches
	fr[4].touchers = 0b0100;         // blue's second car sends the ball at its own goal
	fr[10].touchers = 0b0001; fr[10].saver = 0;  // blue car 0 makes the "save"
	auto v = Run(2, fr);
	CHECK_EQ(v[10][0], 0.0f);
}

// "Conservative by design": a save that takes two touch steps is blocked, because the first touch step already made blue the last toucher
TEST(OpposedSave_a_save_reported_on_the_second_touch_step_is_blocked) {
	std::vector<OS> fr(14);
	fr[2].touchers = 0b10;
	fr[9].touchers = 0b01;                         // blue's first contact step (the engine has not reported the save yet)
	fr[10].touchers = 0b01; fr[10].saver = 0;      // second step of the same contact: the event arrives now
	auto v = Run(1, fr);
	CHECK_EQ(v[9][0], 0.0f);
	CHECK_EQ(v[10][0], 0.0f);                      // prevTouchTeam is blue: no pay
}

TEST(OpposedSave_the_step_of_the_save_itself_does_not_count_as_a_prior_touch) {
	// the saver touches ON the save step: the update to curTouchTeam happens in PreStep AFTER prevTouchTeam was latched, so the shooter's touch is "previous"
	OpposedSaveReward r;
	GameState init = Make(1, OS{}); r.Reset(init);
	GameState a = Make(1, OS{ 0b10, -1 }); r.PreStep(a);
	CHECK_EQ(r.curTouchTeam, (int)Team::ORANGE); CHECK_EQ(r.prevTouchTeam, -1);
	GameState b = Make(1, OS{ 0b01, 0 }); r.PreStep(b);
	CHECK_EQ(r.curTouchTeam, (int)Team::BLUE); CHECK_EQ(r.prevTouchTeam, (int)Team::ORANGE);
	CHECK_EQ(r.GetReward(b.players[0], b, false), 1.0f);
}

TEST(OpposedSave_a_same_step_double_touch_before_the_save_blocks_it) {
	std::vector<OS> fr(14);
	fr[3].touchers = 0b11;                          // both teams touch on the same step: ambiguous (-2)
	fr[10].touchers = 0b01; fr[10].saver = 0;
	auto v = Run(1, fr);
	CHECK_EQ(v[10][0], 0.0f);
}

// ...for either team: with the ambiguity handled the OTHER way round (the first toucher in index order is blue) an orange save would be paid
TEST(OpposedSave_a_same_step_double_touch_blocks_an_orange_save_too) {
	std::vector<OS> fr(14);
	fr[3].touchers = 0b11;
	fr[10].touchers = 0b10; fr[10].saver = 1;
	CHECK_EQ(Run(1, fr)[10][1], 0.0f);
}

TEST(OpposedSave_a_save_with_no_touch_history_pays_nothing) {
	std::vector<OS> fr(6);
	fr[4].touchers = 0b01; fr[4].saver = 0;         // e.g. a moving ball at reset: nobody touched it yet
	auto v = Run(1, fr);
	CHECK_EQ(v[4][0], 0.0f);
}

TEST(OpposedSave_only_the_saver_is_paid_and_a_save_flag_on_the_opponent_pays_the_other_way) {
	std::vector<OS> fr(12);
	fr[2].touchers = 0b0010;                        // orange car 1 shoots
	fr[9].touchers = 0b0100; fr[9].saver = 2;       // blue car 2 (not car 0) saves
	auto v = Run(2, fr);
	CHECK_EQ(v[9][2], 1.0f);
	CHECK_EQ(v[9][0], 0.0f); CHECK_EQ(v[9][1], 0.0f); CHECK_EQ(v[9][3], 0.0f);
	std::vector<OS> fr2(12);                        // and symmetrically orange saving a blue shot
	fr2[2].touchers = 0b0001; fr2[9].touchers = 0b1000; fr2[9].saver = 3;
	CHECK_EQ(Run(2, fr2)[9][3], 1.0f);
}

TEST(OpposedSave_reset_clears_the_touch_history) {
	OpposedSaveReward r;
	GameState init = Make(1, OS{}); r.Reset(init);
	GameState a = Make(1, OS{ 0b10, -1 }); r.PreStep(a);
	r.Reset(init);                                  // a new episode
	CHECK_EQ(r.curTouchTeam, -1); CHECK_EQ(r.prevTouchTeam, -1);
	GameState b = Make(1, OS{ 0b01, 0 }); r.PreStep(b);
	CHECK_EQ(r.GetReward(b.players[0], b, false), 0.0f);   // the earlier episode's orange touch is forgotten
}

// Wiring as in training: ZeroSum(OpposedSave, 0.3). The source comment says "ZeroSumReward(_, 0)" but BuildRewards uses TEAM_SPIRIT = 0.3.
//   1v1: saver +1, shooter -1 (x25 = +25 / -25, a 50 swing).  2v2: saver 0.85, teammate 0.15, each orange car -0.5
TEST(OpposedSave_zero_sum_values_1v1_and_2v2_at_team_spirit_0p3) {
	{
		Reward* zs = new ZeroSumReward(new OpposedSaveReward(), 0.3f);
		GameState init = Make(1, OS{}); zs->Reset(init);
		GameState a = Make(1, OS{ 0b10, -1 }); zs->PreStep(a); zs->GetAllRewards(a, false);
		GameState b = Make(1, OS{ 0b01, 0 }); zs->PreStep(b);
		auto v = zs->GetAllRewards(b, false);
		CHECK_NEAR(v[0], 1.0f, 1e-6f); CHECK_NEAR(v[1], -1.0f, 1e-6f);
		CHECK_NEAR(25.f * v[0], 25.0f, 1e-4f);
		delete zs;
	}
	{
		Reward* zs = new ZeroSumReward(new OpposedSaveReward(), 0.3f);
		GameState init = Make(2, OS{}); zs->Reset(init);
		GameState a = Make(2, OS{ 0b0010, -1 }); zs->PreStep(a); zs->GetAllRewards(a, false);
		GameState b = Make(2, OS{ 0b0001, 0 }); zs->PreStep(b);
		auto v = zs->GetAllRewards(b, false);
		CHECK_NEAR(v[0], 0.85f, 1e-6f); CHECK_NEAR(v[2], 0.15f, 1e-6f);
		CHECK_NEAR(v[1], -0.5f, 1e-6f); CHECK_NEAR(v[3], -0.5f, 1e-6f);
		CHECK_NEAR(v[0] + v[1] + v[2] + v[3], 0.0f, 1e-6f);
		delete zs;
	}
}
