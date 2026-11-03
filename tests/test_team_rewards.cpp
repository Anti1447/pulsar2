#include "TestFramework.h"

#include <algorithm>
#include <cmath>
#include <RLGymCPP/Rewards/CommonRewards.h>
#include <RLGymCPP/Rewards/ZeroSumReward.h>
#include <RLGymCPP/Gamestates/GameState.h>

using namespace RLGC;

// Team-play reward semantics (4.0): TEAM_SPIRIT sharing in ZeroSumReward and the
// team-closest BallProximityPotentialReward. The 1v1-equivalence tests pin the invariant
// the live 4.0 resume relies on: team changes must be inert on 1v1 rows.

static constexpr float GAMMA = 0.9985f;
static constexpr float SCALE = BallProximityPotentialReward::LIU_DIST_SCALE;

static Player MakePlayer(int index, Team team, Vec pos, bool demoed = false) {
	Player p = {};
	p.index = index;
	p.carId = index + 1;
	p.team = team;
	p.pos = pos;
	p.isDemoed = demoed;
	p.isOnGround = true;
	return p;
}

// Builds a prev->cur state pair with players' prev pointers wired. Positions are
// (prevPos, curPos) per player; teams alternate with the given per-player team list.
struct StatePair {
	// Held by unique_ptr-ish ownership: the test scope keeps this alive
	GameState prev, cur;

	StatePair(const std::vector<Team>& teams,
		const std::vector<Vec>& prevPos, const std::vector<Vec>& curPos,
		Vec prevBall, Vec curBall,
		const std::vector<bool>& prevDemoed = {}, const std::vector<bool>& curDemoed = {}) {
		for (int i = 0; i < (int)teams.size(); i++) {
			bool pd = i < (int)prevDemoed.size() ? prevDemoed[i] : false;
			bool cd = i < (int)curDemoed.size() ? curDemoed[i] : false;
			prev.players.push_back(MakePlayer(i, teams[i], prevPos[i], pd));
			cur.players.push_back(MakePlayer(i, teams[i], curPos[i], cd));
		}
		prev.ball.pos = prevBall;
		cur.ball.pos = curBall;
		cur.prev = &prev;
		for (int i = 0; i < (int)teams.size(); i++)
			cur.players[i].prev = &prev.players[i];
	}
};

// Single-car potential. Deliberately RESTATED here rather than calling TeamPhi: an oracle
// that called the implementation under test would be circular, and the point of these tests
// is that the team-closest form reduces to the per-player form at n=1.
//
// MUST TRACK BallProximityPotentialReward::TeamPhi. It drifted once already: 5.0 added the
// far-field linear term (CommonRewards.h:105-108 / PULSAR5.md - the pure exp saturates past
// ~2000uu, the measured "gradient desert" behind ~9% dead far-field frames) and this oracle
// kept the pure exp, so all four BallProx tests failed on a clean tree from 2026-07-22 until
// 2026-07-25. If TeamPhi's shape changes again, change it here in the same commit.
static float Phi(Vec ball, Vec car) {
	float d = (ball - car).Length();
	return expf(-d / SCALE) + 0.08f * (d < 12000.f ? 1.f - d / 12000.f : 0.f);
}

// The per-player potential difference, for the 1v1 equivalence check
static float PerPlayerProx(Vec prevCar, Vec curCar, Vec prevBall, Vec curBall) {
	return GAMMA * Phi(curBall, curCar) - Phi(prevBall, prevCar);
}

// Team potential oracle for 2+ cars (2026-11-03 spacing-aware form), restated here for the same reason as Phi: weights (1, 1/2, 1/4) cut by a
// smoothstep crowding discount on the ball-distance gap between rank neighbours (1 at <= 500, 0 at >= 1600), scaled so the largest potential is 1.08 per mode.
static float TeamOracle(Vec ball, std::vector<Vec> cars, int total) {
	std::vector<float> d;
	for (auto& c : cars) d.push_back((ball - c).Length());
	std::sort(d.begin(), d.end());
	int m = (int)d.size();
	auto b = [](int k) { return powf(0.5f, (float)k); };
	auto crowd = [](float gap) { float t = fminf(1.f, fmaxf(0.f, (gap - 500.f) / 1100.f)); return 1.f - t * t * (3.f - 2.f * t); };
	float w = b(m - 1), sum = w * Phi(Vec(d[m - 1], 0, 0), Vec(0, 0, 0));
	for (int k = m - 2; k >= 0; k--) { w = b(k) - (b(k) - w) * crowd(d[k + 1] - d[k]); sum += w * Phi(Vec(d[k], 0, 0), Vec(0, 0, 0)); }
	const float maxSum = total <= 1 ? 1.08f : total == 2 ? 1.27815f : 1.32016f;   // the largest un-normalised sum per team size
	return sum * 1.08f / maxSum;
}

