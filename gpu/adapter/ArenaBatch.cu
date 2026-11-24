#include "ArenaBatch.h"
#include <rocketsim_cuda/sim_context.cuh>
#include <cuda_runtime.h>
#include <cstring>
#include <stdexcept>
#include <string>

namespace pulsar_gpu {
	using namespace rocketsim_cuda;
	static void Ck(cudaError_t e, const char* what) { if (e != cudaSuccess) throw std::runtime_error(std::string("ArenaBatch ") + what + ": " + cudaGetErrorString(e)); }

	struct Ctx {
		ModeSpec spec; int cars; int firstArena; uint32_t teamMask = 0;
		std::unique_ptr<SimContext> sim;
		std::vector<CarStatePOD> carPod; std::vector<BallStatePOD> ballPod;
		std::vector<CarControls> controls;
		std::vector<CarData> cars_h; std::vector<BallData> balls_h;
		std::vector<uint8_t> evBump, evBumped, evDemo, evDemoed, isGoal, scoringTeam, padActive;
		std::vector<float> padCd, pred; std::vector<uint32_t> ticks;
		float* dPred = nullptr;
	};

	struct ArenaBatch::Impl { std::vector<Ctx> ctxs; int total = 0; std::vector<std::pair<int, int>> where; };

	static void ToPod(const CarData& c, CarStatePOD& p) {
		p.pos = Vec3(c.pos[0], c.pos[1], c.pos[2]); p.vel = Vec3(c.vel[0], c.vel[1], c.vel[2]); p.ang_vel = Vec3(c.angVel[0], c.angVel[1], c.angVel[2]);
		p.quat = Quat(c.quat[0], c.quat[1], c.quat[2], c.quat[3]);
		p.boost = c.boost; p.team = c.team; p.is_on_ground = c.onGround; for (int i = 0; i < 4; i++) p.wheels_with_contact[i] = c.wheels[i];
		p.has_jumped = c.hasJumped; p.has_double_jumped = c.hasDoubleJumped; p.has_flipped = c.hasFlipped; p.is_jumping = c.isJumping; p.is_flipping = c.isFlipping;
		p.is_demoed = c.isDemoed; p.is_supersonic = c.isSupersonic; p.is_auto_flipping = c.isAutoFlipping; p.is_boosting = c.isBoosting;
		p.jump_time = c.jumpTime; p.flip_time = c.flipTime; p.air_time = c.airTime; p.air_time_since_jump = c.airTimeSinceJump; p.handbrake_val = c.handbrake;
		p.demo_respawn_timer = c.demoRespawn; p.supersonic_time = c.supersonicTime; p.auto_flip_timer = c.autoFlipTimer; p.auto_flip_torque_scale = c.autoFlipTorqueScale;
		p.time_since_boosted = c.timeSinceBoosted; p.boosting_time = c.boostingTime; p.flip_rel_torque = Vec3(c.flipRelTorque[0], c.flipRelTorque[1], c.flipRelTorque[2]);
		p.world_contact_has_contact = c.worldContact; p.world_contact_normal = Vec3(c.worldNormal[0], c.worldNormal[1], c.worldNormal[2]);
		p.car_contact_other_car_id = c.carContactOther; p.car_contact_cooldown_timer = c.carContactCooldown;
		p.ball_hit_is_valid = c.hitValid; p.ball_hit_rel_pos = Vec3(c.hitRelPos[0], c.hitRelPos[1], c.hitRelPos[2]); p.ball_hit_extra_hit_force = Vec3(c.hitExtra[0], c.hitExtra[1], c.hitExtra[2]); p.ball_hit_tick_count = c.hitTick;
	}
	static void FromPod(const CarStatePOD& p, CarData& c) {
		auto v3 = [](const Vec3& v, float* o) { o[0] = v.x; o[1] = v.y; o[2] = v.z; };
		v3(p.pos, c.pos); v3(p.vel, c.vel); v3(p.ang_vel, c.angVel); c.quat[0] = p.quat.w; c.quat[1] = p.quat.x; c.quat[2] = p.quat.y; c.quat[3] = p.quat.z;
		c.boost = p.boost; c.team = p.team; c.onGround = p.is_on_ground; for (int i = 0; i < 4; i++) c.wheels[i] = p.wheels_with_contact[i];
		c.hasJumped = p.has_jumped; c.hasDoubleJumped = p.has_double_jumped; c.hasFlipped = p.has_flipped; c.isJumping = p.is_jumping; c.isFlipping = p.is_flipping;
		c.isDemoed = p.is_demoed; c.isSupersonic = p.is_supersonic; c.isAutoFlipping = p.is_auto_flipping; c.isBoosting = p.is_boosting;
		c.jumpTime = p.jump_time; c.flipTime = p.flip_time; c.airTime = p.air_time; c.airTimeSinceJump = p.air_time_since_jump; c.handbrake = p.handbrake_val;
		c.demoRespawn = p.demo_respawn_timer; c.supersonicTime = p.supersonic_time; c.autoFlipTimer = p.auto_flip_timer; c.autoFlipTorqueScale = p.auto_flip_torque_scale;
		c.timeSinceBoosted = p.time_since_boosted; c.boostingTime = p.boosting_time; v3(p.flip_rel_torque, c.flipRelTorque);
		c.worldContact = p.world_contact_has_contact; v3(p.world_contact_normal, c.worldNormal); c.carContactOther = p.car_contact_other_car_id; c.carContactCooldown = p.car_contact_cooldown_timer;
		c.hitValid = p.ball_hit_is_valid; v3(p.ball_hit_rel_pos, c.hitRelPos); v3(p.ball_hit_extra_hit_force, c.hitExtra); c.hitTick = p.ball_hit_tick_count;
	}

