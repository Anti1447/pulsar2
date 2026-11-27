#pragma once

// Training-fleet team layout (2026-11-26, user-directed): which team sizes arena `index` plays.
// Blocks, in index order: [1v1][2v2][3v3][uneven]. The uneven block cycles through 2v1, 3v1 and 3v2 with the bigger team
// alternating between blue and orange (blue-orange: 2-1, 1-2, 3-1, 1-3, 3-2, 2-3), so a block of a multiple of 6 arenas gives
// every shape both colours equally often. With no uneven arenas the layout is exactly the older [1v1][2v2][3v3] one.
// Pure functions: no Arena, no RNG (tests/test_uneven_arenas.cpp).
namespace RLGC {
	struct TeamSizes {
		int blue = 1, orange = 1;
		bool Uneven() const { return blue != orange; }
	};

	constexpr int UNEVEN_SHAPES = 6;

	inline TeamSizes UnevenShape(int i) {
		constexpr int blue[UNEVEN_SHAPES] = { 2, 1, 3, 1, 3, 2 };
		constexpr int orange[UNEVEN_SHAPES] = { 1, 2, 1, 3, 2, 3 };
		int k = ((i % UNEVEN_SHAPES) + UNEVEN_SHAPES) % UNEVEN_SHAPES;
		return { blue[k], orange[k] };
	}

	inline TeamSizes ArenaTeamSizes(int index, int numGames, int num2v2, int num3v3, int numUneven) {
		if (numGames <= 0)
			return { 1, 1 };
		int unevenStart = numGames - numUneven;
		if (numUneven > 0 && index >= unevenStart)
			return UnevenShape(index - unevenStart);
		if (index >= unevenStart - num3v3)
			return { 3, 3 };
		if (index >= unevenStart - num3v3 - num2v2)
			return { 2, 2 };
		return { 1, 1 };
	}

	// Arenas in the uneven block for a requested share, rounded down to a multiple of 6
	inline int UnevenArenaCount(int numGames, float fraction) {
		if (!(fraction > 0.f) || numGames <= 0)
			return 0;
		int n = (int)(numGames * (fraction < 0.5f ? fraction : 0.5f));
		return n - n % UNEVEN_SHAPES;
	}
}
