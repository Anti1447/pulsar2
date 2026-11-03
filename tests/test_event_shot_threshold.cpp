#include "TestFramework.h"

#include <RLGymCPP/EnvSet/EnvSet.h>
#include <RLGymCPP/Rewards/CommonRewards.h>
#include <RLGymCPP/ObsBuilders/AdvancedObs.h>
#include <RLGymCPP/ActionParsers/ActionParser.h>
#include <RLGymCPP/StateSetters/StateSetter.h>

#include <filesystem>

using namespace RLGC;

// The engine's shot event decides which saves OpposedSaveReward can pay (2026-11-03: user-directed widening of the tracker's shotMinSpeed from
// 1750 to 1000 uu/s, set in EnvSet.cpp). A shot also needs the ball predicted to score within shotMinScoreTime = 2.0 s (time measured on y only),
// so at ~1100 uu/s it must be struck from within about 2200 uu of the goal line. Scenario: an orange car drives south at 1500 uu/s (no boost, no
// flip) into a ball at (0, -3200); the ball leaves at about 1100 uu/s toward the blue goal and is neither a shot at 1750 nor unshot at 1000.

namespace {
	void InitRocketSimShot() {
		static bool done = false;
		if (done) return;
		for (const char* path : { "collision_meshes", "../collision_meshes", "build/collision_meshes" })
			if (std::filesystem::exists(std::filesystem::path(path) / "soccar")) { RocketSim::Init(path); break; }
		done = true;
	}

	// Places an idle blue car far away, an orange car at (0,-1700) doing 1500 uu/s south, the ball resting at (0,-3200)
	void Setup(Arena* arena, Car*& blue, Car*& orange) {
		blue = arena->AddCar(Team::BLUE);
		orange = arena->AddCar(Team::ORANGE);
		CarState b; b.pos = Vec(3000, 3000, 17); b.rotMat = Angle(0, 0, 0).ToRotMat(); blue->SetState(b);
		CarState o; o.pos = Vec(0, -1700, 17); o.vel = Vec(0, -1500, 0); o.rotMat = Angle(-1.5708f, 0, 0).ToRotMat(); o.boost = 0; orange->SetState(o);
		BallState bs; bs.pos = Vec(0, -3200, 93); arena->ball->SetState(bs);
	}
}

// With the tracker's own default (1750) the 1100 uu/s hit is not a shot; with the 1000 uu/s threshold EnvSet sets it is
TEST(ShotEvent_a_1100_uu_per_s_ground_hit_is_a_shot_at_threshold_1000_and_not_at_1750) {
	InitRocketSimShot();
	auto shots = [](float minSpeed) {
		Arena* arena = Arena::Create(GameMode::SOCCAR);
		Car* blue; Car* orange; Setup(arena, blue, orange);
		GameEventTracker tracker({});
		tracker.config.shotMinSpeed = minSpeed;
		int shotCalls = 0;
		tracker.SetShotCallback([](Arena*, Car*, Car*, void* ud) { (*(int*)ud)++; }, &shotCalls);
		float maxBallSpeed = 0;
		for (int s = 0; s < 60; s++) {
			orange->controls = CarControls(); orange->controls.throttle = 1;
			arena->Step(8);
			tracker.Update(arena);
			maxBallSpeed = std::max(maxBallSpeed, arena->ball->GetState().vel.Length());
		}
		delete arena;
		return std::make_pair(shotCalls, maxBallSpeed);
	};
	auto strict = shots(1750.f), wide = shots(1000.f);
	CHECK(strict.second > 1000.f && strict.second < 1750.f);   // the hit really is between the two thresholds
	CHECK_EQ(strict.first, 0);
	CHECK(wide.first >= 1);
}

// End to end through the real EnvSet: the shot event reaches the shooter's eventState.shot for the step it is raised
namespace {
	class TwoCarSetter : public StateSetter {
	public:
		void ResetArena(Arena* arena) override {
			Car* blue = arena->_cars.front(); Car* orange = *std::next(arena->_cars.begin());
			CarState b; b.pos = Vec(3000, 3000, 17); blue->SetState(b);
			CarState o; o.pos = Vec(0, -1700, 17); o.vel = Vec(0, -1500, 0); o.rotMat = Angle(-1.5708f, 0, 0).ToRotMat(); o.boost = 0; orange->SetState(o);
			BallState bs; bs.pos = Vec(0, -3200, 93); arena->ball->SetState(bs);
		}
	};
	class ThrottleParser : public ActionParser {
	public:
		int GetActionAmount() override { return 1; }
		Action ParseAction(int, const Player&, const GameState&) override { Action a = {}; a.throttle = 1; return a; }
	};
}

TEST(ShotEvent_EnvSet_raises_the_shot_flag_for_the_1100_uu_per_s_hit) {
	InitRocketSimShot();
	EnvSetConfig cfg = {};
	cfg.numArenas = 1; cfg.tickSkip = 8; cfg.actionDelay = 0; cfg.saveRewards = false;
	cfg.envCreateFn = [](int) {
		auto arena = Arena::Create(GameMode::SOCCAR);
		arena->AddCar(Team::BLUE); arena->AddCar(Team::ORANGE);
		EnvCreateResult r = {};
		r.arena = arena;
		r.actionParser = new ThrottleParser();
		r.obsBuilder = new AdvancedObs();
		r.stateSetter = new TwoCarSetter();
		r.rewards = { { new GoalReward(), 1.f } };
		return r;
	};
	EnvSet envSet(cfg);
	IList actions(envSet.state.numPlayers, 0);
	int shotSteps = 0;
	for (int s = 0; s < 60; s++) {
		envSet.StepFirstHalf(false);
		envSet.StepSecondHalf(actions, false);
		if (envSet.state.gameStates[0].players[1].eventState.shot) shotSteps++;
		if (envSet.state.terminals[0]) break;
	}
	envSet.Sync();
	CHECK(shotSteps >= 1);
}
