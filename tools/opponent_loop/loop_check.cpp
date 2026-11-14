// Checklist 1.3: the external opponents inside a training-style loop (docs/Pre_Run_Checklist.md).
//
// Builds a real mixed 1v1/2v2/3v3 EnvSet (production tickSkip 8 / actionDelay 7, production-style state setters and
// terminals) and runs the SAME serve mechanics Learner.cpp's collection loop uses: the partitioned opponent roll,
// BeginServe, a random oppTeam, the isOld mask over players, Act() with the PREVIOUS step's terminals, our side acting
// from its masks. It is a re-statement of the loop, not the Learner itself (the Learner needs a full trainer config);
// the lines it mirrors are Learner.cpp ~1755-1770 (roll), ~1832-1840 (oppTeam), ~2068 (Act).
// Usage (from the repo root):  ./build/OpponentLoop [seed] [arenas per mode]
#include <GigaLearnCPP/ExternalOpponent.h>
#include <GigaLearnCPP/NextoOpponent.h>
#include <GigaLearnCPP/GGLBotOpponent.h>
#include <GigaLearnCPP/ExternalPlayMonitor.h>

#include <RLGymCPP/EnvSet/EnvSet.h>
#include <RLGymCPP/Rewards/CommonRewards.h>
#include <RLGymCPP/TerminalConditions/NoTouchCondition.h>
#include <RLGymCPP/TerminalConditions/GoalScoreCondition.h>
#include <RLGymCPP/ObsBuilders/AdvancedObsPadded.h>
#include <RLGymCPP/StateSetters/KickoffState.h>
#include <RLGymCPP/StateSetters/RandomState.h>
#include <RLGymCPP/StateSetters/BallNearCarState.h>
#include <RLGymCPP/StateSetters/AirDrillState.h>
#include <RLGymCPP/StateSetters/CombinedState.h>
#include <RLGymCPP/ActionParsers/DefaultAction.h>

#include <torch/cuda.h>

#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <random>
#include <set>

using namespace RLGC;

namespace {
	int g_fail = 0;
	void Check(bool ok, const std::string& what) {
		std::cout << (ok ? "  PASS  " : "  FAIL  ") << what << std::endl;
		if (!ok)
			g_fail++;
	}

	double Now() {
		return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
	}

	constexpr int TICK_SKIP = 8, ACTION_DELAY = 7;
	int ARENAS_PER_MODE = 40; // 40 x (2 + 4 + 6) = 480 players; argv[2] overrides (341 = the PHASE B fleet of 1023 arenas)

	std::unique_ptr<EnvSet> MakeEnvSet() {
		EnvSetConfig cfg = {};
		cfg.numArenas = ARENAS_PER_MODE * 3;
		cfg.tickSkip = TICK_SKIP;
		cfg.actionDelay = ACTION_DELAY;
		cfg.saveRewards = false;
		cfg.envCreateFn = [](int index) {
			int perTeam = index < ARENAS_PER_MODE ? 1 : (index < 2 * ARENAS_PER_MODE ? 2 : 3);
			auto arena = Arena::Create(GameMode::SOCCAR);
			for (int i = 0; i < perTeam; i++) {
				arena->AddCar(Team::BLUE);
				arena->AddCar(Team::ORANGE);
			}
			EnvCreateResult r = {};
			r.arena = arena;
			r.actionParser = new DefaultAction();
			r.obsBuilder = new AdvancedObsPadded(3);
			r.stateSetter = new CombinedState({
				{ new BallNearCarState(600, 900), 0.35f },
				{ new AirDrillState(), 0.20f },
				{ new KickoffState(), 0.15f },
				{ new RandomState(true, true, false), 0.30f },
			});
			r.terminalConditions = { new NoTouchCondition(10), new GoalScoreCondition() };
			r.rewards = { { new GoalReward(), 1.f } };
			return r;
		};
		return std::make_unique<EnvSet>(cfg);
	}

