// Phase authority: can a planner with boost on/off per decision move the first-contact tick so that a decision boundary lands 1-2 ticks
// before it (the measured wavedash window), for decision spacing G = 8 (ts8), 4 (ts4)?
//   baseline: no shaping (ballistic), random decision-grid phase       -> P ~ 2/G
//   planner : choose boost on/off for each of the first L decisions (2^L patterns) to make some boundary p, with c - p in {1,2}.
// Setup: car falling with a flip available, nose pitched up (no pitch input), boost 100, z0 160-300, vz0 -300..0. Alignment only: it does not check
// the other wavedash conditions, so it is an UPPER bound on timing authority.
#include <RLGymCPP/Framework.h>
#include <cstdio>
#include <cmath>
#include <random>
#include <vector>
using namespace RocketSim;
static Arena* A; static Car* C;

static CarState Init(float thetaDeg, float vx, float vz, float z0) {
	CarState s = C->GetState(); float th = thetaDeg * 3.14159265f / 180.f;
	s.pos = Vec(-3000, -3500, z0); s.vel = Vec(vx, 0, vz); s.angVel = Vec(0, 0, 0); s.boost = 100;
	s.rotMat = RotMat(Vec(cosf(th), 0, sinf(th)), Vec(0, 1, 0), Vec(-sinf(th), 0, cosf(th)));
	s.isOnGround = false; s.hasJumped = false; s.hasDoubleJumped = false; s.hasFlipped = false; s.isJumping = false;
	s.airTimeSinceJump = 0; s.airTime = 0; s.flipTime = 0; s.jumpTime = 0; s.isFlipping = false;
	for (int i = 0; i < 4; i++) s.wheelsWithContact[i] = false;
	return s;
}
// first tick with any wheel contact; boost bit per decision for the first L decisions starting at tick phi, spacing G
static int Contact(const CarState& s0, int phi, int G, int L, unsigned pattern) {
	C->SetState(s0);
	for (int t = 1; t <= 260; t++) {
		int d = (t - 1 - phi) >= 0 ? (t - 1 - phi) / G : -1;       // decision index in force during tick t
		CarControls c = {}; c.boost = (d >= 0 && d < L && ((pattern >> d) & 1));
		C->controls = c; A->Step(1);
		CarState s = C->GetState();
		for (int i = 0; i < 4; i++) if (s.wheelsWithContact[i]) return t;
	}
	return -1;
}

int main() {
	RocketSim::Init("build/collision_meshes");
	A = Arena::Create(GameMode::SOCCAR); C = A->AddCar(Team::BLUE);
	BallState bs; bs.pos = Vec(0, 4500, 93); A->ball->SetState(bs);
	std::mt19937 rng(7); std::uniform_real_distribution<float> U(0.f, 1.f);
	const int NS = 250;
	printf("press window = ticks 1..2 before first contact (engine-measured, one geometry)\n");
	printf("G  lookahead  patterns | baseline (no shaping)  | planner can align | mean contact-tick spread over patterns (ticks)\n");
	struct Cfg { int G, L; } cfgs[] = { {8, 3}, {8, 6}, {4, 6}, {4, 3} };
	for (auto cf : cfgs) {
		int base = 0, plan = 0, total = 0; double spread = 0; int ns = 0;
		for (int n = 0; n < NS; n++) {
			float th = 15.f + 20.f * U(rng), vz = -300.f * U(rng), z0 = 160.f + 140.f * U(rng);
			CarState s0 = Init(th, 1000, vz, z0);
			for (int phi = 0; phi < cf.G; phi++) {
				int c0 = Contact(s0, phi, cf.G, cf.L, 0);
				if (c0 < 0) continue;
				total++;
				// baseline: is a boundary exactly 1 or 2 ticks before the ballistic contact (and after the shaping decisions)
				auto aligned = [&](int c) { for (int p = c - 2; p <= c - 1; p++) if (p >= phi + cf.L * cf.G && ((p - phi) % cf.G) == 0) return true; return false; };
				if (aligned(c0)) base++;
				bool ok = false; int lo = c0, hi = c0;
				for (unsigned pat = 0; pat < (1u << cf.L); pat++) {
					int c = Contact(s0, phi, cf.G, cf.L, pat);
					if (c < 0) continue; lo = std::min(lo, c); hi = std::max(hi, c);
					if (aligned(c)) ok = true;
				}
				plan += ok; spread += hi - lo; ns++;
			}
		}
		printf("%d  %3d ticks  %5u    |   %5.1f%%  (theory %4.1f%%) |  %5.1f%%           | %.1f\n", cf.G, cf.L * cf.G, 1u << cf.L, 100.0 * base / total, 200.0 / cf.G, 100.0 * plan / total, spread / std::max(1, ns));
	}
	return 0;
}
