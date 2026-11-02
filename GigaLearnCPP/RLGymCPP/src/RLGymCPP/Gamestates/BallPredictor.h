#pragma once
#include "../Framework.h"

namespace RLGC {
	// Predicts one arena's ball by simulating it alone in a car-less copy of the arena: the same
	// job as v2 RocketSim's BallPredTracker, which the v3 engine has no equivalent of.
	//
	// pred[i] is the ball i ticks after the last Update (pred[0] = the ball given). An Update that
	// finds the ball where the previous prediction said it would be only shifts the prediction and
	// simulates the few new ticks at the end; anything else (a touch, a goal reset, a state setter)
	// re-simulates every tick.
	class BallPredictor {
	public:
		std::vector<BallState> pred;
		uint64_t fullPredictions = 0, reusedPredictions = 0;

		BallPredictor(GameMode gameMode, int numPredTicks);
		~BallPredictor();
		RG_NO_COPY(BallPredictor);

		// curTick: the arena tick count of `ball` (GameState::lastTickCount)
		void Update(const BallState& ball, uint64_t curTick);

		// Same margins as v2's BallState::Matches
		static bool Matches(const BallState& a, const BallState& b);

	private:
		Arena* arena;
		int numPredTicks;
		uint64_t lastTick = 0;

		void PredictFrom(size_t start);
	};
}