	ArenaBatch::ArenaBatch(const std::vector<ModeSpec>& modes) : impl(new Impl) {
		for (const ModeSpec& m : modes) {
			Ctx c; c.spec = m; c.cars = m.blue + m.orange; c.firstArena = impl->total;
			// alternating layout while both teams have cars, remainder to the larger team (slot team via bit mask)
			int b = m.blue, o = m.orange; uint32_t mask = 0;
			for (int s = 0; s < c.cars; s++) {
				bool wantBlue = (s % 2 == 0);
				int team = (wantBlue ? (b > 0) : (o == 0)) ? 0 : 1;
				if (team == 0) b--; else { o--; mask |= 1u << s; }
			}
			c.teamMask = mask;
			c.sim.reset(new SimContext((uint32_t)m.arenas, (uint32_t)c.cars));
			c.sim->SetTeamLayout(mask);
			c.sim->ResetToDefault();
			const size_t n = (size_t)m.arenas;
			c.carPod.resize(n * c.cars); c.ballPod.resize(n); c.controls.resize(n * c.cars); c.cars_h.resize(n * c.cars); c.balls_h.resize(n);
			c.evBump.resize(n * c.cars); c.evBumped.resize(n * c.cars); c.evDemo.resize(n * c.cars); c.evDemoed.resize(n * c.cars);
			c.isGoal.resize(n); c.scoringTeam.resize(n); c.padActive.resize(n * NUM_PADS); c.padCd.resize(n * NUM_PADS); c.ticks.resize(n); c.pred.resize(n * 8 * 6);
			Ck(cudaMalloc(&c.dPred, n * PRED_TICKS * 6 * sizeof(float)), "alloc pred");
			for (int a = 0; a < m.arenas; a++) impl->where.push_back({ (int)impl->ctxs.size(), a });
			impl->total += m.arenas;
			impl->ctxs.push_back(std::move(c));
		}
	}
	ArenaBatch::~ArenaBatch() { for (auto& c : impl->ctxs) if (c.dPred) cudaFree(c.dPred); }

	int ArenaBatch::NumArenas() const { return impl->total; }
	int ArenaBatch::NumCars(int a) const { return impl->ctxs[impl->where[a].first].cars; }
	int ArenaBatch::TeamOfSlot(int a, int s) const { return (impl->ctxs[impl->where[a].first].teamMask >> s) & 1u; }

