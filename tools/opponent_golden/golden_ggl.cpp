// Golden test for the GalactusV2 / ChronosV2 ports (checklist item 1.2), OUR side. Two modes:
//
//   ./OpponentGoldenGGL gen   <states.json> [seed]
//       Records sim states (tools/opponent_golden/states_common.h) with everything the original GameState needs, and what
//       the PORT (GGLBotOpponent) builds for each player: observation, action mask, Chronos mirror flag.
//   ./OpponentGoldenGGL judge <states.json> <ref_galactus.json> <ref_chronos.json> <model_galactus.json> <model_chronos.json>
//       Compares with what the ORIGINAL bots' own sources build from the same states (build/ref_galactus, build/ref_chronos,
//       made by build_ref.sh): observation, mask, every action row's parsed controls, the original model code's logits and
//       picks against ours, and the net's pick on both
//       observations (CPU and CUDA). The originals shuffle their padded player slots with a random engine, so slots are
//       compared as sets and the net is run on slot-sorted copies; the shuffle's effect on the pick is reported apart.
#include <GigaLearnCPP/GGLBotOpponent.h>

#include "states_common.h"

#include <RLGymCPP/ActionParsers/DefaultAction.h>
#include <RLGymCPP/Gamestates/BallPredictor.h>
#include <RLGymCPP/Gamestates/StateUtil.h>

#include <ATen/Context.h>
#include <torch/cuda.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <iostream>

using namespace RLGC;
using json = nlohmann::json;
using GGL::GGLBotKind;
using GGL::GGLBotOpponent;

static json V(const Vec& v) { return json::array({ v.x, v.y, v.z }); }
static json Act8(const Action& a) { return json::array({ a.throttle, a.steer, a.pitch, a.yaw, a.roll, a.jump, a.boost, a.handbrake }); }

// Observation geometry shared by both bots: [prefix][self 29][2 teammate slots][3 opponent slots][suffix]
struct Layout { int obsSize, prefix, suffix; };
static const Layout LAYOUT_GALACTUS = { 258, 54, 30 };   // 9 + 8 + 34 + 3 (nearest big pad) = 54, then self; 5 ball-pred samples (30) after
static const Layout LAYOUT_CHRONOS = { 225, 51, 0 };     // 9 + 8 + 34, then self
constexpr int BLOCK = 29, TEAMMATE_SLOTS = 2, OPPONENT_SLOTS = 3;

static std::vector<std::vector<float>> Blocks(const std::vector<float>& obs, const Layout& L, int from, int count) {
	std::vector<std::vector<float>> out;
	for (int i = 0; i < count; i++)
		out.emplace_back(obs.begin() + from + i * BLOCK, obs.begin() + from + (i + 1) * BLOCK);
	return out;
}

// Same observation with the teammate and opponent slots sorted by content (a canonical order)
static std::vector<float> Canonical(const std::vector<float>& obs, const Layout& L) {
	int teamFrom = L.prefix + BLOCK, oppFrom = teamFrom + TEAMMATE_SLOTS * BLOCK;
	auto team = Blocks(obs, L, teamFrom, TEAMMATE_SLOTS);
	auto opp = Blocks(obs, L, oppFrom, OPPONENT_SLOTS);
	// Sorted by value, with a coarse key so 1e-7 noise cannot reorder two different players
	auto key = [](const std::vector<float>& a, const std::vector<float>& b) {
		for (size_t i = 0; i < a.size(); i++) {
			float x = std::round(a[i] * 1000.f), y = std::round(b[i] * 1000.f);
			if (x != y)
				return x < y;
		}
		return false;
	};
	std::sort(team.begin(), team.end(), key);
	std::sort(opp.begin(), opp.end(), key);
	std::vector<float> out(obs.begin(), obs.begin() + teamFrom);
	for (auto& b : team) out.insert(out.end(), b.begin(), b.end());
	for (auto& b : opp) out.insert(out.end(), b.begin(), b.end());
	out.insert(out.end(), obs.begin() + oppFrom + OPPONENT_SLOTS * BLOCK, obs.end());
	return out;
}

