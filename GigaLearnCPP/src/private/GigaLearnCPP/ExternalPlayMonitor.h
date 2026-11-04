#pragma once
#include <RLGymCPP/Gamestates/GameState.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

namespace GGL {

	// Per-external-bot play statistics gathered on serve iterations (checklist 1.5 follow-up, 2026-11-05).
	// Cumulative counters, persisted in RUNNING_STATS.json next to the goal counters, so the series survives
	// restarts. Everything is ratios of counters: the report divides, the counters only add.
	//
	// "Served step" = one arena stepped while the bot played in it. "Bot side" = the bot's team in that arena.
	struct ExternalPlayStats {
		enum Counter {
			// per mode (1v1, 2v2, 3v3): served steps and goals, by who scored
			STEPS_1V1, STEPS_2V2, STEPS_3V3,
			GF_1V1, GF_2V2, GF_3V3,      // goals WE scored on the bot (GF) / the bot scored on us (GA), as Goals For/Against
			GA_1V1, GA_2V2, GA_3V3,
			// per bot side (BLUE, ORANGE)
			STEPS_BOT_BLUE, STEPS_BOT_ORANGE,
			GF_BOT_BLUE, GF_BOT_ORANGE,
			GA_BOT_BLUE, GA_BOT_ORANGE,
			// kickoffs (episodes that start with the ball at rest in the centre)
			KICKOFFS, KICKOFF_BOT_FIRST, KICKOFF_US_FIRST, KICKOFF_NO_TOUCH, KICKOFF_TOUCH_STEPS,
			KICKOFF_GF_5S, KICKOFF_GA_5S,
			// play
			TOUCH_STEPS_BOT, TOUCH_STEPS_US,       // arena-steps in which a car of that side touched the ball
			BALL_STEPS_BOT_HALF,                   // arena-steps with the ball in the bot's defensive half
			SHOTS_BOT, SHOTS_US, DEMOS_BOT, DEMOS_US,
			NUM_COUNTERS
		};
		// Goals are counted FROM OUR SIDE, as the existing Goals For/Against: GF = we scored on the bot
		std::array<std::atomic<int64_t>, NUM_COUNTERS> c = {};

		static const char* Name(int i) {
			static const char* names[NUM_COUNTERS] = {
				"steps_1v1", "steps_2v2", "steps_3v3", "gf_1v1", "gf_2v2", "gf_3v3", "ga_1v1", "ga_2v2", "ga_3v3",
				"steps_bot_blue", "steps_bot_orange", "gf_bot_blue", "gf_bot_orange", "ga_bot_blue", "ga_bot_orange",
				"kickoffs", "kickoff_bot_first", "kickoff_us_first", "kickoff_no_touch", "kickoff_touch_steps",
				"kickoff_gf_5s", "kickoff_ga_5s",
				"touch_steps_bot", "touch_steps_us", "ball_steps_bot_half", "shots_bot", "shots_us", "demos_bot", "demos_us",
			};
			return names[i];
		}

		int64_t Get(Counter k) const { return c[k]; }

		// name -> value for the metric report (the caller prefixes the bot's name). A ratio with an empty
		// denominator is left out rather than reported as 0.
		std::vector<std::pair<std::string, float>> Report(int tickSkip) const {
			std::vector<std::pair<std::string, float>> out;
			auto ratio = [&](const std::string& n, double num, double den, double scale) {
				if (den > 0)
					out.push_back({ n, (float)(num / den * scale) });
			};
			const char* modes[3] = { "1v1", "2v2", "3v3" };
			for (int m = 0; m < 3; m++)
				ratio(std::string("GD per 1k Steps (") + modes[m] + ")",
					(double)(c[GF_1V1 + m] - c[GA_1V1 + m]), (double)c[STEPS_1V1 + m], 1000.0);
			ratio("GD per 1k Steps (Bot Blue)", (double)(c[GF_BOT_BLUE] - c[GA_BOT_BLUE]), (double)c[STEPS_BOT_BLUE], 1000.0);
			ratio("GD per 1k Steps (Bot Orange)", (double)(c[GF_BOT_ORANGE] - c[GA_BOT_ORANGE]), (double)c[STEPS_BOT_ORANGE], 1000.0);

			double k = (double)c[KICKOFFS];
			if (k > 0)
				out.push_back({ "Kickoff Count", (float)k });
			ratio("Kickoff Bot First Touch %", (double)c[KICKOFF_BOT_FIRST], k, 100.0);
			ratio("Kickoff No Touch %", (double)c[KICKOFF_NO_TOUCH], k, 100.0);
			ratio("Kickoff Mean Touch Time s", (double)c[KICKOFF_TOUCH_STEPS] * tickSkip / 120.0,
				(double)(c[KICKOFF_BOT_FIRST] + c[KICKOFF_US_FIRST]), 1.0);
			ratio("Kickoff Goals For 5s %", (double)c[KICKOFF_GF_5S], k, 100.0);
			ratio("Kickoff Goals Against 5s %", (double)c[KICKOFF_GA_5S], k, 100.0);

			double steps = (double)(c[STEPS_1V1] + c[STEPS_2V2] + c[STEPS_3V3]);
			ratio("Touch Share Bot %", (double)c[TOUCH_STEPS_BOT], (double)(c[TOUCH_STEPS_BOT] + c[TOUCH_STEPS_US]), 100.0);
			ratio("Ball In Bot Half %", (double)c[BALL_STEPS_BOT_HALF], steps, 100.0);
			ratio("Shots Bot per 1k Steps", (double)c[SHOTS_BOT], steps, 1000.0);
			ratio("Shots Us per 1k Steps", (double)c[SHOTS_US], steps, 1000.0);
			ratio("Demos Bot per 1k Steps", (double)c[DEMOS_BOT], steps, 1000.0);
			ratio("Demos Us per 1k Steps", (double)c[DEMOS_US], steps, 1000.0);
			return out;
		}
	};

