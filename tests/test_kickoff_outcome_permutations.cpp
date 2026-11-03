#include "TestFramework.h"

#include <RLGymCPP/Rewards/CommonRewards.h>
#include <RLGymCPP/Rewards/KickoffOutcomeReward.h>
#include <RLGymCPP/Gamestates/GameState.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <map>
#include <set>

using namespace RLGC;

// KickoffOutcomeReward on EVERY kickoff the engine can produce (the same 85 layouts as tests/test_kickoff_permutations.cpp: 5 + 20 + 60
// ordered choices of spawn spots for 1v1 / 2v2 / 3v3), with KickoffRaceReward run on the SAME simulation so the two can be compared.
// Races (cars turned to face the ball, throttle and boost, driven straight; "idle" = no controls):
//   A everyone rushes, B blue rushes / orange idles (a delay kickoff), C blue starts 3 steps early, D orange starts 3 steps early,
//   E nobody moves (standoff), F everyone rushes WITHOUT boost (slow but contesting)

namespace {
	constexpr int SPOTS = 5;
	const float SPOT_X[SPOTS] = { -2048, 2048, -256, 256, 0 };
	const float SPOT_Y[SPOTS] = { -2560, -2560, -3840, -3840, -4608 };

	void InitRS() {
		static bool done = false;
		if (done) return;
		for (const char* path : { "collision_meshes", "../collision_meshes", "build/collision_meshes" })
			if (std::filesystem::exists(std::filesystem::path(path) / "soccar")) { RocketSim::Init(path); break; }
		done = true;
	}
	int SpotOf(const Vec& p) {
		for (int s = 0; s < SPOTS; s++) if (fabsf(p.x - SPOT_X[s]) < 1.f && fabsf(p.y - SPOT_Y[s]) < 1.f) return s;
		return -1;
	}
	struct Layout { int n; std::vector<int> spot; int seed; };
	Arena* MakeArena(int n) {
		Arena* a = Arena::Create(GameMode::SOCCAR);
		for (int i = 0; i < n; i++) { a->AddCar(Team::BLUE); a->AddCar(Team::ORANGE); }
		return a;
	}
	std::vector<Layout> Enumerate(int n, int expected) {
		std::map<std::vector<int>, int> found;
		Arena* arena = MakeArena(n);
		for (long long i = 1; i <= 6000 && (int)found.size() < expected; i++) {
			int seed = (int)((i * 2654435761ull) % 2147483647ull);
			arena->ResetToRandomKickoff(seed);
			std::vector<int> spot;
			for (Car* c : arena->_cars) if (c->team == Team::BLUE) spot.push_back(SpotOf(c->GetState().pos));
			if (!found.count(spot)) found[spot] = seed;
		}
		delete arena;
		std::vector<Layout> out;
		for (auto& kv : found) out.push_back({ n, kv.first, kv.second });
		return out;
	}
	std::vector<Layout> All() {
		std::vector<Layout> all;
		for (int n = 1; n <= 3; n++) { auto v = Enumerate(n, n == 1 ? 5 : n == 2 ? 20 : 60); all.insert(all.end(), v.begin(), v.end()); }
		return all;
	}
	std::string Name(const Layout& L) { std::string s = std::to_string(L.n) + "v" + std::to_string(L.n) + " spots"; for (int x : L.spot) s += " " + std::to_string(x); return s; }

	struct Result {
		int firstTouchStep = -1, teamsTouchingFirst = 0;       // bit 1 blue, bit 2 orange
		int outPayStep = -1; std::vector<float> out;            // KickoffOutcome payment
		float ballY = 0, ballVy = 0; int lastTouchTeam = 0;     // state at the outcome payment (lastTouch: +1 blue, -1 orange)
		int racePayStep = -1; std::vector<float> race;          // KickoffRace payment
		int outPayments = 0;
		bool standoff = false;
	};

