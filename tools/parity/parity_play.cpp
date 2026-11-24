// G1 Part B runner (docs/gpu_env/G1_PREREG.md): plays a checkpoint (and optionally an older policy) in 1v1 / 2v2 / 3v3 on one physics arm
// and logs one CSV row per episode. Arms: cpu = RocketSim (whichever engine this binary links), gpu = RocketSim-CUDA through ArenaBatch.
// Both arms use the SAME code for obs, masks, action table, episode rules and metrics; only the physics differs. Starts are paired:
// `--starts f` reads the file if it exists, else samples the reset mix on the CPU and writes it.
// usage: parity_play --arm cpu|gpu --mode 1v1|2v2|3v3 --episodes N --blue DIR --orange DIR --starts FILE --out CSV [--arenas K] [--seed S]
#include <GigaLearnCPP/GGLBotOpponent.h>
#include <RLGymCPP/ActionParsers/DefaultAction.h>
#include <RLGymCPP/StateSetters/AirDrillState.h>
#include <RLGymCPP/StateSetters/AirPlayState.h>
#include <RLGymCPP/StateSetters/BallNearCarState.h>
#include <RLGymCPP/StateSetters/CombinedState.h>
#include <RLGymCPP/StateSetters/KickoffState.h>
#include <RLGymCPP/StateSetters/RandomState.h>
#ifdef GGL_GPU_ENV
#include "ArenaBatch.h"
#endif
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>

using namespace RLGC;

constexpr int TICK_SKIP = 4, NO_TOUCH_STEPS = 600 /*20 s*/, MAX_STEPS = 3600 /*120 s*/;

struct StartCar { float pos[3], vel[3], angVel[3], fwd[3], up[3], boost; uint8_t onGround, hasJumped, hasDoubleJumped, hasFlipped, isFlipping, isJumping; float jumpTime, flipTime, airTime, airTimeSinceJump; };
struct Start { StartCar cars[6]; float ball[9]; uint8_t kickoff; };

static int TeamOfSlot(int blue, int orange, int slot) { // same alternating layout as pulsar_gpu::ArenaBatch
	int b = blue, o = orange;
	for (int s = 0; s <= slot; s++) { bool wantBlue = (s % 2 == 0); int team = (wantBlue ? (b > 0) : (o == 0)) ? 0 : 1; if (s == slot) return team; if (team == 0) b--; else o--; }
	return 0;
}
static void CopyVec(float* o, const Vec& v) { o[0] = v.x; o[1] = v.y; o[2] = v.z; }

// ---------------------------------------------------------------- starts
static std::vector<Start> SampleStarts(int blue, int orange, int n) {
	CombinedState mix({ { new BallNearCarState(600, 900), 0.20f }, { new AirDrillState(), 0.20f }, { new AirPlayState(), 0.20f }, { new KickoffState(), 0.20f }, { new RandomState(true, true, false), 0.20f } });
	Arena* arena = Arena::Create(GameMode::SOCCAR);
	std::vector<Car*> cars;
	for (int s = 0; s < blue + orange; s++) cars.push_back(arena->AddCar(TeamOfSlot(blue, orange, s) == 0 ? Team::BLUE : Team::ORANGE));
	std::vector<Start> out(n);
	for (int e = 0; e < n; e++) {
		mix.ResetArena(arena);
		Start& st = out[e]; memset(&st, 0, sizeof(st));
		for (size_t i = 0; i < cars.size(); i++) {
			CarState c = cars[i]->GetState(); StartCar& d = st.cars[i];
			CopyVec(d.pos, c.pos); CopyVec(d.vel, c.vel); CopyVec(d.angVel, c.angVel); CopyVec(d.fwd, c.rotMat.forward); CopyVec(d.up, c.rotMat.up);
			d.boost = c.boost; d.onGround = c.isOnGround; d.hasJumped = c.hasJumped; d.hasDoubleJumped = c.hasDoubleJumped; d.hasFlipped = c.hasFlipped; d.isFlipping = c.isFlipping; d.isJumping = c.isJumping;
			d.jumpTime = c.jumpTime; d.flipTime = c.flipTime; d.airTime = c.airTime; d.airTimeSinceJump = c.airTimeSinceJump;
		}
		BallState b = arena->ball->GetState(); CopyVec(st.ball, b.pos); CopyVec(st.ball + 3, b.vel); CopyVec(st.ball + 6, b.angVel);
		st.kickoff = (fabsf(b.pos.x) < 1 && fabsf(b.pos.y) < 1 && b.vel.Length() < 1);
	}
	delete arena;
	return out;
}

