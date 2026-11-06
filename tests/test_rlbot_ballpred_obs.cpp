#include "TestFramework.h"

#include <RLGymCPP/ObsBuilders/AdvancedObsPadded.h>
#include <RLGymCPP/Gamestates/BallPredictor.h>
#include <RLGymCPP/Gamestates/GameState.h>

#include <cmath>
#include <filesystem>

using namespace RLGC;

// The RLBot client has no arena: src/RLBotClient.cpp predicts the ball with a BallPredictor and feeds pred[i * 30] (i = 1..8)
// to the builder as external slices. That must give the SAME 278 floats training builds from the arena path
// (AdvancedObsPadded::AddBallPredToObs), for both teams. This mirrors the client's code on states from a real arena.

static void EnsureRocketSimLive() {
	static bool inited = false;
	if (inited)
		return;
	for (const char* path : { "collision_meshes", "../collision_meshes", "build/collision_meshes" })
		if (std::filesystem::exists(std::filesystem::path(path) / "soccar")) {
			RocketSim::Init(path);
			break;
		}
	inited = true;
}

TEST(RLBotClient_external_ballpred_obs_equals_the_training_obs) {
	EnsureRocketSimLive();
	constexpr int SLICES = 8, TICKS = AdvancedObsPadded::BALL_PRED_SLICE_TICKS;
	Arena* arena = Arena::Create(GameMode::SOCCAR);
	arena->AddCar(Team::BLUE);
	arena->AddCar(Team::ORANGE);
	arena->ResetToRandomKickoff(0);
	BallState ball = {};
	ball.pos = Vec(800, -600, 900);
	ball.vel = Vec(-400, 900, 600);
	ball.angVel = Vec(1, -2, 0.5f);
	arena->ball->SetState(ball);

	AdvancedObsPadded training(3, false, SLICES); // the arena path
	AdvancedObsPadded live(3, false, SLICES);     // the client's path
	BallPredictor clientPredictor(GameMode::SOCCAR, SLICES * TICKS + 1);

	float worst = 0;
	uint64_t clientTick = 0;
	for (int step = 0; step < 12; step++) {
		GameState withArena = GameState(arena);
		GameState noArena = withArena;
		noArena.lastArena = nullptr; // what ToGameState() produces

		clientTick += 8;
		clientPredictor.Update(noArena.ball, clientTick);
		std::vector<PhysState> slices;
		for (int i = 1; i <= SLICES; i++)
			slices.push_back(clientPredictor.pred[(size_t)i * TICKS]);
		live.SetExternalBallPred(slices);

		for (int p = 0; p < 2; p++) { // blue, then orange (inverted frame)
			FList a = training.BuildObs(withArena.players[p], withArena);
			FList b = live.BuildObs(noArena.players[p], noArena);
			CHECK_EQ((int)a.size(), 278);
			CHECK_EQ((int)b.size(), 278);
			for (size_t i = 0; i < a.size(); i++)
				worst = std::max(worst, fabsf(a[i] - b[i]));
		}
		arena->Step(8);
	}
	std::cout << "    largest |training obs - client obs| over 12 steps, both teams: " << worst << std::endl;
	CHECK(worst < 1e-4f);
	delete arena;
}
