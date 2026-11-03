#include "TestFramework.h"
#include "RewardTestUtil.h"

#include <RLGymCPP/Rewards/CommonRewards.h>
#include <RLGymCPP/Rewards/ZeroSumReward.h>

#include <cmath>
#include <filesystem>

using namespace RLGC;
using namespace RewardTest;

// Audit of WallJumpToBallReward (checklist item 2, weight 60, wrapped ZeroSum(TEAM_SPIRIT 0.3)).
// A per-player latch opens the step a car is airborne having been ON A WALL the step before, stays open through the whole aerial
// (a ball touch does NOT close it), and closes on any ground/wall contact or demo.
//   Phi = exp(-|ball - car| / 1410) while the latch is open, else 0          r = gamma * Phi(now) - Phi(previous step, with its own latch)
//   wall = on the ground with a world contact whose normal has |z| < 0.7071068 (the engine's ground/wall split)
// Hand-computed anchors (an independent script of the same formula), Phi(d) = exp(-d/1410):
//   d 0 -> 1.0 | 500 -> 0.70145 | 1000 -> 0.49203 | 1410 -> 0.36788 | 2820 -> 0.13534 | 4000 -> 0.05861
// Fixture: fabricated states (no engine). tests/test_pbr_rewards.cpp already covers the qualitative cases (a cycle nets zero, no wall
// no reward, away from the ball pays ~0, demo); this file adds exact values, the boundary, persistence after a touch and the team wiring.
// The engine side (does a real car leaving a real wall open the latch?) is in tools/reward_lab/packs/wall_jump.

namespace {
	constexpr float G = 0.9969f; // TRAIN_GAMMA

	struct WJ {
		Vec car, ball;
		bool ground = false, wallContact = false, demoed = false, touch = false;
		Vec normal = Vec(1, 0, 0);
	};

	WJ OnWall(Vec car, Vec ball) { WJ f; f.car = car; f.ball = ball; f.ground = true; f.wallContact = true; f.normal = Vec(-1, 0, 0); return f; }
	WJ Air(Vec car, Vec ball) { WJ f; f.car = car; f.ball = ball; return f; }
	WJ Floor(Vec car, Vec ball) { WJ f; f.car = car; f.ball = ball; f.ground = true; f.wallContact = true; f.normal = Vec(0, 0, 1); return f; }

	void Fill(GameState& g, const WJ& f, int idx = 0, Team team = Team::BLUE) {
		Player p = MakePlayer(1 + idx, idx, team, f.car);
		p.isOnGround = f.ground; p.worldContact.hasContact = f.wallContact; p.worldContact.contactNormal = f.normal; p.isDemoed = f.demoed; p.ballTouchedStep = f.touch;
		p.rotMat = RotMat::GetIdentity();
		if (f.ground) p.rotMat.up = f.normal; // supported: the roof points along the surface normal
		g.players.push_back(p); g.ball.pos = f.ball;
	}

	// per-step rewards of one blue car over a frame sequence (frame 0 has no previous state, like the live loop)
	std::vector<float> Run(WallJumpToBallReward& r, const std::vector<WJ>& frames) {
		std::vector<GameState> st(frames.size());
		for (size_t i = 0; i < frames.size(); i++) { st[i].deltaTime = STEP_DT; Fill(st[i], frames[i]); }
		for (size_t i = 1; i < st.size(); i++) { st[i].prev = &st[i - 1]; st[i].players[0].prev = &st[i - 1].players[0]; }
		r.Reset(st[0]);
		std::vector<float> out;
		for (auto& s : st) { r.PreStep(s); out.push_back(r.GetReward(s.players[0], s, false)); }
		return out;
	}
	float Sum(const std::vector<float>& v) { float s = 0; for (float x : v) s += x; return s; }
}

TEST(WallJump2_wall_boundary_is_exactly_the_engine_split) {
	auto mk = [](float nz) { Player p = {}; p.isOnGround = true; p.rotMat = RotMat::GetIdentity(); p.rotMat.up = Vec(sqrtf(1 - nz * nz), 0, nz); return p; };
	CHECK(WallJumpToBallReward::OnWall(mk(0.70f)));     // steeper than 45 degrees from vertical: a wall
	CHECK(!WallJumpToBallReward::OnWall(mk(0.72f)));    // shallower: still ground (a ramp)
	CHECK(WallJumpToBallReward::OnWall(mk(-0.70f)));    // the test uses |nz|, so a slightly overhanging wall counts
	CHECK(!WallJumpToBallReward::OnWall(mk(-0.9f)));    // a ceiling does not
}

