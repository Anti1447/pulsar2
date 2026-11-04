// Golden-state dumper for checklist item 1.2 (docs/Pre_Run_Checklist.md).
//
// Records real RocketSim states (kickoffs, drives, aerials, walls, demos, spent pads, corners, 1v1/2v2/3v3, both
// teams) and, for each, what the NextoOpponent PORT builds and decides: the net input (q, kv), the logits on CPU
// and CUDA, and the action Act() returns. tools/opponent_golden/compare_nexto.py feeds the same states through the
// ORIGINAL nexto_obs.py + agent.py and diffs. Usage (from build/):  ./OpponentGolden <out.json> [seed]
// (GGL_GOLDEN_TF32=0 turns CUDA TF32 off; the default matches the trainer)
#include <GigaLearnCPP/NextoOpponent.h>

#include <RLGymCPP/Gamestates/GameState.h>
#include <RLGymCPP/CommonValues.h>

#include <ATen/Context.h>
#include <torch/cuda.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>

using namespace RLGC;
using json = nlohmann::json;

static std::mt19937_64 g_rng;
static float Rand(float lo, float hi) { return std::uniform_real_distribution<float>(lo, hi)(g_rng); }
static int RandInt(int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(g_rng); }

static json V(const Vec& v) { return json::array({ v.x, v.y, v.z }); }

static void RandomizeCar(Car* car, bool aerial) {
	CarState s = car->GetState();
	s.pos = Vec(Rand(-3500, 3500), Rand(-4500, 4500), aerial ? Rand(300, 1800) : 17.f);
	s.vel = Vec(Rand(-1500, 1500), Rand(-1500, 1500), aerial ? Rand(-800, 800) : 0.f);
	s.angVel = aerial ? Vec(Rand(-4, 4), Rand(-4, 4), Rand(-4, 4)) : Vec(0, 0, 0);
	s.rotMat = Angle(Rand(-3.14f, 3.14f), aerial ? Rand(-1.2f, 1.2f) : 0.f, aerial ? Rand(-3.14f, 3.14f) : 0.f).ToRotMat();
	s.boost = Rand(0, 100);
	car->SetState(s);
}

static void RandomizeBall(Arena* arena, int kind) {
	BallState b = arena->ball->GetState();
	switch (kind) {
	case 0: b.pos = Vec(Rand(-3500, 3500), Rand(-4500, 4500), Rand(100, 1800)); break;
	case 1: b.pos = Vec(Rand(0, 1) < 0.5f ? -3900.f : 3900.f, Rand(0, 1) < 0.5f ? -4800.f : 4800.f, 150.f); break; // corner
	case 2: b.pos = Vec(Rand(-700, 700), Rand(0, 1) < 0.5f ? -5200.f : 5200.f, 150.f); break;                       // in a net
	default: b.pos = Vec(Rand(-3000, 3000), Rand(-4000, 4000), 93.f); break;
	}
	b.vel = Vec(Rand(-2000, 2000), Rand(-2000, 2000), Rand(-500, 900));
	b.angVel = Vec(Rand(-5, 5), Rand(-5, 5), Rand(-5, 5));
	arena->ball->SetState(b);
}

static void ScrambleControls(Arena* arena) {
	for (Car* car : arena->_cars) {
		CarControls c = {};
		c.throttle = (float)RandInt(-1, 1);
		c.steer = (float)RandInt(-1, 1);
		c.pitch = (float)RandInt(-1, 1);
		c.yaw = (float)RandInt(-1, 1);
		c.roll = (float)RandInt(-1, 1);
		c.jump = RandInt(0, 3) == 0;
		c.boost = RandInt(0, 1);
		c.handbrake = RandInt(0, 4) == 0;
		car->controls = c;
	}
}

