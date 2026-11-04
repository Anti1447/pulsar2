#pragma once
// One-game-per-arena match loop, stepped exactly like the trainer steps its fleet (Reset() BEFORE the step, the
// controllers answering between the two halves). Shared by the sim round robin of the external bots
// (tools/opponent_roundrobin, checklist 1.5) and by the in-trainer gauntlet of OUR policy (GauntletEval.cpp), so both
// measure with the same rules.
//
// A game is one episode: it ends on a goal, on whatever the arena's terminal conditions say (NoTouch), or after
// maxSeconds; the last two count as draws. Torch-free on purpose: a controller is any type with
//   void BeginServe(int numPlayers);
//   void Act(const std::vector<GameState>&, const std::vector<bool>& isOld,
//            const std::vector<uint8_t>& prevArenaTerminals, std::vector<int>& outActionIdx);
// (GGL::ExternalOpponent already fits).
#include <RLGymCPP/EnvSet/EnvSet.h>

#include <algorithm>
#include <random>
#include <vector>

namespace GGL {

	struct MatchConfig {
		int playersPerTeam = 1;    // Players per team in every arena (speed/boost are reported per car)
		float maxSeconds = 90.f;   // A game still running after this long is a draw
		// false: A is BLUE in every arena. true: A is blue in even arenas and orange in odd ones (a 50/50 split of the
		// sides for any N >= 2); results are always from A's side.
		bool alternateSides = false;
	};

	struct MatchResult {
		int games = 0, winsA = 0, winsB = 0, draws = 0;
		double seconds = 0;
		double touchStepsA = 0, touchStepsB = 0, speedA = 0, speedB = 0, boostA = 0, boostB = 0;
		double stepsTotal = 0;
		// Who touched the ball first. Both in the same step = firstTouchBoth (a contested ball), counted for neither.
		int firstTouchA = 0, firstTouchB = 0, firstTouchBoth = 0;
	};

	// Seeds RocketSim's global random engine for the scope, then puts it back exactly as it was. A gauntlet inside a
	// training run must be reproducible (fixed seed) WITHOUT leaving the training's own random stream replaced by the
	// same sequence after every run. Only call it where no collection worker is running.
	class ScopedRandSeed {
	public:
		explicit ScopedRandSeed(unsigned seed) : saved(RocketSim::Math::GetRandEngine()) {
			RocketSim::Math::GetRandEngine().seed(seed);
		}
		~ScopedRandSeed() { RocketSim::Math::GetRandEngine() = saved; }
		ScopedRandSeed(const ScopedRandSeed&) = delete;
		ScopedRandSeed& operator=(const ScopedRandSeed&) = delete;
	private:
		std::default_random_engine saved;
	};

	// Plays N games (arenas 0..N-1 of `env`, one game each) between controller A and controller B. Every arena restarts
	// its episode first. `env` may have more than N arenas; the extras run but aren't counted.
	template<class CtrlA, class CtrlB>
	MatchResult PlayMatch(RLGC::EnvSet& env, CtrlA& A, CtrlB& B, int N, const MatchConfig& mc) {
		using namespace RLGC;
		const int tickSkip = env.config.tickSkip;

		std::fill(env.state.terminals.begin(), env.state.terminals.end(), 1);
		env.Reset();

		const int numPlayers = env.state.numPlayers;
		A.BeginServe(numPlayers);
		B.BeginServe(numPlayers);
		auto aTeamOf = [&](int arena) { return mc.alternateSides ? arena % 2 : 0; };
		std::vector<bool> isA(numPlayers, false), isB(numPlayers, false);
		{
			int i = 0;
			for (int a = 0; a < (int)env.state.gameStates.size(); a++)
				for (auto& p : env.state.gameStates[a].players) {
					(((int)p.team == aTeamOf(a)) ? isA : isB)[i] = true;
					i++;
				}
		}

		struct G { bool done = false; int steps = 0, firstTouch = -1; double touch[2] = {}, speed[2] = {}, boost[2] = {}; };
		std::vector<G> games(N);
		MatchResult res;
		res.games = N;
		int remaining = N;
		const int maxSteps = (int)(mc.maxSeconds * 120.f / tickSkip);

		for (int step = 0; step < maxSteps + 5 && remaining > 0; step++) {
			std::vector<uint8_t> prevTerminals = env.state.terminals; // BEFORE Reset(), which zeroes them
			env.Reset();
			env.StepFirstHalf(false);

			std::vector<int> ext(numPlayers, 0);
			A.Act(env.state.gameStates, isA, prevTerminals, ext);
			B.Act(env.state.gameStates, isB, prevTerminals, ext);
			IList actions(ext.begin(), ext.end());
			env.StepSecondHalf(actions, false);

			for (int a = 0; a < N; a++) {
				G& g = games[a];
				if (g.done) continue;
				auto& gs = env.state.gameStates[a];
				const int aTeam = aTeamOf(a);
				g.steps++;
				bool touched[2] = {}; // [0] = A, [1] = B
				for (auto& p : gs.players) {
					int side = ((int)p.team == aTeam) ? 0 : 1;
					g.speed[side] += p.vel.Length() / mc.playersPerTeam;
					g.boost[side] += p.boost / mc.playersPerTeam;
					if (p.ballTouchedStep) {
						g.touch[side]++;
						touched[side] = true;
					}
				}
				if (g.firstTouch == -1 && (touched[0] || touched[1]))
					g.firstTouch = (touched[0] && touched[1]) ? -2 : (touched[0] ? 0 : 1); // -2 = both in the same step
				bool terminal = env.state.terminals[a] != 0;
				if (terminal || g.steps >= maxSteps) {
					g.done = true;
					remaining--;
					if (gs.goalScored) {
						Team conceded = RS_TEAM_FROM_Y(gs.ball.pos.y);
						if ((int)conceded != aTeam) res.winsA++; else res.winsB++;
					} else res.draws++;
				}
			}
		}
		for (auto& g : games) {
			if (!g.done) { res.draws++; }
			res.seconds += g.steps * tickSkip / 120.0;
			res.stepsTotal += g.steps;
			res.touchStepsA += g.touch[0]; res.touchStepsB += g.touch[1];
			res.speedA += g.speed[0]; res.speedB += g.speed[1];
			res.boostA += g.boost[0]; res.boostB += g.boost[1];
			if (g.firstTouch == 0) res.firstTouchA++;
			if (g.firstTouch == 1) res.firstTouchB++;
			if (g.firstTouch == -2) res.firstTouchBoth++;
		}
		return res;
	}
}
