// Checklist 1.5: round robin of the external bots in the sim, 1v1, fixed seeds (docs/Pre_Run_Checklist.md).
//
// Every ordered pair (A as blue, B as orange) plays N games, one per arena, all arenas stepped together exactly as the
// trainer steps them (tickSkip 8, actionDelay 7, Reset() before the step, the bots answering through Act()). A game is
// one episode: it ends on a goal, on 10 s without a touch (NoTouch), or after MAX_SECONDS; the last two count as draws.
// Participants: Nexto, GalactusV2, ChronosV2 and a uniform-random-valid-action floor (stand-in for a random-weight policy;
// the real random-weight 6.0 net lost 574-7,333 to the bots in the 6.0 smoke, docs/6.0.md). Mirror pairs (A vs A) give
// the side bias of the sim itself.
//
// Two suites: "kickoff" = every game starts from the engine's random kickoff (5 layouts in 1v1; the real game), and
// "scrimmage" = the training state mix (ball near car, drills, kickoffs, random states) for diversity, because a
// deterministic bot (Nexto) from only 5 kickoff layouts replays almost the same game many times.
//
// Usage (repo root):  ./build/OpponentRoundRobin <kickoff|scrimmage> [N=200] [seed=20261105] [out.md] [players per team=1]
// With 2 or 3 players per team each side is ONE bot controlling the whole team (exercises team-size handling).
#include <GigaLearnCPP/ExternalOpponent.h>
#include <GigaLearnCPP/NextoOpponent.h>
#include <GigaLearnCPP/GGLBotOpponent.h>

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
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>

using namespace RLGC;

namespace {
	constexpr int TICK_SKIP = 8, ACTION_DELAY = 7;
	constexpr float MAX_SECONDS = 90.f;
	int g_perTeam = 1; // argv[5]: 1 = 1v1, 2 = 2v2, 3 = 3v3 (each side is one bot controlling the whole team)

	// Uniform over the valid actions of the row's mask
	class RandomController : public GGL::ExternalOpponent {
	public:
		EnvSet* env = nullptr;
		RandomController() { name = "Random"; }
		void Act(const std::vector<GameState>& states, const std::vector<bool>& isOld,
			const std::vector<uint8_t>& prevArenaTerminals, std::vector<int>& out) override {
			if (!env->state.actionMasks.Defined())
				return;
			const int n = DefaultAction().GetActionAmount();
			for (int p = 0; p < (int)isOld.size(); p++) {
				if (!isOld[p]) continue;
				std::vector<int> valid;
				for (int a = 0; a < n; a++)
					if (env->state.actionMasks.At(p, a))
						valid.push_back(a);
				out[p] = valid.empty() ? 0 : valid[RocketSim::Math::RandInt(0, (int)valid.size())];
			}
		}
	};

	struct Participant {
		std::string name;
		std::shared_ptr<GGL::ExternalOpponent> asBlue, asOrange;
	};

	struct PairResult {
		int games = 0, winsA = 0, winsB = 0, draws = 0;
		double seconds = 0;
		double touchStepsA = 0, touchStepsB = 0, speedA = 0, speedB = 0, boostA = 0, boostB = 0;
		double stepsTotal = 0;
		int firstTouchA = 0, firstTouchB = 0, firstTouchBoth = 0;
	};

