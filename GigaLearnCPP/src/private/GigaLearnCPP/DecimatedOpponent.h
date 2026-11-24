#pragma once
#include "ExternalOpponent.h"

#include <memory>
#include <vector>

namespace GGL {

	// Runs an external bot at ITS trained decision rate inside a finer-grained env (2026-11-22, the tickSkip-2 run).
	//
	// The bots here (Nexto, ChronosV2, GalactusV2, Dice) were all trained at tickSkip 8 (one decision per 8 ticks) with a 7-tick
	// action latency, and their obs builders and Nexto's scripted kickoff count steps in those units. When the learner steps at
	// tickSkip 2 / actionDelay 1, calling them every env step would make them act at 60 Hz with a 1-tick latency, which is a
	// different (and untrained-for) bot. This wrapper calls the inner bot once per botTickSkip ticks (every
	// P = botTickSkip / tickSkip env steps, per arena), holds its action in between, and releases a decision
	// emitStep = (botActionDelay - actionDelay) / tickSkip env steps after it was made, so the new action first takes effect on
	// the same tick (7 of 8) as in training. The inner bot's own semantics (prevAction, the kickoff script, fresh-episode handling)
	// are untouched: it sees exactly one Act per 8 ticks.
	//
	// Fresh episodes (prevArenaTerminals) restart the phase and release the first decision immediately (the original's first
	// window would run a neutral action; the difference is under half a second and only at a kickoff).
	class DecimatedOpponent : public ExternalOpponent {
	public:
		std::shared_ptr<ExternalOpponent> inner;
		int period, emitStep;

		DecimatedOpponent(std::shared_ptr<ExternalOpponent> inner, int tickSkip, int actionDelay, int botTickSkip, int botActionDelay)
			: inner(std::move(inner)) {
			RG_ASSERT(botTickSkip % tickSkip == 0);
			RG_ASSERT(botActionDelay >= actionDelay && (botActionDelay - actionDelay) % tickSkip == 0);
			period = botTickSkip / tickSkip;
			emitStep = (botActionDelay - actionDelay) / tickSkip;
			RG_ASSERT(emitStep < period);
			name = this->inner->name;
			temperature = this->inner->temperature;
		}

		int MaxTeamSize() const override { return inner->MaxTeamSize(); }

		void BeginServe(int numPlayers) override {
			inner->BeginServe(numPlayers);
			phase.clear();
			active.assign(numPlayers, 0); pending.assign(numPlayers, 0);
		}

		void Act(const std::vector<RLGC::GameState>& states, const std::vector<bool>& isOld,
			const std::vector<uint8_t>& prevArenaTerminals, std::vector<int>& outActionIdx) override {
			inner->temperature = temperature;
			const int numArenas = (int)states.size();
			const int numPlayers = (int)isOld.size();
			if ((int)phase.size() != numArenas) phase.assign(numArenas, -1);
			if ((int)active.size() != numPlayers) { active.assign(numPlayers, 0); pending.assign(numPlayers, 0); }

			std::vector<uint8_t> fresh(numArenas, 0), terms(numArenas, 0);
			std::vector<bool> mask(numPlayers, false);
			bool anyDue = false;
			int g = 0;
			for (int a = 0; a < numArenas; a++) {
				bool f = phase[a] < 0 || (a < (int)prevArenaTerminals.size() && prevArenaTerminals[a] != 0);
				phase[a] = f ? 0 : (phase[a] + 1) % period;
				fresh[a] = f;
				const int n = (int)states[a].players.size();
				if (phase[a] == 0) {
					if (a < (int)prevArenaTerminals.size()) terms[a] = prevArenaTerminals[a];
					for (int o = 0; o < n; o++) if (isOld[g + o]) { mask[g + o] = true; anyDue = true; }
				}
				g += n;
			}
			if (anyDue) {
				std::vector<int> tmp(numPlayers, 0);
				inner->Act(states, mask, terms, tmp);
				for (int i = 0; i < numPlayers; i++) if (mask[i]) pending[i] = tmp[i];
			}
			g = 0;
			for (int a = 0; a < numArenas; a++) {
				const int n = (int)states[a].players.size();
				if (phase[a] == emitStep || fresh[a])
					for (int o = 0; o < n; o++) active[g + o] = pending[g + o];
				for (int o = 0; o < n; o++) if (isOld[g + o]) outActionIdx[g + o] = active[g + o];
				g += n;
			}
		}

	private:
		std::vector<int> phase;                  // per arena: env steps since this arena's last decision (0 = decide now), -1 = unknown
		std::vector<int> active, pending;        // per global player: the action being held / the last decision awaiting release
	};
}