static std::vector<float> Shuffled(const std::vector<float>& obs, const Layout& L) {
	int teamFrom = L.prefix + BLOCK, oppFrom = teamFrom + TEAMMATE_SLOTS * BLOCK;
	auto team = Blocks(obs, L, teamFrom, TEAMMATE_SLOTS);
	auto opp = Blocks(obs, L, oppFrom, OPPONENT_SLOTS);
	std::shuffle(team.begin(), team.end(), golden::Rng());
	std::shuffle(opp.begin(), opp.end(), golden::Rng());
	std::vector<float> out(obs.begin(), obs.begin() + teamFrom);
	for (auto& b : team) out.insert(out.end(), b.begin(), b.end());
	for (auto& b : opp) out.insert(out.end(), b.begin(), b.end());
	out.insert(out.end(), obs.begin() + oppFrom + OPPONENT_SLOTS * BLOCK, obs.end());
	return out;
}

// ------------------------------------------------------------------ gen
static int Gen(const char* outPath) {
	if (!golden::InitRocketSim())
		return 2;
	GGLBotOpponent galactus(GGLBotKind::GALACTUS_V2, "../rlbot-run/galactus_v2", torch::kCPU);
	GGLBotOpponent chronos(GGLBotKind::CHRONOS_V2, "../rlbot-run/chronos_v2", torch::kCPU);
	DefaultAction ours;

	json out;
	out["snapshots"] = json::array();
	auto Snapshot = [&](Arena* arena, const std::string& tag) {
		GameState gs(arena);
		const int n = (int)gs.players.size();
		for (auto& p : gs.players)
			p.prevAction = ours.actions[golden::RandInt(0, (int)ours.actions.size() - 1)];

		json snap;
		snap["tag"] = tag;
		snap["n"] = n;
		snap["ball"] = { { "pos", V(gs.ball.pos) }, { "vel", V(gs.ball.vel) }, { "angvel", V(gs.ball.angVel) } };
		json pads = json::array(), padsInv = json::array(), timers = json::array(), timersInv = json::array();
		for (int i = 0; i < CommonValues::BOOST_LOCATIONS_AMOUNT; i++) {
			pads.push_back((bool)gs.boostPads[i]);
			padsInv.push_back((bool)gs.boostPadsInv[i]);
			timers.push_back(gs.boostPadTimers[i]);
			timersInv.push_back(gs.boostPadTimersInv[i]);
		}
		snap["pads"] = pads; snap["pads_inv"] = padsInv; snap["pad_timers"] = timers; snap["pad_timers_inv"] = timersInv;

		json players = json::array();
		for (int i = 0; i < n; i++) {
			const Player& p = gs.players[i];
			players.push_back({
				{ "car_id", p.carId }, { "team", (int)p.team }, { "pos", V(p.pos) }, { "vel", V(p.vel) }, { "angvel", V(p.angVel) },
				{ "fwd", V(p.rotMat.forward) }, { "right", V(p.rotMat.right) }, { "up", V(p.rotMat.up) },
				{ "boost", p.boost }, { "on_ground", (bool)p.isOnGround }, { "has_jumped", (bool)p.hasJumped },
				{ "has_flipped", (bool)p.hasFlipped }, { "has_double_jumped", (bool)p.hasDoubleJumped },
				{ "air_time_since_jump", p.airTimeSinceJump }, { "demoed", (bool)p.isDemoed },
				{ "contact", (bool)p.worldContact.hasContact }, { "contact_normal", V(p.worldContact.contactNormal) },
				{ "prev_action", Act8(p.prevAction) } });
		}
		snap["players"] = players;

		// Ball prediction as the port samples it (ticks 30/60/120/180/240); the reference gets the very same samples
		BallPredictor pred(GameMode::SOCCAR, 241);
		pred.Update(gs.ball, gs.lastTickCount);
		json preds = json::array();
		for (int ticks : { 30, 60, 120, 180, 240 }) {
			const BallState& b = pred.pred[ticks];
			preds.push_back({ { "pos", V(b.pos) }, { "vel", V(b.vel) }, { "angvel", V(b.angVel) },
				{ "fwd", V(b.rotMat.forward) }, { "right", V(b.rotMat.right) }, { "up", V(b.rotMat.up) } });
		}
		snap["preds"] = preds;

		json portG = json::array(), portC = json::array();
		for (int i = 0; i < n; i++) {
			std::vector<float> obs(galactus.obsSize);
			std::vector<uint8_t> mask(galactus.actions.size());
			galactus.BuildGalactusObs(obs.data(), gs.players[i], gs, pred);
			galactus.BuildMask(mask.data(), gs.players[i]);
			portG.push_back({ { "obs", obs }, { "mask", mask } });

			std::vector<float> obsC(chronos.obsSize);
			std::vector<uint8_t> maskC(chronos.actions.size());
			chronos.BuildChronosObs(obsC.data(), gs.players[i], gs);
			chronos.BuildMask(maskC.data(), gs.players[i]);
			portC.push_back({ { "obs", obsC }, { "mask", maskC }, { "mirrored", GGLBotOpponent::MirroredForChronos(gs.players[i]) } });
		}
		snap["port_galactus"] = portG;
		snap["port_chronos"] = portC;
		out["snapshots"].push_back(std::move(snap));
	};
	golden::GenerateStates(Snapshot);
	std::ofstream(outPath) << out.dump();
	std::cout << "wrote " << out["snapshots"].size() << " snapshots to " << outPath << std::endl;
	return 0;
}

