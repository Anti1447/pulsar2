#pragma once
#include "Reward.h"
#include "RewardMath.h"
#include "KickoffUtil.h"
#include <atomic>
#include <algorithm>
#include <cmath>
#include <vector>

// Ported from RLGymBot_v3 (KickoffOutcomeReward.h). Logic unchanged unless noted.

namespace RLGC {

// ============================================================================
// KickoffOutcomeReward  (Rev 1.1, 2026-10-18)
// One-shot, team-level score for HOW A KICKOFF TURNED OUT, paid a fixed horizon after first touch.
// Existing kickoff rewards shape the approach (speed, flip, boost, roles); none of them measure the
// result. This pays the result, identically for 1v1 / 2v2 / 3v3.
//
// Rev 1.1 (aggression): Rev 1.0 paid nothing when nobody touched, so waiting for the opponent to commit
//   was free (and Silver has no KickoffStandoffPenalty -- that is Phase 5 only). Now:
//   (a) STANDOFF: no touch by standoffSec (default 3.0s) -> everyone eats -standoffPenalty once
//       (takers 100%, support share), and the kickoff is closed (no later outcome payout).
//   (b) FIRST TOUCH: a +/- term for which team touched first (0 if both teams touch the same step).
//   Waiting is now strictly worse than contesting: a guaranteed -0.6 vs an outcome centered near 0.
//
//   Armed:     Reset(initialState) with RewardMath::IsKickoffActive(ball). Random-state episodes never arm.
//   Clock:     score window starts at the first step ANY player touches the ball.
//   Resolves:  once, at first-touch + horizonSec (default 2.0s), OR on goalScored, OR on the terminal step.
//   Score, per team T (dir = +1 blue / -1 orange; every term is positive when the ball is on T's side of
//   the fight), clipped to [-1, 1]:
//     field     = clip(ball.y*dir / 2500)            weight 0.40   ball displacement toward opp goal
//     possess   = +1 if last touch is T else -1      weight 0.20
//     momentum  = clip(ball.vel.y*dir / 2300)        weight 0.15   ball is leaving/entering our half
//     firstTouch= +1 T touched first / -1 opp / 0     weight 0.15   arriving first (0 on a same-step double)
//     coverage  = +1 / -1: some teammate is goalside weight 0.10   team size >= 2 only (0 in 1v1)
//                 of the ball / none is
//     goal in window overrides everything: +1 for the scoring team, -1 for the conceded team.
//   field/possess/momentum/firstTouch/goal are exactly antisymmetric between teams (blue = -orange);
//   coverage is not.
//   Credit: every teammate receives the team score; kickoff TAKERS (closest to ball at spawn; ties both
//           count) get 100%, non-takers supportShare (default 0.5) so the support role isn't a free ride
//           but isn't punished for the taker's outcome either.
//   Anti-farming: one payout per team per episode, requires an actual touch, window anchored on the
//           first touch, nothing to loop. Double-commit / passive support are NOT handled here
//           (KickoffDoubleCommitPenalty / KickoffNoChaseTeamPenalty).
//   Credit horizon: 2.0s keeps PPO credit assignment tractable (<= 2s guidance, CLAUDE.md 7A).
//
// Metrics (static atomics -> Kickoff/Outcome/*): Armed, Resolved, GoalInWindow, MeanAbsScore,
//   MeanFirstTouchSec, plus per-mode (1v1/2v2/3v3) resolved count and mean |score|.
// ============================================================================
	class KickoffOutcomeReward : public Reward {
	public:
		struct Config {
			float horizonSec     = 2.0f;
			float fieldScaleUU   = 2500.0f;
			float momentumScale  = 2300.0f;
			float wField         = 0.40f;
			float wPossess       = 0.20f;
			float wMomentum      = 0.15f;
			float wFirstTouch    = 0.15f;
			float wCoverage      = 0.10f;
			float supportShare   = 0.5f;
			float standoffSec    = 3.0f;   // no touch by this long after kickoff -> standoff penalty
			float standoffPenalty = 0.6f;  // paid once as -standoffPenalty (before taker/support share)
		};

		struct Metrics {
			long long armed = 0, resolved = 0, goals = 0, standoffs = 0;
			double meanAbsScore = 0, meanFirstTouchSec = 0;
			long long modeResolved[3] = { 0, 0, 0 };
			double modeMeanAbs[3] = { 0, 0, 0 };
		};

		static Metrics GetAndResetMetrics() {
			Metrics m;
			m.armed = s_armed.exchange(0);
			m.resolved = s_resolved.exchange(0);
			m.goals = s_goals.exchange(0);
			m.standoffs = s_standoffs.exchange(0);
			double absSum = s_absScoreMilli.exchange(0) / 1000.0;
			double ftSum = s_firstTouchMilli.exchange(0) / 1000.0;
			if (m.resolved > 0) {
				m.meanAbsScore = absSum / double(m.resolved);
				m.meanFirstTouchSec = ftSum / double(m.resolved);
			}
			for (int i = 0; i < 3; i++) {
				m.modeResolved[i] = s_modeResolved[i].exchange(0);
				double a = s_modeAbsMilli[i].exchange(0) / 1000.0;
				if (m.modeResolved[i] > 0) m.modeMeanAbs[i] = a / double(m.modeResolved[i]);
			}
			return m;
		}

		// gcc rejects `const Config& = Config()` as a default argument here (Config has default member
		// initializers and is nested in the class still being defined; MSVC accepts it), so two constructors.
		KickoffOutcomeReward() : cfg() {}
		explicit KickoffOutcomeReward(const Config& cfg) : cfg(cfg) {}