	Result Play(const Layout& L, int blueStart, int orangeStart, bool boost = true, int steps = 120) {
		Arena* arena = MakeArena(L.n);
		arena->ResetToRandomKickoff(L.seed);
		for (Car* c : arena->_cars) { auto st = c->GetState(); st.rotMat = Angle(atan2f(-st.pos.y, -st.pos.x), 0, 0).ToRotMat(); c->SetState(st); }
		GameState gs = GameState(arena);
		KickoffOutcomeReward outR; KickoffRaceReward raceR;
		KickoffOutcomeReward::GetAndResetMetrics();
		outR.Reset(gs); raceR.Reset(gs);
		Result res; int nc = (int)arena->_cars.size();
		for (int step = 1; step <= steps; step++) {
			for (Car* c : arena->_cars) {
				int start = c->team == Team::BLUE ? blueStart : orangeStart;
				c->controls = CarControls();
				if (step - 1 < start) continue;
				c->controls.throttle = 1; c->controls.boost = boost;
			}
			arena->Step(8);
			gs.UpdateFromArena(arena, std::vector<Action>(nc), NULL);
			outR.PreStep(gs); raceR.PreStep(gs);
			auto o = outR.GetAllRewards(gs, false), r = raceR.GetAllRewards(gs, false);
			if (res.firstTouchStep < 0) {
				for (int i = 0; i < nc; i++) if (gs.players[i].ballTouchedStep) { res.firstTouchStep = step; res.teamsTouchingFirst |= (gs.players[i].team == Team::BLUE) ? 1 : 2; }
			}
			bool anyO = false, anyR = false;
			for (int i = 0; i < nc; i++) { anyO |= (o[i] != 0); anyR |= (r[i] != 0); }
			if (anyO) {
				res.outPayments++;
				if (res.outPayStep < 0) {
					res.outPayStep = step; res.out = o; res.ballY = gs.ball.pos.y; res.ballVy = gs.ball.vel.y;
					for (auto& p : gs.players) if ((int)p.carId == gs.lastTouchCarID) res.lastTouchTeam = (p.team == Team::BLUE) ? 1 : -1;
				}
			}
			if (anyR && res.racePayStep < 0) { res.racePayStep = step; res.race = r; }
		}
		auto m = KickoffOutcomeReward::GetAndResetMetrics();
		res.standoff = m.standoffs > 0;
		delete arena;
		return res;
	}
	float TeamSum(const std::vector<float>& v, bool blue) { float s = 0; for (size_t i = 0; i < v.size(); i++) if ((i % 2 == 0) == blue) s += v[i]; return s; }
}

TEST(KickoffOutcomePerms_every_layout_every_race_pays_at_most_once) {
	InitRS();
	int total = 0;
	for (auto& L : All()) {
		total++;
		for (auto [bs, os, boost] : { std::tuple<int, int, bool>{ 0, 0, true }, { 0, 99, true }, { 0, 3, true }, { 3, 0, true }, { 99, 99, true }, { 0, 0, false } }) {
			Result r = Play(L, bs, os, boost);
			CHECK(r.outPayments <= 1);
			for (float x : r.out) CHECK(x >= -1.0f && x <= 1.0f);
		}
	}
	CHECK_EQ(total, 85);
}

// E: nobody moves for 3 s: everyone is charged: takers -0.6, support -0.3 (a pure standoff, not zero-sum: both teams lose)
TEST(KickoffOutcomePerms_a_standoff_charges_every_layout_minus_0p6_to_takers_and_minus_0p3_to_support) {
	InitRS();
	int total = 0;
	for (auto& L : All()) {
		total++;
		Result r = Play(L, 99, 99);
		CHECK(r.standoff);
		CHECK(r.outPayStep == 45 || r.outPayStep == 46);
		float mn = 0, mx = -9;
		for (float x : r.out) { mn = std::min(mn, x); mx = std::max(mx, x); }
		CHECK_NEAR(mn, -0.6f, 1e-6f);                       // somebody is a taker on each team
		CHECK(mx <= -0.29f && mx >= -0.61f);
		for (float x : r.out) CHECK(fabsf(x + 0.6f) < 1e-5f || fabsf(x + 0.3f) < 1e-5f);
		CHECK_EQ(r.racePayStep, -1);                        // KickoffRace pays nothing for a standoff
	}
	CHECK_EQ(total, 85);
}

// A: everyone rushes, a point-symmetric race. Reports what Outcome pays, and whether it is fair to the two teams.
TEST(KickoffOutcomePerms_a_symmetric_rush_is_a_tie_but_the_outcome_is_decided_by_player_order) {
	InitRS();
	int total = 0, doubles = 0, blueBetter = 0, orangeBetter = 0, even = 0, standoffs = 0, lastBlue = 0, lastOrange = 0;
	double sumBlue = 0, sumOrange = 0;
	for (auto& L : All()) {
		Result r = Play(L, 0, 0);
		total++;
		if (r.standoff) { standoffs++; continue; }
		if (r.teamsTouchingFirst == 3) doubles++;
		if (r.lastTouchTeam > 0) lastBlue++; else if (r.lastTouchTeam < 0) lastOrange++;
		float b = TeamSum(r.out, true), o = TeamSum(r.out, false);
		sumBlue += b; sumOrange += o;
		if (fabsf(b - o) < 1e-4f) even++; else if (b > o) blueBetter++; else orangeBetter++;
	}
	std::cout << "    all-rush races over " << total << " layouts: same-step double touch on " << doubles << "; standoffs " << standoffs
		<< "; last toucher at the payout: blue " << lastBlue << ", orange " << lastOrange
		<< "; outcome favours blue in " << blueBetter << ", orange in " << orangeBetter << ", even in " << even
		<< "; mean team sum blue " << sumBlue / total << " orange " << sumOrange / total << std::endl;
	CHECK_EQ(total, 85);
	CHECK_EQ(standoffs, 0);
	CHECK_EQ(doubles, 85);                    // every symmetric rush is a same-step double touch
	// CURRENT BEHAVIOUR (a finding): the possession term follows GameState::lastTouchCarID, which on a same-step touch is the HIGHEST player
	// index (orange), so a perfectly symmetric race is scored for orange in most layouts. If the possession rule is fixed this must change.
	CHECK(orangeBetter >= 60);
	CHECK(blueBetter <= 25);
}

