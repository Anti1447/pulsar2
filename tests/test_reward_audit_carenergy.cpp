#include "TestFramework.h"
#include "RewardTestUtil.h"

#include <RLGymCPP/Rewards/CommonRewards.h>
#include <RLGymCPP/Rewards/ZeroSumReward.h>

#include <cmath>

using namespace RLGC;
using namespace RewardTest;

// Audit of CarEnergyPotentialReward (checklist item 2, weight 75, wrapped ZeroSum(TEAM_SPIRIT 0.3)).
//   Phi(car) = ( (speed/2300)^2 * 2300^2/2  +  650 * max(0, z - 17)  +  boostEnergy ) / (2300^2/2)
//            = (speed/2300)^2  +  650 * max(0, z - 17) / 2645000  +  (sqrt(0.01*boost + 0.01) - 0.1) / 0.905
//   r = gamma * Phi(now) - Phi(previous step)         (0 with no previous state; 0 on a demo transition or while demoed;
//                                                      Phi(now) = 0 on a goal step, a truncation keeps gamma * Phi(now))
// Hand-computed anchors (boost term b(x) = (sqrt(0.01x + 0.01) - 0.1) / 0.905):
//   b(0) = 0, b(100) = 0.999986, b(33.33) = 0.5370, b(50) = 0.67861, b(12) = 0.28791, b(62) = 0.76655, b(2.22) = 0.0878
//   speed 1150 -> 0.25, speed 2300 -> 1.0, 800 uu above the floor -> 650*800/2645000 = 0.19659
// Fixture: fabricated states, one or two cars. The engine side is in tools/reward_lab/packs/car_energy.

namespace {
	constexpr float G = 0.9969f; // TRAIN_GAMMA

	struct CarFrame { Vec pos = Vec(0, 0, 17), vel = Vec(0, 0, 0); float boost = 0; bool demoed = false; };

	// One car (blue, index 0), previous frame -> current frame
	float R(const CarFrame& a, const CarFrame& b, float gamma, bool goal = false) {
		GameState prev, cur; prev.deltaTime = cur.deltaTime = STEP_DT;
		for (GameState* g : { &prev, &cur }) {
			const CarFrame& f = (g == &prev) ? a : b;
			Player p = MakePlayer(1, 0, Team::BLUE, f.pos);
			p.vel = f.vel; p.boost = f.boost; p.isDemoed = f.demoed;
			g->players.push_back(p);
		}
		cur.prev = &prev; cur.players[0].prev = &prev.players[0];
		cur.goalScored = goal;
		CarEnergyPotentialReward r(gamma);
		return r.GetReward(cur.players[0], cur, false);
	}
	float Phi(const CarFrame& f) { Player p = MakePlayer(1, 0, Team::BLUE, f.pos); p.vel = f.vel; p.boost = f.boost; return CarEnergyPotentialReward::Phi(p); }
}

TEST(CarEnergy_potential_components_at_known_points) {
	CHECK_NEAR(Phi({ Vec(0, 0, 17), Vec(0, 0, 0), 0 }), 0.0f, 1e-6f);                      // at rest, empty, on the floor
	CHECK_NEAR(Phi({ Vec(0, 0, 17), Vec(1150, 0, 0), 0 }), 0.25f, 1e-5f);                 // (1150/2300)^2
	CHECK_NEAR(Phi({ Vec(0, 0, 17), Vec(2300, 0, 0), 0 }), 1.0f, 1e-5f);                  // supersonic
	CHECK_NEAR(Phi({ Vec(0, 0, 817), Vec(0, 0, 0), 0 }), 0.19659f, 1e-5f);                // 650 * 800 / 2645000
	CHECK_NEAR(Phi({ Vec(0, 0, 17), Vec(0, 0, 0), 100 }), 0.999986f, 1e-5f);              // a full tank is one supersonic KE
	CHECK_NEAR(Phi({ Vec(0, 0, 17), Vec(0, 0, 0), 33.33f }), 0.53696f, 1e-4f);
	CHECK_NEAR(Phi({ Vec(0, 0, 17), Vec(0, 0, 0), 50 }), 0.67861f, 1e-4f);
	CHECK_NEAR(Phi({ Vec(0, 0, 817), Vec(1150, 0, 0), 50 }), 0.25f + 0.19659f + 0.67861f, 1e-4f); // the three terms add
	CHECK_NEAR(Phi({ Vec(0, 0, 5), Vec(0, 0, 0), 0 }), 0.0f, 1e-6f);                      // below z 17 the height term is clamped at 0
}

