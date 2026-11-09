#pragma once
#include "../Gamestates/GameState.h"
#include "../Rewards/Events/DemoGeometry.h"
#include <vector>

// ============================================================================
// DemoTelemetry.h - 2026-11-09
//
// State-only demo measurements, independent of which demo reward is in the stack (they read DemoGeometry.h, which the reward also uses).
// Wired into StepCallback in src/ExampleMain.cpp (Demo/* panels). Pre-registration: docs/reward_scenarios/OffensiveDemo_2026-11-09.md.
//
// Per demo:   Demo/Offensive Factor, Demo/High Factor Share (f >= 0.5), Demo/Multiplier, Demo/Victim Line Dist, Demo/Victim Ball Dist,
//             Demo/Unresolved Rate (a demo whose victim could not be identified).
// Baseline:   Demo/Opp Mean Factor = f of EVERY opponent every sampled step. Demo/Offensive Factor above this means the bot picks its
//             targets; equal to it means demos are position-blind (the conviction for adding the multiplier).
// Conversion: Demo/Goal After Own Demo = per goal, was there a demo by the SCORING team within the last WINDOW_STEPS (4 s at 15 Hz)?
//             Demo/Own Demo Window Fraction = per team-step, is such a window open? Their ratio is the lift (> 1: demos lead to goals).
//             Team games only (a demo in 1v1 has no teammate to exploit the window).
// ============================================================================

namespace RLGC {
	namespace DemoTelemetry {
		constexpr int WINDOW_STEPS = 30; // 4.0 s at 15 Hz (8/120 s per step)
		constexpr float HIGH_FACTOR = 0.5f;

		// Per-arena memory of steps since the last demo by each team (index 0 = BLUE, 1 = ORANGE). -1 = none this episode.
		struct ArenaMem { int sinceDemo[2] = { -1, -1 }; };

		inline int TeamIdx(Team t) { return (int)t == (int)Team::BLUE ? 0 : 1; }

		// Advance one arena's window by one step. A state with no prev is the first step of an episode: forget.
		inline void Advance(ArenaMem& m, const GameState& s) {
			if (!s.prev) m = ArenaMem();
			for (int t = 0; t < 2; t++)
				if (m.sinceDemo[t] >= 0) m.sinceDemo[t] = (m.sinceDemo[t] + 1 > WINDOW_STEPS) ? -1 : m.sinceDemo[t] + 1;
			for (auto& p : s.players)
				if (p.eventState.demo) m.sinceDemo[TeamIdx(p.team)] = 0;
		}

		inline bool WindowOpen(const ArenaMem& m, Team t) { return m.sinceDemo[TeamIdx(t)] >= 0; }

		// The team that scored (the ball is in the OTHER team's net: ball y > 0 is ORANGE's goal, scored by BLUE).
		inline Team Scorer(const GameState& s) { return s.ball.pos.y > 0 ? Team::BLUE : Team::ORANGE; }

		// Mean f over the opponents of `viewer` (what a position-blind demo would average).
		inline float OppFactor(const Player& opp, Team attackingTeam, const GameState& s) {
			const Vec ballPos = s.prev ? s.prev->ball.pos : s.ball.pos;
			return DemoGeometry::FactorAt(ballPos, DemoGeometry::PosBefore(opp), attackingTeam);
		}
	}
}