	void ArenaBatch::SetState(int arena, const std::vector<CarData>& cars, const BallData& ball) {
		Ctx& c = impl->ctxs[impl->where[arena].first]; const int e = impl->where[arena].second;
		if ((int)cars.size() != c.cars) throw std::runtime_error("ArenaBatch::SetState: wrong car count");
		int32_t idx = e; int32_t* d; Ck(cudaMalloc(&d, 4), "alloc"); Ck(cudaMemcpy(d, &idx, 4, cudaMemcpyHostToDevice), "idx");
		c.sim->ResetEnvironmentsIndexed(d, 1); Ck(cudaStreamSynchronize(c.sim->GetStream()), "reset sync"); cudaFree(d);
		std::vector<CarStatePOD> pods(c.cars);
		for (int s = 0; s < c.cars; s++) { ToPod(cars[s], pods[s]); pods[s].team = (uint8_t)((c.teamMask >> s) & 1u); }
		BallStatePOD bp; bp.pos = Vec3(ball.pos[0], ball.pos[1], ball.pos[2]); bp.vel = Vec3(ball.vel[0], ball.vel[1], ball.vel[2]); bp.ang_vel = Vec3(ball.angVel[0], ball.angVel[1], ball.angVel[2]); bp.quat = Quat(ball.quat[0], ball.quat[1], ball.quat[2], ball.quat[3]);
		c.sim->CopyCarStateToDevice(pods.data(), (uint32_t)(e * c.cars), (uint32_t)c.cars);
		c.sim->CopyBallStateToDevice(&bp, (uint32_t)e, 1);
		Ck(cudaStreamSynchronize(c.sim->GetStream()), "setstate sync");
	}

	void ArenaBatch::SetControls(int arena, const std::vector<ControlData>& ctl) {
		Ctx& c = impl->ctxs[impl->where[arena].first]; const int e = impl->where[arena].second;
		for (int s = 0; s < c.cars; s++) {
			CarControls& k = c.controls[(size_t)e * c.cars + s]; const ControlData& d = ctl[s];
			k.throttle = d.throttle; k.steer = d.steer; k.pitch = d.pitch; k.yaw = d.yaw; k.roll = d.roll; k.jump = d.jump; k.boost = d.boost; k.handbrake = d.handbrake;
		}
	}

	void ArenaBatch::Step(int ticks) {
		for (Ctx& c : impl->ctxs) {
			c.sim->CopyControlsToDevice(c.controls.data(), 0, (uint32_t)c.controls.size());
			c.sim->ClearStepEvents();
			for (int t = 0; t < ticks; t++) c.sim->Step((uint32_t)c.spec.arenas);
		}
	}