TEST(CarEnergy_first_step_and_no_previous_player_are_zero) {
	GameState cur; cur.deltaTime = STEP_DT;
	cur.players.push_back(MakePlayer(1, 0, Team::BLUE, Vec(0, 0, 17)));
	CarEnergyPotentialReward r(G);
	CHECK_EQ(r.GetReward(cur.players[0], cur, false), 0.0f); // no state.prev
	GameState prev; cur.prev = &prev;
	CHECK_EQ(r.GetReward(cur.players[0], cur, false), 0.0f); // state.prev set, player.prev not
}

TEST(CarEnergy_kinetic_energy_gain_is_the_squared_speed_difference) {
	// 0 -> 1150 uu/s: +0.25 (gamma 1); 1150 -> 2300: +0.75 (the second half of the speed is worth 3x the first)
	CHECK_NEAR(R({}, { Vec(0, 0, 17), Vec(1150, 0, 0), 0 }, 1.0f), 0.25f, 1e-5f);
	CHECK_NEAR(R({ Vec(0, 0, 17), Vec(1150, 0, 0), 0 }, { Vec(0, 0, 17), Vec(2300, 0, 0), 0 }, 1.0f), 0.75f, 1e-5f);
}

// "This term must never tax aerials": converting speed into height is energy-neutral. 1300 uu/s straight up from z 17 reaches
// 1300^2 / (2*650) = 1300 uu higher with 0 speed; both states have Phi = (1300/2300)^2 = 0.31947.
TEST(CarEnergy_trading_speed_for_height_is_free) {
	float r = R({ Vec(0, 0, 17), Vec(0, 0, 1300), 0 }, { Vec(0, 0, 1317), Vec(0, 0, 0), 0 }, 1.0f);
	CHECK_NEAR(r, 0.0f, 1e-5f);
}

TEST(CarEnergy_boost_spent_without_gaining_speed_costs_the_fuel_price) {
	// One step of boosting burns 33.3/s * (8/120) s = 2.22 boost. At a full tank: b(100) - b(97.78) = 0.012272
	float full = R({ Vec(), Vec(), 100 }, { Vec(), Vec(), 97.78f }, 1.0f);
	CHECK_NEAR(full, -0.012272f, 1e-4f);
	// near empty the price per step is steeper but capped: draining the last 2.22 costs b(2.22) = 0.0878 (plain sqrt would cost 0.149)
	float low = R({ Vec(), Vec(), 2.22f }, { Vec(), Vec(), 0 }, 1.0f);
	CHECK_NEAR(low, -0.0878f, 2e-4f);
}

TEST(CarEnergy_boost_pickups_are_paid_and_small_ones_pay_most_when_low) {
	// big pad (+100) from empty: +0.999986; x75 = +75 (half a Goal) for one pickup
	CHECK_NEAR(R({ Vec(), Vec(), 0 }, { Vec(), Vec(), 100 }, 1.0f), 0.999986f, 1e-5f);
	// small pad (+12): from empty b(12) - b(0) = 0.28791 (x75 = 21.6); from half tank b(62) - b(50) = 0.0879 (x75 = 6.6)
	CHECK_NEAR(R({ Vec(), Vec(), 0 }, { Vec(), Vec(), 12 }, 1.0f), 0.28791f, 1e-4f);
	CHECK_NEAR(R({ Vec(), Vec(), 50 }, { Vec(), Vec(), 62 }, 1.0f), 0.0879f, 1e-4f);
}

TEST(CarEnergy_demo_transitions_and_demoed_cars_pay_nothing) {
	CarFrame alive{ Vec(0, 0, 17), Vec(1000, 0, 0), 50 }, dead = alive; dead.demoed = true; dead.vel = Vec(0, 0, 0); dead.boost = 33;
	CHECK_EQ(R(alive, dead, G), 0.0f);  // the step the car is demolished
	CHECK_EQ(R(dead, dead, G), 0.0f);   // while it stays demolished
	CarFrame respawn = alive; respawn.vel = Vec(0, 0, 0); respawn.boost = 33; respawn.pos = Vec(2000, 3000, 17);
	CHECK_EQ(R(dead, respawn, G), 0.0f); // the step it respawns (teleport with a fresh tank is not an action)
}

