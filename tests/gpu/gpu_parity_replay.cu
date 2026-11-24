// GPU replayer for the shared parity scenarios (tools/parity/scenarios.h): same CSV format as the CPU recorder.
// usage: gpu_parity_replay <outdir> [filter]
#include <rocketsim_cuda/sim_context.cuh>
#include <cuda_runtime.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include "../../tools/parity/scenarios.h"
using namespace rocketsim_cuda;
using namespace parity;

static Vec3 V(Vec3f v) { return Vec3(v.x, v.y, v.z); }

// Quaternion from the basis whose COLUMNS are forward, right, up (right = up x forward).
static Quat QuatFromBasis(Vec3f f, Vec3f u) {
	Vec3f r = Cross(u, f);
	float m00 = f.x, m10 = f.y, m20 = f.z, m01 = r.x, m11 = r.y, m21 = r.z, m02 = u.x, m12 = u.y, m22 = u.z;
	float tr = m00 + m11 + m22, w, x, y, z;
	if (tr > 0) { float s = std::sqrt(tr + 1.f) * 2; w = 0.25f * s; x = (m21 - m12) / s; y = (m02 - m20) / s; z = (m10 - m01) / s; }
	else if (m00 > m11 && m00 > m22) { float s = std::sqrt(1.f + m00 - m11 - m22) * 2; w = (m21 - m12) / s; x = 0.25f * s; y = (m01 + m10) / s; z = (m02 + m20) / s; }
	else if (m11 > m22) { float s = std::sqrt(1.f + m11 - m00 - m22) * 2; w = (m02 - m20) / s; x = (m01 + m10) / s; y = 0.25f * s; z = (m12 + m21) / s; }
	else { float s = std::sqrt(1.f + m22 - m00 - m11) * 2; w = (m10 - m01) / s; x = (m02 + m20) / s; y = (m12 + m21) / s; z = 0.25f * s; }
	return Quat(w, x, y, z);
}
static void Basis(const Quat& q, float f[3], float u[3]) {
	f[0] = 1 - 2 * (q.y * q.y + q.z * q.z); f[1] = 2 * (q.x * q.y + q.w * q.z); f[2] = 2 * (q.x * q.z - q.w * q.y);
	u[0] = 2 * (q.x * q.z + q.w * q.y); u[1] = 2 * (q.y * q.z - q.w * q.x); u[2] = 1 - 2 * (q.x * q.x + q.y * q.y);
}

int main(int argc, char** argv) {
	if (argc < 2) { std::cerr << "usage: gpu_parity_replay <outdir> [filter]\n"; return 1; }
	std::filesystem::path out = argv[1]; std::filesystem::create_directories(out);
	std::string filter = argc > 2 ? argv[2] : "";
	for (const Scenario& sc : BuildScenarios()) {
		if (!filter.empty() && sc.name.find(filter) == std::string::npos) continue;
		const uint32_t n = (uint32_t)sc.cars.size();
		SimContext ctx(1, n);
		uint32_t mask = 0; for (uint32_t i = 0; i < n; i++) mask |= (uint32_t)sc.cars[i].team << i;
		ctx.SetTeamLayout(mask);
		std::vector<CarStatePOD> cp(n);
		for (uint32_t i = 0; i < n; i++) {
			const CarInit& c = sc.cars[i]; CarStatePOD& p = cp[i];
			p.pos = V(c.pos); p.vel = V(c.vel); p.ang_vel = V(c.angVel); p.quat = QuatFromBasis(c.forward, c.up);
			p.boost = c.boost; p.is_on_ground = c.onGround; for (int w = 0; w < 4; w++) p.wheels_with_contact[w] = c.onGround ? 1 : 0; p.has_jumped = c.hasJumped; p.has_double_jumped = c.hasDoubleJumped; p.has_flipped = c.hasFlipped; p.team = (uint8_t)c.team;
		}
		BallStatePOD bp; bp.pos = V(sc.ball.pos); bp.vel = V(sc.ball.vel); bp.ang_vel = V(sc.ball.angVel);
		ctx.CopyCarStateToDevice(cp.data(), 0, n); ctx.CopyBallStateToDevice(&bp, 0, 1); cudaDeviceSynchronize();

		std::ofstream f(out / (sc.name + ".csv")); f << std::setprecision(9);
		f << "tick,bpx,bpy,bpz,bvx,bvy,bvz,bwx,bwy,bwz";
		for (uint32_t i = 0; i < n; i++)
			for (const char* k : { "px", "py", "pz", "vx", "vy", "vz", "wx", "wy", "wz", "fx", "fy", "fz", "ux", "uy", "uz", "boost", "ground", "jumped", "dj", "flipped", "demoed", "hit" })
				f << ",c" << i << "_" << k;
		f << "\n";
		auto dump = [&](int tick) {
			BallStatePOD b; ctx.CopyBallStateToHost(&b, 0, 1); std::vector<CarStatePOD> cs(n); ctx.CopyCarStateToHost(cs.data(), 0, n); cudaDeviceSynchronize();
			f << tick << "," << b.pos.x << "," << b.pos.y << "," << b.pos.z << "," << b.vel.x << "," << b.vel.y << "," << b.vel.z << "," << b.ang_vel.x << "," << b.ang_vel.y << "," << b.ang_vel.z;
			for (uint32_t i = 0; i < n; i++) {
				const CarStatePOD& s = cs[i]; float fw[3], up[3]; Basis(s.quat, fw, up);
				f << "," << s.pos.x << "," << s.pos.y << "," << s.pos.z << "," << s.vel.x << "," << s.vel.y << "," << s.vel.z << "," << s.ang_vel.x << "," << s.ang_vel.y << "," << s.ang_vel.z
				  << "," << fw[0] << "," << fw[1] << "," << fw[2] << "," << up[0] << "," << up[1] << "," << up[2]
				  << "," << s.boost << "," << (int)s.is_on_ground << "," << (int)s.has_jumped << "," << (int)s.has_double_jumped << "," << (int)s.has_flipped << "," << (int)s.is_demoed
				  << "," << (s.ball_hit_is_valid ? (long long)s.ball_hit_tick_count : -1LL);
			}
			f << "\n";
		};
		dump(0);
		for (int t = 0; t < sc.ticks; t++) {
			std::vector<CarControls> cc(n);
			for (uint32_t i = 0; i < n; i++) {
				Ctl k = sc.ctl(t, (int)i);
				cc[i].throttle = k.throttle; cc[i].steer = k.steer; cc[i].pitch = k.pitch; cc[i].yaw = k.yaw; cc[i].roll = k.roll; cc[i].jump = k.jump; cc[i].boost = k.boost; cc[i].handbrake = k.handbrake;
			}
			ctx.CopyControlsToDevice(cc.data(), 0, n);
			ctx.Step(1);
			dump(t + 1);
		}
		std::printf("replayed %s (%d ticks)\n", sc.name.c_str(), sc.ticks);
	}
	return 0;
}
