#include "TestFramework.h"

#include <RLGymCPP/Rewards/UnifiedAirDribbleReward.h>

#include <filesystem>

using namespace RLGC;

// UnifiedAirDribbleReward audit regression (2026-11-07). Touches are injected through the cars' ball-hit info so the geometry is exact and
// repeatable: the car sits 200 uu below a ball that is moving toward the opponent goal (no real contact). Probe before the fix: one aerial
// pop paid 0.55-1.04 raw (22-41 at the commented-out weight 40; GoalReward is 150).

namespace {
	void InitUadr() {
		static bool done = false;
		if (done) return;
		for (const char* path : { "collision_meshes", "../collision_meshes", "build/collision_meshes" })
			if (std::filesystem::exists(std::filesystem::path(path) / "soccar")) { RocketSim::Init(path); break; }
		done = true;
	}

	struct Rig {
		Arena* arena;
		std::vector<Car*> cars;
		UnifiedAirDribbleReward rew;
		GameState gs, prevGs;
		float ballY = 1500;
		bool carFar = false;

		Rig(int blue, int orange) {
			InitUadr();
			arena = Arena::Create(GameMode::SOCCAR);
			for (int i = 0; i < blue; i++) cars.push_back(arena->AddCar(Team::BLUE));
			for (int i = 0; i < orange; i++) cars.push_back(arena->AddCar(Team::ORANGE));
			Place(false);
			gs = GameState(arena);
			rew.Reset(gs);
		}
		~Rig() { delete arena; }

		// Ball at (0, ballY, 800) heading toward the +y goal; car 0 below it, roof up (or upside down). Other cars far away on the ground.
		void Place(bool upsideDown) {
			BallState bs; bs.pos = Vec(0, ballY, 800); bs.vel = Vec(0, 700, 0);
			arena->ball->SetState(bs);
			for (size_t i = 0; i < cars.size(); i++) {
				CarState cs = {};
				if (i == 0) {
					cs.pos = carFar ? Vec(3000, -3000, 17) : Vec(0, ballY - 40, 600);
					cs.vel = Vec(0, 700, 0);
					cs.isOnGround = false;
					if (upsideDown) cs.rotMat = RotMat::LookAt(Vec(0, 1, 0), Vec(0, 0, -1));
				} else {
					cs.pos = Vec(-3000 + 600 * (float)i, -4000, 17);
				}
				cars[i]->SetState(cs);
			}
		}

		// One 8-tick step; `touchers` get a fresh touch stamped on the last tick. Returns the reward for car 0.
		float Step(std::vector<int> touchers, bool upsideDown = false) {
			Place(upsideDown);
			arena->Step(8);
			ballY = arena->ball->GetState().pos.y;
			for (int c : touchers) {
				cars[c]->_ballHitInfo.isValid = true;
				cars[c]->_ballHitInfo.tickCountWhenHit = arena->tickCount - 1;
			}
			prevGs = gs;
			gs.UpdateFromArena(arena, std::vector<Action>(cars.size()), &prevGs);
			rew.PreStep(gs);
			return ((Reward&)rew).GetReward(gs.players[0], gs, false);
		}
	};
}

TEST(UnifiedAirDribble_a_lone_pop_pays_nothing_even_after_the_sequence_ends) {
	Rig r(1, 0);
	float total = 0;
	total += r.Step({ 0 });                 // the opening touch
	CHECK(r.rew.HasActiveSequence(r.gs.players[0].carId));
	for (int i = 0; i < 4; i++) total += r.Step({});
	// end the sequence: put the car far from the ball (disconnect) so it resolves
	r.carFar = true;
	total += r.Step({});
	CHECK(!r.rew.HasActiveSequence(r.gs.players[0].carId));
	CHECK(total == 0.0f);
}

TEST(UnifiedAirDribble_the_second_roof_side_touch_pays) {
	Rig r(1, 0);
	r.Step({ 0 });
	float second = 0;
	for (int i = 0; i < 3; i++) second += r.Step({});
	CHECK(second == 0.0f);
	float paid = r.Step({ 0 });
	CHECK(paid > 0.0f);
}

TEST(UnifiedAirDribble_a_wheels_side_touch_does_not_open_or_count) {
	Rig r(1, 0);
	float total = 0;
	total += r.Step({ 0 }, /*upsideDown*/ true);
	CHECK(!r.rew.HasActiveSequence(r.gs.players[0].carId)); // wheels toward the ball: no sequence
	total += r.Step({ 0 }, true);
	CHECK(total == 0.0f);
}

TEST(UnifiedAirDribble_a_simultaneous_opponent_touch_ends_the_sequence_even_when_a_teammate_is_listed_first) {
	// blue0 (owner), blue1 (teammate), orange0. Both blue1 and orange0 touch on the same step; the trigger used to stop at the first toucher.
	Rig r(2, 1);
	r.Step({ 0 });
	CHECK(r.rew.HasActiveSequence(r.gs.players[0].carId));
	r.Step({ 1, 2 });
	CHECK(!r.rew.HasActiveSequence(r.gs.players[0].carId));
}

TEST(UnifiedAirDribble_a_teammate_touch_alone_does_not_end_the_sequence) {
	Rig r(2, 1);
	r.Step({ 0 });
	r.Step({ 1 });
	CHECK(r.rew.HasActiveSequence(r.gs.players[0].carId));
}
