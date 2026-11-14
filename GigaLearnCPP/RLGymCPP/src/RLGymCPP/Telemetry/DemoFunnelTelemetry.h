#pragma once
#include "../Gamestates/GameState.h"
#include <vector>
#include <array>

// ============================================================================
// DemoFunnelTelemetry.h - 2026-11-13. MEASUREMENT ONLY (no reward, no actuation). Wired into StepCallback (Demo/Funnel* panels).
//
// Question it answers: when the bot does NOT demo, where does the funnel break?  approach -> closing fast -> supersonic -> contact.
//
// Per alive player-step (rates; all read state only):
//   Demo/Funnel Near Rate         nearest alive opponent within NEAR_DIST.
//   Demo/Funnel Closing Rate      ... and the closing speed toward it > CLOSING_SPEED.
//   Demo/Funnel Supersonic Rate   ... and the player is supersonic (a demo needs it: DemoTelemetry::DemoPlausible).
// Per ATTEMPT = rising edge of the Supersonic stage (not true the step before). The attempt stays open ATTEMPT_STEPS steps:
//   Demo/Funnel Attempts          count this iteration (+ Cumulative)
//   Demo/Attempt Conversion       per attempt, 1 if this player credited a demo while it was open, else 0 (reported when it closes)
//   Demo/Unattempted Demo Share   per demo, 1 if the bumper had NO open attempt (the funnel definition missing real demos)
// Reading: Near high but Closing low = the bot is beside opponents, not driving at them (approach). Closing high but Supersonic low =
// it chases without boost/speed. Supersonic attempts high but Conversion low = it arrives but misses (contact / aim).
// ============================================================================

namespace RLGC {
	namespace DemoFunnel {
		constexpr float NEAR_DIST = 1200.0f;    // uu. At the 2300 uu/s top speed that is ~0.5 s = ~8 steps at 15 Hz from first "near" to contact.
		constexpr float CLOSING_SPEED = 500.0f; // uu/s of relative speed along the line to the opponent
		constexpr int ATTEMPT_STEPS = 10;       // 0.67 s: contact from NEAR_DIST at the top speed takes ~8 steps, with margin
		constexpr int MAX_PLAYERS = 6;

		struct Stage { bool near = false, closing = false, supersonic = false; };

		// Stage of `p` against its nearest alive opponent. A demoed player has no stage.
		inline Stage Evaluate(const Player& p, const GameState& s) {
			Stage st;
			if (p.isDemoed) return st;
			float best = 1e30f; const Player* opp = nullptr;
			for (const Player& o : s.players) {
				if (o.team == p.team || o.isDemoed) continue;
				const float d = (o.pos - p.pos).Length();
				if (d < best) { best = d; opp = &o; }
			}
			if (!opp) return st;
			st.near = best <= NEAR_DIST;
			if (st.near && best > 1.0f) {
				const Vec dir = (opp->pos - p.pos) / best;
				st.closing = (p.vel - opp->vel).Dot(dir) > CLOSING_SPEED;
			}
			st.supersonic = st.closing && p.isSupersonic;
			return st;
		}

		struct ArenaMem {
			std::array<bool, MAX_PLAYERS> wasStage3{};
			std::array<int, MAX_PLAYERS> open{}; // steps left in the open attempt, 0 = none
			std::array<bool, MAX_PLAYERS> converted{};
		};

		struct Out {
			int attempts = 0;
			std::vector<char> closed;      // per closed attempt: 1 converted / 0 expired
			std::vector<char> unattempted; // per demo bumper this step: 1 if it had NO open attempt
		};

		// Advance one arena by one step. First step of an episode (no prev) forgets everything (attempts spanning a reset are dropped, not scored).
		inline Out Advance(ArenaMem& m, const GameState& s) {
			Out out;
			if (!s.prev) m = ArenaMem();
			for (size_t i = 0; i < s.players.size() && i < (size_t)MAX_PLAYERS; i++) {
				const Player& p = s.players[i];
				const bool wasOpen = m.open[i] > 0;
				if (p.eventState.demo) {
					out.unattempted.push_back(!wasOpen);
					if (wasOpen) m.converted[i] = true;
				}
				if (wasOpen) {
					if (m.converted[i]) { out.closed.push_back(1); m.open[i] = 0; m.converted[i] = false; }
					else if (--m.open[i] == 0) out.closed.push_back(0);
				}
				const bool st3 = Evaluate(p, s).supersonic;
				if (st3 && !m.wasStage3[i] && m.open[i] == 0) { m.open[i] = ATTEMPT_STEPS; m.converted[i] = false; out.attempts++; }
				m.wasStage3[i] = st3;
			}
			return out;
		}
	}
}
