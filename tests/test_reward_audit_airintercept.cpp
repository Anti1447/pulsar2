#include "TestFramework.h"
#include "RewardTestUtil.h"

#include <RLGymCPP/Rewards/CommonRewards.h>
#include <RLGymCPP/Rewards/ZeroSumReward.h>

#include <cmath>

using namespace RLGC;
using namespace RewardTest;

// Audit of AirInterceptPotentialReward (checklist item 2, weight 75, wrapped ZeroSum(TEAM_SPIRIT 0.3)).
//   Phi = ballFrac * carFrac * exp(-|ball - car| / 1410)
//   ballFrac = clamp((ball.z - 300) / 1300, 0, 1)      carFrac = clamp((car.z - 17) / 500, 0, 1)
//   r = gamma * Phi(ball now, car now) - Phi(ball previous, car previous)     (0 with no previous player; 0 if demoed now or before)
// Hand-computed anchors (an independent script of the same formula):
//   ball z 950, car z 267 (0.5 * 0.5), 683 uu apart: 0.25 * exp(-683/1410) = 0.15402
//   ball z 1600, car z 517, 1083 apart: 1 * 1 * exp(-1083/1410) = 0.46393
//   in contact range (150 apart) with both fractions 1: exp(-150/1410) = 0.89908, the most Phi can be
// Fixture: fabricated states (no engine). The engine side is in tools/reward_lab/packs/air_intercept.

namespace {
	constexpr float G = 0.9969f; // TRAIN_GAMMA

	struct AI { Vec ball, car; bool demoed = false; };

	float Phi(const AI& f) { return AirInterceptPotentialReward::Phi(f.ball, f.car); }

	// reward for a single blue car between two frames
	float R(const AI& a, const AI& b, float gamma) {
		GameState prev, cur; prev.deltaTime = cur.deltaTime = STEP_DT;
		for (GameState* g : { &prev, &cur }) {
			const AI& f = (g == &prev) ? a : b;
			Player p = MakePlayer(1, 0, Team::BLUE, f.car); p.isDemoed = f.demoed;
			g->players.push_back(p); g->ball.pos = f.ball;
		}
		cur.prev = &prev; cur.players[0].prev = &prev.players[0];
		AirInterceptPotentialReward r(gamma);
		return r.GetReward(cur.players[0], cur, false);
	}
}

TEST(AirIntercept_potential_anchors) {
	CHECK_NEAR(Phi({ Vec(0, 0, 950), Vec(0, 0, 267) }), 0.15402f, 1e-4f);
	CHECK_NEAR(Phi({ Vec(0, 0, 1600), Vec(0, 0, 517) }), 0.46393f, 1e-4f);
	CHECK_NEAR(Phi({ Vec(0, 0, 1600), Vec(0, 0, 1450) }), 0.89908f, 1e-4f); // 150 apart, both fractions clamped at 1
	CHECK_NEAR(Phi({ Vec(0, 0, 5000), Vec(0, 0, 3000) }) , expf(-2000.f / 1410.f), 1e-5f); // fractions clamp at 1 above the ranges
}

TEST(AirIntercept_first_step_and_missing_previous_player_are_zero) {
	GameState cur; cur.deltaTime = STEP_DT; cur.players.push_back(MakePlayer(1, 0, Team::BLUE, Vec(0, 0, 400))); cur.ball.pos = Vec(0, 0, 1000);
	AirInterceptPotentialReward r(G);
	CHECK_EQ(r.GetReward(cur.players[0], cur, false), 0.0f);
	GameState prev; cur.prev = &prev;
	CHECK_EQ(r.GetReward(cur.players[0], cur, false), 0.0f);
}

// "Parking under a dropping ball earns nothing": a grounded car has carFrac 0, so Phi is 0 whatever the ball does.
TEST(AirIntercept_a_grounded_car_under_a_high_ball_is_worth_exactly_zero) {
	CHECK_EQ(Phi({ Vec(0, 0, 1200), Vec(0, 0, 17) }), 0.0f);
	CHECK_EQ(R({ Vec(0, 0, 1200), Vec(0, 0, 17) }, { Vec(0, 0, 1100), Vec(0, 0, 17) }, G), 0.0f);
	CHECK_EQ(Phi({ Vec(0, 0, 1200), Vec(0, 0, 5) }), 0.0f); // below z 17 is clamped too
}

TEST(AirIntercept_a_low_ball_is_worth_zero_even_to_a_car_high_above_it) {
	CHECK_EQ(Phi({ Vec(0, 0, 300), Vec(0, 0, 500) }), 0.0f);
	CHECK_EQ(Phi({ Vec(0, 0, 150), Vec(0, 0, 500) }), 0.0f);
	CHECK(Phi({ Vec(0, 0, 301), Vec(0, 0, 500) }) > 0.0f); // the floor is exactly 300
}

TEST(AirIntercept_climbing_toward_a_high_ball_pays_the_potential_difference) {
	// ball fixed at z 1100; car 300 -> 500 straight below it (gamma 1): Phi = 0.6154 * carFrac * exp(-dist/1410)
	AI a{ Vec(0, 0, 1100), Vec(0, 0, 300) }, b{ Vec(0, 0, 1100), Vec(0, 0, 500) };
	CHECK_NEAR(R(a, b, 1.0f), Phi(b) - Phi(a), 1e-6f);
	CHECK_NEAR(R(a, b, 1.0f), 0.19094f, 1e-4f); // Phi 0.19749 -> 0.38843, a real visible gradient (x75 = +14.3)
}