// ---------------------------------------------------------------- engines
struct Engine {
	int blue, orange, arenas;
	virtual ~Engine() {}
	virtual void Init(int a, const Start& s) = 0;
	virtual void Controls(int a, const std::vector<Action>& act) = 0;
	virtual void Step() = 0;
	virtual void Refresh(int a, GameState& gs, const std::vector<Action>& act) = 0; // fills gs from the engine after Step / Init
	virtual bool Goal(int a) = 0;
	virtual void Flush() {} // make states set by Init visible to Refresh (the GPU mirrors are only valid after a download)
	virtual void BeforeAct(int a, GameState& gs, GGL::GGLBotOpponent& botB, GGL::GGLBotOpponent& botO) {}
};

struct CpuEngine : Engine {
	std::vector<Arena*> arenaVec; std::vector<std::vector<Car*>> carVec;
	CpuEngine(int b, int o, int n) { blue = b; orange = o; arenas = n;
		for (int a = 0; a < n; a++) { Arena* ar = Arena::Create(GameMode::SOCCAR); std::vector<Car*> cs; for (int s = 0; s < b + o; s++) cs.push_back(ar->AddCar(TeamOfSlot(b, o, s) == 0 ? Team::BLUE : Team::ORANGE)); arenaVec.push_back(ar); carVec.push_back(cs); } }
	~CpuEngine() { for (auto* a : arenaVec) delete a; }
	void Init(int a, const Start& s) override {
		for (size_t i = 0; i < carVec[a].size(); i++) {
			const StartCar& d = s.cars[i]; CarState c = {};
			c.pos = Vec(d.pos[0], d.pos[1], d.pos[2]); c.vel = Vec(d.vel[0], d.vel[1], d.vel[2]); c.angVel = Vec(d.angVel[0], d.angVel[1], d.angVel[2]);
			c.rotMat = RotMat::LookAt(Vec(d.fwd[0], d.fwd[1], d.fwd[2]), Vec(d.up[0], d.up[1], d.up[2]));
			c.boost = d.boost; c.isOnGround = d.onGround; for (int w = 0; w < 4; w++) c.wheelsWithContact[w] = d.onGround;
			c.hasJumped = d.hasJumped; c.hasDoubleJumped = d.hasDoubleJumped; c.hasFlipped = d.hasFlipped; c.isFlipping = d.isFlipping; c.isJumping = d.isJumping;
			c.jumpTime = d.jumpTime; c.flipTime = d.flipTime; c.airTime = d.airTime; c.airTimeSinceJump = d.airTimeSinceJump;
			carVec[a][i]->SetState(c);
		}
		BallState b = {}; b.pos = Vec(s.ball[0], s.ball[1], s.ball[2]); b.vel = Vec(s.ball[3], s.ball[4], s.ball[5]); b.angVel = Vec(s.ball[6], s.ball[7], s.ball[8]); arenaVec[a]->ball->SetState(b);
	}
	void Controls(int a, const std::vector<Action>& act) override { for (size_t i = 0; i < carVec[a].size(); i++) carVec[a][i]->controls = (CarControls)act[i]; }
	void Step() override { for (auto* ar : arenaVec) ar->Step(TICK_SKIP); }
	void Refresh(int a, GameState& gs, const std::vector<Action>& act) override { gs.ResetBeforeStep(); gs.UpdateFromArena(arenaVec[a], act, NULL); }
	bool Goal(int a) override { return arenaVec[a]->IsBallScored(); }
};

