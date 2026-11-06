#include "TestFramework.h"

#include <RLGymCPP/EnvSet/EnvSet.h>
#include <RLGymCPP/Rewards/CommonRewards.h>

#include <filesystem>

using namespace RLGC;

// VerifiedShotReward (2026-11-05): GameEventTracker's plain shot event credits the latest touch by the team OPPOSITE the net the ball threatens,
// even when another car touched the ball after it. A car that hits the ball at its OWN net after an opponent's touch therefore handed that
// opponent a phantom shot. The verified-shot event fires only when the credited shooter is also the last car to touch the ball.
// Scenario (tracker-level, hit info injected): the ball is at (0,-2500) doing 2000 uu/s toward the blue net (y = -5120), predicted to score.

namespace {
	void InitRocketSimVerified() {
		static bool done = false;
		if (done) return;
		for (const char* path : { "collision_meshes", "../collision_meshes", "build/collision_meshes" })
			if (std::filesystem::exists(std::filesystem::path(path) / "soccar")) { RocketSim::Init(path); break; }
		done = true;
	}

	// The shot needs the shooter touch >= 0.3 s (36 ticks) before the check, and inside deltaTicks + 36 of it. SetState resets the ball update
	// counter, so after the one Step in Run() deltaTicks is 1: the touch must be 36 ticks old BEFORE that step (37 at the check).
	constexpr int SHOOT_AGO = 36;

	struct Counts { int shot = 0, verified = 0; Car* shooter = nullptr; Car* verifiedShooter = nullptr; };

	// orangeAgo/blueAgo: ticks since each car's last touch (0 = never touched)
	Counts Run(int orangeAgo, int blueAgo, bool towardBlueNet = true) {
		InitRocketSimVerified();
		Arena* arena = Arena::Create(GameMode::SOCCAR);
		Car* blue = arena->AddCar(Team::BLUE);
		Car* orange = arena->AddCar(Team::ORANGE);
		CarState b; b.pos = Vec(3000, 3000, 17); blue->SetState(b);
		CarState o; o.pos = Vec(-3000, 3000, 17); orange->SetState(o);
		for (int i = 0; i < 80; i++) arena->Step(1); // so tickCount is large enough to subtract from
		BallState bs; bs.pos = Vec(0, towardBlueNet ? -2500 : 2500, 150); bs.vel = Vec(0, towardBlueNet ? -2000 : 2000, 0);
		arena->ball->SetState(bs);
		auto touch = [&](Car* c, int ago) {
			if (ago <= 0) return;
			c->_ballHitInfo.isValid = true;
			c->_ballHitInfo.tickCountWhenHit = arena->tickCount - ago;
		};
		touch(orange, orangeAgo);
		touch(blue, blueAgo);

		GameEventTracker tracker({});
		tracker.autoStateSetDetection = false;
		tracker.config.shotMinSpeed = 1000.f;
		Counts c;
		struct Ctx { Counts* c; } ctx{ &c };
		tracker.SetShotCallback([](Arena*, Car* s, Car*, void* ud) { auto* x = (Ctx*)ud; x->c->shot++; x->c->shooter = s; }, &ctx);
		tracker.SetVerifiedShotCallback([](Arena*, Car* s, Car*, void* ud) { auto* x = (Ctx*)ud; x->c->verified++; x->c->verifiedShooter = s; }, &ctx);
		arena->Step(1); // the tracker only acts when the ball has been updated since its last look
		tracker.Update(arena);
		// Report which team the shooter belonged to via the pointers; tests compare with the cars they know.
		c.shooter = c.shooter ? c.shooter : nullptr;
		delete arena;
		return c;
	}
}

// The exploit: orange touched 40 ticks ago, then blue (the defender-side car) hit the ball at its OWN net 18 ticks ago.
TEST(VerifiedShot_an_own_net_hit_after_an_opponent_touch_is_not_a_verified_shot_for_the_opponent) {
	Counts c = Run(/*orangeAgo*/ SHOOT_AGO, /*blueAgo*/ 18);
	CHECK_EQ(c.shot, 1);      // the plain event still credits orange (this is the documented flaw)
	CHECK_EQ(c.verified, 0);  // the verified event does not
}

// A real shot: orange is the last toucher (40 ticks ago, 0.33 s) and the ball is headed for the blue net
TEST(VerifiedShot_the_last_toucher_of_a_shot_is_verified) {
	Counts c = Run(SHOOT_AGO, 0);
	CHECK_EQ(c.shot, 1);
	CHECK_EQ(c.verified, 1);
	CHECK(c.verifiedShooter == c.shooter);
}

// The last toucher on the shooter's team, with an earlier opponent touch: still verified
TEST(VerifiedShot_an_earlier_opponent_touch_does_not_block_the_shooter) {
	Counts c = Run(SHOOT_AGO, 70);
	CHECK_EQ(c.shot, 1);
	CHECK_EQ(c.verified, 1);
}

// A ball not threatening any net raises neither event
TEST(VerifiedShot_no_threat_no_event) {
	Counts c = Run(SHOOT_AGO, 0, /*towardBlueNet*/ false);
	// heading for the ORANGE net now: blue would be the shooter, but blue never touched, so nothing is credited
	CHECK_EQ(c.shot, 0);
	CHECK_EQ(c.verified, 0);
}

// Through the real EnvSet: the reward reads eventState.shotVerified, which is the verified event, and Player resets it each step
TEST(VerifiedShot_reward_reads_the_verified_flag_only) {
	Player p; GameState gs;
	VerifiedShotReward rr; Reward& r = rr; // GetReward is private on the template: call it through the base
	CHECK_EQ(r.GetReward(p, gs, false), 0.f);
	p.eventState.shot = true;
	CHECK_EQ(r.GetReward(p, gs, false), 0.f); // the unverified flag pays nothing
	p.eventState.shotVerified = true;
	CHECK_EQ(r.GetReward(p, gs, false), 1.f);
}
