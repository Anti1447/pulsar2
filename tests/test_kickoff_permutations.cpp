#include "TestFramework.h"

#include <RLGymCPP/Rewards/CommonRewards.h>
#include <RLGymCPP/Rewards/ZeroSumReward.h>
#include <RLGymCPP/Gamestates/GameState.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <map>
#include <set>

using namespace RLGC;

// EVERY kickoff the engine can produce, for 1v1, 2v2 and 3v3, run through the real engine with KickoffRaceReward.
//
// How the engine builds a kickoff (RocketSim Arena::ResetToRandomKickoff): there are 5 spawn spots
//   (-2048,-2560) (2048,-2560) (-256,-3840) (256,-3840) (0,-4608), shuffled; blue car i takes spot order[i], orange car i takes the
//   same spot rotated 180 degrees about the centre. So a kickoff with n cars a side is an ORDERED choice of n of the 5 spots:
//   5 (1v1) + 20 (2v2) + 60 (3v3) = 85 permutations. (The ball is always at rest at the centre.) Which spot a given car INDEX gets
//   matters to the reward (player order decides who is first), which is why the order is enumerated, not just the set.
// The enumeration drives ResetToRandomKickoff(seed) over many seeds and asserts it found all 5, 20 and 60.
//
// For each permutation, four races are played with a straight rush (every car turned to face the ball, throttle and boost, no steering):
//   A  everybody rushes             -> a point-symmetric race: the touch should be a tie, and must be fair
//   B  blue rushes, orange idles    -> a delay kickoff: nobody is paid
//   C  blue rushes from step 0, orange starts 3 steps late    -> blue wins the race
//   D  orange rushes from step 0, blue starts 3 steps late    -> orange wins; its payment must equal C's blue payment (no team bias)
// Invariants asserted everywhere: the ZeroSum-wrapped rewards sum to 0 on every step; at most one step ever pays; the paid
// players are exactly players who touched the ball on that step; equal-step payments are equal; every payment is 1 - k/45 > 0 for a
// touch at step k <= 44.

namespace {
	constexpr int SPOTS = 5;
	const float SPOT_X[SPOTS] = { -2048, 2048, -256, 256, 0 };
	const float SPOT_Y[SPOTS] = { -2560, -2560, -3840, -3840, -4608 };

	void InitRocketSimKO() {
		static bool done = false;
		if (done) return;
		for (const char* path : { "collision_meshes", "../collision_meshes", "build/collision_meshes" })
			if (std::filesystem::exists(std::filesystem::path(path) / "soccar")) { RocketSim::Init(path); break; }
		done = true;
	}

	int SpotOf(const Vec& p) {
		for (int s = 0; s < SPOTS; s++)
			if (fabsf(p.x - SPOT_X[s]) < 1.f && fabsf(p.y - SPOT_Y[s]) < 1.f) return s;
		return -1;
	}

	struct Layout { int n; std::vector<int> spot; int seed; };

	Arena* MakeArena(int n) {
		Arena* arena = Arena::Create(GameMode::SOCCAR);
		for (int i = 0; i < n; i++) { arena->AddCar(Team::BLUE); arena->AddCar(Team::ORANGE); }
		return arena;
	}

	// All layouts for n cars a side, found by sweeping seeds
	std::vector<Layout> Enumerate(int n, int expected) {
		std::map<std::vector<int>, int> found;
		Arena* arena = MakeArena(n);
		for (long long i = 1; i <= 6000 && (int)found.size() < expected; i++) {
			int seed = (int)((i * 2654435761ull) % 2147483647ull);
			arena->ResetToRandomKickoff(seed);
			std::vector<int> spot;
			for (Car* c : arena->_cars)
				if (c->team == Team::BLUE) spot.push_back(SpotOf(c->GetState().pos));
			if (!found.count(spot)) found[spot] = seed;
		}
		delete arena;
		std::vector<Layout> out;
		for (auto& kv : found) out.push_back({ n, kv.first, kv.second });
		return out;
	}