TEST(WallJump2_first_frame_is_zero) {
	WallJumpToBallReward r(G);
	auto v = Run(r, { OnWall(Vec(4079, 0, 500), Vec(3500, 0, 500)) });
	CHECK_EQ(v[0], 0.0f);
}

// Leaving the wall 560 uu from the ball pays the whole potential at once: the latch opens with Phi already high.
TEST(WallJump2_the_exit_step_pays_gamma_times_phi_at_once) {
	Vec ball(3500, 0, 500);
	WallJumpToBallReward r(G);
	auto v = Run(r, { OnWall(Vec(4079, 0, 500), ball), Air(Vec(4060, 0, 514), ball) });
	CHECK_NEAR(v[1], G * 0.67214f, 1e-4f);                 // d = 560.175
	CHECK_NEAR(60.f * v[1], 40.2f, 0.2f);                  // about +40 weighted for one wall exit near a ball
}

TEST(WallJump2_a_full_cycle_pays_phi_up_the_steps_and_refunds_on_landing) {
	Vec ball(3000, 0, 500);
	WallJumpToBallReward r(1.0f);
	auto v = Run(r, { OnWall(Vec(4079, 0, 500), ball), Air(Vec(4000, 0, 520), ball), Air(Vec(3300, 0, 505), ball),
		Air(Vec(3100, 0, 500), ball), Floor(Vec(3100, 0, 17), ball) });
	CHECK_NEAR(v[1], 0.49196f, 1e-4f);                      // latch opens: +Phi(d 1000)
	CHECK_NEAR(v[2], 0.80832f - 0.49196f, 1e-4f);           // closing
	CHECK_NEAR(v[3], 0.93153f - 0.80832f, 1e-4f);
	CHECK_NEAR(v[4], -0.93153f, 1e-4f);                     // landing clears the latch: full refund
	CHECK_NEAR(Sum(v), 0.0f, 1e-5f);
}

// "intentionally NOT cleared on the ball touch": after the strike the ball flies off, the potential drops and the car is charged for it
TEST(WallJump2_the_latch_stays_open_after_a_touch_and_the_ball_leaving_is_charged) {
	WallJumpToBallReward r(1.0f);
	Vec near(3000, 0, 500), far(5000, 0, 500);
	WJ strike = Air(Vec(3100, 0, 500), near); strike.touch = true; // car 100 uu from the ball, touching it
	WJ after = Air(Vec(3100, 0, 500), far); after.touch = true; // the ball is now 1900 uu away (the car has not moved)
	auto v = Run(r, { OnWall(Vec(4079, 0, 500), near), Air(Vec(3600, 0, 500), near), strike, after });
	CHECK_NEAR(v[3], 0.25988f - 0.93153f, 1e-4f);           // -0.6717: the strike's approach credit is partly given back
	CHECK(v[3] < -0.6f);
}

TEST(WallJump2_a_flat_ground_jump_and_a_ceiling_never_open_the_latch) {
	Vec ball(3000, 0, 500);
	WallJumpToBallReward r(G);
	WJ ceiling; ceiling.car = Vec(3200, 0, 2000); ceiling.ball = ball; ceiling.ground = true; ceiling.wallContact = true; ceiling.normal = Vec(0, 0, -1);
	auto a = Run(r, { Floor(Vec(4000, 0, 17), ball), Air(Vec(3900, 0, 300), ball), Air(Vec(3300, 0, 500), ball) });
	for (float x : a) CHECK_EQ(x, 0.0f);
	WallJumpToBallReward r2(G);
	auto b = Run(r2, { ceiling, Air(Vec(3200, 0, 1900), ball), Air(Vec(3200, 0, 1700), ball) });
	for (float x : b) CHECK_EQ(x, 0.0f);
}

TEST(WallJump2_landing_on_another_wall_also_clears_the_latch) {
	Vec ball(0, 0, 800);
	WallJumpToBallReward r(1.0f);
	WJ otherWall = OnWall(Vec(-4079, 0, 600), ball); otherWall.normal = Vec(1, 0, 0);
	auto v = Run(r, { OnWall(Vec(4079, 0, 500), ball), Air(Vec(3000, 0, 700), ball), Air(Vec(-2000, 0, 700), ball), otherWall });
	CHECK(v[3] < 0.0f);                                      // refund on touching the opposite wall
	CHECK_NEAR(Sum(v), 0.0f, 1e-5f);                         // the whole hop nets zero
}