TEST(BallProx_1v1_identical_to_per_player_form) {
	StatePair sp(
		{ Team::BLUE, Team::ORANGE },
		{ Vec(-2000, -2000, 17), Vec(1500, 2500, 17) },
		{ Vec(-1500, -1600, 17), Vec(1400, 2600, 17) },
		Vec(0, 0, 93), Vec(100, -50, 93));

	BallProximityPotentialReward r(GAMMA);
	for (int i = 0; i < 2; i++) {
		float expected = PerPlayerProx(sp.prev.players[i].pos, sp.cur.players[i].pos,
			sp.prev.ball.pos, sp.cur.ball.pos);
		CHECK_NEAR(r.GetReward(sp.cur.players[i], sp.cur, false), expected, 1e-6f);
	}
}

TEST(BallProx_1v1_demo_guard_matches_old_behavior) {
	// Demo transition (alive -> demoed): old code returned 0; team form must too
	StatePair spTrans(
		{ Team::BLUE, Team::ORANGE },
		{ Vec(-2000, 0, 17), Vec(2000, 0, 17) },
		{ Vec(-2000, 0, 17), Vec(2000, 0, 17) },
		Vec(0, 0, 93), Vec(0, 0, 93),
		{ false, false }, { true, false });
	BallProximityPotentialReward r(GAMMA);
	CHECK_NEAR(r.GetReward(spTrans.cur.players[0], spTrans.cur, false), 0.f, 1e-9f);

	// Steady demoed (demoed both steps): old code returned 0; team form diffs an
	// empty-team Phi (0) against 0
	StatePair spSteady(
		{ Team::BLUE, Team::ORANGE },
		{ Vec(-2000, 0, 17), Vec(2000, 0, 17) },
		{ Vec(-2000, 0, 17), Vec(2000, 0, 17) },
		Vec(0, 0, 93), Vec(500, 0, 93),
		{ true, false }, { true, false });
	CHECK_NEAR(r.GetReward(spSteady.cur.players[0], spSteady.cur, false), 0.f, 1e-9f);
	// ...while the opponent (alive, ball moved) still gets its normal team diff
	float expOpp = PerPlayerProx(spSteady.prev.players[1].pos, spSteady.cur.players[1].pos,
		spSteady.prev.ball.pos, spSteady.cur.ball.pos);
	CHECK_NEAR(r.GetReward(spSteady.cur.players[1], spSteady.cur, false), expOpp, 1e-6f);
}

TEST(BallProx_2v2_second_man_counts_at_half_when_spaced_and_the_closest_man_still_pays_most) {
	// Blue 0 is the closest man (803 uu) and static; blue 2 (second man, 3500 -> 3000 uu away: gap > 1600, no crowding) moves 500uu closer.
	// Since 2026-11-03 the second man's distance counts at weight 1/2, so his approach pays (it was free in the closest-car form).
	Vec B(0, 0, 93);
	StatePair sp(
		{ Team::BLUE, Team::ORANGE, Team::BLUE, Team::ORANGE },
		{ Vec(0, -800, 17), Vec(0, 2000, 17), Vec(0, -3500, 17), Vec(0, 3500, 17) },
		{ Vec(0, -800, 17), Vec(0, 2000, 17), Vec(0, -3000, 17), Vec(0, 3500, 17) },
		B, B);

	BallProximityPotentialReward r(GAMMA);
	float expected = GAMMA * TeamOracle(B, { Vec(0, -800, 17), Vec(0, -3000, 17) }, 2) - TeamOracle(B, { Vec(0, -800, 17), Vec(0, -3500, 17) }, 2);
	float holdTaxOnly = (GAMMA - 1.f) * TeamOracle(B, { Vec(0, -800, 17), Vec(0, -3500, 17) }, 2);
	CHECK(expected > holdTaxOnly + 1e-4f);                              // the second man's approach is now worth something
	for (int i : { 0, 2 })
		CHECK_NEAR(r.GetReward(sp.cur.players[i], sp.cur, false), expected, 1e-6f);

	// And the closest man advancing pays more than the second man advancing by the same distance, again equal for both teammates
	StatePair sp2(
		{ Team::BLUE, Team::ORANGE, Team::BLUE, Team::ORANGE },
		{ Vec(0, -800, 17), Vec(0, 2000, 17), Vec(0, -3500, 17), Vec(0, 3500, 17) },
		{ Vec(0, -600, 17), Vec(0, 2000, 17), Vec(0, -3500, 17), Vec(0, 3500, 17) },
		B, B);
	float expected2 = GAMMA * TeamOracle(B, { Vec(0, -600, 17), Vec(0, -3500, 17) }, 2) - TeamOracle(B, { Vec(0, -800, 17), Vec(0, -3500, 17) }, 2);
	CHECK(expected2 > expected);
	CHECK_NEAR(r.GetReward(sp2.cur.players[0], sp2.cur, false), expected2, 1e-6f);
	CHECK_NEAR(r.GetReward(sp2.cur.players[2], sp2.cur, false), expected2, 1e-6f);
}

