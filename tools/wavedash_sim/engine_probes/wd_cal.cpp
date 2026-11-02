// Calibration probes for the 2D wavedash sim and the availability question.
//  P1  flip availability WITHOUT a jump (car falling from the ceiling): does a flip press register, and until when?
//  P2  flip impulse vs speed: dvx, dvz applied at the flip (front flip, level car, airborne)
//  P3  flip rotation profile: pitch angle vs tick during a front flip
//  P4  pitch control response: angle / angular speed under pitch = +1 held
#include <RLGymCPP/Framework.h>
#include <cstdio>
#include <cmath>
#include <vector>
using namespace RocketSim;
static Arena* A; static Car* C;

static CarState Air(float thetaDeg, float vx, float z0, bool hasJumped) {
	CarState s = C->GetState(); float th = thetaDeg * 3.14159265f / 180.f;
	s.pos = Vec(-3000, -3500, z0); s.vel = Vec(vx, 0, 0); s.angVel = Vec(0, 0, 0); s.boost = 0;
	s.rotMat = RotMat(Vec(cosf(th), 0, sinf(th)), Vec(0, 1, 0), Vec(-sinf(th), 0, cosf(th)));
	s.isOnGround = false; s.hasJumped = hasJumped; s.hasDoubleJumped = false; s.hasFlipped = false; s.isJumping = false;
	s.airTimeSinceJump = 0; s.airTime = 0; s.flipTime = 0; s.jumpTime = 0; s.isFlipping = false;
	for (int i = 0; i < 4; i++) s.wheelsWithContact[i] = false;
	return s;
}
static float Pitch(const CarState& s) { return atan2f(s.rotMat.forward.z, s.rotMat.forward.x); }

