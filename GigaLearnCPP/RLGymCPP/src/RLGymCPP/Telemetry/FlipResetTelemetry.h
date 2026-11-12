#pragma once
#include "../Gamestates/GameState.h"
#include "../Rewards/PlayerBall/FlipResetTouchReward.h"
#include "../Rewards/PlayerBall/FlipResetAirTouchReward.h"
#include <vector>

// ============================================================================
// FlipResetTelemetry.h - 2026-11-11
//
// State-only flip-reset counters, independent of which flip-reset reward is in the stack (they read FlipResetTouchReward's predicates, so the
// reward and its measurement cannot drift). Wired into StepCallback in src/ExampleMain.cpp (FlipReset/* panels).
//
//   "any"        : HasFlipReset() false -> true between consecutive steps, for any reason (includes leaving a wall/ramp without jumping).
//   "afterTouch" : the same edge with a ball touch by that player on the previous step (what FlipResetTouchReward pays).
//
// Per episode, per player. An episode is the run of states from one with no `prev` (the first step after a reset) to the step before the next such
// state; it is flushed when the next episode's first state arrives. The last episode still running when an iteration ends is reported next iteration.
// ============================================================================

namespace RLGC {
	namespace FlipResetTelemetry {
		constexpr int MAX_PLAYERS = 6; // 3v3

		struct EpisodeMem {
			int steps = 0;
			int nPlayers = 0;
			int any[MAX_PLAYERS] = {};
			int afterTouch[MAX_PLAYERS] = {};
			int airAfterTouch[MAX_PLAYERS] = {}; // afterTouch with the ball above FlipResetAirTouchReward::AIR_BALL_Z at the touch (2026-11-12)
		};

		struct StepEvents { int any = 0, afterTouch = 0, airAfterTouch = 0; };

		// Count this state's events into the episode memory; returns the totals over players for this step.
		inline StepEvents Observe(EpisodeMem& m, const GameState& s) {
			StepEvents ev;
			m.steps++;
			m.nPlayers = (int)s.players.size() < MAX_PLAYERS ? (int)s.players.size() : MAX_PLAYERS;
			for (int i = 0; i < m.nPlayers; i++) {
				const Player& p = s.players[i];
				if (FlipResetTouchReward::ResetEdge(p)) { m.any[i]++; ev.any++; }
				if (FlipResetTouchReward::ResetAfterTouch(p)) { m.afterTouch[i]++; ev.afterTouch++; }
				if (FlipResetAirTouchReward::AirResetAfterTouch(p, s)) { m.airAfterTouch[i]++; ev.airAfterTouch++; }
			}
			return ev;
		}

		// True when `m` holds a finished episode to report (call before Observe on a state with no prev).
		inline bool HasEpisode(const EpisodeMem& m) { return m.steps > 0; }
	}
}