	// One recorded serve: for every step the states Act saw, the terminals it was given, and what it answered
	struct Recorded {
		std::vector<std::vector<GameState>> states;
		std::vector<std::vector<uint8_t>> terminals;
		std::vector<std::vector<int>> acts;
		std::vector<bool> isOld;
		int numPlayers = 0;
	};

	enum Cat { KICKOFF, GROUND, AERIAL, NUM_CATS };
	const char* CAT_NAME[NUM_CATS] = { "kickoff", "midfield/ground", "aerial" };

	Cat Categorize(const GameState& gs, const Player& p) {
		bool kickoff = fabsf(gs.ball.pos.x) < 1 && fabsf(gs.ball.pos.y) < 1 && gs.ball.vel.Length() < 1;
		if (kickoff)
			return KICKOFF;
		if (!p.isOnGround && p.pos.z > 300)
			return AERIAL;
		return GROUND;
	}

	struct Stats {
		long rows = 0, idle = 0, coast = 0, boost = 0, jump = 0;
		std::map<int, long> hist;
	};

	// Drive one serve iteration with `bot` for `steps` steps; the bot's team is picked as the loop picks it
	Recorded RunServe(EnvSet& env, GGL::ExternalOpponent& bot, int steps, Stats stats[NUM_CATS],
		const std::vector<std::array<float, 8>>& ourTable, int& teamBlueCount, long& oldRowsWrong,
		GGL::ExternalPlayStats& play) {

		Recorded rec;
		int numPlayers = env.state.numPlayers;
		rec.numPlayers = numPlayers;
		bot.BeginServe(numPlayers);

		Team oppTeam = Team(RocketSim::Math::RandInt(0, 2));
		if (oppTeam == Team::BLUE)
			teamBlueCount++;
		std::vector<bool> isOld(numPlayers, false);
		// As Learner.cpp: an arena larger than the bot's MaxTeamSize() (Dice: 2) is not served - nobody in it is an "old" row
		std::vector<uint8_t> served;
		{
			int i = 0;
			for (auto& gs : env.state.gameStates) {
				int perTeam[2] = { 0, 0 };
				for (auto& p : gs.players)
					perTeam[(int)p.team]++;
				const bool ok = std::max(perTeam[0], perTeam[1]) <= bot.MaxTeamSize();
				served.push_back(ok);
				for (auto& p : gs.players)
					isOld[i++] = ok && (p.team == oppTeam);
			}
		}
		rec.isOld = isOld;
		GGL::ExternalPlayMonitor monitor;
		monitor.BeginServe((int)env.state.gameStates.size());
		std::vector<uint8_t> startedFresh;

		const int numActions = DefaultAction().GetActionAmount();
		for (int step = 0; step < steps; step++) {
			// As Learner.cpp: copy the previous step's terminal flags, THEN Reset() (which zeroes them), then step
			std::vector<uint8_t> prevTerminals = env.state.terminals;
			env.Reset();
			env.StepFirstHalf(false);

			std::vector<int> ext(numPlayers, -1);
			bot.Act(env.state.gameStates, isOld, prevTerminals, ext);
			rec.states.push_back(env.state.gameStates);
			rec.terminals.push_back(prevTerminals);
			rec.acts.push_back(ext);
			startedFresh = prevTerminals;

			IList actions(numPlayers, 0);
			for (int p = 0; p < numPlayers; p++) {
				if (isOld[p]) {
					if (ext[p] < 0 || ext[p] >= numActions)
						oldRowsWrong++;
					actions[p] = ext[p];
				} else {
					if (ext[p] != -1)
						oldRowsWrong++; // Act touched a row it was not asked for
					if (!env.state.actionMasks.Defined())
						continue;
					std::vector<int> valid;
					for (int a = 0; a < numActions; a++)
						if (env.state.actionMasks.At(p, a))
							valid.push_back(a);
					if (!valid.empty())
						actions[p] = valid[RocketSim::Math::RandInt(0, (int)valid.size())];
				}
			}

			// Statistics on what the opponent's rows did
			int g = 0;
			for (auto& gs : env.state.gameStates)
				for (auto& p : gs.players) {
					if (isOld[g] && ext[g] >= 0) {
						auto& a = ourTable[ext[g]];
						Stats& s = stats[Categorize(gs, p)];
						s.rows++;
						s.hist[ext[g]]++;
						bool j = a[5] > 0.5f, b = a[6] > 0.5f, h = a[7] > 0.5f;
						if (b) s.boost++;
						if (j) s.jump++;
						if (!j && !b && !h && a[0] == 0.f) {
							s.coast++;
							if (a[1] == 0.f && a[2] == 0.f && a[3] == 0.f && a[4] == 0.f) s.idle++;
						}
					}
					g++;
				}

			env.StepSecondHalf(actions, false);
			monitor.Step(env.state.gameStates, startedFresh, env.state.terminals, oppTeam, TICK_SKIP, play, &served);
		}
		return rec;
	}

