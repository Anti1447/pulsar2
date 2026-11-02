#pragma once
// ============================================================================
// RewardTrace.h - what a reward reports about one step, for the Reward Lab.
// 2026-11-02 | Rev 1.0 | docs/REWARD_VIEWER_SPEC.md section 3.1 (P0).
//
// A reward OPTS IN by emitting through its `trace` pointer from inside the SAME GetReward() that
// produces the training value:
//
//     if (trace) trace->Gate(player.carId, "kickoff", false, "ball at centre, speed < 100");
//
// THE RULE: emission is observation only. It must never change a value, an early return, a map entry
// or a cooldown. There is no second "explain" implementation of the logic, and the reward is never called
// a second time for display (that would tick its cooldowns twice). The guard is tests/test_reward_trace.cpp:
// the same reward returns bit-identical values with a sink attached and without one.
//
// `trace` is null in training, so the cost of an instrumented reward is one predictable branch per
// emission site. Strings are `const char*` and are only copied into the sink when one is attached.
//
// The sink is NOT thread-safe: one sink per reward per arena, driven by one thread (the Lab host).
// Geometry is world-space (uu) and carries a semantic ROLE, not a colour, so the page owns the styling.
// ============================================================================
#include "../Gamestates/GameState.h"
#include <cstdint>
#include <string>
#include <vector>

namespace RLGC {

	enum class TraceRole : uint8_t { Info, Target, Corridor, Threshold, Ok, Blocked, Phi };
	enum class TracePrimKind : uint8_t { Point, Line, Arrow, Ring, Box, Text };

	inline const char* TraceRoleName(TraceRole r) {
		switch (r) {
		case TraceRole::Info: return "info"; case TraceRole::Target: return "target"; case TraceRole::Corridor: return "corridor";
		case TraceRole::Threshold: return "threshold"; case TraceRole::Ok: return "ok"; case TraceRole::Blocked: return "blocked";
		case TraceRole::Phi: return "phi";
		}
		return "info";
	}
	inline const char* TracePrimKindName(TracePrimKind k) {
		switch (k) {
		case TracePrimKind::Point: return "point"; case TracePrimKind::Line: return "line"; case TracePrimKind::Arrow: return "arrow";
		case TracePrimKind::Ring: return "ring"; case TracePrimKind::Box: return "box"; case TracePrimKind::Text: return "text";
		}
		return "point";
	}

	struct TraceComponent { std::string name; float value = 0; };
	struct TraceGate { std::string name; bool passed = false; std::string reason; };
	// Point: a. Line / Arrow: a -> b. Ring: centre a, radius `radius`, horizontal. Box: centre a, half extents b. Text: at a.
	struct TracePrim { TracePrimKind kind = TracePrimKind::Point; TraceRole role = TraceRole::Info; Vec a = {}, b = {}; float radius = 0; std::string text; };

	struct PlayerTrace {
		uint32_t carId = 0;
		std::vector<TraceComponent> components;   // named numeric parts of the value (e.g. severity, team factor)
		std::vector<TraceGate> gates;             // every condition that can zero or change the value, with pass/fail
		std::vector<std::string> events;          // things that happened this step (e.g. "paid", "cooldown_started")
		std::vector<TracePrim> prims;             // world-space geometry the reward used
		int primsDropped = 0;                     // emissions beyond MAX_PRIMS_PER_PLAYER
	};

	class TraceSink {
	public:
		// A verbose reward must not be able to flood the viewer's socket (spec section 8).
		static constexpr size_t MAX_PRIMS_PER_PLAYER = 256;

		std::vector<PlayerTrace> players;   // a handful of entries: linear lookup by carId

		void Clear() { players.clear(); }

		void Component(uint32_t carId, const char* name, float value) { For(carId).components.push_back({ name, value }); }
		void Gate(uint32_t carId, const char* name, bool passed, const char* reason = "") { For(carId).gates.push_back({ name, passed, reason }); }
		void Event(uint32_t carId, const char* name) { For(carId).events.push_back(name); }

		void Point(uint32_t carId, TraceRole role, Vec p) { Prim(carId, TracePrimKind::Point, role, p, Vec(0, 0, 0), 0, nullptr); }
		void Line(uint32_t carId, TraceRole role, Vec a, Vec b) { Prim(carId, TracePrimKind::Line, role, a, b, 0, nullptr); }
		void Arrow(uint32_t carId, TraceRole role, Vec from, Vec to) { Prim(carId, TracePrimKind::Arrow, role, from, to, 0, nullptr); }
		void Ring(uint32_t carId, TraceRole role, Vec centre, float radius) { Prim(carId, TracePrimKind::Ring, role, centre, Vec(0, 0, 0), radius, nullptr); }
		void Box(uint32_t carId, TraceRole role, Vec centre, Vec halfExtents) { Prim(carId, TracePrimKind::Box, role, centre, halfExtents, 0, nullptr); }
		void Text(uint32_t carId, TraceRole role, Vec at, const char* text) { Prim(carId, TracePrimKind::Text, role, at, Vec(0, 0, 0), 0, text); }

		PlayerTrace& For(uint32_t carId) {
			for (auto& p : players)
				if (p.carId == carId) return p;
			players.emplace_back();
			players.back().carId = carId;
			return players.back();
		}
		const PlayerTrace* Find(uint32_t carId) const {
			for (auto& p : players)
				if (p.carId == carId) return &p;
			return nullptr;
		}

	private:
		void Prim(uint32_t carId, TracePrimKind kind, TraceRole role, Vec a, Vec b, float radius, const char* text) {
			PlayerTrace& pt = For(carId);
			if (pt.prims.size() >= MAX_PRIMS_PER_PLAYER) { pt.primsDropped++; return; }
			TracePrim p; p.kind = kind; p.role = role; p.a = a; p.b = b; p.radius = radius; if (text) p.text = text;
			pt.prims.push_back(std::move(p));
		}
	};

} // namespace RLGC
