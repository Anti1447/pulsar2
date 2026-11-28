#include "TestFramework.h"

#include "../GigaLearnCPP/src/private/GigaLearnCPP/GGLBotOpponent.h"

#include <RLGymCPP/EnvSet/EnvSet.h>
#include <RLGymCPP/Rewards/CommonRewards.h>
#include <RLGymCPP/Rewards/WavedashGainReward.h>
#include <RLGymCPP/ObsBuilders/AdvancedObsPadded.h>
#include <RLGymCPP/ActionParsers/DefaultAction.h>
#include <RLGymCPP/StateSetters/KickoffState.h>
#include <RLGymCPP/StateSetters/RandomState.h>
#include <RLGymCPP/StateSetters/BallNearCarState.h>
#include <RLGymCPP/StateSetters/AirDrillState.h>
#include <RLGymCPP/StateSetters/AirPlayState.h>
#include <RLGymCPP/StateSetters/CombinedState.h>
#include <RLGymCPP/TerminalConditions/NoTouchCondition.h>
#include <RLGymCPP/TerminalConditions/GoalScoreCondition.h>

#include <cstdio>
#include <filesystem>
#include <map>

using namespace RLGC;
using namespace GGL;

// WavedashReward audit on the CPU engine (RocketSim v3): plays a given 278-obs policy (the tick-4 Pulsar3 shapes) on the training
// reset mix with sampled actions and describes every payment. Off unless GGL_WAVEDASH_AUDIT names a checkpoint folder.
// The RocketGPU counterpart (same measures) is ~/pulsar_rg/pulsar/wavedash_audit.py.

