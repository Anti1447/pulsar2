#pragma once
#include "../Gamestates/GameState.h"

namespace RLGC {
	// The 45-degree corner walls are the planes |x| + |y| = 8064; a box clamp on SIDE_WALL_X / BACK_WALL_Y alone leaves spawns
	// beyond them (0.3-1.8% of support placements before 2026-11-17, docs/GCO_RESET_MIX.md P3). 7900 keeps a car centre
	// ~115 uu clear of the plane, the same bound RandomState uses.
	constexpr float CORNER_SPAWN_MAX = 7900.f;
	inline bool InsideCorners(const Vec& p) { return fabsf(p.x) + fabsf(p.y) <= CORNER_SPAWN_MAX; }
	// Pulls x, y toward the centre until the point is inside the corner planes (z unchanged; never moves it past a box clamp).
	inline Vec ClampToCorners(Vec p) {
		float s = fabsf(p.x) + fabsf(p.y);
		if (s > CORNER_SPAWN_MAX) { float k = CORNER_SPAWN_MAX / s; p.x *= k; p.y *= k; }
		return p;
	}

	class StateSetter {
	public:
		virtual void ResetArena(Arena* arena) = 0;
	};
}