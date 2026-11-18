// Phase 0 of docs/GCO_LOOKAHEAD_TEST.md. Offline, CPU only, no checkpoint, no GPU, no trainer contact.
//
//   (a) cost of rsf_arena_snapshot / rsf_arena_restore / Step(8 ticks) for 1v1, 2v2, 3v3 arenas, 1 and T threads
//   (b) exactness: snapshot at a mid-game state, drive a control stream, record the end state, restore, drive the SAME
//       stream again, compare bit-for-bit (ball, cars, boost pads). Raw FFI snapshot only: not ArenaSnapshot's
//       per-field fixups, so this is what a search rollout would really get.
//   (c) the rollout unit cost: snapshot + H steps + restore, in rollouts per second per thread (no policy inference).
//
// Usage (from build/, where collision_meshes/ lives):
//   OMP_NUM_THREADS=1 nice -n 19 ./LookaheadBench [threads=4] [seconds=3]
#include <GigaLearnCPP/Framework.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <thread>
#include <vector>

using Clock = std::chrono::steady_clock;

struct Gen {
	uint64_t s;
	explicit Gen(uint64_t seed) : s(seed) {}
	uint32_t Next() { s = s * 6364136223846793005ULL + 1442695040888963407ULL; return (uint32_t)(s >> 33); }
	float U() { return (float)(Next() % 2001) / 1000.f - 1.f; }
	bool Chance(uint32_t n) { return (Next() % n) == 0; }
	CarControls Make() {
		CarControls c = {};
		c.throttle = U(); c.steer = U(); c.pitch = U(); c.yaw = U(); c.roll = U();
		c.boost = Chance(3); c.jump = Chance(5); c.handbrake = Chance(8);
		return c;
	}
};

static Arena* MakeArena(int teamSize, int seed) {
	Arena* a = Arena::Create(GameMode::SOCCAR);
	for (int i = 0; i < teamSize; i++) { a->AddCar(Team::BLUE); a->AddCar(Team::ORANGE); }
	a->ResetToRandomKickoff(seed);
	return a;
}

// One decision step of 8 ticks under per-step controls (what a search rollout does).
static void DecisionStep(Arena* a, Gen& g) {
	for (Car* c : a->_cars) c->controls = g.Make();
	a->Step(8);
}

// Bit-exact digest of everything a rollout could read back.
static std::vector<float> Digest(Arena* a) {
	std::vector<float> d;
	auto pushPhys = [&](const PhysState& p) {
		d.insert(d.end(), { p.pos.x, p.pos.y, p.pos.z, p.vel.x, p.vel.y, p.vel.z, p.angVel.x, p.angVel.y, p.angVel.z });
		d.insert(d.end(), { p.rotMat.forward.x, p.rotMat.forward.y, p.rotMat.forward.z, p.rotMat.up.x, p.rotMat.up.y, p.rotMat.up.z });
	};
	pushPhys(a->ball->GetState());
	for (Car* c : a->_cars) {
		CarState s = c->GetState();
		pushPhys(s);
		d.insert(d.end(), { s.boost, s.jumpTime, s.flipTime, s.airTime, s.airTimeSinceJump, s.handbrakeVal, s.demoRespawnTimer,
			(float)s.isOnGround, (float)s.hasJumped, (float)s.hasDoubleJumped, (float)s.hasFlipped, (float)s.isFlipping,
			(float)s.isJumping, (float)s.isDemoed });
	}
	for (BoostPad* p : a->_boostPads) { BoostPadState s = p->GetState(); d.push_back((float)s.isActive); d.push_back(s.cooldown); }
	return d;
}

static bool Same(const std::vector<float>& x, const std::vector<float>& y, size_t* firstDiff) {
	if (x.size() != y.size()) { *firstDiff = (size_t)-1; return false; }
	for (size_t i = 0; i < x.size(); i++)
		if (memcmp(&x[i], &y[i], sizeof(float)) != 0) { *firstDiff = i; return false; }
	return true;
}

static double NowS() { return std::chrono::duration<double>(Clock::now().time_since_epoch()).count(); }

struct Cost { double stepUs, snapUs, restoreUs, rolloutPerSec; };

// Timed loops on one thread, one arena. `H` = rollout horizon in decision steps.
static Cost Bench(int teamSize, int H, double seconds, int seed) {
	Arena* a = MakeArena(teamSize, seed);
	Gen g(seed * 7919 + 1);
	for (int i = 0; i < 60; i++) DecisionStep(a, g); // leave the kickoff, get a live state

	Cost r = {};
	// Step cost
	{
		int n = 0; double t0 = NowS();
		while (NowS() - t0 < seconds / 3) { DecisionStep(a, g); n++; if (n % 400 == 0) a->ResetToRandomKickoff(seed + n); }
		r.stepUs = (NowS() - t0) / n * 1e6;
	}
	// Snapshot / restore cost
	{
		int n = 0; double t0 = NowS();
		while (NowS() - t0 < seconds / 6) { RsfArenaSnap* s = rsf_arena_snapshot(a->_rs); rsf_arena_snapshot_free(s); n++; }
		r.snapUs = (NowS() - t0) / n * 1e6;
		RsfArenaSnap* s = rsf_arena_snapshot(a->_rs);
		n = 0; t0 = NowS();
		while (NowS() - t0 < seconds / 6) { rsf_arena_restore(a->_rs, s); n++; }
		r.restoreUs = (NowS() - t0) / n * 1e6;
		rsf_arena_snapshot_free(s);
	}
	// Whole rollout unit: snapshot + H decision steps + restore
	{
		int n = 0; double t0 = NowS();
		while (NowS() - t0 < seconds / 3) {
			RsfArenaSnap* s = rsf_arena_snapshot(a->_rs);
			for (int i = 0; i < H; i++) DecisionStep(a, g);
			rsf_arena_restore(a->_rs, s);
			rsf_arena_snapshot_free(s);
			n++;
		}
		r.rolloutPerSec = n / (NowS() - t0);
	}
	delete a;
	return r;
}

