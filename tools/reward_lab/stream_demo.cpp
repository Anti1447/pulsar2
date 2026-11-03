// reward_lab_stream_demo - drive a scenario through the REAL stream tap, with no trainer (docs/REWARD_VIEWER_SPEC.md section 10, P4-3).
//
//   REWARD_LAB_STREAM=127.0.0.1:9280 reward_lab_stream_demo <scenario.json> [--loop] [--hz 15] [--episodes N]
//
// Runs the scenario's script on a real EnvSet whose rewards are wrapped by MaybeInstallStreamTapsFromEnv: the same call the render branch of the trainer
// would make. So what arrives at `serve.py --stream` is exactly what a render run would send, made from a scenario you can read. Paced at --hz steps per
// second (default 15, the bot's decision rate). With --loop it resets and plays again until --episodes (default: forever) or Ctrl+C.
// Needs REWARD_LAB_STREAM set (exit 2 otherwise). Run from the repo root (needs build/collision_meshes).
#include <RLGymCPP/Lab/LabEnv.h>
#include <RLGymCPP/Lab/RewardRegistry.h>
#include <RLGymCPP/Rewards/RewardStreamTap.h>

#include <chrono>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>

using namespace RLGC;
using namespace RLGC::Lab;

int main(int argc, char** argv) {
	std::string file; bool loop = false; double hz = 15; int episodes = 0;
	for (int i = 1; i < argc; i++) {
		std::string a = argv[i];
		if (a == "--loop") loop = true;
		else if (a == "--hz" && i + 1 < argc) hz = std::atof(argv[++i]);
		else if (a == "--episodes" && i + 1 < argc) episodes = std::atoi(argv[++i]);
		else if (a == "-h" || a == "--help") { std::cout << "usage: REWARD_LAB_STREAM=127.0.0.1:9280 reward_lab_stream_demo <scenario.json> [--loop] [--hz 15] [--episodes N]\n"; return 0; }
		else file = a;
	}
	if (file.empty()) { std::cerr << "no scenario given (try --help)\n"; return 2; }
	if (hz <= 0 || hz > 1000) { std::cerr << "--hz must be in (0, 1000]\n"; return 2; }
	std::ifstream f(file); if (!f) { std::cerr << file << ": cannot open\n"; return 2; }
	std::stringstream ss; ss << f.rdbuf();
	Scenario sc; std::vector<std::string> errs;
	if (!ParseScenarioText(ss.str(), sc, errs)) { std::cerr << file << ": INVALID SCENARIO\n"; for (auto& e : errs) std::cerr << "  " << e << "\n"; return 2; }
	std::vector<WeightedReward> rewards;
	if (!BuildRewardStack(sc.rewards, rewards, errs)) { for (auto& e : errs) std::cerr << e << "\n"; return 2; }
	if (!MaybeInstallStreamTapsFromEnv(rewards, sc.tickSkip)) { std::cerr << "REWARD_LAB_STREAM is not set to a usable 127.x.x.x:PORT; nothing to stream to\n"; return 2; }
	LabEnv* env = LabEnv::Create(sc, rewards);
	if (!env) { std::cerr << "cannot create the environment (run from the repo root; needs build/collision_meshes)\n"; return 2; }

	const auto period = std::chrono::duration<double>(1.0 / hz);
	int done = 0;
	do {
		int step = 0;
		while (step < sc.steps) {
			auto next = std::chrono::steady_clock::now() + std::chrono::duration_cast<std::chrono::steady_clock::duration>(period);
			std::vector<Action> acts;
			for (int c = 0; c < (int)sc.cars.size(); c++) acts.push_back(ScriptedAction(sc, c, step));
			env->Step(acts);
			step++;
			if (env->env->state.terminals[0] != 0) break;
			std::this_thread::sleep_until(next);
		}
		done++;
		std::cerr << "[stream-demo] episode " << done << " played (" << step << " steps)\n";
		if (!loop || (episodes > 0 && done >= episodes)) break;
		env->env->ResetArena(0);
	} while (true);
	delete env; FreeRewardStack(rewards);
	return 0;
}