int main(int argc, char** argv) {
	if (argc < 2) {
		std::cerr << "usage: OpponentGolden <out.json> [seed]\n";
		return 2;
	}
	g_rng.seed(argc > 2 ? std::stoull(argv[2]) : 20261105);

	bool meshes = false;
	for (const char* path : { "collision_meshes", "../collision_meshes", "build/collision_meshes" })
		if (std::filesystem::exists(std::filesystem::path(path) / "soccar")) {
			RocketSim::Init(path);
			meshes = true;
			break;
		}
	if (!meshes) {
		std::cerr << "no collision_meshes folder found\n";
		return 2;
	}

	// The trainer runs CUDA matmuls with TF32 on (LearnerConfig::allowTF32 = true); GGL_GOLDEN_TF32=0 for strict fp32
	{
		const char* e = std::getenv("GGL_GOLDEN_TF32");
		bool tf32 = !(e && e[0] == '0');
		at::globalContext().setAllowTF32CuBLAS(tf32);
		at::globalContext().setAllowTF32CuDNN(tf32);
		std::cout << "CUDA TF32 matmuls: " << (tf32 ? "on (as in training)" : "off") << std::endl;
	}
	const char* modelPath = "../rlbot-run/nexto/nexto-model.pt";
	GGL::NextoOpponent cpu(modelPath, torch::kCPU);
	std::unique_ptr<GGL::NextoOpponent> cuda;
	if (torch::cuda::is_available())
		cuda = std::make_unique<GGL::NextoOpponent>(modelPath, torch::kCUDA);

	json out;
	out["lookup"] = cpu.lookup;
	out["has_cuda"] = (bool)cuda;
	out["snapshots"] = json::array();

	auto Logits = [&](GGL::NextoOpponent& op, const std::vector<float>& q, const std::vector<float>& kv, int nEnt) {
		torch::NoGradGuard ng;
		auto tQ = torch::from_blob((void*)q.data(), { 1, 1, 32 }, torch::kFloat32).clone().to(op.device);
		auto tKv = torch::from_blob((void*)kv.data(), { 1, nEnt, 24 }, torch::kFloat32).clone().to(op.device);
		auto tM = torch::zeros({ 1, nEnt }, torch::TensorOptions().device(op.device));
		auto res = op.model.forward({ std::make_tuple(tQ, tKv, tM) }).toTuple();
		auto logits = res->elements()[0].toTensor().to(torch::kCPU, torch::kFloat32).reshape({ -1 });
		std::vector<float> v(logits.data_ptr<float>(), logits.data_ptr<float>() + logits.numel());
		return v;
	};

	auto Snapshot = [&](Arena* arena, const std::string& tag) {
		GameState gs(arena);
		int n = (int)gs.players.size();
		int nEnt = n + 1 + CommonValues::BOOST_LOCATIONS_AMOUNT;

		json snap;
		snap["tag"] = tag;
		snap["n"] = n;
		snap["ball"] = { { "pos", V(gs.ball.pos) }, { "vel", V(gs.ball.vel) }, { "angvel", V(gs.ball.angVel) } };
		json pads = json::array();
		for (int i = 0; i < CommonValues::BOOST_LOCATIONS_AMOUNT; i++)
			pads.push_back((bool)gs.boostPads[i]);
		snap["pads"] = pads;

		std::vector<std::array<float, 8>> prev(n);
		json players = json::array();
		for (int i = 0; i < n; i++) {
			const Player& p = gs.players[i];
			prev[i] = cpu.lookup[RandInt(0, (int)cpu.lookup.size() - 1)];
			players.push_back({
				{ "team", (int)p.team }, { "pos", V(p.pos) }, { "vel", V(p.vel) }, { "angvel", V(p.angVel) },
				{ "fwd", V(p.rotMat.forward) }, { "right", V(p.rotMat.right) }, { "up", V(p.rotMat.up) },
				{ "boost", p.boost }, { "on_ground", (bool)p.isOnGround }, { "has_flip", (bool)p.HasFlipOrJump() },
				{ "demoed", (bool)p.isDemoed }, { "prev_action", prev[i] } });
		}
		snap["players"] = players;

		// What the port builds and decides, per player
		json port = json::array();
		for (int i = 0; i < n; i++) {
			std::vector<float> q(32), kv((size_t)nEnt * 24);
			GGL::NextoOpponent::BuildObs(gs, i, prev[i], kv.data(), q.data());
			json e;
			e["q"] = q;
			e["kv"] = kv;
			auto lc = Logits(cpu, q, kv, nEnt);
			e["logits_cpu"] = lc;
			e["argmax_cpu"] = (int)(std::max_element(lc.begin(), lc.end()) - lc.begin());
			if (cuda) {
				auto lg = Logits(*cuda, q, kv, nEnt);
				e["logits_cuda"] = lg;
				e["argmax_cuda"] = (int)(std::max_element(lg.begin(), lg.end()) - lg.begin());
			}
			port.push_back(e);
		}
		snap["port"] = port;

		// End to end: Act() on the same state, with the same previous actions
		{
			std::vector<GameState> states = { gs };
			std::vector<bool> isOld(n, true);
			std::vector<uint8_t> terms = { 0 };
			std::vector<int> act(n, -1);
			cpu.BeginServe(n);
			for (int i = 0; i < n; i++)
				cpu.prevActions[i] = prev[i];
			cpu.Act(states, isOld, terms, act);
			snap["act_cpu"] = act;
		}
		out["snapshots"].push_back(std::move(snap));
	};

	for (int perTeam = 1; perTeam <= 3; perTeam++) {
		Arena* arena = Arena::Create(GameMode::SOCCAR);
		for (int i = 0; i < perTeam; i++) {
			arena->AddCar(Team::BLUE);
			arena->AddCar(Team::ORANGE);
		}
		std::string mode = std::to_string(perTeam) + "v" + std::to_string(perTeam);

		// Every spawn layout the kickoff picker can produce
		for (int seed = 0; seed < 14; seed++) {
			arena->ResetToRandomKickoff(seed);
			Snapshot(arena, mode + " kickoff seed " + std::to_string(seed));
		}

		// Random drives: ground, wall/ceiling, aerials, flips, spent pads, demos, ball in corners and nets
		for (int scenario = 0; scenario < 50; scenario++) {
			arena->ResetToRandomKickoff(100 + scenario);
			bool aerial = scenario % 3 == 0;
			RandomizeBall(arena, scenario % 4);
			for (Car* car : arena->_cars)
				if (RandInt(0, 3) != 0)
					RandomizeCar(car, aerial && RandInt(0, 1));
			if (scenario % 5 == 1) // a few spent pads
				for (auto* pad : arena->_boostPads)
					if (RandInt(0, 2) == 0) {
						BoostPadState ps = pad->GetState();
						ps.isActive = false;
						ps.cooldown = Rand(0.5f, 10.f);
						pad->SetState(ps);
					}
			if (scenario % 7 == 2) // a demolished car
			{
				Car* car = *arena->_cars.begin();
				CarState cs = car->GetState();
				cs.isDemoed = true;
				cs.demoRespawnTimer = 2.f;
				car->SetState(cs);
			}
			int windows = RandInt(0, 6);
			for (int w = 0; w < windows; w++) {
				ScrambleControls(arena);
				arena->Step(8);
			}
			Snapshot(arena, mode + " drive " + std::to_string(scenario));
		}
		delete arena;
	}

	std::ofstream f(argv[1]);
	f << out.dump();
	std::cout << "wrote " << out["snapshots"].size() << " snapshots to " << argv[1] << " (cuda " << (cuda ? "yes" : "no") << ")\n";
	return 0;
}
