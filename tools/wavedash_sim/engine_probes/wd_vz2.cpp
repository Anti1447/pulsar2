// Dodge vertical mechanic: vz vs ticks since the press, 1-tick resolution around the onset, then coarse, for several vz0.
#include <RLGymCPP/Framework.h>
#include <cstdio>
#include <cmath>
using namespace RocketSim;
static Arena* A; static Car* C;
static CarState Air(float vx, float vz, float z0) {
	CarState s = C->GetState();
	s.pos = Vec(-3000, -3500, z0); s.vel = Vec(vx, 0, vz); s.angVel = Vec(0, 0, 0); s.boost = 0;
	s.rotMat = RotMat(Vec(1, 0, 0), Vec(0, 1, 0), Vec(0, 0, 1));
	s.isOnGround = false; s.hasJumped = false; s.hasDoubleJumped = false; s.hasFlipped = false; s.isJumping = false;
	s.airTimeSinceJump = 0; s.airTime = 0; s.flipTime = 0; s.jumpTime = 0; s.isFlipping = false;
	for (int i = 0; i < 4; i++) s.wheelsWithContact[i] = false;
	return s;
}
int main() {
	RocketSim::Init("build/collision_meshes");
	A = Arena::Create(GameMode::SOCCAR); C = A->AddCar(Team::BLUE);
	BallState bs; bs.pos = Vec(0, 4500, 93); A->ball->SetState(bs);
	for (float vz0 : { -200.f, 0.f }) {
		printf("vz0=%.0f: ticks since press -> vz (press at tick 0; gravity alone would add -5.4/tick)\n", vz0);
		C->SetState(Air(1000, vz0, 1700));
		float prev = vz0;
		for (int t = 0; t < 130; t++) {
			CarControls c = {}; if (t == 0) { c.jump = true; c.pitch = -1.f; } C->controls = c; A->Step(1);
			CarState s = C->GetState();
			bool show = (t >= 14 && t <= 34) || t % 8 == 0 || (t >= 74 && t <= 86);
			if (show) printf("  +%3d vz %8.2f  d=%6.2f flip=%d\n", t + 1, s.vel.z, s.vel.z - prev, (int)s.isFlipping);
			prev = s.vel.z;
		}
	}
	return 0;
}
