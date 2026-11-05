// Observation scale measurement (checklist: normalize the observations).
//
// Builds arenas exactly as the 6.0 trainer does (AdvancedObsPadded(3, shuffle on, 8 ball-prediction slices) = 278 floats,
// the training reset mix, tickSkip 8, actionDelay 7), plays uniformly random valid actions, and records for every
// feature: mean, std, min, max and the share of values with |x| > 1 / > 3 / > 10. Output is a markdown table and, with a
// second path, a JSON file with mean/std/min/max per feature (the input to the model's fixed input scaling).
//
// With GGL_OBS_BOTS=1 the cars are driven by Galactus (blue) against Chronos (orange) instead of random actions (1v1 only),
// so the numbers describe real play (boost pickups, fast driving, aerials), not random flailing.
//
// Usage (repo root):  ./build/ObsStats [numArenas=256] [steps=400] [players per team=1] [out.md] [out.json] [seed]
#include <GigaLearnCPP/ExternalOpponent.h>
#include <GigaLearnCPP/GGLBotOpponent.h>
#include <GigaLearnCPP/MatchLoop.h>
#include <torch/cuda.h>
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

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

using namespace RLGC;

int main(int argc, char** argv) {
	int numArenas = argc > 1 ? std::stoi(argv[1]) : 256;
	int steps = argc > 2 ? std::stoi(argv[2]) : 400;
	int perTeam = argc > 3 ? std::stoi(argv[3]) : 1;
	std::string outMd = argc > 4 ? argv[4] : "";
	std::string outJson = argc > 5 ? argv[5] : "";
	unsigned seed = argc > 6 ? (unsigned)std::stoul(argv[6]) : 20261105;
	for (const char* path : { "collision_meshes", "../collision_meshes", "build/collision_meshes" })
		if (std::filesystem::exists(std::filesystem::path(path) / "soccar")) { RocketSim::Init(path); break; }
	RocketSim::Math::GetRandEngine().seed(seed);

	EnvSetConfig cfg = {};
	cfg.numArenas = numArenas;
	cfg.tickSkip = 8;
	cfg.actionDelay = 7;
	cfg.saveRewards = false;
	cfg.envCreateFn = [perTeam](int) {
		auto arena = Arena::Create(GameMode::SOCCAR);
		for (int i = 0; i < perTeam; i++) { arena->AddCar(Team::BLUE); arena->AddCar(Team::ORANGE); }
		EnvCreateResult r = {};
		r.arena = arena;
		r.actionParser = new DefaultAction();
		r.obsBuilder = new AdvancedObsPadded(3, true, 8);
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
	EnvSet env(cfg);
	const int D = env.obsSize;
	std::vector<double> n(D, 0), mean(D, 0), m2(D, 0), mn(D, 1e30), mx(D, -1e30), c1(D, 0), c3(D, 0), c10(D, 0);
	const int A = DefaultAction().GetActionAmount();

	auto accumulate = [&](EnvSet& e) {
		const int P = e.state.numPlayers;
		for (int p = 0; p < P; p++)
			for (int j = 0; j < D; j++) {
				double x = e.state.obs.At(p, j);
				n[j]++;
				double d = x - mean[j];
				mean[j] += d / n[j];
				m2[j] += d * (x - mean[j]);
				mn[j] = std::min(mn[j], x);
				mx[j] = std::max(mx[j], x);
				double ax = std::fabs(x);
				c1[j] += ax > 1; c3[j] += ax > 3; c10[j] += ax > 10;
			}
	};
	if (std::getenv(GGL_OBS_BOTS) && perTeam == 1) {
		torch::Device dev = torch::cuda::is_available() ? torch::Device(torch::kCUDA, 0) : torch::Device(torch::kCPU);
		GGL::GGLBotOpponent a(GGL::GGLBotKind::GALACTUS_V2, rlbot-run/galactus_v2, dev);
		GGL::GGLBotOpponent b(GGL::GGLBotKind::CHRONOS_V2, rlbot-run/chronos_v2, dev);
		GGL::MatchConfig mc;
		mc.playersPerTeam = 1;
		mc.maxSeconds = steps * 8 / 120.f;
		mc.onStep = accumulate;
		GGL::PlayMatch(env, a, b, numArenas, mc);
	} else {
		std::fill(env.state.terminals.begin(), env.state.terminals.end(), 1);
		for (int s = 0; s < steps; s++) {
			env.Reset();
			env.StepFirstHalf(false);
			const int P = env.state.numPlayers;
			accumulate(env);
			IList actions(P);
			for (int p = 0; p < P; p++) {
				std::vector<int> valid;
				for (int a = 0; a < A; a++)
					if (!env.state.actionMasks.Defined() || env.state.actionMasks.At(p, a)) valid.push_back(a);
				actions[p] = valid.empty() ? 0 : valid[RocketSim::Math::RandInt(0, (int)valid.size())];
			}
			env.StepSecondHalf(actions, false);
		}
	}

	std::ostringstream md, js;
	md << "| idx | mean | std | min | max | %|x|>1 | %|x|>3 | %|x|>10 |\n|---|---|---|---|---|---|---|---|\n";
	js << "{\"obs_size\":" << D << ",\"rows\":" << (long long)n[0] << ",\"mean\":[";
	std::vector<double> sd(D);
	for (int j = 0; j < D; j++) {
		sd[j] = n[j] > 1 ? std::sqrt(m2[j] / (n[j] - 1)) : 0;
		md << "| " << j << " | " << std::setprecision(4) << mean[j] << " | " << sd[j] << " | " << mn[j] << " | " << mx[j]
			<< " | " << 100 * c1[j] / n[j] << " | " << 100 * c3[j] / n[j] << " | " << 100 * c10[j] / n[j] << " |\n";
		js << std::setprecision(9) << mean[j] << (j + 1 < D ? "," : "");
	}
	js << "],\"std\":[";
	for (int j = 0; j < D; j++) js << sd[j] << (j + 1 < D ? "," : "");
	js << "],\"min\":[";
	for (int j = 0; j < D; j++) js << mn[j] << (j + 1 < D ? "," : "");
	js << "],\"max\":[";
	for (int j = 0; j < D; j++) js << mx[j] << (j + 1 < D ? "," : "");
	js << "]}\n";

	// Summary to stdout
	int constant = 0, small = 0, big = 0, huge = 0;
	for (int j = 0; j < D; j++) {
		if (sd[j] < 1e-6) constant++;
		else if (sd[j] < 0.1) small++;
		if (sd[j] > 1.5) big++;
		if (std::max(std::fabs(mn[j]), std::fabs(mx[j])) > 10) huge++;
	}
	std::cout << "obs " << D << " floats, " << (long long)n[0] << " rows: constant(std<1e-6) " << constant << ", small(std<0.1) " << small
		<< ", std>1.5: " << big << ", any |x|>10: " << huge << "\n";
	if (!outMd.empty()) std::ofstream(outMd) << md.str();
	if (!outJson.empty()) std::ofstream(outJson) << js.str();
	return 0;
}
