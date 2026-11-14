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
//
// Watch mode (checklist 1.4):  ./build/OpponentRoundRobin watch <A> <B> [games=6] [kickoff|scrimmage] [speed=1] [seed] [fixed]
// plays A against B on ONE arena in real time and streams every step to the RocketSimVis web viewer (start
// tools/viz/RocketSimVisWeb/server.py and open http://localhost:9275). A bot is Nexto, GalactusV2, ChronosV2 or Random.
// The colours swap every game (both bots are seen on both sides) unless the last argument is `fixed`: A stays blue, B orange.
#include <GigaLearnCPP/ExternalOpponent.h>
#include <GigaLearnCPP/NextoOpponent.h>
#include <GigaLearnCPP/GGLBotOpponent.h>
#include <GigaLearnCPP/MatchLoop.h>
#include <GigaLearnCPP/Util/RenderSender.h>
#include <pybind11/embed.h>

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

	using PairResult = GGL::MatchResult; // shared with the in-trainer gauntlet (MatchLoop.h)

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
		GGL::MatchConfig mc;
		mc.playersPerTeam = g_perTeam;
		mc.maxSeconds = MAX_SECONDS;
		return GGL::PlayMatch(env, A, B, N, mc); // A is blue everywhere
	}

	std::string Pct(double p, int n) { // "xx.x (+-y.y)" with the normal-approx 95% half width, in points
		std::ostringstream s;
		double hw = n > 0 ? 196.0 * sqrt(std::max(0.0, p * (1 - p)) / n) : 0;
		s << std::fixed << std::setprecision(1) << 100 * p << " ±" << hw;
		return s.str();
	}
}

namespace {
	// Watch mode: two bots on one arena, streamed to the viewer
	int RunWatch(int argc, char** argv) {
		if (argc < 4) {
			std::cerr << "usage: OpponentRoundRobin watch <A> <B> [games=6] [kickoff|scrimmage] [speed=1] [seed] [fixed]" << std::endl;
			return 2;
		}
		std::string nameA = argv[2], nameB = argv[3];
		int games = argc > 4 ? std::stoi(argv[4]) : 6;
		std::string suite = argc > 5 ? argv[5] : "kickoff";
		float speed = argc > 6 ? (float)std::atof(argv[6]) : 1.f;
		unsigned seed = argc > 7 ? (unsigned)std::stoul(argv[7]) : 20261105;
		bool fixedColours = argc > 8 && std::string(argv[8]) == "fixed"; // A stays blue, B stays orange
		if (suite != "kickoff" && suite != "scrimmage") { std::cerr << "suite: kickoff | scrimmage" << std::endl; return 2; }

		for (const char* path : { "collision_meshes", "../collision_meshes", "build/collision_meshes" })
			if (std::filesystem::exists(std::filesystem::path(path) / "soccar")) { RocketSim::Init(path); break; }

		torch::Device dev = torch::cuda::is_available() ? torch::Device(torch::kCUDA, 0) : torch::Device(torch::kCPU);
		auto env = MakeEnv(1, suite == "kickoff");

		auto make = [&](const std::string& name) -> std::shared_ptr<GGL::ExternalOpponent> {
			if (name == "Nexto") return std::make_shared<GGL::NextoOpponent>("rlbot-run/nexto/nexto-model.pt", dev);
			if (name == "GalactusV2") return std::make_shared<GGL::GGLBotOpponent>(GGL::GGLBotKind::GALACTUS_V2, "rlbot-run/galactus_v2", dev);
			if (name == "ChronosV2") return std::make_shared<GGL::GGLBotOpponent>(GGL::GGLBotKind::CHRONOS_V2, "rlbot-run/chronos_v2", dev);
			if (name == "Dice") return std::make_shared<GGL::GGLBotOpponent>(GGL::GGLBotKind::DICE, "rlbot-run/dice", dev); // 1v1/2v2 only; not in the default round robin
			if (name == "Random") { auto r = std::make_shared<RandomController>(); r->env = env.get(); return r; }
			std::cerr << "unknown bot \"" << name << "\" (Nexto, GalactusV2, ChronosV2, Dice, Random)" << std::endl;
			return nullptr;
		};
		auto a = make(nameA);
		auto b = make(nameB);
		if (const char* t = std::getenv("GGL_BOT_TEMPERATURE")) {
			if (a) a->temperature = (float)std::atof(t);
			if (b) b->temperature = (float)std::atof(t);
		}
		if (!a || !b) return 2;

		pybind11::initialize_interpreter();
		pybind11::module::import("sys").attr("path").attr("insert")(0, "GigaLearnCPP");
		GGL::RenderSender sender(speed);

		int winsA = 0, winsB = 0, draws = 0;
		for (int g = 0; g < games; g++) {
			// Even games: A is blue. Odd games: B is blue. The viewer shows blue on the left.
			bool aBlue = fixedColours || (g % 2) == 0;
			RocketSim::Math::GetRandEngine().seed(seed + g);
			GGL::MatchConfig mc;
			mc.playersPerTeam = g_perTeam;
			mc.maxSeconds = MAX_SECONDS;
			mc.onStep = [&](EnvSet& e) { sender.Send(e.state.gameStates[0]); };
			std::cerr << "game " << g + 1 << "/" << games << ": " << (aBlue ? nameA : nameB) << " (blue) vs " << (aBlue ? nameB : nameA) << " (orange)" << std::endl;
			GGL::MatchResult r = aBlue ? GGL::PlayMatch(*env, *a, *b, 1, mc) : GGL::PlayMatch(*env, *b, *a, 1, mc);
			// r is from the blue bot's side
			int blueWin = r.winsA, orangeWin = r.winsB;
			int aWin = aBlue ? blueWin : orangeWin, bWin = aBlue ? orangeWin : blueWin;
			winsA += aWin; winsB += bWin; draws += r.draws;
			std::cerr << "  -> " << (aWin ? nameA + " scored" : bWin ? nameB + " scored" : std::string("no goal (draw)"))
				<< ", " << std::fixed << std::setprecision(1) << r.seconds << " s, first touch "
				<< (r.firstTouchA ? "blue" : r.firstTouchB ? "orange" : r.firstTouchBoth ? "both at once" : "nobody") << std::endl;
		}
		std::cerr << nameA << " " << winsA << " - " << winsB << " " << nameB << ", " << draws << " draws" << std::endl;
		env->Sync();
		return 0;
	}
}

int main(int argc, char** argv) {
	if (argc > 1 && std::string(argv[1]) == "watch")
		return RunWatch(argc, argv);
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
	// GGL_BOT_TEMPERATURE=T: every bot samples from softmax(logits / T) instead of argmax (ExternalOpponent::temperature)
	const float botTemperature = std::getenv("GGL_BOT_TEMPERATURE") ? (float)std::atof(std::getenv("GGL_BOT_TEMPERATURE")) : 0.f;
	auto addBot = [&](const std::string& name, std::function<std::shared_ptr<GGL::ExternalOpponent>()> mk) {
		auto a = mk(), b = mk();
		a->temperature = b->temperature = botTemperature;
		parts.push_back({ name, a, b });
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
