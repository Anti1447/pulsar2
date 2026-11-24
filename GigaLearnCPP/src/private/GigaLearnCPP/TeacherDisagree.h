#pragma once
#include "GGLBotOpponent.h"

#include <array>
#include <cmath>
#include <memory>

namespace GGL {

	// E9 (docs/GCO_IMITATION_CHRONOS.md section 11): how often do two teachers choose differently on the SAME state?
	// `primary` plays (its action is what the arena executes); `observer` is queried on exactly the same states and its action probabilities are compared
	// with the primary's. Both bots are the real ports with their own obs builders, so the comparison is between the bots as shipped. Nothing here trains or
	// changes play: the observer's output is discarded after it is scored.
	struct DisagreeStats {
		static constexpr int NSIT = 4; // kickoff, airborne, on the ground near the ball (< 1000 uu), on the ground far from it
		static constexpr const char* SIT[NSIT] = { "kickoff", "air", "ground_near", "ground_far" };
		struct Cell { double n = 0, agree = 0, js = 0, klPO = 0, klOP = 0, pOnObsArgmax = 0, entP = 0, entO = 0, tv = 0, sampleAgree = 0, selfP = 0, selfO = 0, confP = 0, confO = 0; double jsHist[6] = {}; };
		std::array<Cell, NSIT + 1> cells; // [NSIT] = all
	};

	class ObservedTeacher : public ExternalOpponent {
	public:
		std::shared_ptr<GGLBotOpponent> primary, observer;
		DisagreeStats stats;

		ObservedTeacher(std::shared_ptr<GGLBotOpponent> p, std::shared_ptr<GGLBotOpponent> o) : primary(p), observer(o) { name = p->name + "|" + o->name; }

		int MaxTeamSize() const override { return std::min(primary->MaxTeamSize(), observer->MaxTeamSize()); }
		void BeginServe(int numPlayers) override { primary->BeginServe(numPlayers); observer->BeginServe(numPlayers); }

		void Act(const std::vector<RLGC::GameState>& states, const std::vector<bool>& isOld, const std::vector<uint8_t>& prevArenaTerminals,
			std::vector<int>& outActionIdx) override {
			primary->recordProbs = observer->recordProbs = true;
			primary->temperature = observer->temperature = 0.f;
			primary->Act(states, isOld, prevArenaTerminals, outActionIdx);
			std::vector<int> scratch(outActionIdx.size(), 0);
			observer->Act(states, isOld, prevArenaTerminals, scratch);
			primary->recordProbs = observer->recordProbs = false;
			const auto& rows = primary->probRowPlayer;
			if (rows.empty() || rows != observer->probRowPlayer)
				return;
			const int A = (int)primary->actions.size();
			std::vector<int> arenaOf(isOld.size()), idxOf(isOld.size());
			{
				int g = 0;
				for (int a = 0; a < (int)states.size(); a++)
					for (int i = 0; i < (int)states[a].players.size(); i++, g++) { arenaOf[g] = a; idxOf[g] = i; }
			}
			for (int r = 0; r < (int)rows.size(); r++) {
				const float* P = primary->probRows.data() + (size_t)r * A;
				const float* O = observer->probRows.data() + (size_t)r * A;
				const auto& gs = states[arenaOf[rows[r]]];
				const auto& pl = gs.players[idxOf[rows[r]]];
				int sit;
				float bx = gs.ball.pos.x, by = gs.ball.pos.y;
				if (sqrtf(bx * bx + by * by) < 50.f && gs.ball.vel.Length() < 10.f) sit = 0;
				else if (!pl.isOnGround) sit = 1;
				else sit = ((pl.pos - gs.ball.pos).Length() < 1000.f) ? 2 : 3;
				int aP = 0, aO = 0;
				double js = 0, klPO = 0, klOP = 0, eP = 0, eO = 0, tv = 0, sa = 0, sp = 0, so = 0;
				for (int k = 0; k < A; k++) {
					if (P[k] > P[aP]) aP = k;
					if (O[k] > O[aO]) aO = k;
					double p = P[k], o = O[k], m = 0.5 * (p + o);
					tv += 0.5 * fabs(p - o); sa += p * o; sp += p * p; so += o * o;
					if (p > 1e-9) { eP -= p * log(p); js += 0.5 * p * log(p / m); klPO += p * log(p / std::max(o, 1e-6)); }
					if (o > 1e-9) { eO -= o * log(o); js += 0.5 * o * log(o / m); klOP += o * log(o / std::max(p, 1e-6)); }
				}
				for (int c : { sit, DisagreeStats::NSIT }) {
					auto& cell = stats.cells[c];
					cell.n++; cell.agree += (aP == aO); cell.js += js; cell.klPO += klPO; cell.klOP += klOP; cell.pOnObsArgmax += P[aO];
					cell.entP += eP; cell.entO += eO; cell.tv += tv; cell.sampleAgree += sa; cell.selfP += sp; cell.selfO += so; cell.confP += P[aP]; cell.confO += O[aO];
					int bin = js < 0.01 ? 0 : js < 0.05 ? 1 : js < 0.15 ? 2 : js < 0.3 ? 3 : js < 0.5 ? 4 : 5;
					cell.jsHist[bin]++;
				}
			}
		}
	};
}