#ifdef GGL_GPU_ENV
struct GpuEngine : Engine {
	pulsar_gpu::ArenaBatch batch; std::vector<std::vector<uint64_t>> lastHit; int padMap[CommonValues::BOOST_LOCATIONS_AMOUNT];
	GpuEngine(int b, int o, int n) : batch({ { b, o, n } }), lastHit(n, std::vector<uint64_t>(b + o, ~0ULL)) { blue = b; orange = o; arenas = n;
		const float* pp = pulsar_gpu::ArenaBatch::PadPositions();
		for (int i = 0; i < CommonValues::BOOST_LOCATIONS_AMOUNT; i++) { int best = -1; float bd = 1e30f;
			for (int j = 0; j < pulsar_gpu::NUM_PADS; j++) { float dx = pp[j * 3] - CommonValues::BOOST_LOCATIONS[i].x, dy = pp[j * 3 + 1] - CommonValues::BOOST_LOCATIONS[i].y; float d = dx * dx + dy * dy; if (d < bd) { bd = d; best = j; } }
			padMap[i] = best; } }
	void Init(int a, const Start& s) override {
		std::vector<pulsar_gpu::CarData> cars(blue + orange);
		for (size_t i = 0; i < cars.size(); i++) {
			const StartCar& d = s.cars[i]; pulsar_gpu::CarData& c = cars[i];
			std::copy(d.pos, d.pos + 3, c.pos); std::copy(d.vel, d.vel + 3, c.vel); std::copy(d.angVel, d.angVel + 3, c.angVel);
			// quaternion from the basis with columns forward, right = up x forward, up
			float f[3] = { d.fwd[0], d.fwd[1], d.fwd[2] }, u[3] = { d.up[0], d.up[1], d.up[2] };
			float r[3] = { u[1] * f[2] - u[2] * f[1], u[2] * f[0] - u[0] * f[2], u[0] * f[1] - u[1] * f[0] };
			float m00 = f[0], m10 = f[1], m20 = f[2], m01 = r[0], m11 = r[1], m21 = r[2], m02 = u[0], m12 = u[1], m22 = u[2], tr = m00 + m11 + m22, w, x, y, z;
			if (tr > 0) { float q = sqrtf(tr + 1.f) * 2; w = 0.25f * q; x = (m21 - m12) / q; y = (m02 - m20) / q; z = (m10 - m01) / q; }
			else if (m00 > m11 && m00 > m22) { float q = sqrtf(1.f + m00 - m11 - m22) * 2; w = (m21 - m12) / q; x = 0.25f * q; y = (m01 + m10) / q; z = (m02 + m20) / q; }
			else if (m11 > m22) { float q = sqrtf(1.f + m11 - m00 - m22) * 2; w = (m02 - m20) / q; x = (m01 + m10) / q; y = 0.25f * q; z = (m12 + m21) / q; }
			else { float q = sqrtf(1.f + m22 - m00 - m11) * 2; w = (m10 - m01) / q; x = (m02 + m20) / q; y = (m12 + m21) / q; z = 0.25f * q; }
			c.quat[0] = w; c.quat[1] = x; c.quat[2] = y; c.quat[3] = z;
			c.boost = d.boost; c.team = (uint8_t)batch.TeamOfSlot(a, (int)i); c.onGround = d.onGround; for (int k = 0; k < 4; k++) c.wheels[k] = d.onGround;
			c.hasJumped = d.hasJumped; c.hasDoubleJumped = d.hasDoubleJumped; c.hasFlipped = d.hasFlipped; c.isFlipping = d.isFlipping; c.isJumping = d.isJumping;
			c.jumpTime = d.jumpTime; c.flipTime = d.flipTime; c.airTime = d.airTime; c.airTimeSinceJump = d.airTimeSinceJump;
			lastHit[a][i] = ~0ULL;
		}
		pulsar_gpu::BallData b; std::copy(s.ball, s.ball + 3, b.pos); std::copy(s.ball + 3, s.ball + 6, b.vel); std::copy(s.ball + 6, s.ball + 9, b.angVel);
		batch.SetState(a, cars, b); dirty = true;
	}
	void Controls(int a, const std::vector<Action>& act) override {
		std::vector<pulsar_gpu::ControlData> c(act.size());
		for (size_t i = 0; i < act.size(); i++) { c[i].throttle = act[i].throttle; c[i].steer = act[i].steer; c[i].pitch = act[i].pitch; c[i].yaw = act[i].yaw; c[i].roll = act[i].roll; c[i].jump = act[i].jump == 1; c[i].boost = act[i].boost == 1; c[i].handbrake = act[i].handbrake == 1; }
		batch.SetControls(a, c);
	}
	void Step() override { batch.Step(TICK_SKIP); batch.Sync(); dirty = false; }
	static RotMat Rot(const float* q) { float w = q[0], x = q[1], y = q[2], z = q[3];
		return RotMat(Vec(1 - 2 * (y * y + z * z), 2 * (x * y + w * z), 2 * (x * z - w * y)), Vec(2 * (x * y - w * z), 1 - 2 * (x * x + z * z), 2 * (y * z + w * x)), Vec(2 * (x * z + w * y), 2 * (y * z - w * x), 1 - 2 * (x * x + y * y))); }
	void Refresh(int a, GameState& gs, const std::vector<Action>& act) override {
		gs.ResetBeforeStep(); gs.lastArena = NULL; gs.prev = NULL; uint64_t tick = batch.Tick(a); gs.deltaTime = TICK_SKIP / 120.f; gs.lastTickCount = tick;
		const pulsar_gpu::BallData& b = batch.Ball(a);
		gs.ball.pos = Vec(b.pos[0], b.pos[1], b.pos[2]); gs.ball.vel = Vec(b.vel[0], b.vel[1], b.vel[2]); gs.ball.angVel = Vec(b.angVel[0], b.angVel[1], b.angVel[2]); gs.ball.rotMat = Rot(b.quat);
		int n = blue + orange; gs.players.resize(n);
		for (int i = 0; i < n; i++) {
			const pulsar_gpu::CarData& c = batch.Car(a, i); Player& p = gs.players[i];
			p.index = i; p.carId = i + 1; p.team = c.team ? Team::ORANGE : Team::BLUE; p.prev = NULL;
			p.pos = Vec(c.pos[0], c.pos[1], c.pos[2]); p.vel = Vec(c.vel[0], c.vel[1], c.vel[2]); p.angVel = Vec(c.angVel[0], c.angVel[1], c.angVel[2]); p.rotMat = Rot(c.quat);
			p.boost = c.boost; p.isOnGround = c.onGround; for (int k = 0; k < 4; k++) p.wheelsWithContact[k] = c.wheels[k];
			p.hasJumped = c.hasJumped; p.hasDoubleJumped = c.hasDoubleJumped; p.hasFlipped = c.hasFlipped; p.isJumping = c.isJumping; p.isFlipping = c.isFlipping;
			p.isDemoed = c.isDemoed; p.isSupersonic = c.isSupersonic; p.jumpTime = c.jumpTime; p.flipTime = c.flipTime; p.airTime = c.airTime; p.airTimeSinceJump = c.airTimeSinceJump;
			p.handbrakeVal = c.handbrake; p.demoRespawnTimer = c.demoRespawn; p.supersonicTime = c.supersonicTime; p.isAutoFlipping = c.isAutoFlipping; p.autoFlipTimer = c.autoFlipTimer; p.autoFlipTorqueScale = c.autoFlipTorqueScale;
			p.flipRelTorque = Vec(c.flipRelTorque[0], c.flipRelTorque[1], c.flipRelTorque[2]); p.worldContact.hasContact = c.worldContact; p.worldContact.contactNormal = Vec(c.worldNormal[0], c.worldNormal[1], c.worldNormal[2]);
			p.ballHitInfo.isValid = c.hitValid; p.ballHitInfo.tickCountWhenHit = c.hitTick; p.ballHitInfo.relativePosOnBall = Vec(c.hitRelPos[0], c.hitRelPos[1], c.hitRelPos[2]); p.ballHitInfo.extraHitVel = Vec(c.hitExtra[0], c.hitExtra[1], c.hitExtra[2]);
			// A touch this step = this car's hit tick changed since the last refresh (the engine's tick bookkeeping differs by one from the CPU's)
			bool touched = c.hitValid && c.hitTick != lastHit[a][i]; lastHit[a][i] = c.hitValid ? c.hitTick : ~0ULL;
			p.ballTouchedStep = touched; p.ballTouchedTick = touched; if (touched) gs.lastTouchCarID = p.carId;
			p.prevAction = act[i];
			p.eventState.bump = c.bump; p.eventState.bumped = c.bumped; p.eventState.demo = c.demo; p.eventState.demoed = c.demoed;
		}
		int N = CommonValues::BOOST_LOCATIONS_AMOUNT; gs.boostPads.resize(N); gs.boostPadsInv.resize(N); gs.boostPadTimers.resize(N); gs.boostPadTimersInv.resize(N);
		for (int i = 0; i < N; i++) { int j = padMap[i], ji = padMap[N - i - 1];
			gs.boostPads[i] = batch.PadActive(a)[j]; gs.boostPadsInv[i] = batch.PadActive(a)[ji]; gs.boostPadTimers[i] = batch.PadCooldown(a)[j]; gs.boostPadTimersInv[i] = batch.PadCooldown(a)[ji]; }
		gs.goalScored = batch.GoalScored(a);
	}
	bool Goal(int a) override { return batch.GoalScored(a); }
	bool dirty = false;
	void Flush() override { if (dirty) { batch.Sync(); dirty = false; } }
	void BeforeAct(int a, GameState& gs, GGL::GGLBotOpponent& bb, GGL::GGLBotOpponent& bo) override {
		std::vector<PhysState> slices(8); const float* p = batch.BallPredSlices(a);
		for (int k = 0; k < 8; k++) { slices[k].pos = Vec(p[k * 6], p[k * 6 + 1], p[k * 6 + 2]); slices[k].vel = Vec(p[k * 6 + 3], p[k * 6 + 4], p[k * 6 + 5]); }
		for (GGL::GGLBotOpponent* bot : { &bb, &bo }) { while ((int)bot->pulsarObs.size() <= a) bot->pulsarObs.push_back(std::make_unique<AdvancedObsPadded>(3, false, 8)); bot->pulsarObs[a]->SetExternalBallPred(slices); }
	}
};
#endif

