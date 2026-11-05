#pragma once
#include "ExternalOpponent.h"
#include "NextoKickoff.h"

#include <torch/script.h>

namespace GGL {

	// EXTERNAL TRAINING OPPONENT: Nexto (2026-07-20, user-directed "play better
	// bots to force the aerials"). Nexto is a public SSL-level RLGym bot that
	// plays the air constantly - contesting it fills the buffer with the high-ball
	// states our self-play never generates, and it doubles as a FIXED external
	// yardstick (Nexto/Goals For/Against) immune to the version-pool inflation
	// documented in H2_TRUNCATION.md. Same tick_skip (8) as this run; its EARL
	// attention net handles any team size, so it serves 1v1/2v2/3v3 alike.
	//
	// This class adapts Nexto's interface to ours: its TorchScript module
	// (rlbot-run/nexto/nexto-model.pt, loaded verbatim - never trained here), a
	// C++ port of its per-entity obs builder (rlbot-run/nexto/nexto_obs.py is the
	// reference, replicated EXACTLY - including its shipped quirks, e.g. orange
	// observers see mirrored pad positions paired with absolute-order pad flags;
	// fidelity to the trained distribution beats local notions of correctness),
	// and its 90-row action lookup mapped to OUR DefaultAction indices by tuple
	// equality at construction (hard-fails if any row has no match, so table
	// drift can never silently scramble its controls).
	//
	// Threading: see ExternalOpponent.
	class NextoOpponent : public ExternalOpponent {
	public:
		torch::jit::script::Module model;
		torch::Device device;
		// Nexto lookup row -> our DefaultAction index (both 90 entries)
		std::vector<int> actionMap;
		// Nexto's raw 8-float rows (its q vector wants the PARSED previous action)
		std::vector<std::array<float, 8>> lookup;
		// Per-global-player previous parsed action (zeroed on episode reset/serve start)
		std::vector<std::array<float, 8>> prevActions;
		// Nexto's hardcoded kickoff (NextoKickoff.h). On by default; GGL_NEXTO_SCRIPTED_KICKOFF=0 turns it off
		// (pure argmax from the first step, the behaviour before 2026-11-05) for A/B comparisons.
		bool scriptedKickoff = true;
		NextoKickoff kickoff;
		// NextoKickoff::Kind -> Nexto lookup row (exact tuple match, hard-fails if the table drifts)
		std::array<int, NextoKickoff::NUM_KINDS> kickoffLookupIdx = {};

		NextoOpponent(const std::string& modelPath, torch::Device device);

		// Player `o`'s net input as Act builds it: kvOut = (n + 1 + 34) x 24 floats (arena order), qOut = 32 floats
		static void BuildObs(const RLGC::GameState& gs, int o, const std::array<float, 8>& prevAction,
			float* kvOut, float* qOut);

		// Zero all previous-action memory (call once when a serve iteration begins:
		// Nexto picks up arenas mid-episode, same cold start its RLBot boot has)
		void BeginServe(int numPlayers) override;

		// A fresh episode (prevArenaTerminals) zeroes that arena's players' previous actions first
		void Act(const std::vector<RLGC::GameState>& states,
			const std::vector<bool>& isOld,
			const std::vector<uint8_t>& prevArenaTerminals,
			std::vector<int>& outActionIdx) override;
	};
}
