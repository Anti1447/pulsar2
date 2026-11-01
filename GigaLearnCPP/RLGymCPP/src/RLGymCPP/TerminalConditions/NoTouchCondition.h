#pragma once
#include "TerminalCondition.h"

namespace RLGC {
	class NoTouchCondition : public TerminalCondition {
	public:

		float timeSinceTouch = 0;
		float maxTime;
		// false = a real ending (done, no bootstrap), for runs that penalize the timeout (NoTouchTimeoutPenalty)
		bool truncate;

		NoTouchCondition(float maxTime, bool truncate = true) : maxTime(maxTime), truncate(truncate) {
		}

		virtual void Reset(const GameState& initialState) {
			timeSinceTouch = 0;
		};

		virtual bool IsTerminal(const GameState& currentState) {
			for (auto& player : currentState.players) {
				if (player.ballTouchedStep) {
					timeSinceTouch = 0;
					return false;
				}
			}

			timeSinceTouch += currentState.deltaTime;
			return timeSinceTouch >= maxTime;
		}

		virtual bool IsTruncation() {
			return truncate;
		}
	};
}