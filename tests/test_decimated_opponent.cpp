#include "TestFramework.h"

#include "../GigaLearnCPP/src/private/GigaLearnCPP/DecimatedOpponent.h"

using namespace GGL;

// DecimatedOpponent (tickSkip-2 run): a bot trained at one decision per 8 ticks with a 7-tick latency, inside an env stepping 2 ticks with actionDelay 1.
// period = 4 env steps, emitStep = (7-1)/2 = 3: the bot decides at step 0, its action reaches the env at step 3 and is held through step 6.

namespace {
	// Inner bot whose k-th Act call (per arena) answers 100 + k, so the schedule of calls and releases is readable from the output.
	struct CountingBot : ExternalOpponent {
		std::vector<int> calls; int totalCalls = 0;
		CountingBot() { name = "fake"; }
		void BeginServe(int numPlayers) override { calls.assign(numPlayers, 0); }
		void Act(const std::vector<RLGC::GameState>& states, const std::vector<bool>& isOld, const std::vector<uint8_t>&, std::vector<int>& out) override {
			totalCalls++;
			for (size_t g = 0; g < isOld.size(); g++) if (isOld[g]) out[g] = 100 + calls[g]++;
		}
	};
	std::vector<RLGC::GameState> TwoArenas1v1() {
		std::vector<RLGC::GameState> s(2);
		for (auto& g : s) g.players.resize(2);
		return s;
	}
}

TEST(Decimated_decides_every_fourth_step_and_releases_on_the_fourth) {
	auto inner = std::make_shared<CountingBot>();
	DecimatedOpponent d(inner, 2, 1, 8, 7);
	CHECK_EQ(d.period, 4);
	CHECK_EQ(d.emitStep, 3);
	auto states = TwoArenas1v1();
	d.BeginServe(4);
	std::vector<bool> isOld = { false, true, false, true }; // the bot plays player 1 of each arena
	std::vector<int> seen;
	for (int step = 0; step < 12; step++) {
		std::vector<uint8_t> terms = { (uint8_t)(step == 0), (uint8_t)(step == 0) }; // step 0 = a fresh episode in both arenas
		std::vector<int> out(4, -1);
		d.Act(states, isOld, terms, out);
		seen.push_back(out[1]);
		CHECK_EQ(out[0], -1); // players the bot does not control are left alone
	}
	// fresh episode: the first decision (100) is released at once; then decisions 101 (made at step 4) and 102 (step 8) reach the env 3 steps later
	const int want[12] = { 100, 100, 100, 100, 100, 100, 100, 101, 101, 101, 101, 102 };
	for (int i = 0; i < 12; i++) CHECK_EQ(seen[i], want[i]);
	CHECK_EQ(inner->totalCalls, 3); // 3 calls for 12 steps: the bot is not asked on the other 9
}

TEST(Decimated_phases_are_per_arena_and_a_reset_restarts_one) {
	auto inner = std::make_shared<CountingBot>();
	DecimatedOpponent d(inner, 2, 1, 8, 7);
	auto states = TwoArenas1v1();
	d.BeginServe(4);
	std::vector<bool> isOld = { false, true, false, true };
	std::vector<int> a0, a1;
	for (int step = 0; step < 8; step++) {
		// arena 0 starts at step 0; arena 1 is reset at step 2 (a goal), so its decisions fall 2 steps later
		std::vector<uint8_t> terms = { (uint8_t)(step == 0), (uint8_t)(step == 0 || step == 2) };
		std::vector<int> out(4, -1);
		d.Act(states, isOld, terms, out);
		a0.push_back(out[1]); a1.push_back(out[3]);
	}
	const int w0[8] = { 100, 100, 100, 100, 100, 100, 100, 101 };
	const int w1[8] = { 100, 100, 101, 101, 101, 101, 101, 101 }; // restarted at step 2: first decision (101, its second) released at once; next one decides at step 6
	for (int i = 0; i < 8; i++) CHECK_EQ(a0[i], w0[i]);
	for (int i = 0; i < 8; i++) CHECK_EQ(a1[i], w1[i]);
}