// ---------------------------------------------------------------- main
struct Ep { int steps = 0, goal = -1, firstTouchTeam = -1, kickoff = 0, touchB = 0, touchO = 0, aerial = 0; double speedSum = 0, boostSpent = 0, carSteps = 0; int sinceTouch = 0; };

int main(int argc, char** argv) {
	std::map<std::string, std::string> args;
	for (int i = 1; i + 1 < argc; i += 2) args[argv[i]] = argv[i + 1];
	std::string arm = args["--arm"], mode = args["--mode"]; int episodes = std::stoi(args["--episodes"]); int K = args.count("--arenas") ? std::stoi(args["--arenas"]) : 256;
	int blue = mode[0] - '0', orange = mode[2] - '0';
	for (const char* path : { "collision_meshes", "../collision_meshes", "build/collision_meshes", "/home/nhafe/pulsar2/build/collision_meshes" })
		if (std::filesystem::exists(std::filesystem::path(path) / "soccar")) { RocketSim::Init(path, true); break; }

	std::vector<Start> starts; std::string sf = args["--starts"];
	{ std::ifstream in(sf, std::ios::binary); if (in) { starts.resize(episodes); in.read((char*)starts.data(), sizeof(Start) * episodes); if (!in) { starts.clear(); } } }
	if (starts.empty()) { starts = SampleStarts(blue, orange, episodes); std::ofstream out(sf, std::ios::binary); out.write((const char*)starts.data(), sizeof(Start) * starts.size()); std::cout << "wrote " << starts.size() << " starts to " << sf << "\n"; }

#if defined(PARITY_OBSDIFF) && defined(GGL_GPU_ENV)
	// Observation parity: identical starts on both engines, the SAME AdvancedObsPadded(3, false, 8); features 0..229 must agree to float precision (they depend
	// on state fields and pad ordering only), 230..277 are the ball prediction (physics of each engine).
	{
		CpuEngine cpu(blue, orange, 1); GpuEngine gpu(blue, orange, 1);
		AdvancedObsPadded obsC(3, false, 8), obsG(3, false, 8);
		const int P = blue + orange, NF = 278; std::vector<double> maxd(NF, 0.0); std::vector<int> bad(NF, 0); int rows = 0;
		for (int e = 0; e < episodes; e++) {
			cpu.Init(0, starts[e]); gpu.Init(0, starts[e]); gpu.Flush();
			GameState gc, gg; std::vector<Action> zero(P);
			cpu.Refresh(0, gc, zero); gpu.Refresh(0, gg, zero);
			std::vector<PhysState> slices(8); const float* pp = gpu.batch.BallPredSlices(0);
			for (int k = 0; k < 8; k++) { slices[k].pos = Vec(pp[k * 6], pp[k * 6 + 1], pp[k * 6 + 2]); slices[k].vel = Vec(pp[k * 6 + 3], pp[k * 6 + 4], pp[k * 6 + 5]); }
			obsG.SetExternalBallPred(slices);
			for (int i = 0; i < P; i++) {
				FList a = obsC.BuildObs(gc.players[i], gc), b = obsG.BuildObs(gg.players[i], gg); rows++;
				for (int f = 0; f < NF && f < (int)a.size() && f < (int)b.size(); f++) { double d = fabs((double)a[f] - b[f]); maxd[f] = std::max(maxd[f], d); if (d > 1e-3) bad[f]++; }
			}
		}
		std::cout << "obsdiff rows=" << rows << "\nfeatures 0..229 (state/pads/flags): worst features:\n";
		std::vector<int> idx(230); for (int i = 0; i < 230; i++) idx[i] = i; std::sort(idx.begin(), idx.end(), [&](int x, int y) { return maxd[x] > maxd[y]; });
		for (int k = 0; k < 12; k++) std::cout << "  f" << idx[k] << " max|d|=" << maxd[idx[k]] << " rows>1e-3: " << bad[idx[k]] << "\n";
		double pm = 0; int pb = 0; for (int f = 230; f < NF; f++) { pm = std::max(pm, maxd[f]); pb += bad[f]; }
		{ // error by slice: a flat profile means an alignment problem, growth with horizon means physics divergence
			std::vector<double> sum(8, 0.0), mx(8, 0.0); std::vector<long> cnt(8, 0);
			for (int e2 = 0; e2 < std::min(episodes, 400); e2++) {
				cpu.Init(0, starts[e2]); gpu.Init(0, starts[e2]); gpu.Flush(); GameState gc2, gg2; std::vector<Action> z2(P); cpu.Refresh(0, gc2, z2); gpu.Refresh(0, gg2, z2);
				std::vector<PhysState> sl(8); const float* q = gpu.batch.BallPredSlices(0); for (int k = 0; k < 8; k++) { sl[k].pos = Vec(q[k * 6], q[k * 6 + 1], q[k * 6 + 2]); sl[k].vel = Vec(q[k * 6 + 3], q[k * 6 + 4], q[k * 6 + 5]); }
				obsG.SetExternalBallPred(sl); FList a = obsC.BuildObs(gc2.players[0], gc2), b = obsG.BuildObs(gg2.players[0], gg2);
				for (int k = 0; k < 8; k++) for (int f = 0; f < 6; f++) { double d = fabs((double)a[230 + k * 6 + f] - b[230 + k * 6 + f]); sum[k] += d; cnt[k]++; mx[k] = std::max(mx[k], d); }
			}
			std::cout << "ball prediction error by slice (0.25 s each): mean|d| / max|d|\n"; for (int k = 0; k < 8; k++) std::cout << "  slice " << k + 1 << ": " << sum[k] / cnt[k] << " / " << mx[k] << "\n";
		}
		std::cout << "features 230..277 (ball prediction): max|d|=" << pm << " feature-rows>1e-3: " << pb << "\n";
		return 0;
	}
#endif
	torch::Device dev(torch::kCUDA, 0);
	GGL::GGLBotOpponent botB(GGL::GGLBotKind::PULSAR2, args["--blue"], dev), botO(GGL::GGLBotKind::PULSAR2, args["--orange"], dev);
	DefaultAction parser;
	std::unique_ptr<Engine> eng;
	if (arm == "cpu") eng.reset(new CpuEngine(blue, orange, K));
#ifdef GGL_GPU_ENV
	else eng.reset(new GpuEngine(blue, orange, K));
#else
	else { std::cerr << "built without GGL_GPU_ENV\n"; return 2; }
#endif
	const int P = blue + orange;
	std::vector<GameState> gs(K); std::vector<std::vector<Action>> act(K, std::vector<Action>(P)); std::vector<Ep> cur(K); std::vector<int> epIdx(K, -1);
	std::vector<std::vector<float>> prevBoost(K, std::vector<float>(P, 0)); std::vector<Ep> done; done.reserve(episodes);
	int nextEp = 0;
	std::vector<bool> maskB(K * P), maskO(K * P); std::vector<uint8_t> prevTerm(K, 0); std::vector<int> outB(K * P, 0), outO(K * P, 0);
	std::vector<int> needRefresh;
	auto load = [&](int a) {
		if (nextEp >= episodes) { epIdx[a] = -1; return; }
		epIdx[a] = nextEp++; cur[a] = Ep(); cur[a].kickoff = starts[epIdx[a]].kickoff;
		eng->Init(a, starts[epIdx[a]]);
		for (auto& x : act[a]) x = Action{};
		needRefresh.push_back(a);
	};
	// Newly loaded arenas must be read AFTER the engine has made their state visible (GPU: a download), never before.
	auto flushAndRefresh = [&]() {
		if (needRefresh.empty()) return;
		eng->Flush();
		for (int a : needRefresh) { eng->Refresh(a, gs[a], act[a]); for (int i = 0; i < P; i++) prevBoost[a][i] = gs[a].players[i].boost; }
		needRefresh.clear();
	};
	for (int a = 0; a < K; a++) load(a);
	flushAndRefresh();
	for (int a = 0; a < K; a++) for (int i = 0; i < P; i++) {
		bool isBlue = TeamOfSlot(blue, orange, i) == 0; maskB[a * P + i] = isBlue; maskO[a * P + i] = !isBlue;
		if ((gs[a].players[i].team == Team::BLUE) != isBlue) { std::cerr << "FATAL: arena " << a << " slot " << i << " has the wrong team after init\n"; return 3; }
	}
	auto t0 = std::chrono::steady_clock::now(); long long totalSteps = 0;
	while (true) {
		int active = 0; for (int a = 0; a < K; a++) active += epIdx[a] >= 0; if (!active) break;
		for (int a = 0; a < K; a++) if (epIdx[a] >= 0) eng->BeforeAct(a, gs[a], botB, botO);
		botB.Act(gs, maskB, prevTerm, outB); botO.Act(gs, maskO, prevTerm, outO);
		for (int a = 0; a < K; a++) {
			if (epIdx[a] < 0) continue;
			for (int i = 0; i < P; i++) { int idx = maskB[a * P + i] ? outB[a * P + i] : outO[a * P + i]; act[a][i] = parser.ParseAction(idx, gs[a].players[i], gs[a]); }
			eng->Controls(a, act[a]);
		}
		eng->Step();
		for (int a = 0; a < K; a++) {
			if (epIdx[a] < 0) continue;
			eng->Refresh(a, gs[a], act[a]); Ep& e = cur[a]; e.steps++; totalSteps++;
			bool anyTouch = false; int firstTeam = -1;
			for (int i = 0; i < P; i++) {
				const Player& p = gs[a].players[i];
				e.speedSum += p.vel.Length(); e.carSteps++; e.boostSpent += std::max(prevBoost[a][i] - p.boost, 0.f); prevBoost[a][i] = p.boost;
				if (p.ballTouchedStep) { anyTouch = true; if (firstTeam < 0) firstTeam = p.team == Team::BLUE ? 0 : 1; (p.team == Team::BLUE ? e.touchB : e.touchO)++; if (gs[a].ball.pos.z > 300 && !p.isOnGround) e.aerial++; }
			}
			if (anyTouch) { e.sinceTouch = 0; if (e.firstTouchTeam < 0) e.firstTouchTeam = firstTeam; } else e.sinceTouch++;
			bool goal = eng->Goal(a); int team = -1;
			if (goal) team = gs[a].ball.pos.y > 0 ? 0 : 1; // blue scores at +y
			if (goal || e.sinceTouch >= NO_TOUCH_STEPS || e.steps >= MAX_STEPS) { e.goal = goal ? team : -1; done.push_back(e); load(a); }
		}
		flushAndRefresh();
		if (totalSteps % 20000 < K) std::cerr << "\r" << done.size() << "/" << episodes << " episodes, " << totalSteps << " steps, " << (int)(totalSteps / std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count()) << " steps/s   " << std::flush;
	}
	std::ofstream out(args["--out"]);
	out << "arm,mode,steps,goal,firstTouchTeam,kickoff,touchB,touchO,aerial,speedSum,boostSpent,carSteps\n";
	for (const Ep& e : done) out << arm << "," << mode << "," << e.steps << "," << e.goal << "," << e.firstTouchTeam << "," << e.kickoff << "," << e.touchB << "," << e.touchO << "," << e.aerial << "," << e.speedSum << "," << e.boostSpent << "," << e.carSteps << "\n";
	std::cerr << "\ndone " << done.size() << " episodes, " << totalSteps << " steps\n";
	return 0;
}
