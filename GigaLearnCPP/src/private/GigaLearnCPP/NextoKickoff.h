#pragma once
#include <RLGymCPP/Gamestates/GameState.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace GGL {

	// Nexto's HARDCODED KICKOFF (rlbot-run/nexto/bot.py: KICKOFF_CONTROLS, maybe_do_kickoff), torch-free so the
	// logic is unit-tested without the model. Added 2026-11-05: in the 1.5 round robin the pure-argmax Nexto won
	// 0% of the first touches against Galactus and Chronos, i.e. it was a weaker opponent than the bot people
	// actually play (checklist 1.5 FLAG 1 / the "Nexto scripted/random behaviour" decision).
	//
	// WHAT THE ORIGINAL DOES (120 Hz ticks, counted from the first tick of the kickoff phase):
	//   44 ticks throttle+boost | 16 steer left+boost | 8 jump+boost | 4 boost | 4 flip (yaw .8, pitch -.7, jump, boost)
	//   | 52 pitch up+boost | 40 roll right, pitch .5, no boost.   168 ticks in all.
	// It is used only by the "kickoff taker": the car(s) whose distance to the ball is within 10 uu of the closest
	// car of the match, and, among teammates at that same distance, only the one whose turn it is (see IsTaker).
	// The script is applied while the ball is still on y == 0; everyone else, and the taker after the 168 ticks,
	// plays the net.
	//
	// WHAT THIS PORT DOES DIFFERENTLY (and why): our env holds ONE discrete action per step of tickSkip (8) ticks, so
	// the script is played one action per 8-tick step, 21 steps = 168 ticks, picking per step the control that holds
	// for most of its 8 ticks (the script is on a 4-tick grid, so some steps are a tie). Two deliberate choices:
	//   * a step with the jump button RELEASED sits between the jump and the flip: a flip needs a fresh press, and
	//     a held button for two steps would never flip (the original releases it for 4 ticks).
	//   * the sim applies a step's action from tick 8s+7, a 7-tick latency the live client has too (the 6.0 run uses
	//     actionDelay 7 to match it), so the script is not shifted to compensate.
	// The discrete table has no half sticks, and jump rows allow only roll (not yaw) for a sideways flip:
	//   flip  yaw .8 / pitch -.7  ->  roll 1 / pitch -1 (the sim's dodge direction is (-pitch, yaw + roll): the same
	//                                 forward-right diagonal, 45 deg instead of 49 deg)
	//   last segment  pitch .5 + roll 1, no boost  ->  roll 1 only (a tie between pitch 0 and 1 resolved to 0)
	// Everything else is exact. Not ported: the "stochastic_kickoffs" beta .5 sampling for the NON-takers (pure
	// argmax everywhere, as decided on 2026-11-05) and the celebration randomness.
	struct NextoKickoff {
		enum Kind { BOOST, STEER_LEFT, JUMP, FLIP, PITCH_UP, ROLL, NUM_KINDS };

		static constexpr int STEPS = 21; // 168 ticks / tickSkip 8
		// BOOST at index 8 is the release step (jump button up) before the flip at index 9
		static constexpr Kind STEP_KIND[STEPS] = {
			BOOST, BOOST, BOOST, BOOST, BOOST,
			STEER_LEFT, STEER_LEFT,
			JUMP, BOOST, FLIP,
			PITCH_UP, PITCH_UP, PITCH_UP, PITCH_UP, PITCH_UP, PITCH_UP,
			ROLL, ROLL, ROLL, ROLL, ROLL,
		};

		// The rows of our DefaultAction table as [throttle, steer, pitch, yaw, roll, jump, boost, handbrake]
		static const std::array<float, 8>& TableRow(Kind k) {
			static const std::array<float, 8> rows[NUM_KINDS] = {
				{ 1, 0, 0, 0, 0, 0, 1, 0 },  // BOOST
				{ 1, -1, 0, -1, 0, 0, 1, 0 }, // STEER_LEFT
				{ 1, 0, 0, 0, 0, 1, 1, 0 },  // JUMP
				{ 1, 0, -1, 0, 1, 1, 1, 1 }, // FLIP (roll 1 + pitch -1; handbrake 1 as every jump row with a stick input)
				{ 1, 0, 1, 0, 0, 0, 1, 0 },  // PITCH_UP
				{ 0, 0, 0, 0, 1, 0, 0, 0 },  // ROLL
			};
			return rows[k];
		}

		// What the ORIGINAL controller held (Nexto's own previous-action input when it next runs its net).
		// [throttle, steer, pitch, yaw, roll, jump, boost, handbrake]
		static const std::array<float, 8>& OriginalRow(Kind k) {
			static const std::array<float, 8> rows[NUM_KINDS] = {
				{ 1, 0, 0, 0, 0, 0, 1, 0 },
				{ 1, -1, 0, 0, 0, 0, 1, 0 },
				{ 1, 0, 0, 0, 0, 1, 1, 0 },
				{ 1, 0, -0.7f, 0.8f, 0, 1, 1, 0 },
				{ 1, 0, 1, 0, 0, 0, 1, 0 },
				{ 1, 0, 0.5f, 0, 1, 0, 0, 0 },
			};
			return rows[k];
		}

		// Per global player: -1 not decided, -2 not a taker, >= 0 the script step of the last Update
		std::vector<int> step;
		// Per arena: the kickoff decision is only allowed on the first Act of an episode
		std::vector<uint8_t> armed;

		void Begin(int numPlayers) {
			step.assign(numPlayers, -1);
			armed.clear(); // sized to the arena count on the first Update, every arena armed
		}

		// The ball sits at the centre, untouched
		static bool BallAtKickoff(const RLGC::GameState& gs) {
			return std::fabs(gs.ball.pos.x) < 1.f && std::fabs(gs.ball.pos.y) < 1.f && gs.ball.vel.Length() < 1.f;
		}

		// Nexto's maybe_do_kickoff decision, literally: within 10 uu (2D) of the closest car to the ball, and,
		// among teammates just as close, no teammate may fail the original's `is_left` test (blue: the teammate's x is
		// smaller than ours; orange: larger), i.e. `if not is_left: is_kickoff_taker = False  # Left goes`.
		static bool IsTaker(const RLGC::GameState& gs, int me) {
			const int n = (int)gs.players.size();
			std::vector<float> dist(n);
			float minD = 1e30f;
			for (int i = 0; i < n; i++) {
				float dx = gs.players[i].pos.x - gs.ball.pos.x, dy = gs.players[i].pos.y - gs.ball.pos.y;
				dist[i] = std::sqrt(dx * dx + dy * dy);
				minD = std::fmin(minD, dist[i]);
			}
			if (std::fabs(minD - dist[me]) > 10.f)
				return false;
			const bool blue = gs.players[me].team == Team::BLUE;
			for (int i = 0; i < n; i++) {
				if (i == me || gs.players[i].team != gs.players[me].team || std::fabs(dist[i] - dist[me]) > 10.f)
					continue;
				bool isLeft = blue ? (gs.players[i].pos.x < gs.players[me].pos.x) : (gs.players[i].pos.x > gs.players[me].pos.x);
				if (!isLeft)
					return false;
			}
			return true;
		}

		// One Act call: for every player marked in isOld, outKind[g] = the scripted Kind to play this step, or -1
		// (play the net). prevArenaTerminals as in ExternalOpponent::Act (nonzero = a fresh episode).
		void Update(const std::vector<RLGC::GameState>& states, const std::vector<bool>& isOld,
			const std::vector<uint8_t>& prevArenaTerminals, std::vector<int>& outKind) {

			outKind.assign(step.size(), -1);
			if (armed.size() != states.size())
				armed.assign(states.size(), 1);
			int g0 = 0;
			for (int a = 0; a < (int)states.size(); a++) {
				const RLGC::GameState& gs = states[a];
				const int n = (int)gs.players.size();
				if (a < (int)prevArenaTerminals.size() && prevArenaTerminals[a]) {
					for (int o = 0; o < n; o++)
						step[g0 + o] = -1;
					armed[a] = 1;
				}
				const bool decide = armed[a] && BallAtKickoff(gs);
				armed[a] = 0; // only the first Act of an episode (or of a serve) may start the script
				for (int o = 0; o < n; o++) {
					int g = g0 + o;
					if (!isOld[g])
						continue;
					bool justDecided = false;
					if (step[g] == -1 && decide) {
						step[g] = IsTaker(gs, o) ? 0 : -2;
						justDecided = true;
					}
					if (step[g] >= 0 && !justDecided)
						step[g]++;
					// the original applies it only while the ball is still on y == 0
					if (step[g] >= 0 && step[g] < STEPS && std::fabs(gs.ball.pos.y) < 0.5f)
						outKind[g] = STEP_KIND[step[g]];
				}
				g0 += n;
			}
		}
	};
}