	void ArenaBatch::Sync() {
		for (Ctx& c : impl->ctxs) {
			cudaStream_t st = c.sim->GetStream(); const size_t n = (size_t)c.spec.arenas, nc = n * c.cars;
			c.sim->PredictBallTrajectory(c.dPred, PRED_TICKS, 6);
			Ck(cudaStreamSynchronize(st), "pre-download sync");
			c.sim->CopyCarStateToHost(c.carPod.data(), 0, (uint32_t)nc);
			c.sim->CopyBallStateToHost(c.ballPod.data(), 0, (uint32_t)n);
			Ck(cudaStreamSynchronize(st), "download sync");
			const CarStateSoA& cs = c.sim->GetCarState(); const ArenaStateSoA& as = c.sim->GetArenaState();
			Ck(cudaMemcpy(c.evBump.data(), cs.step_bump, nc, cudaMemcpyDeviceToHost), "ev"); Ck(cudaMemcpy(c.evBumped.data(), cs.step_bumped, nc, cudaMemcpyDeviceToHost), "ev");
			Ck(cudaMemcpy(c.evDemo.data(), cs.step_demo, nc, cudaMemcpyDeviceToHost), "ev"); Ck(cudaMemcpy(c.evDemoed.data(), cs.step_demoed, nc, cudaMemcpyDeviceToHost), "ev");
			Ck(cudaMemcpy(c.isGoal.data(), as.is_goal, n, cudaMemcpyDeviceToHost), "goal"); Ck(cudaMemcpy(c.scoringTeam.data(), as.scoring_team, n, cudaMemcpyDeviceToHost), "team");
			Ck(cudaMemcpy(c.padActive.data(), as.pad_is_active, n * NUM_PADS, cudaMemcpyDeviceToHost), "pads"); Ck(cudaMemcpy(c.padCd.data(), as.pad_cooldown, n * NUM_PADS * 4, cudaMemcpyDeviceToHost), "pads");
			Ck(cudaMemcpy(c.ticks.data(), as.tick_count, n * 4, cudaMemcpyDeviceToHost), "tick");
			std::vector<float> pred(n * PRED_TICKS * 6); Ck(cudaMemcpy(pred.data(), c.dPred, pred.size() * 4, cudaMemcpyDeviceToHost), "pred");
			for (size_t e = 0; e < n; e++) {
				for (int k = 0; k < 8; k++) std::memcpy(&c.pred[(e * 8 + k) * 6], &pred[(e * PRED_TICKS + 30 * (k + 1)) * 6], 6 * sizeof(float));
				BallData& b = c.balls_h[e]; const BallStatePOD& p = c.ballPod[e];
				b.pos[0] = p.pos.x; b.pos[1] = p.pos.y; b.pos[2] = p.pos.z; b.vel[0] = p.vel.x; b.vel[1] = p.vel.y; b.vel[2] = p.vel.z; b.angVel[0] = p.ang_vel.x; b.angVel[1] = p.ang_vel.y; b.angVel[2] = p.ang_vel.z;
				b.quat[0] = p.quat.w; b.quat[1] = p.quat.x; b.quat[2] = p.quat.y; b.quat[3] = p.quat.z;
				for (int s = 0; s < c.cars; s++) {
					size_t i = e * c.cars + s; FromPod(c.carPod[i], c.cars_h[i]);
					c.cars_h[i].bump = c.evBump[i]; c.cars_h[i].bumped = c.evBumped[i]; c.cars_h[i].demo = c.evDemo[i]; c.cars_h[i].demoed = c.evDemoed[i];
				}
			}
		}
	}

	const CarData& ArenaBatch::Car(int a, int s) const { const Ctx& c = impl->ctxs[impl->where[a].first]; return c.cars_h[(size_t)impl->where[a].second * c.cars + s]; }
	const BallData& ArenaBatch::Ball(int a) const { return impl->ctxs[impl->where[a].first].balls_h[impl->where[a].second]; }
	bool ArenaBatch::GoalScored(int a) const { return impl->ctxs[impl->where[a].first].isGoal[impl->where[a].second] != 0; }
	int ArenaBatch::ScoringTeam(int a) const { return impl->ctxs[impl->where[a].first].scoringTeam[impl->where[a].second]; }
	uint64_t ArenaBatch::Tick(int a) const { return impl->ctxs[impl->where[a].first].ticks[impl->where[a].second]; }
	const uint8_t* ArenaBatch::PadActive(int a) const { return &impl->ctxs[impl->where[a].first].padActive[(size_t)impl->where[a].second * NUM_PADS]; }
	const float* ArenaBatch::PadCooldown(int a) const { return &impl->ctxs[impl->where[a].first].padCd[(size_t)impl->where[a].second * NUM_PADS]; }
	const float* ArenaBatch::BallPredSlices(int a) const { return &impl->ctxs[impl->where[a].first].pred[(size_t)impl->where[a].second * 8 * 6]; }
	const float* ArenaBatch::PadPositions() {
		static float p[NUM_PADS * 3]; static bool init = false;
		if (!init) { for (int i = 0; i < NUM_PADS; i++) { p[i * 3] = SOCCAR_BOOST_PADS[i].x; p[i * 3 + 1] = SOCCAR_BOOST_PADS[i].y; p[i * 3 + 2] = SOCCAR_BOOST_PADS[i].z; } init = true; }
		return p;
	}
}
