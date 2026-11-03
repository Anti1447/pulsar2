#include "TestFramework.h"

#include <RLGymCPP/EnvSet/EnvSet.h>
#include <RLGymCPP/Rewards/CommonRewards.h>
#include <RLGymCPP/TerminalConditions/NoTouchCondition.h>
#include <RLGymCPP/ObsBuilders/AdvancedObsPadded.h>
#include <RLGymCPP/StateSetters/KickoffState.h>
#include <RLGymCPP/ActionParsers/DefaultAction.h>
#include <RLGymCPP/Gamestates/BallPredictor.h>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>

using namespace RLGC;

// Item 4 of docs/Pre_Run_Checklist.md: the 8 ball-prediction slices checked in a REAL arena with
// cars (touches, resets, kickoffs), not only on fabricated states, plus the cost at scale.
// tests/test_advanced_obs_padded.cpp covers the layout and the reuse path on a car-free flight.

static constexpr int BP_SLICES = 8;
static constexpr int BP_TICKS = AdvancedObsPadded::BALL_PRED_SLICE_TICKS;
static constexpr int BP_PADDED = 230;

static void EnsureRocketSimBallPred() {
	static bool inited = false;
	if (inited)
		return;
	for (const char* path : { "collision_meshes", "../collision_meshes", "build/collision_meshes" }) {
		if (std::filesystem::exists(std::filesystem::path(path) / "soccar")) {
			RocketSim::Init(path);
			break;
		}
	}
	inited = true;
}

// Largest |a-b| over the ball-prediction block of two obs
static float PredBlockDiff(const FList& a, const FList& b) {
	float worst = 0;
	for (int i = BP_PADDED; i < BP_PADDED + BP_SLICES * 6; i++)
		worst = std::max(worst, fabsf(a[i] - b[i]));
	return worst;
}

// Slices the builder reports must be where the real ball ends up: a lob across an arena with idle cars
TEST(BallPredArena_slices_match_where_the_real_ball_goes) {
	EnsureRocketSimBallPred();
	Arena* arena = Arena::Create(GameMode::SOCCAR);
	arena->AddCar(Team::BLUE);
	arena->AddCar(Team::ORANGE);
	arena->ResetToRandomKickoff(0);
	BallState ball = {};
	ball.pos = Vec(800, -600, 900);
	ball.vel = Vec(-400, 900, 600);
	ball.angVel = Vec(1, -2, 0.5f);
	arena->ball->SetState(ball);

	GameState gs = GameState(arena);
	AdvancedObsPadded padded(3, false, BP_SLICES);
	padded.BuildObs(gs.players[0], gs);
	std::vector<BallState> pred = padded.GetBallPredictor()->pred; // copy: the arena is about to move on

	float worst = 0;
	for (int s = 1; s <= BP_SLICES; s++) {
		arena->Step(BP_TICKS);
		BallState real = arena->ball->GetState();
		float err = real.pos.Dist(pred[(size_t)s * BP_TICKS].pos);
		worst = std::max(worst, err);
		CHECK(err < 1.0f);
	}
	std::cout << "    worst slice position error vs the real ball over 2 s: " << worst << " uu" << std::endl;
	delete arena;
}

// Both cars drive at the ball with boost, so there are real touches. At every decision step the
// persistent builder (shift/reuse) must agree with a builder that predicts from scratch.
// A state-setter reset (ResetToRandomKickoff) is forced mid-rollout.
TEST(BallPredArena_reuse_agrees_with_fresh_prediction_through_touches_and_resets) {
	EnsureRocketSimBallPred();
	Arena* arena = Arena::Create(GameMode::SOCCAR);
	Car* blue = arena->AddCar(Team::BLUE);
	Car* orange = arena->AddCar(Team::ORANGE);
	arena->ResetToRandomKickoff(1);

	GameState gs = GameState(arena);
	AdvancedObsPadded persistent(3, false, BP_SLICES);
	persistent.BuildObs(gs.players[0], gs);

	int touches = 0, resets = 0;
	float worst = 0;
	for (int step = 0; step < 300; step++) {
		if (step == 150) { // state-setter style reset mid-episode
			arena->ResetToRandomKickoff(7);
			resets++;
		}
		for (Car* c : { blue, orange }) {
			c->controls = CarControls();
			c->controls.throttle = 1;
			c->controls.boost = true;
			// steer toward the ball in the car's frame
			Vec toBall = arena->ball->GetState().pos - c->GetState().pos;
			Vec fwd = c->GetState().rotMat.forward;
			float cross = fwd.x * toBall.y - fwd.y * toBall.x;
			c->controls.steer = cross > 0 ? 1.f : -1.f;
		}
		arena->Step(8);
		gs.UpdateFromArena(arena, std::vector<Action>(2), NULL);
		for (auto& p : gs.players)
			if (p.ballTouchedStep)
				touches++;

		FList a = persistent.BuildObs(gs.players[0], gs);
		AdvancedObsPadded fresh(3, false, BP_SLICES);
		FList b = fresh.BuildObs(gs.players[0], gs);
		worst = std::max(worst, PredBlockDiff(a, b));
	}

	const BallPredictor* pred = persistent.GetBallPredictor();
	std::cout << "    touches " << touches << ", resets " << resets << ", full predictions " << pred->fullPredictions
		<< ", reused " << pred->reusedPredictions << ", worst reuse-vs-fresh obs diff " << worst << std::endl;
	CHECK(touches >= 1);                      // the rollout really hit the ball
	CHECK(pred->fullPredictions >= 3);        // initial + the touch(es) + the reset all re-simulated
	CHECK(pred->reusedPredictions >= 1);      // and the cheap path was used in between
	CHECK(worst < 1e-3f);
	delete arena;
}

