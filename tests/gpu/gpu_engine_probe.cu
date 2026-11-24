// The GPU twin of tools/engine_probe/engine_probe.cpp: identical scenarios, one line each, to diff against the v2 / v3
// numbers in docs/engine_compare/FINDINGS.md (v2: Bullet RocketSim, v3: Rust). Not a pass/fail test; run by hand.
#include <rocketsim_cuda/sim_context.cuh>
#include <cuda_runtime.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>
using namespace rocketsim_cuda;

static float Len(const Vec3& v) { return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z); }
struct World {
	SimContext ctx{ 1, 1 };
	CarStatePOD car; BallStatePOD ball;
	void Set(const CarStatePOD& c, const BallStatePOD& b) { ctx.CopyCarStateToDevice(&c, 0, 1); ctx.CopyBallStateToDevice(&b, 0, 1); cudaDeviceSynchronize(); }
	void Tick(const CarControls& ctl) {
		ctx.CopyControlsToDevice(&ctl, 0, 1);
		ctx.Step(1);
		ctx.CopyCarStateToHost(&car, 0, 1); ctx.CopyBallStateToHost(&ball, 0, 1);
		cudaDeviceSynchronize();
	}
};
static Quat Yaw(float a) { return Quat(std::cos(a / 2), 0, 0, std::sin(a / 2)); }

int main() {
	// 1. ball dropped from z=400 on open ground: bounce peaks
	{
		World w; CarStatePOD c; c.pos = Vec3(3000, 3000, 17); BallStatePOD b; b.pos = Vec3(0, 0, 400);
		w.Set(c, b); std::vector<float> peaks; float prevVz = 0; CarControls ctl{};
		for (int i = 0; i < 1200; i++) { w.Tick(ctl); if (prevVz > 0 && w.ball.vel.z <= 0) peaks.push_back(w.ball.pos.z); prevVz = w.ball.vel.z; }
		std::printf("drop400_ground peaks:"); for (size_t i = 0; i < peaks.size() && i < 6; i++) std::printf(" %.1f", peaks[i]); std::printf("\n");
	}
	// 3. ball rolling at 1500 uu/s: speed after 1,2,3 s
	{
		World w; CarStatePOD c; c.pos = Vec3(3000, 3000, 17); BallStatePOD b; b.pos = Vec3(-2000, 0, 93.15f); b.vel = Vec3(1500, 0, 0);
		w.Set(c, b); CarControls ctl{}; std::printf("roll1500 speed@1s,2s,3s:");
		for (int s = 1; s <= 3; s++) { for (int i = 0; i < 120; i++) w.Tick(ctl); std::printf(" %.1f", Len(w.ball.vel)); } std::printf("\n");
	}
	// 4. car at constant speed hits resting ball 0.5 s later
	for (float sp : { 800.f, 1200.f, 1400.f }) {
		World w; CarStatePOD c; c.pos = Vec3(0, -sp * 0.5f - 160, 17); c.vel = Vec3(0, sp, 0); c.quat = Yaw(1.5708f); c.boost = 0;
		BallStatePOD b; b.pos = Vec3(0, 0, 93.15f); w.Set(c, b);
		float maxBall = 0, atHit = 0, extra = 0; bool hit = false; CarControls ctl{}; ctl.throttle = 1;
		for (int i = 0; i < 240; i++) {
			w.Tick(ctl);
			if (!hit && Len(w.ball.vel) > 5) { hit = true; atHit = Len(w.car.vel); }
			if (w.car.ball_hit_is_valid) extra = std::max(extra, Len(w.car.ball_hit_extra_hit_force));
			maxBall = std::max(maxBall, Len(w.ball.vel));
		}
		std::printf("carhit_%d car_speed_at_contact=%.1f peak_ball_speed=%.1f engine_extraHitVel=%.1f\n", (int)sp, atHit, maxBall, extra);
	}
	// 5. carry
	{
		World w; CarStatePOD c; c.pos = Vec3(0, -3000, 17); c.vel = Vec3(0, 800, 0); c.quat = Yaw(1.5708f); c.boost = 0;
		BallStatePOD b; b.pos = Vec3(0, -3000, 17 + 70 + 93.15f); b.vel = Vec3(0, 800, 0); w.Set(c, b);
		CarControls ctl{}; ctl.throttle = 0.4f; int near = 0;
		for (int i = 0; i < 960; i++) { w.Tick(ctl); if (Len(Vec3(w.ball.pos.x - w.car.pos.x, w.ball.pos.y - w.car.pos.y, w.ball.pos.z - w.car.pos.z)) < 200) near++; }
		std::printf("carry_800 ticks_near_car_of_960=%d\n", near);
	}
	// 6. ball rolling at a stationary car
	for (float sp : { 500.f, 1000.f, 2000.f }) {
		World w; CarStatePOD c; c.pos = Vec3(0, 0, 17); c.quat = Yaw(1.5708f); BallStatePOD b; b.pos = Vec3(0, 1500, 93.15f); b.vel = Vec3(0, -sp, 0);
		w.Set(c, b); CarControls ctl{}; float reb = 0; int near = 0;
		for (int i = 0; i < 480; i++) { w.Tick(ctl); if (w.ball.vel.y > 0) reb = std::max(reb, w.ball.vel.y);
			if (Len(Vec3(w.ball.pos.x - w.car.pos.x, w.ball.pos.y - w.car.pos.y, w.ball.pos.z - w.car.pos.z)) < 160) near++; }
		std::printf("ball_into_parked_car_%d max_rebound_vy=%.1f ticks_within_160uu=%d\n", (int)sp, reb, near);
	}
	// 7. airborne car, held pitch for 8 ticks
	for (float pitch : { 1.f, -1.f }) {
		World w; CarStatePOD c; c.pos = Vec3(0, 0, 400); c.boost = 0; c.is_on_ground = 0; c.has_jumped = 1;
		BallStatePOD b; b.pos = Vec3(2000, 2000, 93.15f); w.Set(c, b); CarControls ctl{}; ctl.pitch = pitch;
		for (int i = 0; i < 8; i++) w.Tick(ctl);
		// forward.z from the quaternion (car forward = +x local)
		const Quat& q = w.car.quat; float fz = 2 * (q.x * q.z - q.w * q.y);
		std::printf("air_pitch_%d forward.z=%.3f angVel=(%.2f,%.2f,%.2f) onGround=%d\n", (int)pitch, fz, w.car.ang_vel.x, w.car.ang_vel.y, w.car.ang_vel.z, (int)w.car.is_on_ground);
	}
	return 0;
}