// B: a delay kickoff. KickoffRace pays nothing (uncontested); KickoffOutcome measures the result 2 s after the touch.
TEST(KickoffOutcomePerms_a_delay_kickoff_is_scored_by_outcome_but_not_by_race) {
	InitRS();
	int total = 0, outcomePays = 0, racePays = 0, blueWins = 0;
	for (auto& L : All()) {
		Result r = Play(L, 0, 99);
		total++;
		if (r.racePayStep >= 0) racePays++;
		if (r.outPayStep >= 0 && !r.standoff) { outcomePays++; if (TeamSum(r.out, true) > TeamSum(r.out, false)) blueWins++; }
	}
	std::cout << "    delay kickoff (orange idle) over " << total << " layouts: KickoffRace paid " << racePays << ", KickoffOutcome paid " << outcomePays
		<< " (blue ahead in " << blueWins << ")" << std::endl;
	CHECK_EQ(racePays, 0);
	CHECK_EQ(outcomePays, total);
	CHECK_EQ(blueWins, total);                // the rusher is paid in every layout, even though the opponent never contested
}

// C / D: a 3-step head start. The mirrored races should be worth the same to the early team.
TEST(KickoffOutcomePerms_a_head_start_race_compared_between_the_two_rewards_and_the_two_teams) {
	InitRS();
	int total = 0, outBlueEarlyWins = 0, outOrangeEarlyWins = 0, mirrorEqual = 0, mirrorUnequal = 0, raceAgree = 0;
	double sumC = 0, sumD = 0;
	for (auto& L : All()) {
		Result c = Play(L, 0, 3), d = Play(L, 3, 0);
		total++;
		float cb = TeamSum(c.out, true) - TeamSum(c.out, false);   // blue minus orange when blue is early
		float dobl = TeamSum(d.out, false) - TeamSum(d.out, true); // orange minus blue when orange is early
		sumC += cb; sumD += dobl;
		if (cb > 0) outBlueEarlyWins++;
		if (dobl > 0) outOrangeEarlyWins++;
		if (fabsf(cb - dobl) < 1e-3f) mirrorEqual++; else mirrorUnequal++;
		bool raceBlue = c.racePayStep >= 0 && TeamSum(c.race, true) > 0, raceOr = d.racePayStep >= 0 && TeamSum(d.race, false) > 0;
		if (raceBlue && raceOr && cb > 0 && dobl > 0) raceAgree++;
	}
	std::cout << "    head-start races over " << total << " layouts: outcome says the EARLY team wins: blue-early " << outBlueEarlyWins << ", orange-early "
		<< outOrangeEarlyWins << "; mirrored races equal " << mirrorEqual << ", unequal " << mirrorUnequal << "; mean margin blue-early " << sumC / total
		<< " vs orange-early " << sumD / total << "; both rewards agree on the early team in " << raceAgree << std::endl;
	CHECK_EQ(total, 85);
}

// F: everyone rushes without boost. Quantify how many layouts the standoff timer (3.0 s) catches although everyone is racing.
TEST(KickoffOutcomePerms_a_no_boost_rush_trips_the_standoff_timer_in_some_layouts) {
	InitRS();
	int total = 0, standoffs = 0; std::map<int, int> touchHist;
	for (auto& L : All()) {
		Result r = Play(L, 0, 0, /*boost=*/false);
		total++;
		if (r.standoff) standoffs++;
		touchHist[r.firstTouchStep]++;
	}
	std::cout << "    no-boost rush over " << total << " layouts: standoff (-0.6 each, kickoff closed) in " << standoffs << "; first touch steps:";
	for (auto& kv : touchHist) std::cout << " " << kv.first << "x" << kv.second;
	std::cout << std::endl;
	CHECK_EQ(total, 85);
	CHECK(standoffs > 0 && standoffs < total);   // some, not all: the slow rushes touch at step 45 (just before the 3.0 s timer) or later
}
