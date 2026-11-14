// REFERENCE side of the Galactus/Chronos golden test (checklist 1.2). Compiled against the ORIGINAL bots' own
// RLGymCPP sources (GalactusV2 @ 217324c / Chronos @ 4f8fdd2, fetched into ~/opponent_ref_src by
// tools/opponent_golden/build_ref.sh), never against ours. Reads the states golden_ggl wrote, rebuilds each into the
// ORIGINAL GameState/Player types, and runs the original observation builder, action mask and action parser:
//   ./ref_galactus states.json out.json     (build with -DREF_GALACTUS)
//   ./ref_chronos  states.json out.json     (build with -DREF_CHRONOS)
//   ./ref_dice     states.json out.json     (build with -DREF_DICE; 3v3 states are skipped)
#include <RLGymCPP/GameStates/GameState.h>
#include <RLGymCPP/GameStates/StateUtil.h>
#include <RLGymCPP/ActionParsers/DefaultAction.h>
#ifdef REF_GALACTUS
#include <RLGymCPP/ObsBuilders/GalactusOBS.h>
#else // Chronos and Dice both use their own AdvancedObsPadded
#include <RLGymCPP/ObsBuilders/AdvancedObsPadded.h>
#endif

#include <nlohmann/json.hpp>

#include <fstream>
#include <iostream>
#include <random>

using json = nlohmann::json;

// --- the one RocketSim function the original sources link against that is not in the Math/PhysState sources linked by
// build_ref.sh (it lives in Car.cpp). Verbatim RocketSim semantics.
bool RocketSim::CarState::HasFlipOrJump() const {
	return isOnGround || (!hasFlipped && !hasDoubleJumped && airTimeSinceJump < RLConst::DOUBLEJUMP_MAX_DELAY);
}
// ---

static Vec ToVec(const json& j) { return Vec(j[0].get<float>(), j[1].get<float>(), j[2].get<float>()); }

static RLGC::Action ToAction(const json& j) {
	RLGC::Action a;
	for (size_t i = 0; i < RLGC::Action::ELEM_AMOUNT; i++)
		a.begin()[i] = j[i].get<float>();
	return a;
}

static PhysState ToPhys(const json& pos, const json& vel, const json& angvel, const json* fwd, const json* right, const json* up) {
	PhysState p;
	p.pos = ToVec(pos);
	p.vel = ToVec(vel);
	p.angVel = ToVec(angvel);
	if (fwd) {
		p.rotMat.forward = ToVec(*fwd);
		p.rotMat.right = ToVec(*right);
		p.rotMat.up = ToVec(*up);
	}
	return p;
}

int main(int argc, char** argv) {
	if (argc < 3) {
		std::cerr << "usage: ref <states.json> <out.json>\n";
		return 2;
	}
	json in = json::parse(std::ifstream(argv[1]));
	json out;
	out["snapshots"] = json::array();

#ifdef REF_GALACTUS
	RLGC::GalactusOBS obsBuilder(3);
	RLGC::DefaultAction act;
	out["bot"] = "galactus";
#elif defined(REF_DICE)
	RLGC::AdvancedObsPadded obsBuilder(2);
	RLGC::DefaultAction act;
	out["bot"] = "dice";
#else
	RLGC::AdvancedObsPadded obsBuilder(3);
	RLGC::DefaultAction act(true);
	out["bot"] = "chronos";
#endif

	for (auto& snap : in["snapshots"]) {
		RLGC::GameState gs;
		const int n = snap["n"].get<int>();
#ifdef REF_DICE
		if (n > 4) { // Dice is built for at most 2 players per team: its builder aborts on a 3v3 state
			out["snapshots"].push_back(json::array());
			continue;
		}
#endif
		gs.ball.pos = ToVec(snap["ball"]["pos"]);
		gs.ball.vel = ToVec(snap["ball"]["vel"]);
		gs.ball.angVel = ToVec(snap["ball"]["angvel"]);

		const int P = RLGC::CommonValues::BOOST_LOCATIONS_AMOUNT;
		for (int i = 0; i < P; i++) {
			gs.boostPads[i] = snap["pads"][i].get<bool>();
			gs.boostPadsInv[i] = snap["pads_inv"][i].get<bool>();
			gs.boostPadTimers[i] = snap["pad_timers"][i].get<float>();
			gs.boostPadTimersInv[i] = snap["pad_timers_inv"][i].get<float>();
		}

		gs.players.resize(n);
		for (int i = 0; i < n; i++) {
			auto& j = snap["players"][i];
			RLGC::Player& p = gs.players[i];
			static_cast<PhysState&>(p) = ToPhys(j["pos"], j["vel"], j["angvel"], &j["fwd"], &j["right"], &j["up"]);
			p.index = i;
			p.carId = j["car_id"].get<uint32_t>();
			p.team = (Team)j["team"].get<int>();
			p.boost = j["boost"].get<float>();
			p.isOnGround = j["on_ground"].get<bool>();
			p.hasJumped = j["has_jumped"].get<bool>();
			p.hasFlipped = j["has_flipped"].get<bool>();
			p.hasDoubleJumped = j["has_double_jumped"].get<bool>();
			p.airTimeSinceJump = j["air_time_since_jump"].get<float>();
			p.isDemoed = j["demoed"].get<bool>();
			p.worldContact.hasContact = j["contact"].get<bool>();
			p.worldContact.contactNormal = ToVec(j["contact_normal"]);
			p.prevAction = ToAction(j["prev_action"]);
		}

#ifdef REF_GALACTUS
		RLGC::GalactusOBS::ExternalBallPredSamples samples;
		for (int s = 0; s < 5; s++) {
			auto& q = snap["preds"][s];
			samples[s] = ToPhys(q["pos"], q["vel"], q["angvel"], &q["fwd"], &q["right"], &q["up"]);
		}
		obsBuilder.SetExternalBallPredSamples(samples);
#endif

		json snapOut = json::array();
		for (int i = 0; i < n; i++) {
			const RLGC::Player& p = gs.players[i];
			RLGC::FList obs = obsBuilder.BuildObs(p, gs);
			std::vector<uint8_t> mask = act.GetActionMask(p, gs);
			json parsed = json::array();
			for (int a = 0; a < act.GetActionAmount(); a++) {
				RLGC::Action r = act.ParseAction(a, p, gs);
				parsed.push_back(json::array({ r.throttle, r.steer, r.pitch, r.yaw, r.roll, r.jump, r.boost, r.handbrake }));
			}
			snapOut.push_back({ { "obs", std::vector<float>(obs.begin(), obs.end()) }, { "mask", mask }, { "parsed", parsed } });
		}
		out["snapshots"].push_back(std::move(snapOut));
	}

	std::ofstream(argv[2]) << out.dump();
	std::cout << "wrote " << out["snapshots"].size() << " snapshots (" << out["bot"].get<std::string>() << ")\n";
	return 0;
}