	struct Race {
		int firstPayStep = -1;                 // the step on which anyone was paid (raw), -1 if never
		int firstTouchStep = -1;               // first step on which anyone touched the ball
		std::vector<float> rawAtPay, wrappedAtPay;
		std::vector<bool> touchedAtPay;
		int payingSteps = 0;                   // number of steps on which anything was paid (must be <= 1)
		float worstZeroSum = 0;                // max |sum of wrapped| over every step
		bool paidNonToucher = false;
	};

	// blueStart / orangeStart: first decision step on which that team's cars drive (before it they idle)
	Race Play(const Layout& L, int blueStart, int orangeStart, int steps = 60) {
		Arena* arena = MakeArena(L.n);
		arena->ResetToRandomKickoff(L.seed);
		// The engine spawns cars facing roughly (not exactly) the ball; a straight rush from the spawn yaw misses on the diagonal and offset
		// spots. Face every car at the ball (a rotation about z, so the point symmetry of the kickoff is kept) and drive straight.
		for (Car* c : arena->_cars) {
			auto st = c->GetState();
			st.rotMat = Angle(atan2f(-st.pos.y, -st.pos.x), 0, 0).ToRotMat();
			c->SetState(st);
		}
		GameState gs = GameState(arena);
		KickoffRaceReward raw; Reward* wrapped = new ZeroSumReward(new KickoffRaceReward(), 0.3f);
		raw.Reset(gs); wrapped->Reset(gs);
		Race res;
		int nc = (int)arena->_cars.size();
		for (int step = 1; step <= steps; step++) {
			for (Car* c : arena->_cars) {
				int start = c->team == Team::BLUE ? blueStart : orangeStart;
				c->controls = CarControls();
				if (step - 1 < start) continue; // idling
				c->controls.throttle = 1; c->controls.boost = true; // straight at the ball (the cars were turned to face it)
			}
			arena->Step(8);
			gs.UpdateFromArena(arena, std::vector<Action>(nc), NULL);
			wrapped->PreStep(gs); raw.PreStep(gs);
			std::vector<float> w = wrapped->GetAllRewards(gs, false), r = raw.GetAllRewards(gs, false);
			float sum = 0; for (float x : w) sum += x;
			res.worstZeroSum = std::max(res.worstZeroSum, fabsf(sum));
			bool anyTouch = false, anyPaid = false;
			for (int i = 0; i < nc; i++) { anyTouch |= gs.players[i].ballTouchedStep; anyPaid |= (r[i] != 0); }
			if (anyTouch && res.firstTouchStep < 0) res.firstTouchStep = step;
			if (anyPaid) {
				res.payingSteps++;
				if (res.firstPayStep < 0) {
					res.firstPayStep = step; res.rawAtPay = r; res.wrappedAtPay = w;
					for (int i = 0; i < nc; i++) { res.touchedAtPay.push_back(gs.players[i].ballTouchedStep); if (r[i] != 0 && !gs.players[i].ballTouchedStep) res.paidNonToucher = true; }
				}
			}
		}
		delete wrapped; delete arena;
		return res;
	}

	std::string Name(const Layout& L) {
		std::string s = std::to_string(L.n) + "v" + std::to_string(L.n) + " spots";
		for (int x : L.spot) s += " " + std::to_string(x);
		return s;
	}
}

TEST(KickoffPerms_the_engine_has_exactly_5_20_and_60_kickoff_layouts) {
	InitRocketSimKO();
	CHECK_EQ((int)Enumerate(1, 5).size(), 5);
	CHECK_EQ((int)Enumerate(2, 20).size(), 20);
	CHECK_EQ((int)Enumerate(3, 60).size(), 60);
	// and never more: the sweep stops at the expected count, so also check the spot sets are valid distinct spots
	for (auto& L : Enumerate(3, 60)) {
		std::set<int> distinct(L.spot.begin(), L.spot.end());
		CHECK_EQ((int)distinct.size(), 3);
		for (int s : L.spot) CHECK(s >= 0 && s < SPOTS);
	}
}