// Exactness: contacts, boost pickups, flips and demos need to actually happen, so report what the drive exercised.
static bool Exactness(int teamSize, int seed, int steps, bool* exercised) {
	Arena* a = MakeArena(teamSize, seed);
	Gen warm(seed + 11);
	for (int i = 0; i < 90; i++) DecisionStep(a, warm);

	RsfArenaSnap* snap = rsf_arena_snapshot(a->_rs);
	std::vector<float> start = Digest(a);

	int padChanges = 0;
	auto padSig = [&]() { int n = 0; for (BoostPad* p : a->_boostPads) n += p->GetState().isActive ? 0 : 1; return n; };
	int p0 = padSig();

	Gen g1(seed * 31 + 5);
	for (int i = 0; i < steps; i++) { DecisionStep(a, g1); padChanges += (padSig() != p0); }
	std::vector<float> end1 = Digest(a);

	rsf_arena_restore(a->_rs, snap);
	std::vector<float> back = Digest(a);
	size_t diff = 0;
	bool restoredOk = Same(start, back, &diff);

	Gen g2(seed * 31 + 5);
	for (int i = 0; i < steps; i++) DecisionStep(a, g2);
	std::vector<float> end2 = Digest(a);
	bool replayOk = Same(end1, end2, &diff);

	*exercised = padChanges > 0;
	printf("  %dv%d seed %d: restore==start %s | replay(%d steps)==first %s%s | boost pads changed during drive: %s\n",
		teamSize, teamSize, seed, restoredOk ? "OK" : "FAIL", steps, replayOk ? "OK" : "FAIL",
		replayOk ? "" : " (first differing digest index printed below)", padChanges > 0 ? "yes" : "no");
	if (!replayOk) printf("    first diff at digest index %zd of %zu\n", (ssize_t)diff, end1.size());
	rsf_arena_snapshot_free(snap);
	delete a;
	return restoredOk && replayOk;
}

int main(int argc, char** argv) {
	int threads = argc > 1 ? atoi(argv[1]) : 4;
	double seconds = argc > 2 ? atof(argv[2]) : 3.0;

	bool inited = false;
	for (const char* path : { "collision_meshes", "../collision_meshes", "build/collision_meshes" })
		if (std::filesystem::exists(std::filesystem::path(path) / "soccar")) { RocketSim::Init(path); inited = true; break; }
	if (!inited) { printf("collision_meshes not found (run from build/)\n"); return 2; }

	printf("== (b) exactness: restore, then replay the same control stream, bit-for-bit ==\n");
	bool allOk = true;
	for (int team = 1; team <= 3; team++)
		for (int seed : { 1, 2, 3, 4 }) {
			bool ex = false;
			allOk &= Exactness(team, seed * 100 + team, 240, &ex); // 240 decision steps = 128 s of play
		}
	printf("exactness: %s\n\n", allOk ? "ALL PASS" : "FAILURES");

	printf("== (a)+(c) cost, 1 thread (rollout horizon H = 30 steps = 2 s, and H = 60 = 4 s) ==\n");
	printf("%-5s %10s %10s %12s %16s %16s\n", "mode", "step8 us", "snap us", "restore us", "rollouts/s H=30", "rollouts/s H=60");
	for (int team = 1; team <= 3; team++) {
		Cost c30 = Bench(team, 30, seconds, 5 + team);
		Cost c60 = Bench(team, 60, seconds, 5 + team);
		printf("%dv%d   %10.1f %10.1f %12.1f %16.1f %16.1f\n", team, team, c30.stepUs, c30.snapUs, c30.restoreUs, c30.rolloutPerSec, c60.rolloutPerSec);
	}

	printf("\n== thread scaling: %d threads, independent arenas, 1v1 and 3v3, H = 30 ==\n", threads);
	for (int team : { 1, 3 }) {
		std::vector<double> rate(threads, 0);
		std::vector<std::thread> ts;
		for (int t = 0; t < threads; t++)
			ts.emplace_back([&, t]() { rate[t] = Bench(team, 30, seconds, 100 + t).rolloutPerSec; });
		for (auto& th : ts) th.join();
		double sum = 0; for (double r : rate) sum += r;
		printf("%dv%d  total %.1f rollouts/s over %d threads (%.1f per thread)\n", team, team, sum, threads, sum / threads);
	}
	return allOk ? 0 : 1;
}