TEST(BallProx_2v2_teammate_demo_skips_step_only) {
	// Blue 2 gets demoed this step: blue rows return 0 (teleport guard), orange rows
	// (no demo-state change on their team) still pay
	StatePair sp(
		{ Team::BLUE, Team::ORANGE, Team::BLUE, Team::ORANGE },
		{ Vec(0, -800, 17), Vec(0, 2000, 17), Vec(0, -3500, 17), Vec(0, 3500, 17) },
		{ Vec(0, -800, 17), Vec(0, 1800, 17), Vec(0, -3500, 17), Vec(0, 3500, 17) },
		Vec(0, 0, 93), Vec(0, 0, 93),
		{ false, false, false, false }, { false, false, true, false });

	BallProximityPotentialReward r(GAMMA);
	CHECK_NEAR(r.GetReward(sp.cur.players[0], sp.cur, false), 0.f, 1e-9f);
	CHECK_NEAR(r.GetReward(sp.cur.players[2], sp.cur, false), 0.f, 1e-9f);
	Vec B(0, 0, 93);
	float expOrange = GAMMA * TeamOracle(B, { Vec(0, 1800, 17), Vec(0, 3500, 17) }, 2) - TeamOracle(B, { Vec(0, 2000, 17), Vec(0, 3500, 17) }, 2);
	CHECK_NEAR(r.GetReward(sp.cur.players[1], sp.cur, false), expOrange, 1e-6f);
	CHECK_NEAR(r.GetReward(sp.cur.players[3], sp.cur, false), expOrange, 1e-6f);
}

TEST(BallProx_2v2_zero_sum_under_wrapper) {
	StatePair sp(
		{ Team::BLUE, Team::ORANGE, Team::BLUE, Team::ORANGE },
		{ Vec(-500, -900, 17), Vec(300, 1200, 17), Vec(-2500, -3000, 17), Vec(2000, 3300, 17) },
		{ Vec(-400, -700, 17), Vec(350, 1000, 17), Vec(-2400, -2800, 17), Vec(2100, 3200, 17) },
		Vec(0, 0, 93), Vec(80, 120, 93));

	// Any teamSpirit: teammates already receive identical raw values, so the wrapped
	// result must be exactly Psi = dPhi_ownTeam - dPhi_oppTeam, summing to 0 over all 4
	for (float ts : { 0.0f, 0.3f, 1.0f }) {
		Reward* zs = new ZeroSumReward(new BallProximityPotentialReward(GAMMA), ts);
		auto rews = zs->GetAllRewards(sp.cur, false);
		float sum = 0;
		for (float v : rews)
			sum += v;
		CHECK_NEAR(sum, 0.f, 1e-5f);
		CHECK_NEAR(rews[0], rews[2], 1e-6f); // teammates identical
		CHECK_NEAR(rews[1], rews[3], 1e-6f);
		CHECK_NEAR(rews[0], -rews[1], 1e-6f); // antisymmetric across teams
		delete zs;
	}
}

TEST(ZeroSum_team_spirit_2v2_share_math) {
	// One blue player touches; TouchBallReward pays them 1. At ts=0.3:
	// toucher = 1*0.7 + 0.5*0.3 = 0.85, teammate = 0.5*0.3 = 0.15, each orange = -0.5
	GameState gs = {};
	gs.players.push_back(MakePlayer(0, Team::BLUE, Vec(0, 0, 17)));
	gs.players.push_back(MakePlayer(1, Team::ORANGE, Vec(0, 500, 17)));
	gs.players.push_back(MakePlayer(2, Team::BLUE, Vec(0, -500, 17)));
	gs.players.push_back(MakePlayer(3, Team::ORANGE, Vec(0, 1000, 17)));
	gs.players[0].ballTouchedStep = true;

	Reward* zs = new ZeroSumReward(new TouchBallReward(), 0.3f);
	auto rews = zs->GetAllRewards(gs, false);
	CHECK_NEAR(rews[0], 0.85f, 1e-6f);
	CHECK_NEAR(rews[2], 0.15f, 1e-6f);
	CHECK_NEAR(rews[1], -0.5f, 1e-6f);
	CHECK_NEAR(rews[3], -0.5f, 1e-6f);
	float sum = rews[0] + rews[1] + rews[2] + rews[3];
	CHECK_NEAR(sum, 0.f, 1e-6f);
	delete zs;

	// ts=0 sanity (the 1v1-era config): all credit personal
	Reward* zs0 = new ZeroSumReward(new TouchBallReward(), 0.0f);
	auto rews0 = zs0->GetAllRewards(gs, false);
	CHECK_NEAR(rews0[0], 1.0f, 1e-6f);
	CHECK_NEAR(rews0[2], 0.0f, 1e-6f);
	delete zs0;
}
