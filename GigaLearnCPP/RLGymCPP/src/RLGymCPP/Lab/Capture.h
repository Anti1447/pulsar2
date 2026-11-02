#pragma once
// ============================================================================
// Capture.h - turn a frame of a live session into a scenario file. 2026-11-02 | Rev 1.0 | docs/REWARD_VIEWER_SPEC.md (P4-2)
//
// "Save this frame", in two honest flavours:
//
//   replay  EXACT. The scenario is the session's start state plus the first k actions as its script, so running it reproduces frames 0..k
//           bit for bit (tests compare every frame fingerprint). It starts where the session started, not at frame k.
//   state   APPROXIMATE. The scenario starts AT frame k: every car's pose, velocity, spin, boost and jump/flip flags and the ball's state,
//           written as an authored scenario. What a scenario file cannot say restarts at its default: the suspension and wheel-contact history,
//           jump and flip timers, boost-pad cooldowns, a demolished car's respawn timer, and actions still in flight under action_delay.
//           A car on a wall or the ceiling restarts AIRBORNE (the setter derives "on ground" from z < 20). Each loss that applies to THIS
//           capture is listed in `warnings` and written into the scenario's description. Measured divergence: docs/REWARD_VIEWER_SPEC.md P4-2.
//
// A capture never writes expectations. An expectation is what a person says should happen, with the reason; a captured run would only echo
// what happened, which is the opposite of agreement. The scenario therefore has no expectations until a person adds them (the runner reports
// that as "no expectations", and Pack sign-off refuses it).
// ============================================================================
#include "RewardLab.h"
#include "LabExport.h"
#include <cctype>

namespace RLGC {
namespace Lab {

	struct CaptureResult {
		Scenario scenario;
		std::vector<std::string> warnings;
		std::string mode;   // "replay" | "state"
	};

	// File-name-safe scenario name: a-z 0-9 _ , starting with a letter or digit, at most 64 characters.
	inline bool ValidCaptureName(const std::string& n) {
		if (n.empty() || n.size() > 64 || !(std::isalnum((unsigned char)n[0]) && !std::isupper((unsigned char)n[0]))) return false;
		for (char c : n) if (!(std::islower((unsigned char)c) || std::isdigit((unsigned char)c) || c == '_')) return false;
		return true;
	}
	inline std::string DefaultCaptureName(const std::string& scenarioName, int frame, const std::string& mode) {
		std::string base;
		for (char c : scenarioName) base += std::isalnum((unsigned char)c) ? (char)std::tolower((unsigned char)c) : '_';
		if (base.empty() || !std::isalnum((unsigned char)base[0])) base = "s" + base;
		std::string n = "captured_" + base + "_f" + std::to_string(frame) + (mode == "state" ? "_state" : "_replay");
		return n.size() > 64 ? n.substr(0, 64) : n;
	}

	// What a state capture loses for THIS state (pure: takes the engine state and the scenario that produced it).
	inline std::vector<std::string> CaptureWarnings(const GameState& gs, const Scenario& sc) {
		std::vector<std::string> w;
		for (size_t i = 0; i < gs.players.size(); i++) {
			const Player& p = gs.players[i];
			const std::string car = "car " + std::to_string(i);
			if (p.isOnGround && p.pos.z >= 20.0f)
				w.push_back(car + " is on a wall or the ceiling (z " + detail::Num(p.pos.z) + "); the scenario setter restarts it AIRBORNE because it derives \"on ground\" from z < 20");
			if (!p.isOnGround && p.pos.z < 20.0f)
				w.push_back(car + " is airborne below z 20; the scenario setter restarts it ON THE GROUND");
			if (p.isJumping || p.isFlipping)
				w.push_back(car + " is mid-" + std::string(p.isFlipping ? "flip" : "jump") + "; the jump and flip timers are not restored");
			if (p.isDemoed) w.push_back(car + " is demolished; its respawn timer is not restored");
		}
		int off = 0;
		for (size_t i = 0; i < gs.boostPads.size(); i++) if (!gs.boostPads[i]) off++;
		if (off) w.push_back(std::to_string(off) + " boost pad(s) were on cooldown; the scenario starts with every pad active");
		if (sc.actionDelay > 0) w.push_back("action_delay is " + std::to_string(sc.actionDelay) + ": actions still in flight at the capture are lost");
		return w;
	}