TEST(WallJump2_a_demo_clears_the_latch_and_later_flight_pays_nothing) {
	Vec ball(3000, 0, 500);
	WallJumpToBallReward r(G);
	WJ dead = Air(Vec(3500, 0, 520), ball); dead.demoed = true;
	WJ back = Air(Vec(2900, 0, 520), ball);
	auto v = Run(r, { OnWall(Vec(4079, 0, 500), ball), Air(Vec(4000, 0, 520), ball), dead, back, Air(Vec(2950, 0, 515), ball) });
	CHECK(v[1] > 0.0f);                                      // latched
	CHECK_EQ(v[2], 0.0f);                                    // the demo step
	CHECK_EQ(v[3], 0.0f);                                    // the step after (previous frame demoed)
	CHECK_EQ(v[4], 0.0f);                                    // the latch is gone, so the rest of the flight pays nothing
}

TEST(WallJump2_each_player_has_its_own_latch) {
	Vec ball(3500, 0, 500);
	GameState a, b; a.deltaTime = b.deltaTime = STEP_DT;
	Fill(a, OnWall(Vec(4079, 0, 500), ball), 0, Team::BLUE); Fill(a, Floor(Vec(-3000, 3000, 17), ball), 1, Team::ORANGE);
	Fill(b, Air(Vec(4060, 0, 514), ball), 0, Team::BLUE);    Fill(b, Air(Vec(-3000, 3000, 300), ball), 1, Team::ORANGE);
	b.prev = &a; b.players[0].prev = &a.players[0]; b.players[1].prev = &a.players[1];
	WallJumpToBallReward r(G);
	r.Reset(a);
	auto v = r.GetAllRewards(b, false);
	CHECK_NEAR(v[0], G * 0.67214f, 1e-4f);                   // blue left a wall near the ball
	CHECK_EQ(v[1], 0.0f);                                    // orange jumped off the floor: no latch
}

TEST(WallJump2_discounted_sum_telescopes_while_the_latch_is_open) {
	Vec ball(2500, 300, 700);
	WallJumpToBallReward r(G);
	std::vector<WJ> frames = { OnWall(Vec(4079, 0, 500), ball) };
	for (int i = 1; i <= 15; i++) frames.push_back(Air(Vec(4079.f - 90.f * i, 20.f * i, 500.f + 12.f * i - 0.6f * i * i), ball));
	frames.push_back(Floor(Vec(2700, 300, 17), ball));
	auto v = Run(r, frames);
	double sum = 0, disc = 1; for (float x : v) { sum += disc * x; disc *= G; }
	CHECK_NEAR((float)sum, 0.0f, 1e-5f);                     // starts and ends with the latch closed (Phi 0 both ends)
}

TEST(WallJump2_holding_the_latch_near_the_ball_leaks_a_small_negative) {
	Vec ball(3500, 0, 500);
	WallJumpToBallReward r(G);
	auto v = Run(r, { OnWall(Vec(4079, 0, 500), ball), Air(Vec(4000, 0, 500), ball), Air(Vec(4000, 0, 500), ball) }); // d = 500 held
	CHECK_NEAR(v[2], (G - 1.0f) * 0.70145f, 1e-5f);          // -0.002174 per step, -0.13 weighted
}

// Wiring: ZeroSum(WallJump, 0.3), 2v2. Blue car 0 leaves a wall with r = 0.67214 (gamma 1); others idle on the floor (Phi 0).
//   car 0: 0.85 r = 0.5713; teammate 0.15 r = 0.1008; each orange car -0.5 r = -0.3361
TEST(WallJump2_zero_sum_2v2_values_at_team_spirit_0p3) {
	Vec ball(3500, 0, 500);
	GameState a, b; a.deltaTime = b.deltaTime = STEP_DT;
	for (int i = 0; i < 4; i++) {
		Team t = i < 2 ? Team::BLUE : Team::ORANGE;
		Fill(a, i == 0 ? OnWall(Vec(4079, 0, 500), ball) : Floor(Vec(-1000.f * i, -2000, 17), ball), i, t);
		Fill(b, i == 0 ? Air(Vec(4060, 0, 514), ball) : Floor(Vec(-1000.f * i, -2000, 17), ball), i, t);
	}
	b.prev = &a; for (int i = 0; i < 4; i++) b.players[i].prev = &a.players[i];
	Reward* zs = new ZeroSumReward(new WallJumpToBallReward(1.0f), 0.3f);
	zs->Reset(a); zs->PreStep(b);
	auto v = zs->GetAllRewards(b, false);
	CHECK_NEAR(v[0], 0.5713f, 1e-3f);
	CHECK_NEAR(v[1], 0.1008f, 1e-3f);
	CHECK_NEAR(v[2], -0.3361f, 1e-3f);
	CHECK_NEAR(v[3], -0.3361f, 1e-3f);
	CHECK_NEAR(v[0] + v[1] + v[2] + v[3], 0.0f, 1e-5f);
	delete zs;
}

