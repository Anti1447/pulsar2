#pragma once
#include "../Reward.h"
#include "../RewardTrace.h"
#include "../../Math.h"

namespace RLGC {

	// Save event with an OPPOSED-SHOT guard.
	//
	// The raw engine save is farmable in 1v1 (verified against GameEventTracker.cpp):
	// GameEventTracker credits the shot to the team OPPOSITE the threatened net
	// (shooterTeam = RS_OPPOSITE_TEAM(goalTeam)), so a player who blasts the ball
	// at their OWN net some time after any opponent touch arms a phantom "shot"
	// credited to the opponent, then collects a "save" by touching it away again -
	// a repeatable zero-sum income stream decoupled from winning.
	//
	// The guard: only pay the save if the last ball touch STRICTLY BEFORE the
	// save step came from the OPPONENT. Genuine save (opponent shoots -> I block)
	// passes; self-served shots (my own touch armed the trajectory) pay 0, because
	// curTouchTeam persists as MY team from my arming touch onward until someone
	// else touches it - so prevTouchTeam is mine, not the opponent's, at the
	// moment I'd collect a self-farmed save. Conservative by design: a double-
	// touch save (my first contact fails to clear, second completes it) is also
	// blocked - under-crediting, never over-crediting.
	//
	// Stateful: last-touch tracking updates in PreStep (RewardWrapper forwards
	// PreStep/Reset to the child, so this works inside ZeroSumReward). Per-arena
	// instance -> no cross-arena state. Wrapped in ZeroSumReward(_, TEAM_SPIRIT) in BuildRewards (0.3): the saver
	// gets 0.85 and a teammate 0.15 in 2v2 (1v1 is unaffected); the opponents are charged either way. Do NOT gate:
	// the reachability gate's attack-oriented level is lowest exactly in the
	// own-half states where saves fire.
	class OpposedSaveReward : public Reward {
	public:
		// -1 = none yet, -2 = ambiguous (both teams touched the same step)
		int curTouchTeam = -1;  // team of the most recent touch, up to and including this step
		int prevTouchTeam = -1; // team of the last touch strictly before this step

		virtual void Reset(const GameState& initialState) override {
			curTouchTeam = prevTouchTeam = -1;
		}

		virtual void PreStep(const GameState& state) override {
			prevTouchTeam = curTouchTeam;

			int touchTeam = -1;
			bool ambiguous = false;
			for (const Player& p : state.players) {
				if (!p.ballTouchedStep)
					continue;
				int t = (int)p.team;
				if (touchTeam == -1)
					touchTeam = t;
				else if (touchTeam != t)
					ambiguous = true;
			}

			if (ambiguous)
				curTouchTeam = -2;
			else if (touchTeam != -1)
				curTouchTeam = touchTeam;
		}

		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			if (!player.eventState.save)
				return 0;

			// Pay only if the touch that created/carried the shot was the opponent's
			return (prevTouchTeam == (int)RS_OPPOSITE_TEAM(player.team)) ? 1.0f : 0.0f;
		}
	};
}
