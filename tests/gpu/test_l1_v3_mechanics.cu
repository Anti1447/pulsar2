// L1 / patches P1-6 and P1-7: jump, double jump, flip, air boost and flip-landing mechanics against RocketSim v3.
// Reference values are rows of the v3 (0.3.5) traces of tools/parity/scenarios.h, recorded 2026-11-24.
#include "GpuTestUtil.h"
#include "../../tools/parity/scenarios.h"
#include <cmath>
using namespace rocketsim_cuda;
using namespace gputest;
using namespace parity;

static float Dist(Vec3 a, float x, float y, float z) { return std::sqrt((a.x - x) * (a.x - x) + (a.y - y) * (a.y - y) + (a.z - z) * (a.z - z)); }

// quaternion for a level or pitched car with zero roll (all scenarios used here)
static Quat FromForward(Vec3f f) {
	float yaw = std::atan2(f.y, f.x), pitch = std::asin(f.z);
	float cy = std::cos(yaw * 0.5f), sy = std::sin(yaw * 0.5f), cp = std::cos(-pitch * 0.5f), sp = std::sin(-pitch * 0.5f);
	return Quat(cy * cp, -sy * sp, cy * sp, sy * cp);
}

static CarStatePOD Run(const char* name, int ticks, BallStatePOD* ballOut = nullptr) {
	for (const Scenario& sc : BuildScenarios()) {
		if (sc.name != name) continue;
		SimContext ctx{ 1, 1 };
		const CarInit& c = sc.cars[0];
		CarStatePOD p; p.pos = Vec3(c.pos.x, c.pos.y, c.pos.z); p.vel = Vec3(c.vel.x, c.vel.y, c.vel.z); p.quat = FromForward(c.forward);
		p.boost = c.boost; p.is_on_ground = c.onGround; for (int w = 0; w < 4; w++) p.wheels_with_contact[w] = c.onGround ? 1 : 0; p.has_jumped = c.hasJumped;
		BallStatePOD b; b.pos = Vec3(sc.ball.pos.x, sc.ball.pos.y, sc.ball.pos.z); b.vel = Vec3(sc.ball.vel.x, sc.ball.vel.y, sc.ball.vel.z);
		ctx.CopyCarStateToDevice(&p, 0, 1); ctx.CopyBallStateToDevice(&b, 0, 1); Sync();
		for (int t = 0; t < ticks; t++) {
			Ctl k = sc.ctl(t, 0); CarControls cc;
			cc.throttle = k.throttle; cc.steer = k.steer; cc.pitch = k.pitch; cc.yaw = k.yaw; cc.roll = k.roll; cc.jump = k.jump; cc.boost = k.boost; cc.handbrake = k.handbrake;
			ctx.CopyControlsToDevice(&cc, 0, 1); ctx.Step(1);
		}
		if (ballOut) *ballOut = DownloadBalls(ctx)[0];
		return DownloadCars(ctx)[0];
	}
	throw TestFailure{ std::string("no scenario ") + name };
}

TEST(JumpsMatchV3) {
	CHECK(Dist(Run("jump_tap_3", 60).pos, 0.f, -3500.f, 90.5f) < 1.f);
	CHECK(Dist(Run("jump_hold_024", 60).pos, 0.f, -3500.f, 190.4f) < 1.f);
	CHECK(Dist(Run("double_jump", 60).pos, 0.f, -3500.f, 199.8f) < 1.f);
}

TEST(SideDodgeMatchesV3) {
	CarStatePOD c = Run("dodge_p0_r1", 60);
	CHECK(Dist(c.pos, -187.6f, -800.f, 76.4f) < 1.f);
	CHECK(Dist(c.vel, -500.f, 0.f, -15.5f) < 1.f);
}

TEST(AirBoostAndAirThrottleMatchV3) { // boost accel in the air equals ground; the first boosting tick is free
	CarStatePOD c = Run("air_roll_yaw_boost", 120);
	CHECK(Dist(c.pos, -35.1f, -703.7f, 642.4f) < 2.f);
	CHECK(Dist(c.vel, -169.3f, 1764.3f, -155.5f) < 3.f);
	CHECK(std::fabs(c.boost - 66.978f) < 0.1f);
}

