#pragma once
// ============================================================================
// LabTypes.h - what the Reward Lab records per decision step. 2026-11-02 | Rev 1.0 | docs/REWARD_VIEWER_SPEC.md (P1)
//
// A Frame is the state after one decision step plus the reward trace of that step. Frame 0 is the initial state and has no
// trace (no reward is computed on a reset). Fingerprint() renders a frame exactly (hex floats), so "replay equals the
// original" is a string comparison with no tolerance.
// ============================================================================
#include "../Rewards/RewardTraceStack.h"
#include "../Rewards/RewardTraceJson.h"
#include <cstdio>
#include <string>
#include <vector>

namespace RLGC {
namespace Lab {

	struct PlayerSnap {
		uint32_t carId = 0; int team = 0;
		Vec pos, vel, angVel, forward, up;
		float boost = 0;
		bool onGround = false, isFlipping = false, hasJumped = false, hasDoubleJumped = false, hasFlipped = false, isDemoed = false, ballTouchedStep = false;
	};
	struct BallSnap { Vec pos, vel, angVel; };

	struct Frame {
		int index = 0;                      // 0 = initial state; k = state after decision step k-1
		std::vector<Action> actions;        // the actions applied to reach this frame (empty for frame 0)
		std::vector<PlayerSnap> players;    // gs.players order = scenario cars order
		BallSnap ball;
		bool goalScored = false;
		uint8_t terminal = 0;               // TerminalType: 0 none, 1 normal, 2 truncated
		bool hasTrace = false;
		StackStepTrace trace;
	};

	inline PlayerSnap SnapPlayer(const Player& p) {
		PlayerSnap s;
		s.carId = p.carId; s.team = (int)p.team; s.pos = p.pos; s.vel = p.vel; s.angVel = p.angVel;
		s.forward = p.rotMat.forward; s.up = p.rotMat.up; s.boost = p.boost;
		s.onGround = p.isOnGround; s.isFlipping = p.isFlipping; s.hasJumped = p.hasJumped; s.hasDoubleJumped = p.hasDoubleJumped;
		s.hasFlipped = p.hasFlipped; s.isDemoed = p.isDemoed; s.ballTouchedStep = p.ballTouchedStep;
		return s;
	}

	namespace detail {
		inline void Hex(std::string& out, float v) { char b[40]; std::snprintf(b, sizeof(b), "%a,", (double)v); out += b; }
		inline void HexV(std::string& out, const Vec& v) { Hex(out, v.x); Hex(out, v.y); Hex(out, v.z); }
	}

	// Exact text form of a frame: state, actions, terminal flags and the full trace JSON.
	inline std::string Fingerprint(const Frame& f) {
		std::string o = "f" + std::to_string(f.index) + "|T" + std::to_string(f.terminal) + (f.goalScored ? "G" : "g") + "|";
		for (auto& a : f.actions) for (float v : a) detail::Hex(o, v);
		o += "|";
		for (auto& p : f.players) {
			o += std::to_string(p.carId) + ":" + std::to_string(p.team) + ":";
			detail::HexV(o, p.pos); detail::HexV(o, p.vel); detail::HexV(o, p.angVel); detail::HexV(o, p.forward); detail::HexV(o, p.up); detail::Hex(o, p.boost);
			o += std::string(p.onGround ? "1" : "0") + (p.isFlipping ? "1" : "0") + (p.hasJumped ? "1" : "0") + (p.hasDoubleJumped ? "1" : "0")
				+ (p.hasFlipped ? "1" : "0") + (p.isDemoed ? "1" : "0") + (p.ballTouchedStep ? "1" : "0") + ";";
		}
		o += "|B";
		detail::HexV(o, f.ball.pos); detail::HexV(o, f.ball.vel); detail::HexV(o, f.ball.angVel);
		if (f.hasTrace) { o += "|"; o += ToJson(f.trace).dump(); }
		return o;
	}

} // namespace Lab
} // namespace RLGC