	// EXACT: the session's start state plus its first `frame` actions as the script. frame in 1..Steps().
	inline bool CaptureReplay(const LabSession& s, int frame, const std::string& name, CaptureResult& out, std::string& err) {
		if (!ValidCaptureName(name)) { err = "name must be lowercase letters, digits and _ (starting with a letter or digit), at most 64 characters"; return false; }
		if (frame < 1 || frame > s.Steps()) { err = "an exact replay needs a frame in 1.." + std::to_string(s.Steps()) + " (frame 0 has no actions to replay: capture its state instead)"; return false; }
		Scenario c = s.GetScenario();
		c.name = name; c.steps = frame; c.actions.clear(); c.expect.clear();
		const size_t nCars = c.cars.size();
		std::vector<Action> prev(nCars, Action(CarControls{}));
		for (int k = 0; k < frame; k++) {
			const auto& step = s.ActionLog()[k];
			for (size_t car = 0; car < nCars; car++) {
				bool same = true;
				for (int j = 0; j < 8; j++) if (step[car][j] != prev[car][j]) same = false;
				if (same) continue;                                   // hold-until-next: only changes are entries
				ActionEntry e; e.car = (int)car; e.from = k; e.action = step[car]; c.actions.push_back(e);
				prev[car] = step[car];
			}
		}
		c.description = "Captured from \"" + s.GetScenario().name + "\" at frame " + std::to_string(frame) + " (exact replay: the same start state and the first "
			+ std::to_string(frame) + " actions as the script, so frames 0.." + std::to_string(frame) + " reproduce bit for bit). It starts where the original started, not at frame "
			+ std::to_string(frame) + ". No expectations: write them with an intent line before this counts as a test.";
		if (std::find(c.tags.begin(), c.tags.end(), "captured") == c.tags.end()) c.tags.push_back("captured");
		out.scenario = c; out.warnings.clear(); out.mode = "replay";
		return true;
	}

	// APPROXIMATE: a scenario that starts at `frame` (0..Steps()), `steps` long (0 = the steps the original had left, at least 1).
	inline bool CaptureState(const LabSession& s, int frame, const std::string& name, int steps, CaptureResult& out, std::string& err) {
		if (!ValidCaptureName(name)) { err = "name must be lowercase letters, digits and _ (starting with a letter or digit), at most 64 characters"; return false; }
		if (frame < 0 || frame > s.Steps()) { err = "frame " + std::to_string(frame) + " is outside 0.." + std::to_string(s.Steps()); return false; }
		if (steps < 0 || steps > 100000) { err = "steps must be 0 (the original's remaining steps) or 1..100000"; return false; }
		// the engine state AT that frame: replay a private copy, so the session being looked at is untouched
		LabSession tmp(s.GetScenario());
		if (!tmp.ok()) { err = "cannot rebuild the session to read its state"; return false; }
		for (int k = 0; k < frame; k++) if (!tmp.StepOnce(s.ActionLog()[k])) { err = "replay stopped early at step " + std::to_string(k); return false; }
		const GameState* gs = tmp.LiveState();
		if (!gs || gs->players.size() != s.GetScenario().cars.size()) { err = "no state to capture"; return false; }

		constexpr float RAD = 180.0f / 3.14159265358979f;
		Scenario c = s.GetScenario();
		c.name = name; c.actions.clear(); c.expect.clear();
		c.steps = steps > 0 ? steps : std::max(1, s.GetScenario().steps - frame);
		for (size_t i = 0; i < gs->players.size(); i++) {
			const Player& p = gs->players[i];
			CarSpec& cs = c.cars[i];
			cs.team = p.team; cs.pos = p.pos; cs.vel = p.vel; cs.angVel = p.angVel; cs.boost = p.boost;
			Angle a = Angle::FromRotMat(p.rotMat);
			cs.yawDeg = a.yaw * RAD; cs.pitchDeg = a.pitch * RAD; cs.rollDeg = a.roll * RAD;
			cs.hasJumped = p.hasJumped; cs.hasDoubleJumped = p.hasDoubleJumped; cs.hasFlipped = p.hasFlipped;
			cs.airTimeSinceJump = p.airTimeSinceJump; cs.isDemoed = p.isDemoed;
		}
		c.ball.pos = gs->ball.pos; c.ball.vel = gs->ball.vel; c.ball.angVel = gs->ball.angVel;
		out.warnings = CaptureWarnings(*gs, s.GetScenario());
		c.description = "Captured from \"" + s.GetScenario().name + "\" at frame " + std::to_string(frame) + " (APPROXIMATE state snapshot: poses, velocities, spin, boost and jump/flip flags;"
			" the suspension and contact history, jump/flip timers, boost-pad cooldowns and demo timers restart at defaults; measured on a ground capture the drift was about 0.1 uu over 10 steps, 0 in the air,"
			" but an event right after the capture, a jump or a ball touch, can amplify it to tens of uu). No expectations: write them with an intent line before this counts as a test.";
		for (auto& w : out.warnings) c.description += " NOTE: " + w + ".";
		if (std::find(c.tags.begin(), c.tags.end(), "captured") == c.tags.end()) c.tags.push_back("captured");
		out.scenario = c; out.mode = "state";
		return true;
	}

} // namespace Lab
} // namespace RLGC