	template<class MakeFn>
	void Replay(MakeFn make, const Recorded& rec, std::vector<std::vector<int>>& out) {
		auto bot = make();
		bot->BeginServe(rec.numPlayers);
		for (size_t s = 0; s < rec.states.size(); s++) {
			std::vector<int> ext(rec.numPlayers, -1);
			bot->Act(rec.states[s], rec.isOld, rec.terminals[s], ext);
			out.push_back(ext);
		}
	}

	double DiffRate(const std::vector<std::vector<int>>& a, const std::vector<std::vector<int>>& b, long& rows) {
		long diff = 0;
		rows = 0;
		for (size_t s = 0; s < a.size(); s++)
			for (size_t p = 0; p < a[s].size(); p++)
				if (a[s][p] >= 0) {
					rows++;
					if (a[s][p] != b[s][p])
						diff++;
				}
		return rows ? (double)diff / rows : 0.0;
	}

	double Tv(const Stats& a, const Stats& b) { // total-variation distance of two action histograms
		std::set<int> keys;
		for (auto& kv : a.hist) keys.insert(kv.first);
		for (auto& kv : b.hist) keys.insert(kv.first);
		double d = 0;
		for (int k : keys) {
			double pa = a.rows ? (double)(a.hist.count(k) ? a.hist.at(k) : 0) / a.rows : 0;
			double pb = b.rows ? (double)(b.hist.count(k) ? b.hist.at(k) : 0) / b.rows : 0;
			d += fabs(pa - pb);
		}
		return d / 2;
	}
}