	// Watches the arenas of one serve iteration. Feed it once per step AFTER the step, with the states the step
	// produced and the terminal flags the PREVIOUS step left (those mark an arena whose episode began with this
	// step). Episodes already running when the serve began are counted for everything except kickoffs: a kickoff
	// is only identified at an episode start.
	class ExternalPlayMonitor {
	public:
		struct Arena {
			bool tracking = false;     // Episode start seen: kickoff bookkeeping is valid
			bool kickoff = false;
			bool touched = false;
			int steps = 0;
		};
		std::vector<Arena> arenas;

		void BeginServe(int numArenas) { arenas.assign(numArenas, Arena{}); }

		// states: this step's post-step states. startedFresh[a]: arena a's episode began with this step.
		// terminalNow[a]: this step ended arena a's episode. botTeam: the team the bot plays.
		void Step(const std::vector<RLGC::GameState>& states, const std::vector<uint8_t>& startedFresh,
			const std::vector<uint8_t>& terminalNow, RocketSim::Team botTeam, int tickSkip, ExternalPlayStats& st) {

			using S = ExternalPlayStats;
			if (arenas.size() != states.size())
				BeginServe((int)states.size());

			for (size_t a = 0; a < states.size(); a++) {
				const RLGC::GameState& gs = states[a];
				Arena& ar = arenas[a];
				int mode = (int)gs.players.size() / 2 - 1;
				if (mode < 0 || mode > 2)
					continue;

				if (a < startedFresh.size() && startedFresh[a]) {
					ar = Arena{};
					ar.tracking = true;
					// The ball is still on the centre spot after the episode's first step if this is a kickoff
					ar.kickoff = fabsf(gs.ball.pos.x) < 5.f && fabsf(gs.ball.pos.y) < 5.f && gs.ball.vel.Length() < 5.f;
					if (ar.kickoff)
						st.c[S::KICKOFFS]++;
				}
				ar.steps++;

				int botSide = (int)botTeam;
				st.c[S::STEPS_1V1 + mode]++;
				st.c[botSide == 0 ? S::STEPS_BOT_BLUE : S::STEPS_BOT_ORANGE]++;
				if (RS_TEAM_FROM_Y(gs.ball.pos.y) == botTeam)
					st.c[S::BALL_STEPS_BOT_HALF]++;

				bool botTouch = false, usTouch = false;
				for (auto& p : gs.players) {
					bool bot = (p.team == botTeam);
					if (p.ballTouchedStep)
						(bot ? botTouch : usTouch) = true;
					if (p.eventState.shot)
						st.c[bot ? S::SHOTS_BOT : S::SHOTS_US]++;
					if (p.eventState.demo)
						st.c[bot ? S::DEMOS_BOT : S::DEMOS_US]++;
				}
				if (botTouch) st.c[S::TOUCH_STEPS_BOT]++;
				if (usTouch) st.c[S::TOUCH_STEPS_US]++;

				// First touch of a kickoff. Both sides in the same step: neither is first (a contested ball)
				if (ar.tracking && ar.kickoff && !ar.touched && (botTouch || usTouch)) {
					ar.touched = true;
					st.c[S::KICKOFF_TOUCH_STEPS] += ar.steps;
					if (botTouch && !usTouch) st.c[S::KICKOFF_BOT_FIRST]++;
					else if (usTouch && !botTouch) st.c[S::KICKOFF_US_FIRST]++;
					else { st.c[S::KICKOFF_BOT_FIRST]++; st.c[S::KICKOFF_US_FIRST]++; }
				}

				if (gs.goalScored) {
					bool weScored = (RS_TEAM_FROM_Y(gs.ball.pos.y) == botTeam); // the bot conceded
					st.c[(weScored ? S::GF_1V1 : S::GA_1V1) + mode]++;
					st.c[weScored ? (botSide == 0 ? S::GF_BOT_BLUE : S::GF_BOT_ORANGE)
						: (botSide == 0 ? S::GA_BOT_BLUE : S::GA_BOT_ORANGE)]++;
					if (ar.tracking && ar.kickoff && ar.steps * tickSkip / 120.f <= 5.f)
						st.c[weScored ? S::KICKOFF_GF_5S : S::KICKOFF_GA_5S]++;
				}

				if (a < terminalNow.size() && terminalNow[a]) {
					if (ar.tracking && ar.kickoff && !ar.touched)
						st.c[S::KICKOFF_NO_TOUCH]++;
					ar = Arena{};
				}
			}
		}
	};
}
