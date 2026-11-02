#include "BallPredictor.h"

RLGC::BallPredictor::BallPredictor(GameMode gameMode, int numPredTicks) : numPredTicks(numPredTicks) {
	RG_ASSERT(numPredTicks > 1);
	arena = Arena::Create(gameMode);
}

RLGC::BallPredictor::~BallPredictor() {
	delete arena;
}

bool RLGC::BallPredictor::Matches(const BallState& a, const BallState& b) {
	return
		a.pos.DistSq(b.pos) < (0.8f * 0.8f) &&
		a.vel.DistSq(b.vel) < (0.4f * 0.4f) &&
		a.angVel.DistSq(b.angVel) < (0.02f * 0.02f);
}

void RLGC::BallPredictor::PredictFrom(size_t start) {
	arena->ball->SetState(pred[start - 1]);
	for (size_t i = start; i < (size_t)numPredTicks; i++) {
		arena->Step(1);
		pred[i] = arena->ball->GetState();
	}
}

void RLGC::BallPredictor::Update(const BallState& ball, uint64_t curTick) {
	uint64_t elapsed = curTick - lastTick;
	bool canReuse = !pred.empty() && curTick >= lastTick && elapsed < (uint64_t)numPredTicks
		&& Matches(pred[elapsed], ball);
	lastTick = curTick;

	if (canReuse) {
		if (elapsed == 0)
			return;
		pred.erase(pred.begin(), pred.begin() + (ptrdiff_t)elapsed);
		size_t start = pred.size();
		pred.resize(numPredTicks);
		PredictFrom(start);
		reusedPredictions++;
	} else {
		pred.assign(numPredTicks, ball);
		PredictFrom(1);
		fullPredictions++;
	}
}