// After a kickoff reset the ball sits still at the centre, so every slice is the ball itself
TEST(BallPredArena_kickoff_slices_are_the_stationary_ball) {
	EnsureRocketSimBallPred();
	Arena* arena = Arena::Create(GameMode::SOCCAR);
	arena->AddCar(Team::BLUE);
	arena->AddCar(Team::ORANGE);
	arena->ResetToRandomKickoff(3);
	GameState gs = GameState(arena);
	AdvancedObsPadded padded(3, false, BP_SLICES);
	padded.BuildObs(gs.players[0], gs);
	const auto& pred = padded.GetBallPredictor()->pred;
	for (int s = 1; s <= BP_SLICES; s++) {
		CHECK(pred[(size_t)s * BP_TICKS].pos.Dist(gs.ball.pos) < 1.0f);
		CHECK(pred[(size_t)s * BP_TICKS].vel.Length() < 1.0f);
	}
	delete arena;
}

// Cost at scale: 310 1v1 arenas (the production game count), steps per second and resident memory
// with and without the 8 slices. Reports; asserts only that both runs complete and finish finite.
static double RunEnvSet(int slices, int numArenas, int steps, long* rssKbAfter) {
	EnvSetConfig cfg = {};
	cfg.numArenas = numArenas;
	cfg.tickSkip = 8;
	cfg.actionDelay = 7;
	cfg.saveRewards = false;
	cfg.envCreateFn = [slices](int) {
		auto arena = Arena::Create(GameMode::SOCCAR);
		arena->AddCar(Team::BLUE);
		arena->AddCar(Team::ORANGE);
		EnvCreateResult result = {};
		result.arena = arena;
		result.actionParser = new DefaultAction();
		result.obsBuilder = new AdvancedObsPadded(3, true, slices);
		result.stateSetter = new KickoffState();
		result.terminalConditions = { new NoTouchCondition(10) };
		result.rewards = { { new BallToGoalPotentialReward(0.9985f), 1.f } };
		return result;
	};
	EnvSet envSet(cfg);
	IList actions(envSet.state.numPlayers, 0);
	for (int i = 0; i < envSet.state.numPlayers; i++)
		actions[i] = i % 8; // a spread of fixed actions: the cars move, some touches happen
	auto t0 = std::chrono::steady_clock::now();
	for (int s = 0; s < steps; s++) {
		envSet.StepFirstHalf(false);
		envSet.StepSecondHalf(actions, false);
	}
	envSet.Sync();
	double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
	long pages = 0, resident = 0;
	std::ifstream("/proc/self/statm") >> pages >> resident;
	*rssKbAfter = resident * 4; // 4 KiB pages
	return secs;
}

TEST(BallPredArena_cost_at_310_games) {
	EnsureRocketSimBallPred();
	const int arenas = 310, steps = 150;
	long rssOff = 0, rssOn = 0;
	double tOff = RunEnvSet(0, arenas, steps, &rssOff);
	double tOn = RunEnvSet(BP_SLICES, arenas, steps, &rssOn);
	double envSteps = (double)arenas * 2 * steps; // player-steps
	std::cout << "    310 x 1v1, " << steps << " steps: no slices " << envSteps / tOff << " player-steps/s ("
		<< tOff << " s), 8 slices " << envSteps / tOn << " player-steps/s (" << tOn << " s), slowdown "
		<< tOn / tOff << "x; resident after: " << rssOff / 1024 << " MB then " << rssOn / 1024 << " MB (cumulative process)" << std::endl;
	CHECK(tOff > 0 && tOn > 0);
}