TEST(WavedashAudit_policy_rollout) {
	const char* folder = std::getenv("GGL_WAVEDASH_AUDIT");
	if (!folder || !folder[0]) return;
	for (const char* path : { "collision_meshes", "../collision_meshes", "build/collision_meshes" })
		if (std::filesystem::exists(std::filesystem::path(path) / "soccar")) { RocketSim::Init(path); break; }
	const int arenas = 128, steps = std::getenv("GGL_WAVEDASH_STEPS") ? std::atoi(std::getenv("GGL_WAVEDASH_STEPS")) : 3600;

	EnvSetConfig cfg = {};
	cfg.numArenas = arenas;
	cfg.tickSkip = 4;
	cfg.actionDelay = 0;
	cfg.saveRewards = false;
	cfg.envCreateFn = [](int) {
		EnvCreateResult r = {};
		r.arena = Arena::Create(GameMode::SOCCAR);
		r.arena->AddCar(Team::BLUE);
		r.arena->AddCar(Team::ORANGE);
		r.actionParser = new DefaultAction();
		r.obsBuilder = new AdvancedObsPadded(3, true, 8);
		r.stateSetter = new CombinedState({
			{ new BallNearCarState(600, 900), 0.2f }, { new AirDrillState(), 0.2f }, { new AirPlayState(), 0.2f },
			{ new KickoffState(), 0.2f }, { new RandomState(true, true, false), 0.2f } });
		r.terminalConditions = { new NoTouchCondition(20), new GoalScoreCondition() };
		r.rewards = { { new WavedashReward(), 1000.f }, { new WavedashGainReward(), 1.f } }; // told apart by size below
		return r;
	};
	EnvSet env(cfg);
	GGLBotOpponent bot(GGLBotKind::PULSAR2, folder, torch::Device(torch::kCPU)); // same net shapes and obs as Pulsar3
	bot.temperature = 1.f;
	torch::manual_seed(7);

	const int n = env.state.numPlayers;
	struct Car { bool flipping = false, ground = true; int flipId = 0, paysThisFlip = 0, lastPay = -1000, flipAge = 0;
		// WavedashGainReward payments: where they happen and whether the car goes somewhere between them
		int lastGain = -1000; Vec lastGainPos = {}; float path = 0; std::vector<int> waitingGain, waitingAny; };
	std::vector<Car> cars(n);
	int64_t decisions = 0, pays = 0, flips = 0, flipping = 0, grounded = 0, groundedFlipping = 0, paidAgain = 0, quickRepeat = 0;
	std::map<int, int64_t> payAge, gaps, exactGap;
	std::vector<std::vector<std::string>> history(n);
	int tracesLeft = 0;
	double gainSum = 0;
	int64_t gainPays = 0;
	double payUpZ = 0, payZ = 0;
	int64_t onFloor = 0, onWall = 0, onCeiling = 0;
	std::vector<bool> all(n, true);
	// per surface (floor, wall/ramp, ceiling): gain payments and value
	int64_t gPays[3] = {}; double gValue[3] = {};
	int64_t chained = 0, loops = 0, gTouched = 0, gProbes = 0, aTouched = 0, aProbes = 0;
	double straightSum = 0, loopValue = 0, gSpeed = 0, allSpeed = 0;
	std::vector<uint8_t> fresh(arenas, 1);
	std::vector<int> actions(n, 0);
	for (int step = 0; step < steps; step++) {
		bot.Act(env.state.gameStates, all, fresh, actions);
		env.StepFirstHalf(false);
		env.StepSecondHalf(IList(actions.begin(), actions.end()), false);
		env.Sync();
		for (int a = 0; a < arenas; a++) {
			auto& gs = env.state.gameStates[a];
			const int start = env.state.arenaPlayerStartIdx[a];
			for (int j = 0; j < (int)gs.players.size(); j++) {
				const Player& p = gs.players[j];
				Car& c = cars[start + j];
				decisions++;
				flipping += p.isFlipping;
				grounded += p.isOnGround;
				groundedFlipping += p.isOnGround && p.isFlipping;
				const bool paid = env.state.rewards[start + j] > 500.f;
				const float gain = env.state.rewards[start + j] - (paid ? 1000.f : 0.f);
				if (gain > 1e-3f) { gainSum += gain; gainPays++; }
				c.path += p.vel.Length() * (4.f / 120.f);
				allSpeed += p.vel.Length();
				// was the ball touched within 3 s (90 decisions) of a gain payment, and of any decision (sampled)?
				auto settle = [&](std::vector<int>& waiting, int64_t& touched) {
					for (size_t w = 0; w < waiting.size();) {
						if (p.ballTouchedStep) { touched++; waiting.erase(waiting.begin() + w); }
						else if (step - waiting[w] >= 90) waiting.erase(waiting.begin() + w);
						else w++;
					}
				};
				settle(c.waitingGain, gTouched);
				settle(c.waitingAny, aTouched);
				if ((step + start + j) % 97 == 0) { c.waitingAny.push_back(step); aProbes++; }
				if (gain > 1e-3f) {
					const int surface = p.pos.z < 60 ? 0 : (p.pos.z > 1950 ? 2 : 1);
					gPays[surface]++; gValue[surface] += gain; gSpeed += p.vel.Length();
					c.waitingGain.push_back(step); gProbes++;
					if (step - c.lastGain <= 60) { // another payment within 2 s: how straight was the way between them?
						const float straight = c.path > 1.f ? (p.pos - c.lastGainPos).Length() / c.path : 0.f;
						chained++; straightSum += straight;
						if (straight < 0.5f) { loops++; loopValue += gain; }
					}
					c.lastGain = step; c.lastGainPos = p.pos; c.path = 0;
				}
				{
					char line[300];
					const Action& act = p.prevAction;
					std::snprintf(line, sizeof(line), "thr %+.0f pitch %+.0f yaw %+.0f roll %+.0f jump %.0f boost %.0f | z %6.1f vz %+7.1f speed %6.0f up.z %+.2f | ground %d flipping %d hasJumped %d hasFlipped %d%s",
						act.throttle, act.pitch, act.yaw, act.roll, act.jump, act.boost, p.pos.z, p.vel.z, p.vel.Length(), p.rotMat.up.z,
						(int)p.isOnGround, (int)p.isFlipping, (int)p.hasJumped, (int)p.hasFlipped, paid ? "  <== PAID" : "");
					auto& h = history[start + j];
					h.push_back(line);
					if (h.size() > 14) h.erase(h.begin());
				}
				if (paid) {
					payZ += p.pos.z; payUpZ += p.rotMat.up.z;
					if (p.pos.z < 60) onFloor++;
					else if (p.pos.z > 1950) onCeiling++;
					else onWall++; // landed with wheels well above the floor: a wall or a ramp
					exactGap[std::min(step - c.lastPay, 40)]++;
					if (tracesLeft > 0 && step > 600 * (4 - tracesLeft) && history[start + j].size() == 14 && step - c.lastPay < 20) {
						tracesLeft--;
						std::printf("WAVEDASH_TRACE step %d arena %d (one line per decision, 4 ticks)\n", step, a);
						for (auto& l : history[start + j]) std::printf("WAVEDASH_TRACE   %s\n", l.c_str());
					}
				}
				if (paid) {
					pays++;
					payAge[std::min(c.flipAge, 30)]++;
					if (c.paysThisFlip > 0) paidAgain++;
					c.paysThisFlip++;
					if (step - c.lastPay <= 30) quickRepeat++;
					gaps[std::min((step - c.lastPay) / 15, 20)]++;
					c.lastPay = step;
				}
				if (p.isFlipping && !c.flipping) { flips++; c.paysThisFlip = 0; c.flipAge = 0; }
				if (p.isFlipping) c.flipAge++;
				c.flipping = p.isFlipping;
				c.ground = p.isOnGround;
			}
			fresh[a] = env.state.terminals[a] != 0;
			if (fresh[a])
				for (int j = 0; j < (int)gs.players.size(); j++) cars[start + j] = Car();
		}
		env.Reset();
	}
	std::printf("WAVEDASH_AUDIT engine=v3-cpu folder=%s decisions=%lld pays_per_decision=%.6f flips_per_decision=%.6f pays_per_flip=%.4f "
		"flipping_frac=%.5f ground_frac=%.4f ground_and_flipping_frac=%.5f repeat_pay_same_flip_frac=%.4f pay_within_2s_of_last_frac=%.4f\n",
		folder, (long long)decisions, (double)pays / decisions, (double)flips / decisions, flips ? (double)pays / flips : 0.0,
		(double)flipping / decisions, (double)grounded / decisions, (double)groundedFlipping / decisions,
		pays ? (double)paidAgain / pays : 0.0, pays ? (double)quickRepeat / pays : 0.0);
	std::printf("WAVEDASH_AUDIT flip_age_at_pay(decisions:count)");
	for (auto& kv : payAge) std::printf(" %d:%lld", kv.first, (long long)kv.second);
	std::printf("\n");
	std::printf("WAVEDASH_AUDIT decisions_between_pays(gap:count)");
	for (auto& kv : exactGap) if (pays && kv.second * 50 > pays) std::printf(" %d:%.1f%%", kv.first, 100.0 * kv.second / pays);
	std::printf("\nWAVEDASH_GAIN pays_per_decision=%.6f mean_value_when_paid=%.4f value_per_decision=%.6f share_of_old_pays=%.4f", (double)gainPays / decisions, gainPays ? gainSum / gainPays : 0, gainSum / decisions, pays ? (double)gainPays / pays : 0);
	const int64_t g = gPays[0] + gPays[1] + gPays[2];
	const double gv = gValue[0] + gValue[1] + gValue[2];
	std::printf("\nGAIN_WHERE pays floor %.1f%% wall_or_ramp %.1f%% ceiling %.1f%% | value floor %.1f%% wall_or_ramp %.1f%% ceiling %.1f%%",
		g ? 100.0 * gPays[0] / g : 0, g ? 100.0 * gPays[1] / g : 0, g ? 100.0 * gPays[2] / g : 0,
		gv ? 100.0 * gValue[0] / gv : 0, gv ? 100.0 * gValue[1] / gv : 0, gv ? 100.0 * gValue[2] / gv : 0);
	std::printf("\nGAIN_CHAIN again_within_2s %.1f%% of pays | mean straightness (distance/path) %.3f | loops (straightness<0.5) %.1f%% of pays, %.1f%% of value",
		g ? 100.0 * chained / g : 0, chained ? straightSum / chained : 0, g ? 100.0 * loops / g : 0, gv ? 100.0 * loopValue / gv : 0);
	std::printf("\nGAIN_USE ball touched within 3 s: after a gain payment %.2f%% (n=%lld), after any decision %.2f%% (n=%lld) | speed at payment %.0f, all %.0f",
		gProbes ? 100.0 * gTouched / gProbes : 0, (long long)gProbes, aProbes ? 100.0 * aTouched / aProbes : 0, (long long)aProbes, g ? gSpeed / g : 0, allSpeed / decisions);
	std::printf("\nWAVEDASH_AUDIT where floor_z<60 %.2f%% wall_or_ramp %.2f%% ceiling %.2f%%", pays ? 100.0 * onFloor / pays : 0, pays ? 100.0 * onWall / pays : 0, pays ? 100.0 * onCeiling / pays : 0);
	std::printf("\nWAVEDASH_AUDIT at_pay mean_z=%.1f mean_up_z=%.3f\n", pays ? payZ / pays : 0, pays ? payUpZ / pays : 0);
}