TEST(KickoffPerms_every_layout_every_race_is_zero_sum_pays_once_and_pays_only_touchers) {
	InitRocketSimKO();
	int layouts = 0;
	for (int n = 1; n <= 3; n++) {
		auto all = Enumerate(n, n == 1 ? 5 : n == 2 ? 20 : 60);
		for (auto& L : all) {
			layouts++;
			for (auto [bs, os] : { std::pair<int, int>{ 0, 0 }, { 0, 99 }, { 0, 3 }, { 3, 0 } }) {
				Race r = Play(L, bs, os);
				if (r.worstZeroSum > 1e-4f || r.payingSteps > 1 || r.paidNonToucher) {
					std::cout << "    VIOLATION in " << Name(L) << " race(" << bs << "," << os << ") zeroSum " << r.worstZeroSum
						<< " payingSteps " << r.payingSteps << " paidNonToucher " << r.paidNonToucher << std::endl;
				}
				CHECK(r.worstZeroSum < 1e-4f);
				CHECK(r.payingSteps <= 1);
				CHECK(!r.paidNonToucher);
				if (r.firstPayStep >= 0) { // equal-step payments are equal
					float v = 0; for (float x : r.rawAtPay) if (x != 0) { if (v == 0) v = x; CHECK_EQ(x, v); }
					CHECK_NEAR(v, 1.0f - (float)r.firstPayStep / 45.f, 1e-6f);
				}
			}
		}
	}
	CHECK_EQ(layouts, 85);
}

// A: everyone rushes. Report where the touches land and assert the window covers every real kickoff.
TEST(KickoffPerms_a_straight_rush_touches_inside_the_window_in_every_layout) {
	InitRocketSimKO();
	int worstStep = 0, bestStep = 999, ties = 0, total = 0, unpaid = 0;
	std::map<int, int> perN[4]; // first-touch step histogram per team size
	for (int n = 1; n <= 3; n++)
		for (auto& L : Enumerate(n, n == 1 ? 5 : n == 2 ? 20 : 60)) {
			Race r = Play(L, 0, 0);
			total++;
			if (r.firstTouchStep < 0) { unpaid++; std::cout << "    NO TOUCH in " << Name(L) << std::endl; continue; }
			worstStep = std::max(worstStep, r.firstTouchStep); bestStep = std::min(bestStep, r.firstTouchStep);
			perN[n][r.firstTouchStep]++;
			if (r.firstPayStep < 0) { unpaid++; std::cout << "    UNPAID touch at step " << r.firstTouchStep << " in " << Name(L) << " (uncontested or outside the window)" << std::endl; }
			int paid = 0; for (float x : r.rawAtPay) if (x != 0) paid++;
			if (paid >= 2) ties++;
		}
	std::cout << "    first-touch steps over " << total << " layouts: fastest " << bestStep << ", slowest " << worstStep << "; ties (2+ paid) " << ties << std::endl;
	for (int n = 1; n <= 3; n++) { std::cout << "    " << n << "v" << n << " histogram:"; for (auto& kv : perN[n]) std::cout << " step" << kv.first << "x" << kv.second; std::cout << std::endl; }
	CHECK_EQ(total, 85);
	CHECK_EQ(unpaid, 0);          // every real kickoff pays under a straight rush
	CHECK(bestStep >= 25);        // sanity: nothing arrives implausibly early
	CHECK(worstStep <= 44);       // inside the 45-step window
}