TEST(FlipAngularSpeedIsReportedUnclampedLikeV3) { // v3 clamps at the start of a tick, so a flip reads about 7.2 rad/s
	CarStatePOD c = Run("dodge_p1_r0", 60);
	float w = std::sqrt(c.ang_vel.x * c.ang_vel.x + c.ang_vel.y * c.ang_vel.y + c.ang_vel.z * c.ang_vel.z);
	CHECK(w > 6.5f); CHECK(w < 7.5f);
}

TEST(FlipLandingStaysNearV3) { // chassis-floor contact (P1-7); before the patch these were 60 to 90 uu off
	CHECK(Dist(Run("dodge_p1_r1", 120).pos, -290.5f, -1124.6f, 120.9f) < 12.f);
	CHECK(Dist(Run("dodge_p-1_r0", 120).pos, -12.f, -389.5f, 84.9f) < 20.f);
}

TEST(AFreshAirborneCarIsNotPitchLocked) { // flip_time starts large (time since the last flip), as after a v3 reset
	SimContext ctx{ 1, 1 };
	CarStatePOD p; p.pos = Vec3(0.f, 0.f, 800.f); p.is_on_ground = 0; for (int w = 0; w < 4; w++) p.wheels_with_contact[w] = 0; p.has_jumped = 1;
	BallStatePOD b; b.pos = Vec3(3000.f, 3000.f, 93.15f);
	ctx.CopyCarStateToDevice(&p, 0, 1); ctx.CopyBallStateToDevice(&b, 0, 1); Sync();
	CarControls cc; cc.pitch = 1.f; ctx.CopyControlsToDevice(&cc, 0, 1);
	for (int t = 0; t < 10; t++) ctx.Step(1); // Step takes a batch size, one tick per call
	CarStatePOD c = DownloadCars(ctx)[0];
	CHECK(std::sqrt(c.ang_vel.x * c.ang_vel.x + c.ang_vel.y * c.ang_vel.y + c.ang_vel.z * c.ang_vel.z) > 0.5f);
}

TEST(OffCentreAerialHitMatchesV3) { // P1-8; before it the car was 50 uu and the ball 54 uu off by the end
	BallStatePOD b; CarStatePOD c = Run("aerialhit_off60", 180, &b);
	CHECK(Dist(c.pos, -142.8f, 1659.6f, 249.2f) < 12.f);
	CHECK(Dist(b.pos, -589.3f, 1246.0f, 975.9f) < 12.f);
}

TEST(DrivingAndSteeringMatchV3) { // P1-10: throttle, brake and steer act in the same tick; before it these were 5 to 15 uu off
	CHECK(Dist(Run("steer_left", 120).pos, -1058.0f, -3774.9f, 17.0f) < 1.5f);
	CHECK(Dist(Run("handbrake_turn", 120).pos, -407.2f, -2545.8f, 17.0f) < 1.5f);
	CHECK(Dist(Run("drive_ground", 120).pos, 0.0f, -2910.2f, 17.0f) < 1.5f);
}

TEST(GroundHitCarryAndJumpHitMatchV3) { // P1-9: contacts solved on the start-of-tick pose, before positions move
	{ BallStatePOD b; CarStatePOD c = Run("carhit_1200_off60", 240, &b);
	  CHECK(Dist(c.pos, -207.7f, 1889.3f, 17.0f) < 3.f); CHECK(Dist(b.pos, -816.3f, 2331.2f, 102.3f) < 35.f); }
	{ BallStatePOD b; CarStatePOD c = Run("carry_800", 120, &b);
	  CHECK(Dist(c.pos, 0.0f, -2066.1f, 17.0f) < 3.f); CHECK(Dist(b.pos, 0.0f, -2240.1f, 122.3f) < 10.f); }
	{ BallStatePOD b; CarStatePOD c = Run("jumphit_double", 200, &b);
	  CHECK(Dist(c.pos, 0.0f, 730.5f, 338.4f) < 12.f); CHECK(Dist(b.pos, 0.0f, 1548.1f, 91.6f) < 15.f); }
}