// Farming check: jump toward the ball and whiff (come back down): the climb is refunded in full.
TEST(AirIntercept_a_whiff_refunds_the_climb) {
	AI ground{ Vec(0, 0, 1100), Vec(0, 0, 17) }, top{ Vec(0, 0, 1100), Vec(0, 0, 500) };
	CHECK_NEAR(R(ground, top, 1.0f) + R(top, ground, 1.0f), 0.0f, 1e-6f);
	float discounted = R(ground, top, G) + R(top, ground, G);
	CHECK(discounted < 0.0f && discounted > -0.003f); // only the (gamma - 1) * Phi leak
}

TEST(AirIntercept_discounted_sum_telescopes) {
	std::vector<AI> path;
	for (int i = 0; i <= 20; i++)
		path.push_back({ Vec(0.f, 100.f * i, 1300.f - 25.f * i), Vec(200.f * sinf(0.3f * i), 80.f * i, 17.f + 25.f * i * (i < 14 ? 1.f : 0.2f)) });
	double sum = 0, disc = 1;
	for (size_t i = 0; i + 1 < path.size(); i++) { sum += disc * R(path[i], path[i + 1], G); disc *= G; }
	double expect = disc * Phi(path.back()) - Phi(path.front());
	CHECK_NEAR((float)sum, (float)expect, 1e-5f);
}

TEST(AirIntercept_demoed_cars_and_the_respawn_step_pay_nothing) {
	AI alive{ Vec(0, 0, 1100), Vec(0, 0, 500) }, dead = alive, back = alive;
	dead.demoed = true; back.car = Vec(2000, 3000, 17);
	CHECK_EQ(R(alive, dead, G), 0.0f);  // demolished this step
	CHECK_EQ(R(dead, dead, G), 0.0f);   // stays demolished
	CHECK_EQ(R(dead, back, G), 0.0f);   // respawn teleport
}

// The reward reads the BALL too: when the ball falls away from a hovering car, the car is charged for it. That is the potential's
// design ("refunds the whiff") but it also charges for what the ball does by itself.
TEST(AirIntercept_a_ball_falling_away_from_a_hovering_car_charges_the_car) {
	AI a{ Vec(0, 0, 1000), Vec(0, 0, 517) }, b{ Vec(0, 0, 300), Vec(0, 0, 517) }; // ball drops to the 300 floor of the ball range
	CHECK_NEAR(R(a, b, 1.0f), -Phi(a), 1e-6f);
	CHECK(R(a, b, 1.0f) < -0.1f);
}

TEST(AirIntercept_hovering_near_a_high_ball_leaks_a_small_negative_per_step) {
	AI hover{ Vec(0, 0, 950), Vec(0, 0, 267) };      // Phi = 0.15402
	CHECK_NEAR(R(hover, hover, G), (G - 1.0f) * 0.15402f, 1e-6f);
}

TEST(AirIntercept_is_bounded_by_0p899_so_one_climb_is_worth_under_68_weighted) {
	float best = 0;
	for (float bz = 300; bz <= 1900; bz += 100)
		for (float cz = 17; cz <= 1900; cz += 100)
			for (float d : { 150.f, 400.f, 900.f }) {
				if (fabsf(bz - cz) > d) continue;
				float xy = sqrtf(d * d - (bz - cz) * (bz - cz));
				best = std::max(best, Phi({ Vec(xy, 0, bz), Vec(0, 0, cz) }));
			}
	CHECK(best <= 0.89908f + 1e-4f);
	CHECK(75.f * 0.89908f < 68.f);
}

// Wiring as in training: ZeroSum(AirIntercept, 0.3), 2v2. Blue car 0 climbs 0.19094 of potential; every other car is grounded
// (Phi 0 before and after).  car 0: 0.19094 * 0.85 = 0.1623; teammate 0.0286; each orange car -0.0955
TEST(AirIntercept_zero_sum_2v2_values_at_team_spirit_0p3) {
	GameState a, b; a.deltaTime = b.deltaTime = STEP_DT;
	for (GameState* g : { &a, &b }) {
		for (int i = 0; i < 4; i++) {
			Vec pos = (i == 0) ? Vec(0, 0, g == &a ? 300.f : 500.f) : Vec(500.f + 300.f * i, 0, 17.f);
			g->players.push_back(MakePlayer(1 + i, i, i < 2 ? Team::BLUE : Team::ORANGE, pos));
		}
		g->ball.pos = Vec(0, 0, 1100);
	}
	b.prev = &a; for (int i = 0; i < 4; i++) b.players[i].prev = &a.players[i];
	Reward* zs = new ZeroSumReward(new AirInterceptPotentialReward(1.0f), 0.3f);
	zs->Reset(a); zs->PreStep(b);
	auto v = zs->GetAllRewards(b, false);
	CHECK_NEAR(v[0], 0.1623f, 1e-3f);
	CHECK_NEAR(v[1], 0.0286f, 1e-3f);
	CHECK_NEAR(v[2], -0.0955f, 1e-3f);
	CHECK_NEAR(v[3], -0.0955f, 1e-3f);
	CHECK_NEAR(v[0] + v[1] + v[2] + v[3], 0.0f, 1e-5f);
	delete zs;
}
