// Golden-state dumper for checklist item 1.2 (docs/Pre_Run_Checklist.md).
//
// Records real RocketSim states (kickoffs, drives, aerials, walls, demos, spent pads, corners, 1v1/2v2/3v3, both
// teams) and, for each, what the NextoOpponent PORT builds and decides: the net input (q, kv), the logits on CPU
// and CUDA, and the action Act() returns. tools/opponent_golden/compare_nexto.py feeds the same states through the
// ORIGINAL nexto_obs.py + agent.py and diffs. Usage (from build/):  ./OpponentGolden <out.json> [seed]
// (GGL_GOLDEN_TF32=0 turns CUDA TF32 off; the default matches the trainer)
#include <GigaLearnCPP/NextoOpponent.h>

#include "states_common.h"
#include <RLGymCPP/CommonValues.h>

#include <ATen/Context.h>
#include <torch/cuda.h>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>

using namespace RLGC;
using golden::Rand;
using golden::RandInt;
using json = nlohmann::json;

static json V(const Vec& v) { return json::array({ v.x, v.y, v.z }); }

int main(int argc, char** argv) {
	if (argc < 2) {
		std::cerr << "usage: OpponentGolden <out.json> [seed]\n";
		return 2;
	}
	golden::Rng().seed(argc > 2 ? std::stoull(argv[2]) : 20261105);

	if (!golden::InitRocketSim())
		return 2;

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

	golden::GenerateStates(Snapshot);

	std::ofstream f(argv[1]);
	f << out.dump();
	std::cout << "wrote " << out["snapshots"].size() << " snapshots to " << argv[1] << " (cuda " << (cuda ? "yes" : "no") << ")\n";
	return 0;
}