TEST(CarEnergy_goal_step_zeroes_the_next_potential_but_a_timeout_keeps_it) {
	CarFrame cruise{ Vec(0, 0, 17), Vec(1150, 0, 0), 0 };       // Phi = 0.25 before and after
	CHECK_NEAR(R(cruise, cruise, G, /*goal=*/true), -0.25f, 1e-5f);   // the goal step: gamma * 0 - 0.25
	CHECK_NEAR(R(cruise, cruise, G, /*goal=*/false), (G - 1.0f) * 0.25f, 1e-6f); // a truncation bootstraps: (gamma - 1) * 0.25
}

// Farming check: any loop of energy states nets zero (undiscounted) or slightly negative (discounted), never positive.
TEST(CarEnergy_an_energy_pump_loop_pays_zero_undiscounted_and_slightly_negative_discounted) {
	std::vector<CarFrame> loop = {
		{ Vec(0, 0, 17), Vec(0, 0, 0), 20 }, { Vec(0, 0, 17), Vec(1500, 0, 0), 20 }, { Vec(0, 0, 500), Vec(400, 0, 0), 20 },
		{ Vec(0, 0, 17), Vec(900, 0, 0), 60 }, { Vec(0, 0, 17), Vec(0, 0, 0), 20 } }; // ends where it began
	float undiscounted = 0, discounted = 0;
	for (size_t i = 0; i + 1 < loop.size(); i++) { undiscounted += R(loop[i], loop[i + 1], 1.0f); discounted += R(loop[i], loop[i + 1], G); }
	CHECK_NEAR(undiscounted, 0.0f, 1e-5f);
	CHECK(discounted < 0.0f && discounted > -0.02f);
}

TEST(CarEnergy_discounted_sum_telescopes) {
	std::vector<CarFrame> path;
	for (int i = 0; i <= 25; i++)
		path.push_back({ Vec(0, 0, 17.f + 300.f * fabsf(sinf(0.3f * i))), Vec(900.f * fabsf(sinf(0.2f * i)), 0, 100.f * sinf(0.5f * i)), 50.f + 40.f * sinf(0.4f * i) });
	double sum = 0, disc = 1;
	for (size_t i = 0; i + 1 < path.size(); i++) { sum += disc * R(path[i], path[i + 1], G); disc *= G; }
	double expect = disc * Phi(path.back()) - Phi(path.front());
	CHECK_NEAR((float)sum, (float)expect, 1e-4f);
}

// Wiring as in training: ZeroSum(CarEnergy, 0.3), 2v2. Blue car 0 gains +0.25 of energy, everyone else stays put.
//   car 0: 0.25 * 0.7 + 0.3 * 0.125 = 0.2125; its teammate: 0.3 * 0.125 = 0.0375; each orange car: -0.125
TEST(CarEnergy_zero_sum_2v2_values_at_team_spirit_0p3) {
	GameState a, b; a.deltaTime = b.deltaTime = STEP_DT;
	for (GameState* g : { &a, &b })
		for (int i = 0; i < 4; i++) {
			Player p = MakePlayer(1 + i, i, i < 2 ? Team::BLUE : Team::ORANGE, Vec(i * 100.f, 0, 17));
			if (g == &b && i == 0) p.vel = Vec(1150, 0, 0);
			g->players.push_back(p);
		}
	b.prev = &a; for (int i = 0; i < 4; i++) b.players[i].prev = &a.players[i];
	Reward* zs = new ZeroSumReward(new CarEnergyPotentialReward(1.0f), 0.3f);
	zs->Reset(a); zs->PreStep(b);
	auto v = zs->GetAllRewards(b, false);
	CHECK_NEAR(v[0], 0.2125f, 1e-5f);
	CHECK_NEAR(v[1], 0.0375f, 1e-5f);
	CHECK_NEAR(v[2], -0.125f, 1e-5f);
	CHECK_NEAR(v[3], -0.125f, 1e-5f);
	CHECK_NEAR(v[0] + v[1] + v[2] + v[3], 0.0f, 1e-5f);
	delete zs;
}

TEST(CarEnergy_the_potential_can_reach_about_2p5_so_its_value_range_exceeds_a_goal) {
	// supersonic (1.0) + full tank (1.0) + at the ceiling z 2044 (650 * 2027 / 2645000 = 0.498) = 2.498; x75 = 187 > Goal 150
	float top = Phi({ Vec(0, 0, 2044), Vec(2300, 0, 0), 100 });
	CHECK_NEAR(top, 2.498f, 2e-3f);
	CHECK(75.f * top > 150.f);
}
