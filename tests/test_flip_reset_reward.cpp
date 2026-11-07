#include "TestFramework.h"

#include <RLGymCPP/Rewards/CommonRewards.h>

#include <filesystem>

using namespace RLGC;

// FlipResetReward audit regression (2026-11-07). A car rises into a high ball. With the WHEELS toward the ball the engine restores the
// flip (HasFlipReset() becomes true) and the reward must pay exactly once per reset, whether the engine reports the reset on the touch step
// or on the step after it (>= 3 wheels on the ball make isOnGround true on the touch step). With the ROOF toward the ball there is no reset
// and no payment. The original reward demanded the roof toward the ball and matched the event on a single step.

namespace {
	void InitFlipResetTest() {
		static bool done = false;
		if (done) return;
		for (const char* path : { "collision_meshes", "../collision_meshes", "build/collision_meshes" })
			if (std::filesystem::exists(std::filesystem::path(path) / "soccar")) { RocketSim::Init(path); break; }
		done = true;
	}

	struct Result { float paid = 0; int pays = 0; int engineResets = 0; };

	// roofSign: +1 roof (car up) toward the ball, -1 wheels toward the ball.
	Result Run(float roofSign, float speed, float z0, bool spendFlip = true) {
		InitFlipResetTest();
		Arena* arena = Arena::Create(GameMode::SOCCAR);
		Car* car = arena->AddCar(Team::BLUE);
		BallState bs; bs.pos = Vec(0, 0, 1200); arena->ball->SetState(bs);
		CarState cs = {};
		cs.pos = Vec(0, 0, z0);
		cs.rotMat = RotMat::LookAt(Vec(1, 0, 0), Vec(0, 0, roofSign));
		cs.hasJumped = true;
		if (spendFlip) { cs.hasFlipped = true; cs.airTime = 0.6f; cs.airTimeSinceJump = 0.6f; }
		cs.vel = Vec(0, 0, speed);
		cs.isOnGround = false;
		car->SetState(cs);
		arena->ball->SetState(bs);

		FlipResetReward reward;
		GameState gs(arena), prevGs;
		reward.Reset(gs);
		Result r;
		bool prevReset = car->GetState().HasFlipReset();
		for (int step = 0; step < 30; step++) {
			arena->Step(8);
			prevGs = gs;
			gs.UpdateFromArena(arena, std::vector<Action>(1), &prevGs);
			float v = ((Reward&)reward).GetReward(gs.players[0], gs, false);
			if (v > 0) { r.paid += v; r.pays++; }
			bool reset = car->GetState().HasFlipReset();
			if (reset && !prevReset) r.engineResets++;
			prevReset = reset;
		}
		delete arena;
		return r;
	}
}

TEST(FlipReset_pays_once_per_real_reset_wheels_first_at_every_phase_and_speed) {
	int trials = 0, withReset = 0, payed = 0, badPays = 0;
	for (float speed : { 300.f, 550.f, 900.f, 1400.f, 2000.f })
		for (float z0 = 700; z0 < 800; z0 += 7) {
			Result r = Run(-1.f, speed, z0);
			trials++;
			if (r.engineResets > 0) withReset++;
			if (r.engineResets > 0 && r.pays >= 1) payed++;
			if (r.pays > r.engineResets) badPays++; // paid more than the engine granted
		}
	printf("  wheels-first: %d trials, %d with an engine reset, %d of those paid\n", trials, withReset, payed);
	CHECK(withReset > 0);
	CHECK_EQ(payed, withReset);
	CHECK_EQ(badPays, 0);
}

TEST(FlipReset_roof_first_contact_is_never_paid) {
	for (float speed : { 300.f, 900.f, 2000.f })
		for (float z0 = 700; z0 < 800; z0 += 14) {
			Result r = Run(+1.f, speed, z0);
			CHECK_EQ(r.pays, 0);
		}
}

TEST(FlipReset_pays_a_ball_height_scaled_amount) {
	Result r = Run(-1.f, 550.f, 750.f);
	CHECK(r.pays >= 1);
	// ball at z ~ 1200 -> (1200 - 500) / 900 ~ 0.78, give or take the bounce
	CHECK(r.paid > 0.5f && r.paid <= 1.0f);
}
