// Harness stub. GalactusV2\x27s GalactusOBS.cpp links a RocketSim BallPredTracker for its TRAINING-time ball prediction.
// The golden test feeds it the external (RLBot-style) samples instead (SetExternalBallPredSamples), so the tracker is
// never reached; this stub only lets the original source link, and aborts if it ever is.
#pragma once
#include <RocketSim/src/Sim/Arena/Arena.h>
#include <cstdlib>

RS_NS_START
struct BallPredTracker {
	std::vector<BallState> predData;
	BallPredTracker(Arena*, size_t) { std::abort(); }
	void UpdatePredFromArena(Arena*) { std::abort(); }
};
RS_NS_END