		virtual void Reset(const GameState& initialState) override {
			armed = RewardMath::IsKickoffActive(initialState.ball.pos, initialState.ball.vel);
			touched = false; resolved = false; justResolved = false;
			firstBlue = firstOrange = false;
			clock = 0.0f; touchClock = 0.0f;
			scoreBlue = scoreOrange = 0.0f;
			takers.clear();
			if (!armed) return;
			s_armed.fetch_add(1);
			// Spawn positions are valid here: Reset() sees the kickoff state before any movement.
			for (const Player& p : initialState.players)
				if (KickoffUtil::IsKickoffTaker(p, initialState)) takers.push_back(p.carId);
		}

		// Runs once per step, before every GetReward() -- the clock lives here (7C: never in GetReward).
		virtual void PreStep(const GameState& state) override {
			justResolved = false;
			if (!armed || resolved) return;
			clock += (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);

			if (!touched) {
				for (const Player& p : state.players) {
					if (!p.ballTouchedStep) continue;
					touched = true; touchClock = clock;
					(p.team == Team::BLUE ? firstBlue : firstOrange) = true;   // both set on a same-step double
				}
			}
			if (!touched) {
				if (clock >= cfg.standoffSec) ResolveStandoff(state);
				return;
			}
			if (state.goalScored || clock - touchClock >= cfg.horizonSec)
				Resolve(state);
		}

		virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
			if (!armed) return 0.0f;
			if (!resolved && touched && isFinal) Resolve(state);   // episode ended inside the window
			if (!justResolved) return 0.0f;

			float s = (player.team == Team::BLUE) ? scoreBlue : scoreOrange;
			bool isTaker = std::find(takers.begin(), takers.end(), player.carId) != takers.end();
			return isTaker ? s : s * cfg.supportShare;
		}

	private:
		inline static std::atomic<long long> s_armed{ 0 }, s_resolved{ 0 }, s_goals{ 0 }, s_standoffs{ 0 };
		inline static std::atomic<long long> s_absScoreMilli{ 0 }, s_firstTouchMilli{ 0 };
		inline static std::atomic<long long> s_modeResolved[3] = { {0}, {0}, {0} };
		inline static std::atomic<long long> s_modeAbsMilli[3] = { {0}, {0}, {0} };

		Config cfg;
		bool armed = false, touched = false, resolved = false, justResolved = false;
		bool firstBlue = false, firstOrange = false;   // which team(s) made the first-touch step
		float clock = 0.0f, touchClock = 0.0f;
		float scoreBlue = 0.0f, scoreOrange = 0.0f;
		std::vector<uint32_t> takers;

		float TeamScore(Team team, const GameState& state) const {
			const float dir = (team == Team::BLUE) ? 1.0f : -1.0f;

			if (state.goalScored) {
				// ball.y > 0 is inside the orange goal -> blue scored
				bool blueScored = state.ball.pos.y > 0.0f;
				return ((team == Team::BLUE) == blueScored) ? 1.0f : -1.0f;
			}

			auto clip = [](float v) { return std::max(-1.0f, std::min(1.0f, v)); };
			float field = clip(state.ball.pos.y * dir / cfg.fieldScaleUU);
			float momentum = clip(state.ball.vel.y * dir / cfg.momentumScale);

			float possess = 0.0f;
			for (const Player& p : state.players) {
				if ((int)p.carId == state.lastTouchCarID) { possess = (p.team == team) ? 1.0f : -1.0f; break; }
			}

			bool mineFirst = (team == Team::BLUE) ? firstBlue : firstOrange;
			bool theirsFirst = (team == Team::BLUE) ? firstOrange : firstBlue;
			float firstTouch = mineFirst == theirsFirst ? 0.0f : (mineFirst ? 1.0f : -1.0f);

			int teamSize = 0; bool anyGoalside = false;
			for (const Player& p : state.players) {
				if (p.team != team) continue;
				teamSize++;
				if (p.pos.y * dir < state.ball.pos.y * dir) anyGoalside = true;   // between ball and own goal (Y-only)
			}
			float coverage = (teamSize >= 2) ? (anyGoalside ? 1.0f : -1.0f) : 0.0f;

			return clip(cfg.wField * field + cfg.wPossess * possess + cfg.wMomentum * momentum
			          + cfg.wFirstTouch * firstTouch + cfg.wCoverage * coverage);
		}

		// Nobody touched by standoffSec: both teams lose the same fixed amount, once, and the kickoff is closed.
		void ResolveStandoff(const GameState&) {
			resolved = true; justResolved = true;
			scoreBlue = scoreOrange = -cfg.standoffPenalty;
			s_standoffs.fetch_add(1);
		}

		void Resolve(const GameState& state) {
			resolved = true; justResolved = true;
			scoreBlue = TeamScore(Team::BLUE, state);
			scoreOrange = TeamScore(Team::ORANGE, state);

			int perTeam = 0;
			for (const Player& p : state.players) if (p.team == Team::BLUE) perTeam++;
			int mode = std::max(0, std::min(2, perTeam - 1));

			long long absMilli = (long long)(std::fabs(scoreBlue) * 1000.0f);
			s_resolved.fetch_add(1);
			s_absScoreMilli.fetch_add(absMilli);
			s_firstTouchMilli.fetch_add((long long)(touchClock * 1000.0f));
			s_modeResolved[mode].fetch_add(1);
			s_modeAbsMilli[mode].fetch_add(absMilli);
			if (state.goalScored) s_goals.fetch_add(1);
		}
	};

} // namespace RLGC