// ------------------------------------------------------------------ judge
struct Tally {
	int netFail = 0;
	double worstLogit = 0;
	int rows = 0, obsFail = 0, maskFail = 0, parsedFail = 0, pickFail = 0, cudaFail = 0, shuffleChanged = 0;
	double worstObs = 0;
	std::string worstTag;
	std::vector<std::string> notes;
	void Note(const std::string& s) { if (notes.size() < 10) notes.push_back(s); }
};

static float MaxDiff(const std::vector<float>& a, const std::vector<float>& b, int* firstIdx = nullptr, float tol = 1e-5f) {
	float worst = 0;
	for (size_t i = 0; i < a.size(); i++) {
		float d = fabsf(a[i] - b[i]);
		if (d > tol && firstIdx && *firstIdx < 0)
			*firstIdx = (int)i;
		worst = std::max(worst, d);
	}
	return worst;
}

static int Judge(const char* statesPath, const char* refGPath, const char* refCPath, const char* modelGPath, const char* modelCPath) {
	json states = json::parse(std::ifstream(statesPath));
	json refs[2] = { json::parse(std::ifstream(refGPath)), json::parse(std::ifstream(refCPath)) };
	json refModels[2] = { json::parse(std::ifstream(modelGPath)), json::parse(std::ifstream(modelCPath)) };
	const bool cuda = torch::cuda::is_available();
	if (cuda) {
		at::globalContext().setAllowTF32CuBLAS(true); // as in training
		at::globalContext().setAllowTF32CuDNN(true);
	}

	DefaultAction ours;
	const char* names[2] = { "GalactusV2", "ChronosV2" };
	const Layout layouts[2] = { LAYOUT_GALACTUS, LAYOUT_CHRONOS };
	const char* portKeys[2] = { "port_galactus", "port_chronos" };
	bool allOk = true;

	for (int b = 0; b < 2; b++) {
		GGLBotOpponent cpu(b == 0 ? GGLBotKind::GALACTUS_V2 : GGLBotKind::CHRONOS_V2,
			b == 0 ? "../rlbot-run/galactus_v2" : "../rlbot-run/chronos_v2", torch::kCPU);
		std::unique_ptr<GGLBotOpponent> gpu;
		if (cuda)
			gpu = std::make_unique<GGLBotOpponent>(cpu.kind, b == 0 ? "../rlbot-run/galactus_v2" : "../rlbot-run/chronos_v2", torch::kCUDA);
		const Layout& L = layouts[b];
		const int numActions = (int)cpu.actions.size();
		Tally t;
		std::vector<float> allRefObs;
		std::vector<uint8_t> allRefMask;

		for (size_t si = 0; si < states["snapshots"].size(); si++) {
			auto& snap = states["snapshots"][si];
			auto& ref = refs[b]["snapshots"][si];
			const int n = snap["n"].get<int>();
			for (int i = 0; i < n; i++) {
				t.rows++;
				std::string tag = snap["tag"].get<std::string>() + " p" + std::to_string(i) + " (team " + std::to_string(snap["players"][i]["team"].get<int>()) + ")";
				auto portObs = snap[portKeys[b]][i]["obs"].get<std::vector<float>>();
				auto portMask = snap[portKeys[b]][i]["mask"].get<std::vector<uint8_t>>();
				bool mirrored = b == 1 && snap[portKeys[b]][i]["mirrored"].get<bool>();
				auto refObs = ref[i]["obs"].get<std::vector<float>>();
				auto refMask = ref[i]["mask"].get<std::vector<uint8_t>>();
				allRefObs.insert(allRefObs.end(), refObs.begin(), refObs.end());
				allRefMask.insert(allRefMask.end(), refMask.begin(), refMask.end());

				if (portObs.size() != refObs.size()) { t.obsFail++; t.Note(tag + " obs size " + std::to_string(portObs.size()) + " vs " + std::to_string(refObs.size())); continue; }

				// observation: prefix and suffix exactly, slots as sets
				int teamFrom = L.prefix + BLOCK, oppFrom = teamFrom + TEAMMATE_SLOTS * BLOCK;
				std::vector<float> pa(portObs.begin(), portObs.begin() + teamFrom), ra(refObs.begin(), refObs.begin() + teamFrom);
				int first = -1;
				float d = MaxDiff(pa, ra, &first);
				std::vector<float> ps(portObs.begin() + oppFrom + OPPONENT_SLOTS * BLOCK, portObs.end()), rs(refObs.begin() + oppFrom + OPPONENT_SLOTS * BLOCK, refObs.end());
				int firstS = -1;
				float dS = MaxDiff(ps, rs, &firstS);
				std::string where = first >= 0 ? "obs index " + std::to_string(first) : (firstS >= 0 ? "suffix index " + std::to_string(firstS) : "");
				for (int g = 0; g < 2 && where.empty(); g++) {
					int from = g == 0 ? teamFrom : oppFrom, cnt = g == 0 ? TEAMMATE_SLOTS : OPPONENT_SLOTS;
					auto pb = Blocks(portObs, L, from, cnt), rb = Blocks(refObs, L, from, cnt);
					for (auto& r : rb) {
						int best = -1; float bd = 1e9f;
						for (size_t k = 0; k < pb.size(); k++) {
							float dd = MaxDiff(pb[k], r);
							if (dd < bd) { bd = dd; best = (int)k; }
						}
						d = std::max(d, bd);
						if (bd > 1e-5f) { where = std::string(g == 0 ? "teammate" : "opponent") + " slot block (best match differs " + std::to_string(bd) + ")"; break; }
						pb.erase(pb.begin() + best);
					}
				}
				d = std::max(d, dS);
				if (d > t.worstObs) { t.worstObs = d; t.worstTag = tag; }
				if (!where.empty() || d > 1e-5f) { t.obsFail++; t.Note(tag + ": " + where + " max diff " + std::to_string(d)); }

				// mask
				if (portMask != refMask) { t.maskFail++; t.Note(tag + ": action mask differs"); }

				// every action row's controls, ours-after-mapping vs the original's ParseAction
				bool parsedOk = true;
				for (int a = 0; a < numActions && parsedOk; a++) {
					const Action& mine = ours.actions[cpu.ToOurAction(a, mirrored)];
					for (int k = 0; k < 8; k++)
						if (mine.begin()[k] != ref[i]["parsed"][a][k].get<float>()) { parsedOk = false; t.Note(tag + ": row " + std::to_string(a) + " control " + std::to_string(k) + " differs"); break; }
				}
				if (!parsedOk) t.parsedFail++;

				// the net's pick, on canonical copies of the two observations (CPU, then CUDA)
				auto cp = Canonical(portObs, L), cr = Canonical(refObs, L);
				std::vector<float> both(cp); both.insert(both.end(), cr.begin(), cr.end());
				std::vector<uint8_t> bothMask(portMask); bothMask.insert(bothMask.end(), refMask.begin(), refMask.end());
				auto picks = cpu.Pick(both, bothMask, 2);
				bool same = picks[0] == picks[1];
				const Action& pm = ours.actions[cpu.ToOurAction(picks[0], mirrored)];
				bool refSame = true;
				for (int k = 0; k < 8; k++)
					refSame &= pm.begin()[k] == ref[i]["parsed"][picks[1]][k].get<float>();
				if (!same || !refSame) { t.pickFail++; t.Note(tag + ": pick port " + std::to_string(picks[0]) + " ref " + std::to_string(picks[1])); }
				if (gpu) {
					auto gp = gpu->Pick(both, bothMask, 2);
					if (gp[0] != picks[0] || gp[1] != picks[1]) { t.cudaFail++; t.Note(tag + ": CUDA pick " + std::to_string(gp[0]) + "/" + std::to_string(gp[1]) + " vs CPU " + std::to_string(picks[0]) + "/" + std::to_string(picks[1])); }
				}

				// the originals' random slot order: how often does it move the pick? (informational)
				std::vector<float> shuf;
				std::vector<uint8_t> shufMask;
				for (int s = 0; s < 8; s++) {
					auto o = Shuffled(portObs, L);
					shuf.insert(shuf.end(), o.begin(), o.end());
					shufMask.insert(shufMask.end(), portMask.begin(), portMask.end());
				}
				auto sp = cpu.Pick(shuf, shufMask, 8);
				for (int p : sp)
					if (p != picks[0]) { t.shuffleChanged++; break; }
			}
		}

		// Network: the ORIGINAL bot's own model code (ref_model_*) vs our Model classes, same weights, same input rows
		{
			const int rowsAll = (int)allRefMask.size() / numActions;
			auto theirLogits = refModels[b]["logits"].get<std::vector<float>>();
			auto theirPicks = refModels[b]["picks"].get<std::vector<int64_t>>();
			if (refModels[b]["rows"].get<int>() != rowsAll) { t.netFail++; t.Note("network: row count differs"); }
			else {
				RG_NO_GRAD;
				auto tObs = torch::from_blob(allRefObs.data(), { rowsAll, cpu.obsSize }, torch::kFloat32).clone();
				auto feats = cpu.models["shared_head"]->Forward(tObs, false);
				auto logits = cpu.models["policy"]->Forward(feats, false).contiguous();
				for (int64_t k = 0; k < (int64_t)rowsAll * numActions; k++)
					t.worstLogit = std::max(t.worstLogit, (double)fabsf(logits.data_ptr<float>()[k] - theirLogits[k]));
				auto mine = cpu.Pick(allRefObs, allRefMask, rowsAll);
				for (int r = 0; r < rowsAll; r++)
					if (mine[r] != theirPicks[r]) {
						t.netFail++;
						t.Note("network: row " + std::to_string(r) + " ours " + std::to_string(mine[r]) + " original " + std::to_string(theirPicks[r]));
					}
			}
		}

		bool ok = !t.obsFail && !t.maskFail && !t.parsedFail && !t.pickFail && !t.cudaFail && !t.netFail && t.worstLogit < 1e-3;
		allOk &= ok;
		std::cout << "== " << names[b] << ": " << t.rows << " player observations\n"
			<< "   obs:    worst difference " << t.worstObs << " (" << t.worstTag << "); failures " << t.obsFail << "\n"
			<< "   mask:   failures " << t.maskFail << "\n"
			<< "   action rows (all " << numActions << " parsed controls): failures " << t.parsedFail << "\n"
			<< "   pick:   failures " << t.pickFail << " (CPU, canonical slot order)\n"
			<< "   CUDA:   " << (cuda ? "failures " + std::to_string(t.cudaFail) : std::string("no CUDA")) << "\n"
			<< "   network (original model code vs ours, same weights): max logit difference " << t.worstLogit << ", pick differences " << t.netFail << "\n"
			<< "   slot-shuffle sensitivity: " << t.shuffleChanged << " of " << t.rows << " observations change their pick under some random slot order (the originals do this too)\n";
		for (auto& s : t.notes)
			std::cout << "     " << s << "\n";
	}
	std::cout << "RESULT: " << (allOk ? "PASS" : "FAIL") << std::endl;
	return allOk ? 0 : 1;
}

int main(int argc, char** argv) {
	golden::Rng().seed(20261105);
	std::string mode = argc > 1 ? argv[1] : "";
	if (mode == "gen" && argc >= 3) {
		if (argc > 3)
			golden::Rng().seed(std::stoull(argv[3]));
		return Gen(argv[2]);
	}
	if (mode == "judge" && argc >= 7)
		return Judge(argv[2], argv[3], argv[4], argv[5], argv[6]);
	std::cerr << "usage: OpponentGoldenGGL gen <states.json> [seed] | judge <states.json> <ref_galactus.json> <ref_chronos.json> <model_galactus.json> <model_chronos.json>\n";
	return 2;
}
