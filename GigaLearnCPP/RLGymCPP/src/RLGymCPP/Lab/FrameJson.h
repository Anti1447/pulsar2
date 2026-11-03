#pragma once
// ============================================================================
// FrameJson.h - one Lab frame as JSON. 2026-11-02 | Rev 1.0 | docs/REWARD_VIEWER_SPEC.md (P4-3)
//
// Split out of LabExport.h so that code which only needs to WRITE a frame (the render-mode stream tap, Rewards/RewardStreamTap.h) does not pull in
// LabSession, EnvSet and the rest of the Lab. The format is the one documented in LabExport.h (run JSON, "frames" items); LabExport.h includes this file.
// ============================================================================
#include "LabTypes.h"
#include "Scenario.h"

namespace RLGC {
namespace Lab {

	// `trace` (when non-null) is written as the frame's trace instead of f.trace, so a caller that already holds the step's StackStepTrace
	// does not have to copy it into a Frame. Pass nullptr for frame 0 (no trace is computed on a reset).
	inline nlohmann::json FrameToJsonWith(const Frame& f, const StackStepTrace* trace) {
		using nlohmann::json;
		json jf;
		jf["i"] = f.index; jf["terminal"] = f.terminal; jf["goal"] = f.goalScored;
		jf["ball"] = { { "pos", detail::V3J(f.ball.pos) }, { "vel", detail::V3J(f.ball.vel) } };
		json players = json::array();
		for (auto& p : f.players)
			players.push_back({ { "car", p.carId }, { "team", p.team }, { "pos", detail::V3J(p.pos) }, { "vel", detail::V3J(p.vel) },
				{ "forward", detail::V3J(p.forward) }, { "up", detail::V3J(p.up) }, { "boost", p.boost }, { "on_ground", p.onGround },
				{ "flipping", p.isFlipping }, { "jumped", p.hasJumped }, { "double_jumped", p.hasDoubleJumped }, { "flipped", p.hasFlipped },
				{ "demoed", p.isDemoed }, { "touched", p.ballTouchedStep } });
		jf["players"] = players;
		json acts = json::array();
		for (auto& a : f.actions) acts.push_back(json(std::vector<float>(a.begin(), a.end())));
		jf["actions"] = acts;
		jf["trace"] = trace ? ToJson(*trace) : json(nullptr);
		return jf;
	}

	inline nlohmann::json FrameToJson(const Frame& f) { return FrameToJsonWith(f, f.hasTrace ? &f.trace : nullptr); }

} // namespace Lab
} // namespace RLGC