int main() {
	RocketSim::Init("build/collision_meshes");
	A = Arena::Create(GameMode::SOCCAR); C = A->AddCar(Team::BLUE);
	BallState bs; bs.pos = Vec(0, 4500, 93); A->ball->SetState(bs);

	printf("== P1 flip availability without a jump (hasJumped=false) falling from z=1900, vz=0: press a front flip at tick F\n");
	printf("   F   t(s)  z@F    airTime airTimeSinceJump  hasJumped  registered\n");
	for (int F : { 5, 60, 100, 150, 151, 200, 250, 280 }) {
		C->SetState(Air(0, 800, 1900, false)); bool reg = false; CarState pre; bool landed = false;
		for (int t = 0; t < 330 && !landed; t++) {
			if (t == F) pre = C->GetState();
			CarControls c = {}; if (t == F) { c.jump = true; c.pitch = -1.f; }
			C->controls = c; A->Step(1); CarState s = C->GetState(); if (s.isFlipping) reg = true; if (s.isOnGround) landed = true;
		}
		printf("%4d  %.2f  %6.0f  %7.3f  %15.3f  %9d  %d%s\n", F, F / 120.f, pre.pos.z, pre.airTime, pre.airTimeSinceJump, (int)pre.hasJumped, (int)reg, landed ? "  (landed before end)" : "");
	}
	printf("   same, but hasJumped=true (a real jump) with airTimeSinceJump=0 at the start:\n");
	for (int F : { 100, 150, 151, 200, 280 }) {
		C->SetState(Air(0, 800, 1900, true)); bool reg = false;
		for (int t = 0; t < 330; t++) { CarControls c = {}; if (t == F) { c.jump = true; c.pitch = -1.f; } C->controls = c; A->Step(1); if (C->GetState().isFlipping) reg = true; if (C->GetState().isOnGround) break; }
		printf("   F=%3d t=%.2fs registered=%d\n", F, F / 120.f, (int)reg);
	}

	printf("\n== P2 flip impulse vs speed (level car, front flip at tick 2, z0=1500): velocity change vs no-flip control after 6 ticks\n");
	printf("   v0     dvx    dvz\n");
	for (float v0 : { 0.f, 500.f, 1000.f, 1500.f, 2000.f, 2200.f, 2300.f }) {
		C->SetState(Air(0, v0, 1500, true)); for (int t = 0; t < 8; t++) { CarControls c = {}; C->controls = c; A->Step(1); } CarState ctl = C->GetState();
		C->SetState(Air(0, v0, 1500, true)); for (int t = 0; t < 8; t++) { CarControls c = {}; if (t == 2) { c.jump = true; c.pitch = -1.f; } C->controls = c; A->Step(1); } CarState fl = C->GetState();
		printf("  %5.0f  %6.1f  %6.1f\n", v0, fl.vel.x - ctl.vel.x, fl.vel.z - ctl.vel.z);
	}
	printf("   backward flip (pitch +1) at v0=1000 and 0:\n");
	for (float v0 : { 0.f, 1000.f, 2000.f }) {
		C->SetState(Air(0, v0, 1500, true)); for (int t = 0; t < 8; t++) { CarControls c = {}; C->controls = c; A->Step(1); } CarState ctl = C->GetState();
		C->SetState(Air(0, v0, 1500, true)); for (int t = 0; t < 8; t++) { CarControls c = {}; if (t == 2) { c.jump = true; c.pitch = 1.f; } C->controls = c; A->Step(1); } CarState fl = C->GetState();
		printf("   back flip v0=%5.0f dvx=%7.1f dvz=%6.1f\n", v0, fl.vel.x - ctl.vel.x, fl.vel.z - ctl.vel.z);
	}

	printf("\n== P3 front-flip rotation: pitch angle (deg, unwrapped; negative = nose down) every 4 ticks from the press, v0=1000, z0=1500\n");
	{
		C->SetState(Air(0, 1000, 1500, true)); float prev = 0, unw = 0; bool first = true; std::vector<float> ang; std::vector<int> flp;
		for (int t = 0; t < 100; t++) {
			CarControls c = {}; if (t == 2) { c.jump = true; c.pitch = -1.f; } C->controls = c; A->Step(1);
			CarState s = C->GetState(); float a = Pitch(s); if (first) { unw = a; first = false; } else { float d = a - prev; while (d > 3.14159f) d -= 6.28318f; while (d < -3.14159f) d += 6.28318f; unw += d; } prev = a;
			ang.push_back(unw * 57.2958f); flp.push_back(s.isFlipping);
		}
		for (int t = 2; t < 100; t += 6) printf("  tick+%2d angle %7.1f  isFlipping=%d\n", t - 2, ang[t], flp[t]);
		int endF = -1; for (int t = 3; t < 100; t++) if (!flp[t] && flp[t - 1]) { endF = t - 2; break; }
		printf("  flip ends %d ticks after the press (%.3f s)\n", endF, endF / 120.f);
	}

	printf("\n== P4 pitch control: pitch=+1 held from rest in the air (z0=1500), angle and pitch rate every 6 ticks\n");
	{
		C->SetState(Air(0, 0, 1500, true)); float a0 = 0, prevA = 0;
		for (int t = 0; t < 72; t++) {
			CarControls c = {}; c.pitch = 1.f; C->controls = c; A->Step(1); CarState s = C->GetState(); float a = Pitch(s);
			if (t % 6 == 5) printf("  tick %2d angle %6.1f deg   rate %5.2f rad/s\n", t + 1, a * 57.2958f, (a - prevA) / (6.f / 120.f));
			if (t % 6 == 5) prevA = a; (void)a0;
		}
		printf("  release: pitch=0 for 24 ticks, rate every 6 ticks\n");
		float pa = Pitch(C->GetState());
		for (int t = 0; t < 24; t++) { CarControls c = {}; C->controls = c; A->Step(1); float a = Pitch(C->GetState()); if (t % 6 == 5) { printf("  rate %5.2f rad/s\n", (a - pa) / (6.f / 120.f)); pa = a; } }
	}
	return 0;
}