// ---------------------------------------------------------------------------------------------------------------------------
// ENGINE FACTS behind the 2026-11-02 fix (docs/REWARD_CHANGE_LOG.md). The first OnWall() needed worldContact.hasContact, which the
// engine sets only when the car's BODY collides with the world (Arena.cpp:421) and clears every tick; a car riding a wall on its
// wheels never sets it, so the latch practically never opened. OnWall() now reads the car's roof direction.
// ---------------------------------------------------------------------------------------------------------------------------
namespace {
	void InitRocketSimWJ() {
		static bool done = false;
		if (done) return;
		for (const char* path : { "collision_meshes", "../collision_meshes", "build/collision_meshes" })
			if (std::filesystem::exists(std::filesystem::path(path) / "soccar")) { RocketSim::Init(path); break; }
		done = true;
	}
	bool EngineOnWall(const CarState& st) {
		Player p = {}; p.isOnGround = st.isOnGround; p.rotMat = st.rotMat;
		return WallJumpToBallReward::OnWall(p);
	}
}

TEST(WallJump2_engine_a_car_resting_on_a_wall_by_its_wheels_is_on_a_wall_every_step) {
	InitRocketSimWJ();
	Arena* arena = Arena::Create(GameMode::SOCCAR);
	Car* car = arena->AddCar(Team::BLUE);
	CarState cs; cs.pos = Vec(4079, 0, 500); cs.rotMat = Angle(1.5708f, 0, 1.5708f).ToRotMat(); cs.boost = 0; car->SetState(cs);
	BallState bs; bs.pos = Vec(0, 3000, 500); bs.vel = Vec(0, 0, 1); arena->ball->SetState(bs);
	int contact = 0, wall = 0;
	for (int s = 0; s < 20; s++) {
		arena->Step(8);
		auto st = car->GetState();
		if (st.worldContact.hasContact) contact++;
		if (EngineOnWall(st)) wall++;
	}
	CHECK_EQ(contact, 0);   // why the old detector never fired: no body contact while on wheels
	CHECK(wall >= 18);     // the roof-direction test sees the wall nearly every step (it slides down to the floor ramp at the end)
	delete arena;
}

TEST(WallJump2_engine_driving_up_a_side_wall_is_on_a_wall_while_up_it_and_never_on_the_floor) {
	InitRocketSimWJ();
	Arena* arena = Arena::Create(GameMode::SOCCAR);
	Car* car = arena->AddCar(Team::BLUE);
	CarState cs; cs.pos = Vec(2500, -1500, 17); cs.vel = Vec(1300, 600, 0); cs.rotMat = Angle(0.43f, 0, 0).ToRotMat(); cs.boost = 100; car->SetState(cs);
	car->controls.throttle = 1; car->controls.boost = true;
	int wallUp = 0, wallOnFloor = 0, ridingUp = 0;
	for (int s = 0; s < 40; s++) {
		arena->Step(8);
		auto st = car->GetState();
		bool wall = EngineOnWall(st);
		if (st.isOnGround && st.pos.x > 4060 && st.pos.z > 200) { ridingUp++; if (wall) wallUp++; }
		if (wall && st.pos.x < 3700) wallOnFloor++;
	}
	CHECK(ridingUp >= 5);        // the car spends several steps up the wall
	CHECK_EQ(wallUp, ridingUp);  // and is detected on every one of them
	CHECK_EQ(wallOnFloor, 0);    // never while still driving on the floor
	delete arena;
}

TEST(WallJump2_engine_driving_on_the_floor_is_never_a_wall) {
	InitRocketSimWJ();
	Arena* arena = Arena::Create(GameMode::SOCCAR);
	Car* car = arena->AddCar(Team::BLUE);
	CarState cs; cs.pos = Vec(0, -3000, 17); cs.vel = Vec(0, 1400, 0); cs.rotMat = Angle(1.5708f, 0, 0).ToRotMat(); cs.boost = 100; car->SetState(cs);
	car->controls.throttle = 1; car->controls.boost = true;
	int wall = 0;
	for (int s = 0; s < 30; s++) { arena->Step(8); if (EngineOnWall(car->GetState())) wall++; }
	CHECK_EQ(wall, 0);
	delete arena;
}