	std::unique_ptr<EnvSet> MakeEnv(int numArenas, bool kickoffOnly) {
		EnvSetConfig cfg = {};
		cfg.numArenas = numArenas;
		cfg.tickSkip = TICK_SKIP;
		cfg.actionDelay = ACTION_DELAY;
		cfg.saveRewards = false;
		cfg.envCreateFn = [kickoffOnly](int) {
			auto arena = Arena::Create(GameMode::SOCCAR);
			for (int i = 0; i < g_perTeam; i++) {
				arena->AddCar(Team::BLUE);
				arena->AddCar(Team::ORANGE);
			}
			EnvCreateResult r = {};
			r.arena = arena;
			r.actionParser = new DefaultAction();
			r.obsBuilder = new AdvancedObsPadded(3);
			if (kickoffOnly)
				r.stateSetter = new KickoffState();
			else
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

	PairResult Play(EnvSet& env, GGL::ExternalOpponent& A, GGL::ExternalOpponent& B, int N, unsigned seed) {
		RocketSim::Math::GetRandEngine().seed(seed);
		for (auto* c : { &A, &B })
			if (auto* r = dynamic_cast<RandomController*>(c))
				r->env = &env;
		// Fresh arenas for a fresh seed: every arena restarts its episode
		std::fill(env.state.terminals.begin(), env.state.terminals.end(), 1);
		env.Reset();

		const int numPlayers = env.state.numPlayers;
		A.BeginServe(numPlayers);
		B.BeginServe(numPlayers);
		std::vector<bool> isBlue(numPlayers, false), isOrange(numPlayers, false);
		{
			int i = 0;
			for (auto& gs : env.state.gameStates)
				for (auto& p : gs.players) {
					(p.team == Team::BLUE ? isBlue : isOrange)[i] = true;
					i++;
				}
		}

		struct G { bool done = false; int steps = 0, firstTouch = -1; double touch[2] = {}, speed[2] = {}, boost[2] = {}; };
		std::vector<G> games(N);
		PairResult res;
		res.games = N;
		int remaining = N;
		const int maxSteps = (int)(MAX_SECONDS * 120.f / TICK_SKIP);

		for (int step = 0; step < maxSteps + 5 && remaining > 0; step++) {
			std::vector<uint8_t> prevTerminals = env.state.terminals;
			env.Reset();
			env.StepFirstHalf(false);

			std::vector<int> ext(numPlayers, 0);
			A.Act(env.state.gameStates, isBlue, prevTerminals, ext);
			B.Act(env.state.gameStates, isOrange, prevTerminals, ext);
			IList actions(ext.begin(), ext.end());
			env.StepSecondHalf(actions, false);

			for (int a = 0; a < N; a++) {
				G& g = games[a];
				if (g.done) continue;
				auto& gs = env.state.gameStates[a];
				g.steps++;
				bool touched[2] = {};
				for (auto& p : gs.players) {
					int t = (int)p.team; // 0 = blue = A
					g.speed[t] += p.vel.Length() / g_perTeam;
					g.boost[t] += p.boost / g_perTeam;
					if (p.ballTouchedStep) {
						g.touch[t]++;
						touched[t] = true;
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
						if (conceded == Team::ORANGE) res.winsA++; else res.winsB++;
					} else res.draws++;
				}
			}
		}
		for (auto& g : games) {
			if (!g.done) { res.draws++; }
			res.seconds += g.steps * TICK_SKIP / 120.0;
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

	std::string Pct(double p, int n) { // "xx.x (+-y.y)" with the normal-approx 95% half width, in points
		std::ostringstream s;
		double hw = n > 0 ? 196.0 * sqrt(std::max(0.0, p * (1 - p)) / n) : 0;
		s << std::fixed << std::setprecision(1) << 100 * p << " ±" << hw;
		return s.str();
	}
}

int main(int argc, char** argv) {
	std::string suite = argc > 1 ? argv[1] : "kickoff";
	int N = argc > 2 ? std::stoi(argv[2]) : 200;
	unsigned seed = argc > 3 ? (unsigned)std::stoul(argv[3]) : 20261105;
	std::string outPath = argc > 4 ? argv[4] : "";
	if (argc > 5) g_perTeam = std::stoi(argv[5]);
	if (suite != "kickoff" && suite != "scrimmage") { std::cerr << "suite: kickoff | scrimmage\n"; return 2; }

	for (const char* path : { "collision_meshes", "../collision_meshes", "build/collision_meshes" })
		if (std::filesystem::exists(std::filesystem::path(path) / "soccar")) { RocketSim::Init(path); break; }

	torch::Device dev = torch::cuda::is_available() ? torch::Device(torch::kCUDA, 0) : torch::Device(torch::kCPU);
	auto env = MakeEnv(N, suite == "kickoff");

	std::vector<Participant> parts;
	auto addBot = [&](const std::string& name, std::function<std::shared_ptr<GGL::ExternalOpponent>()> mk) {
		parts.push_back({ name, mk(), mk() });
	};
	addBot("Nexto", [&] { return std::make_shared<GGL::NextoOpponent>("rlbot-run/nexto/nexto-model.pt", dev); });
	addBot("GalactusV2", [&] { return std::make_shared<GGL::GGLBotOpponent>(GGL::GGLBotKind::GALACTUS_V2, "rlbot-run/galactus_v2", dev); });
	addBot("ChronosV2", [&] { return std::make_shared<GGL::GGLBotOpponent>(GGL::GGLBotKind::CHRONOS_V2, "rlbot-run/chronos_v2", dev); });
	addBot("Random", [&] { return std::make_shared<RandomController>(); });

	std::ostringstream md;
	md << "# Round robin, sim " << g_perTeam << "v" << g_perTeam << " (" << suite << " suite)\n\n"
	   << "N = " << N << " games per ordered pair, seed " << seed << ", tickSkip " << TICK_SKIP << ", actionDelay " << ACTION_DELAY
	   << ", game = one episode (goal, 10 s without a touch, or " << MAX_SECONDS << " s; the last two are draws). A is blue, B is orange. "
	   << "Percentages carry a 95% half-width (normal approximation). Speed in uu/s and boost 0-100 are per-car averages, touches are steps in which the car touched the ball.\n\n"
	   << "| A (blue) | B (orange) | A wins % | B wins % | draws % | first touch A / B / both % | mean game s | touches A / B | speed A / B | boost A / B |\n"
	   << "|---|---|---|---|---|---|---|---|---|---|\n";

	const size_t P = parts.size();
	std::vector<std::vector<PairResult>> R(P, std::vector<PairResult>(P));
	int pairIdx = 0;
	for (size_t i = 0; i < P; i++)
		for (size_t j = 0; j < P; j++) {
			auto t0 = std::chrono::steady_clock::now();
			PairResult r = Play(*env, *parts[i].asBlue, *parts[j].asOrange, N, seed + 1000 * pairIdx++);
			R[i][j] = r;
			double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
			double g = r.games;
			md << "| " << parts[i].name << " | " << parts[j].name << " | " << Pct(r.winsA / g, N) << " | " << Pct(r.winsB / g, N)
			   << " | " << Pct(r.draws / g, N) << " | " << std::fixed << std::setprecision(0) << 100.0 * r.firstTouchA / g << " / " << 100.0 * r.firstTouchB / g << " / " << 100.0 * r.firstTouchBoth / g
			   << " | " << std::setprecision(1) << r.seconds / g << " | " << r.touchStepsA / g << " / " << r.touchStepsB / g
			   << " | " << std::setprecision(0) << r.speedA / std::max(1.0, r.stepsTotal) << " / " << r.speedB / std::max(1.0, r.stepsTotal)
			   << " | " << r.boostA / std::max(1.0, r.stepsTotal) << " / " << r.boostB / std::max(1.0, r.stepsTotal) << " |\n";
			std::cerr << parts[i].name << " vs " << parts[j].name << ": " << r.winsA << "-" << r.winsB << "-" << r.draws << " (" << secs << " s)\n";
		}

	// Per bot against the OTHER participants only (mirrors excluded), both sides pooled, and split by the side it played
	md << "\n## Per bot against the others (mirror games excluded)\n\n"
	   << "| Bot | games | wins % | losses % | draws % | wins % as blue | wins % as orange |\n|---|---|---|---|---|---|---|\n";
	for (size_t i = 0; i < P; i++) {
		int w = 0, l = 0, d = 0, gms = 0, wB = 0, gB = 0, wO = 0, gO = 0;
		for (size_t j = 0; j < P; j++) {
			if (i == j) continue;
			const PairResult& asBlue = R[i][j];   // i is A (blue)
			const PairResult& asOrange = R[j][i]; // i is B (orange)
			w += asBlue.winsA + asOrange.winsB; l += asBlue.winsB + asOrange.winsA; d += asBlue.draws + asOrange.draws;
			gms += asBlue.games + asOrange.games;
			wB += asBlue.winsA; gB += asBlue.games; wO += asOrange.winsB; gO += asOrange.games;
		}
		md << "| " << parts[i].name << " | " << gms << " | " << Pct((double)w / gms, gms) << " | " << Pct((double)l / gms, gms)
		   << " | " << Pct((double)d / gms, gms) << " | " << Pct((double)wB / gB, gB) << " | " << Pct((double)wO / gO, gO) << " |\n";
	}

	md << "\n## Side bias (mirror games: identical players, so blue and orange should win equally often)\n\n"
	   << "| Bot | blue wins % | orange wins % | draws % |\n|---|---|---|---|\n";
	for (size_t i = 0; i < P; i++) {
		const PairResult& m = R[i][i];
		md << "| " << parts[i].name << " | " << Pct((double)m.winsA / m.games, m.games) << " | " << Pct((double)m.winsB / m.games, m.games)
		   << " | " << Pct((double)m.draws / m.games, m.games) << " |\n";
	}

	std::string text = md.str();
	std::cout << text;
	if (!outPath.empty())
		std::ofstream(outPath) << text;
	env->Sync();
	return 0;
}
