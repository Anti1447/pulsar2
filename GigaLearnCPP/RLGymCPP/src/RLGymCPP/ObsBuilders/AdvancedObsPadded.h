#pragma once
#include "AdvancedObs.h"
#include "../Gamestates/BallPredictor.h"

namespace RLGC {
	// AdvancedObs with a FIXED obs size for any team size up to maxPlayersPerTeam, so one
	// policy net can train across 1v1/2v2/3v3 (and carry a checkpoint between them).
	//
	// Layout: AdvancedObs's header and self block are kept byte-identical (ball at 0,
	// self right after the header — offsets the steering landing sims and analysis
	// tooling read), then teammate/opponent slots are padded to fixed counts with
	// all-zero rows, then one presence flag per slot (1 = real player) is appended at
	// the very end so the 29-float per-player stride is preserved.
	//
	// Real players are shuffled across their slots every build to prevent slot bias
	// (same rationale as DefaultObsPadded; safe because the policy is feedforward, and
	// it means every slot's weights train even while the arena mix is mostly 1v1).
	//
	// That shuffle draws from a clock-seeded engine, so the observation is
	// NONDETERMINISTIC for a fixed game state. Harmless while training; fatal for the
	// viewer, where it means restoring a state and playing forward twice gives two
	// different plays (measured: the obs diverged on the very first inference after a
	// byte-identical restore). Set shuffleSlots false to fill slots in order — the
	// policy is trained to be slot-invariant precisely so this costs nothing.
	// Keep it ON for training, or every slot past the first stops receiving gradient.
	//
	// BALL PREDICTION (6.0 lineage, ballPredSlices > 0): after the presence flags, the ball's
	// predicted pos and vel at every BALL_PRED_SLICE_TICKS (0.25s) up to ballPredSlices slices
	// (8 = 2.0s), in the same team frame as the ball at the top of the obs: 6 floats per slice,
	// appended last so every earlier offset is unchanged. The prediction is the engine's own ball
	// simulation (BallPredictor, a car-less copy of the arena), updated once per tick from
	// state.lastArena and reused across players and steps while the ball follows it. A state
	// without an arena (the RLBot client) must supply the slices with SetExternalBallPred,
	// e.g. from RLBot's BallPrediction; a build with neither is a hard error, never zeros.
	class AdvancedObsPadded : public AdvancedObs {
	public:
		static constexpr int BALL_PRED_SLICE_TICKS = 30;

		int maxPlayersPerTeam;
		bool shuffleSlots;
		int ballPredSlices;

		AdvancedObsPadded(int maxPlayersPerTeam, bool shuffleSlots = true, int ballPredSlices = 0)
			: maxPlayersPerTeam(maxPlayersPerTeam), shuffleSlots(shuffleSlots), ballPredSlices(ballPredSlices) {}
		~AdvancedObsPadded();

		// Ball states (absolute, blue frame) at 1..ballPredSlices slices ahead, used for states with
		// no arena. Cleared by ClearExternalBallPred.
		void SetExternalBallPred(const std::vector<PhysState>& slices);
		void ClearExternalBallPred() { externalBallPred.clear(); }

		virtual FList BuildObs(const Player& player, const GameState& state) override;

		// Per-tick prediction (ballPredSlices * BALL_PRED_SLICE_TICKS + 1 ticks) for rewards; null when
		// ballPredSlices == 0 or the state has no arena (external slices are not per-tick).
		virtual const std::vector<BallState>* GetBallPrediction(const GameState& state) override;

		// The predictor this builder uses for arena states (null until the first such build)
		const BallPredictor* GetBallPredictor() const { return ballPredictor; }

	private:
		BallPredictor* ballPredictor = nullptr;
		Arena* ballPredArena = nullptr;           // Arena the predictor was made for
		std::vector<PhysState> externalBallPred;

		void AddBallPredToObs(FList& obs, const GameState& state, bool inv);
		BallPredictor* UpdateBallPredictor(const GameState& state); // creates/updates; null if no arena
	};
}