// A is point-symmetric: blue car i and orange car i start on rotated copies of the same spot. If they touch on the same step they must be
// paid the same, and the ZeroSum wrapper must give blue and orange car i exactly the same value (no team bias); if one is a step
// ahead (floating point can do that) only the earlier one is paid.
TEST(KickoffPerms_a_symmetric_race_is_fair_to_both_teams_in_every_layout) {
	InitRocketSimKO();
	int exactTies = 0, oneSided = 0;
	for (int n = 1; n <= 3; n++)
		for (auto& L : Enumerate(n, n == 1 ? 5 : n == 2 ? 20 : 60)) {
			Race r = Play(L, 0, 0);
			if (r.firstPayStep < 0) continue;
			// Everyone who touched the ball on the paying step must be paid (everyone is rushing, so every touch is contested): this is the
			// same-step rule. (Before the 2026-11-02 fix only the lowest player index was paid and this failed on every tie.)
			for (size_t i = 0; i < r.touchedAtPay.size(); i++)
				if (r.touchedAtPay[i]) CHECK(r.rawAtPay[i] != 0.0f);
			// players are in arena order: blue 0, orange 0, blue 1, orange 1, ...
			float blueSum = 0, orangeSum = 0, blueW = 0, orangeW = 0;
			for (int i = 0; i < 2 * n; i++) { ((i % 2 == 0) ? blueSum : orangeSum) += r.rawAtPay[i]; ((i % 2 == 0) ? blueW : orangeW) += r.wrappedAtPay[i]; }
			if (blueSum != 0 && orangeSum != 0) {
				exactTies++;
				CHECK_NEAR(blueSum, orangeSum, 1e-6f);          // equal raw payment to both teams
				CHECK_NEAR(blueW, 0.0f, 1e-4f);                // and the zero-sum team totals are both 0: nobody is favoured
				CHECK_NEAR(orangeW, 0.0f, 1e-4f);
			} else {
				oneSided++;                                      // one team was a step ahead: it alone is paid
				CHECK(blueSum != 0 || orangeSum != 0);
			}
		}
	std::cout << "    symmetric races: " << exactTies << " exact ties, " << oneSided << " one-team-a-step-ahead" << std::endl;
}

// B: a delay kickoff in every layout (orange never moves): nobody is paid, however fast blue is.
TEST(KickoffPerms_a_delay_kickoff_pays_nothing_in_every_layout) {
	InitRocketSimKO();
	int paidCount = 0, total = 0;
	for (int n = 1; n <= 3; n++)
		for (auto& L : Enumerate(n, n == 1 ? 5 : n == 2 ? 20 : 60)) {
			Race r = Play(L, 0, 99);
			total++;
			if (r.firstPayStep >= 0) { paidCount++; std::cout << "    PAID on a delay in " << Name(L) << " at step " << r.firstPayStep << std::endl; }
		}
	CHECK_EQ(total, 85);
	CHECK_EQ(paidCount, 0);
}

// C and D: a 3-step head start wins the race, and winning is worth the same to either team (no blue bias).
TEST(KickoffPerms_a_head_start_wins_and_pays_blue_and_orange_the_same_in_every_layout) {
	InitRocketSimKO();
	int total = 0, contestedPaid = 0, notPaid = 0;
	for (int n = 1; n <= 3; n++)
		for (auto& L : Enumerate(n, n == 1 ? 5 : n == 2 ? 20 : 60)) {
			Race c = Play(L, 0, 3), d = Play(L, 3, 0);
			total++;
			if (c.firstPayStep < 0 || d.firstPayStep < 0) { notPaid++; std::cout << "    head-start race unpaid in " << Name(L) << " (blue-early paid at " << c.firstPayStep << ", orange-early at " << d.firstPayStep << ")" << std::endl; continue; }
			contestedPaid++;
			// C: only blue players are paid; D: only orange players
			for (int i = 0; i < 2 * n; i++) { if (i % 2 == 1) CHECK_EQ(c.rawAtPay[i], 0.0f); else CHECK_EQ(d.rawAtPay[i], 0.0f); }
			float cb = 0, do_ = 0;
			for (int i = 0; i < 2 * n; i++) { cb += (i % 2 == 0) ? c.rawAtPay[i] : 0; do_ += (i % 2 == 1) ? d.rawAtPay[i] : 0; }
			CHECK(cb > 0.0f); CHECK(do_ > 0.0f);
			CHECK_EQ(c.firstPayStep, d.firstPayStep);         // the mirrored race takes exactly as long
			CHECK_NEAR(cb, do_, 1e-6f);                       // and pays exactly the same
		}
	std::cout << "    head-start races: " << contestedPaid << " of " << total << " paid, " << notPaid << " unpaid" << std::endl;
	CHECK_EQ(total, 85);
	CHECK_EQ(notPaid, 0);
}