int main(int argc, char** argv) {
	unsigned seed = argc > 1 ? (unsigned)std::stoul(argv[1]) : 20261105;
	RocketSim::Math::GetRandEngine().seed(seed);
	if (argc > 2)
		ARENAS_PER_MODE = std::stoi(argv[2]);
	for (const char* path : { "collision_meshes", "../collision_meshes", "build/collision_meshes" })
		if (std::filesystem::exists(std::filesystem::path(path) / "soccar")) {
			RocketSim::Init(path);
			break;
		}

	torch::Device dev = torch::cuda::is_available() ? torch::Device(torch::kCUDA, 0) : torch::Device(torch::kCPU);
	std::cout << "device: " << (dev.is_cuda() ? "CUDA" : "CPU") << ", " << ARENAS_PER_MODE * 3 << " arenas ("
		<< ARENAS_PER_MODE << " each of 1v1/2v2/3v3), tickSkip " << TICK_SKIP << ", actionDelay " << ACTION_DELAY << std::endl;

	struct BotDef {
		std::string name;
		std::function<std::shared_ptr<GGL::ExternalOpponent>(torch::Device)> make;
	};
	std::vector<BotDef> defs = {
		{ "Nexto", [](torch::Device d) { return std::make_shared<GGL::NextoOpponent>("rlbot-run/nexto/nexto-model.pt", d); } },
		{ "GalactusV2", [](torch::Device d) { return std::make_shared<GGL::GGLBotOpponent>(GGL::GGLBotKind::GALACTUS_V2, "rlbot-run/galactus_v2", d); } },
		{ "ChronosV2", [](torch::Device d) { return std::make_shared<GGL::GGLBotOpponent>(GGL::GGLBotKind::CHRONOS_V2, "rlbot-run/chronos_v2", d); } },
		{ "Dice", [](torch::Device d) { return std::make_shared<GGL::GGLBotOpponent>(GGL::GGLBotKind::DICE, "rlbot-run/dice", d); } },
	};

	// Our DefaultAction index -> the 8 controls (through Nexto's lookup and its checked map)
	std::vector<std::array<float, 8>> ourTable(DefaultAction().GetActionAmount());
	{
		GGL::NextoOpponent n("rlbot-run/nexto/nexto-model.pt", torch::kCPU);
		for (size_t k = 0; k < n.actionMap.size(); k++)
			ourTable[n.actionMap[k]] = n.lookup[k];
	}

	// ---- Share -----------------------------------------------------------------------------------------------
	// The roll exactly as Learner.cpp partitions it: roll < serveFrac -> uniform bot
	std::cout << "\n== Share (the loop's roll, 20000 iterations, serveFrac 0.15, 3 bots) ==" << std::endl;
	{
		std::mt19937_64 rng(seed);
		std::uniform_real_distribution<float> oppRoll(0.0f, 1.0f);
		const int N = 20000;
		long perBot[3] = {}, none = 0;
		for (int i = 0; i < N; i++) {
			float roll = oppRoll(rng);
			if (roll < 0.15f) {
				std::uniform_int_distribution<size_t> pickBot(0, 2);
				perBot[pickBot(rng)]++;
			} else none++;
		}
		for (int b = 0; b < 3; b++) {
			double share = (double)perBot[b] / N;
			std::cout << "  " << defs[b].name << ": " << share * 100 << "% (target 5%)" << std::endl;
			Check(fabs(share - 0.05) < 0.007, defs[b].name + " realised share within 0.7 points of 5%");
		}
	}

	auto env = MakeEnvSet();
	std::cout << "players: " << env->state.numPlayers << std::endl;
	for (int i = 0; i < 20; i++) { // warm-up: first steps include arena creation/reset work
		env->Reset();
		env->StepFirstHalf(false);
		env->StepSecondHalf(IList(env->state.numPlayers, 0), false);
	}

	double envStepOnly = 0;
	{
		const int N = 60;
		double t0 = Now();
		for (int i = 0; i < N; i++) {
			env->Reset();
			env->StepFirstHalf(false);
			env->StepSecondHalf(IList(env->state.numPlayers, 0), false);
		}
		envStepOnly = (Now() - t0) / N;
	}

	for (auto& def : defs) {
		std::cout << "\n==== " << def.name << " ====" << std::endl;
		auto bot = def.make(dev);

		Stats stats[NUM_CATS];
		GGL::ExternalPlayStats play;
		int blueCount = 0;
		long wrong = 0;
		const int SERVES = 6, STEPS = 90;

		// Act time inside a full serve: time the whole loop, subtract env-only time
		double t0 = Now();
		Recorded last;
		for (int s = 0; s < SERVES; s++) {
			Recorded r = RunServe(*env, *bot, STEPS, stats, ourTable, blueCount, wrong, play);
			if (s == SERVES - 1)
				last = std::move(r);
		}
		double perStep = (Now() - t0) / (SERVES * STEPS);

		std::cout << "  play monitor (against random-side actions):";
		for (auto& kv : play.Report(TICK_SKIP))
			std::cout << "  [" << kv.first << " " << kv.second << "]";
		std::cout << std::endl;
		Check(play.Get(GGL::ExternalPlayStats::KICKOFFS) > 0, "the monitor sees kickoff episodes in the production state mix");

		// Acts
		long total = 0, idle = 0;
		for (int c = 0; c < NUM_CATS; c++) { total += stats[c].rows; idle += stats[c].idle; }
		std::cout << "  rows acted: " << total << "   all-zero (no-op) fraction: " << (total ? 100.0 * idle / total : 0) << "%" << std::endl;
		for (int c = 0; c < NUM_CATS; c++) {
			auto& s = stats[c];
			if (!s.rows) { std::cout << "  " << CAT_NAME[c] << ": no rows" << std::endl; continue; }
			int top = -1; long topN = 0;
			for (auto& kv : s.hist) if (kv.second > topN) { topN = kv.second; top = kv.first; }
			std::cout << "  " << CAT_NAME[c] << ": rows " << s.rows << ", distinct actions " << s.hist.size()
				<< ", boost " << 100.0 * s.boost / s.rows << "%, jump " << 100.0 * s.jump / s.rows
				<< "%, coast (no throttle/jump/boost/handbrake) " << 100.0 * s.coast / s.rows << "%, all-zero " << 100.0 * s.idle / s.rows << "%, top action " << top << " (" << 100.0 * topN / s.rows << "%)" << std::endl;
		}
		Check(total > 0 && (double)idle / total < 0.10, "acts: the all-zero no-op is under 10% of the rows");
		Check(wrong == 0, "only the isOld rows are filled, all of them with a valid index (0 stray / invalid rows)");
		double tvKG = (stats[KICKOFF].rows && stats[GROUND].rows) ? Tv(stats[KICKOFF], stats[GROUND]) : -1;
		double tvAG = (stats[AERIAL].rows && stats[GROUND].rows) ? Tv(stats[AERIAL], stats[GROUND]) : -1;
		std::cout << "  action-histogram distance (0 = same, 1 = disjoint): kickoff vs ground " << tvKG << ", aerial vs ground " << tvAG << std::endl;
		Check(tvAG > 0.2, "actions differ between aerial and ground states");
		if (stats[KICKOFF].rows >= 30)
			Check(tvKG > 0.2, "actions differ between kickoff and ground states");
		else
			std::cout << "  (kickoff rows < 30: kickoff difference not judged here, see 1.4)" << std::endl;

		// Teams
		std::cout << "  opponent team over " << SERVES << " serves: blue " << blueCount << ", orange " << SERVES - blueCount << std::endl;

		// Team coverage inside one serve: the opponent's rows are exactly one team per arena, all three modes
		{
			int perMode[3] = {};
			int g = 0;
			bool oneTeamPerArena = true;
			for (auto& gs : last.states[0]) {
				std::set<int> teams;
				int n = 0;
				for (auto& p : gs.players) {
					if (last.isOld[g]) { teams.insert((int)p.team); n++; }
					g++;
				}
				if ((int)gs.players.size() / 2 > bot->MaxTeamSize()) { // an arena outside the bot's team-size limit: nobody handed over
					if (n != 0) oneTeamPerArena = false;
					continue;
				}
				if (teams.size() != 1 || n != (int)gs.players.size() / 2) oneTeamPerArena = false;
				else perMode[gs.players.size() / 2 - 1]++;
			}
			std::cout << "  arenas served per mode: 1v1 " << perMode[0] << ", 2v2 " << perMode[1] << ", 3v3 " << perMode[2] << std::endl;
			const int want3v3 = bot->MaxTeamSize() >= 3 ? ARENAS_PER_MODE : 0;
			Check(oneTeamPerArena && perMode[0] == ARENAS_PER_MODE && perMode[1] == ARENAS_PER_MODE && perMode[2] == want3v3,
				"every arena inside the bot's team-size limit has exactly its one team handed to the bot, and no arena outside it is touched");
		}

		// Determinism: replay the same states through a fresh instance
		{
			std::vector<std::vector<int>> again;
			Replay([&]() { return def.make(dev); }, last, again);
			long rows;
			double d = DiffRate(last.acts, again, rows);
			std::cout << "  replay of the identical state sequence on a fresh instance: " << 100 * d << "% of " << rows << " rows differ" << std::endl;
			if (def.name == "Nexto")
				Check(d == 0.0, "deterministic across repeats (identical state sequence -> identical actions)");
			else
				Check(d < 0.4, "repeat differences stay within the slot-shuffle range (informational: expected non-zero)");
		}

		// Serve mechanics (stateful bots only): a flagged arena behaves like a cold start
		if (def.name == "Nexto") {
			std::vector<std::vector<int>> dummy;
			auto histA = def.make(dev), histB = def.make(dev), coldA = def.make(dev), coldB = def.make(dev);
			// Give histA/histB the same history by replaying the recording on both
			auto feed = [&](GGL::ExternalOpponent& b) {
				b.BeginServe(last.numPlayers);
				for (size_t s = 0; s + 1 < last.states.size(); s++) {
					std::vector<int> ext(last.numPlayers, -1);
					b.Act(last.states[s], last.isOld, last.terminals[s], ext);
				}
			};
			feed(*histA); feed(*histB);
			coldA->BeginServe(last.numPlayers); coldB->BeginServe(last.numPlayers);
			const auto& st = last.states.back();
			int nA = (int)st.size();
			std::vector<uint8_t> evenFlag(nA, 0), allFlag(nA, 1), none(nA, 0);
			for (int a = 0; a < nA; a += 2) evenFlag[a] = 1;
			std::vector<int> oH(last.numPlayers, -1), oC(last.numPlayers, -1), oH2(last.numPlayers, -1), oC2(last.numPlayers, -1);
			histA->Act(st, last.isOld, evenFlag, oH);
			coldA->Act(st, last.isOld, evenFlag, oC);
			histB->Act(st, last.isOld, none, oH2);
			coldB->Act(st, last.isOld, none, oC2);
			long flaggedRows = 0, flaggedDiff = 0, freeRows = 0, freeDiff = 0, noFlagRows = 0, noFlagDiff = 0;
			int g = 0;
			for (int a = 0; a < nA; a++)
				for (size_t k = 0; k < st[a].players.size(); k++, g++) {
					if (!last.isOld[g]) continue;
					if (evenFlag[a]) { flaggedRows++; flaggedDiff += oH[g] != oC[g]; }
					else { freeRows++; freeDiff += oH[g] != oC[g]; }
					noFlagRows++; noFlagDiff += oH2[g] != oC2[g];
				}
			std::cout << "  terminal-flagged arenas, history vs cold start: " << flaggedDiff << "/" << flaggedRows << " rows differ" << std::endl;
			std::cout << "  unflagged arenas, history vs cold start: " << freeDiff << "/" << freeRows << " rows differ (history matters when it does)" << std::endl;
			Check(flaggedDiff == 0, "a flagged (fresh-episode) arena is indistinguishable from a cold start");
			Check(noFlagDiff > 0, "control: with no flag the memory does change actions (so the flag test has teeth)");
		} else {
			std::cout << "  (stateless bot: no previous-action memory; terminals are unused by design)" << std::endl;
		}

		// Cost: full serve step time vs env step with no bot
		std::cout << "  step time (env + our random side + bot): " << perStep * 1000 << " ms; env alone: " << envStepOnly * 1000
			<< " ms; bot adds about " << (perStep - envStepOnly) * 1000 << " ms per step ("
			<< (perStep / envStepOnly) << "x)" << std::endl;
	}

	std::cout << "\n" << (g_fail ? "FAILED: " : "ALL PASS: ") << g_fail << " failing checks" << std::endl;
	env->Sync();
	return g_fail ? 1 : 0;
}
