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
// Credit:     Demo/Implausible Rate = per credited demo, was it physically impossible (bumper not supersonic on either step, or more than
//             PLAUSIBLE_MAX_DIST from the victim before the step)? ~0 when events reach the right cars; a misrouted event pairs cars that are
//             nowhere near each other, so it reads high.
//             Demo/Event-State Mismatch Rate = per step with any demo activity, did the callback flags (eventState.demo / .demoed) disagree
//             with the cars whose isDemoed flag actually rose, or have no opposite-team pair? 0 when events are credited correctly.
//             Demo/Events Checked = how many such steps this iteration (the denominator).
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

		// ---- credit checks (2026-11-11): are demo events attached to the right cars? State-only, no reward in the loop ----
		constexpr float PLAUSIBLE_MAX_DIST = 600.0f; // uu between car centres BEFORE the step: contact at ~120 uu plus up to ~310 uu of closing in 8 ticks

		// Could this bumper have demoed this victim? The bumper must be supersonic (on this step or the one before; the flag can flip inside the step)
		// and within PLAUSIBLE_MAX_DIST of the victim's position before the step.
		inline bool DemoPlausible(const Player& bumper, const Player& victim) {
			const bool supersonic = bumper.isSupersonic || (bumper.prev && bumper.prev->isSupersonic);
			const float d = (DemoGeometry::PosBefore(bumper) - DemoGeometry::PosBefore(victim)).Length();
			return supersonic && d <= PLAUSIBLE_MAX_DIST;
		}

		struct CreditCheck {
			bool evaluable = false; // false on the first step of an episode (no previous state to see isDemoed rise against)
			bool anything = false;  // any demo flag set, or any car's isDemoed rose
			bool mismatch = false;
		};

		// Compare the callback-set flags with what the car states independently show. A car whose isDemoed rose must be the one flagged demoed,
		// nobody else may be, a rise needs a flagged bumper (and a flagged bumper a rise), and every flagged bumper needs an opposite-team flagged victim.
		inline CreditCheck StateEventCheck(const GameState& s) {
			CreditCheck c;
			if (!s.prev) return c;
			c.evaluable = true;
			int risen = 0, bumpers = 0, victims = 0;
			for (const Player& p : s.players) {
				const bool rose = p.isDemoed && p.prev && !p.prev->isDemoed;
				risen += rose; bumpers += p.eventState.demo; victims += p.eventState.demoed;
				if (rose != p.eventState.demoed) c.mismatch = true;
			}
			c.anything = risen > 0 || bumpers > 0 || victims > 0;
			if ((risen > 0) != (bumpers > 0)) c.mismatch = true;
			for (const Player& b : s.players) {
				if (!b.eventState.demo) continue;
				bool pair = false;
				for (const Player& v : s.players)
					if (v.eventState.demoed && v.team != b.team) pair = true;
				if (!pair) c.mismatch = true;
			}
			return c;
		}

		// Mean f over the opponents of `viewer` (what a position-blind demo would average).
		inline float OppFactor(const Player& opp, Team attackingTeam, const GameState& s) {
			const Vec ballPos = s.prev ? s.prev->ball.pos : s.ball.pos;
			return DemoGeometry::FactorAt(ballPos, DemoGeometry::PosBefore(opp), attackingTeam);
		}
	}
}
