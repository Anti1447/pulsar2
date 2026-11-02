#pragma once
// Shared fixtures for reward scenario tests (see .claude/skills/reward-harness/SKILL.md).
//
// Everything here fabricates game states: no Arena, no RocketSim::Init, no GPU. A test drives the REAL
// reward header with PreStep() then GetReward() per player, exactly as EnvSet does (deltaTime = tickSkip/120).
// What this proves is the reward's logic on fabricated states. It does not prove the bot learns the
// mechanic, nor that the constants match real play (see the skill's "Limits").
#include <RLGymCPP/Gamestates/GameState.h>
#include <cmath>
#include <vector>

namespace RewardTest {
	using namespace RLGC;

	constexpr float STEP_DT = 8.0f / 120.0f; // tickSkip 8, actionDelay 0

	// Drag-free, restitution-0.6 ball path, one BallState per tick (the shape GameState::ballPred has).
	// Floor, side walls, back wall (outside the goal mouth) and ceiling reflect. A ball that rolls on the
	// floor keeps vz = 0. NOT the engine's ball: use it for logic tests, never to calibrate a threshold.
	inline std::vector<BallState> BallPath(Vec p, Vec v, int ticks = 241) {
		std::vector<BallState> out;
		const float dt = 1.0f / 120.0f;
		constexpr float R = 92.75f;
		for (int k = 0; k < ticks; k++) {
			BallState b; b.pos = p; b.vel = v; out.push_back(b);
			v.z += -650.0f * dt;
			p = p + v * dt;
			if (p.z < R)               { p.z = R;               v.z = -v.z * 0.6f; }
			if (p.z > 2044 - R)        { p.z = 2044 - R;        v.z = -v.z * 0.6f; }
			if (fabsf(p.x) > 4096 - R) { p.x = copysignf(4096 - R, p.x); v.x = -v.x * 0.6f; }
			if (fabsf(p.y) > 5120 - R && (fabsf(p.x) > 892.755f || p.z > 642.775f)) { p.y = copysignf(5120 - R, p.y); v.y = -v.y * 0.6f; }
			if (p.z <= R + 0.01f && fabsf(v.z) < 60.0f) v.z = 0.0f; // clean roll
		}
		return out;
	}

	// A grounded, upright, undemoed car. Set only the fields the reward under test reads.
	inline Player MakePlayer(uint32_t carId, int index, Team team, Vec pos) {
		Player p = {};
		p.carId = carId; p.index = index; p.team = team; p.pos = pos;
		p.vel = Vec(0, 0, 0); p.boost = 60; p.isOnGround = true;
		return p;
	}
}
