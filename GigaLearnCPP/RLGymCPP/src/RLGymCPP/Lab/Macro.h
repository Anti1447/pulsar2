#pragma once
// ============================================================================
// Macro.h - tick-resolution action tracks, projection onto the decision grid, phase sweep.
// 2026-11-02 | Rev 1.0 | docs/REWARD_VIEWER_SPEC.md section 6 (P3, headless part)
//
// A Macro is what a person (or a recorder) did at ENGINE tick resolution (120 Hz): one Action per car per tick.
// The bot cannot do that: it decides every `tickSkip` ticks and holds. So a macro is never run against the reward as it
// stands; it is PROJECTED onto the decision grid first:
//
//     decision step k (of skip S, phase P) uses the macro's action at tick  P + k*S,  held for S ticks   ("hold-last")
//
// Anything the macro did between two sampled ticks (a 1-tick jump press, a short dodge input) is lost. That loss is the
// point: it is how "a human can do this" is told apart from "the bot can do this". RunGrid plays one projection; SweepPhases
// plays all S phases and reports how often the reward paid under each.
//
// File form (*.macro.json, schema 1): hold-until-next entries per car, in ticks (same idea as the scenario's actions).
//   {"schema":1,"kind":"lab_macro","name":"...","description":"...","cars":1,"ticks":240,
//    "entries":[{"car":0,"tick":0,"action":{"throttle":1,...}}, ...]}
// ============================================================================
#include "RewardLab.h"
#include <map>

namespace RLGC {
namespace Lab {

	constexpr int MACRO_SCHEMA = 1;
	constexpr int MACRO_TICK_RATE = 120;

	inline Action IdleAction() { return Action(CarControls{}); }

	inline bool SameAction(const Action& a, const Action& b) {
		return a.throttle == b.throttle && a.steer == b.steer && a.pitch == b.pitch && a.yaw == b.yaw && a.roll == b.roll
			&& a.jump == b.jump && a.boost == b.boost && a.handbrake == b.handbrake;
	}

	struct Macro {
		std::string name, description;
		int cars = 1;
		std::vector<std::vector<Action>> perTick;   // perTick[t][car]

		int Ticks() const { return (int)perTick.size(); }
		// The action a car holds at engine tick `t`; idle at or past the end of the macro (it ended, nothing is held on).
		Action At(int car, int t) const {
			if (t < 0 || t >= Ticks() || car < 0 || car >= cars) return IdleAction();
			return perTick[t][car];
		}
	};

	// What the car did, one decision step at a time, expanded to ticks (each step held for tickSkip ticks). With tickSkip 1 this
	// is an exact tick-level recording.
	inline Macro MacroFromLog(const std::vector<std::vector<Action>>& log, int tickSkip, const std::string& name = "recorded") {
		Macro m; m.name = name;
		m.cars = log.empty() ? 1 : (int)log[0].size();
		for (auto& step : log) for (int k = 0; k < tickSkip; k++) m.perTick.push_back(step);
		return m;
	}

	// Decision-grid projection. Returns one action vector (per car) per decision step; the macro ticks consumed are
	// phase, phase+skip, ... while < Ticks(). `phase` must be in [0, skip).
	inline std::vector<std::vector<Action>> ProjectToGrid(const Macro& m, int skip, int phase) {
		std::vector<std::vector<Action>> out;
		if (skip < 1 || phase < 0 || phase >= skip) return out;
		for (int t = phase; t < m.Ticks(); t += skip) {
			std::vector<Action> step;
			for (int c = 0; c < m.cars; c++) step.push_back(m.At(c, t));
			out.push_back(step);
		}
		return out;
	}

	// ---- file form ----------------------------------------------------------------------------------------------------
	namespace detail {
		inline nlohmann::json ActionJ(const Action& a) {
			return { { "throttle", a.throttle }, { "steer", a.steer }, { "pitch", a.pitch }, { "yaw", a.yaw }, { "roll", a.roll },
				{ "jump", a.jump }, { "boost", a.boost }, { "handbrake", a.handbrake } };
		}
	}

	inline nlohmann::json MacroToJson(const Macro& m) {
		using nlohmann::json;
		json j = { { "schema", MACRO_SCHEMA }, { "kind", "lab_macro" }, { "name", m.name }, { "description", m.description },
			{ "cars", m.cars }, { "ticks", m.Ticks() }, { "entries", json::array() } };
		for (int c = 0; c < m.cars; c++) {
			Action prev = IdleAction();
			for (int t = 0; t < m.Ticks(); t++) {
				const Action& a = m.perTick[t][c];
				if (!SameAction(a, prev)) { j["entries"].push_back({ { "car", c }, { "tick", t }, { "action", detail::ActionJ(a) } }); prev = a; }
			}
		}
		return j;
	}

	inline bool ParseMacro(const nlohmann::json& j, Macro& out, std::vector<std::string>& errors) {
		using nlohmann::json;
		detail::Reader R{ errors };
		const size_t before = errors.size();
		if (!j.is_object()) { errors.push_back("$: expected an object"); return false; }
		R.Strict(j, "", { "schema", "kind", "name", "description", "cars", "ticks", "entries" });
		Macro m; int schema = 0, ticks = 0;
		if (!j.contains("schema") || !R.Int(j["schema"], "schema", schema)) R.Err("schema", "required");
		else if (schema != MACRO_SCHEMA) R.Err("schema", "unsupported (this build reads schema " + std::to_string(MACRO_SCHEMA) + ")");
		if (!j.contains("kind") || !j["kind"].is_string() || j["kind"].get<std::string>() != "lab_macro") R.Err("kind", "must be \"lab_macro\"");
		if (j.contains("name")) R.Str(j["name"], "name", m.name);
		if (j.contains("description")) R.Str(j["description"], "description", m.description);
		if (!j.contains("cars") || !R.Int(j["cars"], "cars", m.cars, 1, 6)) R.Err("cars", "required, 1..6");
		if (!j.contains("ticks") || !R.Int(j["ticks"], "ticks", ticks, 0, 1000000)) R.Err("ticks", "required, 0..1000000");
		struct E { int car, tick; Action a; };
		std::vector<E> es;
		if (!j.contains("entries") || !j["entries"].is_array()) R.Err("entries", "required array");
		else for (size_t i = 0; i < j["entries"].size(); i++) {
			const json& e = j["entries"][i]; const std::string p = "entries[" + std::to_string(i) + "]";
			if (!e.is_object()) { R.Err(p, "expected an object"); continue; }
			R.Strict(e, p, { "car", "tick", "action" });
			E x{ 0, 0, IdleAction() };
			if (!e.contains("car") || !R.Int(e["car"], p + ".car", x.car, 0, 5) || x.car >= m.cars) R.Err(p + ".car", "required, an existing car");
			if (!e.contains("tick") || !R.Int(e["tick"], p + ".tick", x.tick, 0, ticks > 0 ? ticks - 1 : 0)) R.Err(p + ".tick", "required, inside the macro");
			if (!e.contains("action") || !e["action"].is_object()) { R.Err(p + ".action", "expected an object"); continue; }
			const json& a = e["action"];
			R.Strict(a, p + ".action", { "throttle", "steer", "pitch", "yaw", "roll", "jump", "boost", "handbrake" });
			static const char* axes[5] = { "throttle", "steer", "pitch", "yaw", "roll" };
			static const char* btns[3] = { "jump", "boost", "handbrake" };
			double v;
			for (int k = 0; k < 5; k++) if (a.contains(axes[k]) && R.Num(a[axes[k]], p + ".action." + axes[k], v, -1, 1)) x.a[k] = (float)v;
			for (int k = 0; k < 3; k++) if (a.contains(btns[k]) && R.Num(a[btns[k]], p + ".action." + btns[k], v, 0, 1)) {
				if (v != 0 && v != 1) R.Err(p + ".action." + btns[k], "buttons are 0 or 1"); else x.a[5 + k] = (float)v;
			}
			es.push_back(x);
		}
		if (errors.size() != before) return false;
		// entries must be in order per car (a later entry for the same tick would silently win)
		std::map<int, int> lastTick;
		for (auto& e : es) {
			if (lastTick.count(e.car) && e.tick <= lastTick[e.car]) { errors.push_back("entries: car " + std::to_string(e.car) + " has entries out of order or two at tick " + std::to_string(e.tick)); return false; }
			lastTick[e.car] = e.tick;
		}
		m.perTick.assign(ticks, std::vector<Action>(m.cars, IdleAction()));
		for (int c = 0; c < m.cars; c++) {
			Action cur = IdleAction(); size_t ei = 0;
			std::vector<E> mine; for (auto& e : es) if (e.car == c) mine.push_back(e);
			for (int t = 0; t < ticks; t++) {
				while (ei < mine.size() && mine[ei].tick <= t) cur = mine[ei++].a;
				m.perTick[t][c] = cur;
			}
		}
		out = std::move(m);
		return true;
	}

	inline bool ParseMacroText(const std::string& text, Macro& out, std::vector<std::string>& errors) {
		nlohmann::json j;
		try { j = nlohmann::json::parse(text); } catch (const std::exception& ex) { errors.push_back(std::string("not valid JSON: ") + ex.what()); return false; }
		return ParseMacro(j, out, errors);
	}

	// ---- playing a macro -----------------------------------------------------------------------------------------------
	// A scenario with the same situation, rewards and expectations, but no script and a new tick skip / length.
	inline Scenario WithGrid(const Scenario& s, int skip, int steps) {
		Scenario g = s; g.actions.clear(); g.tickSkip = skip; g.steps = steps;
		if (g.actionDelay >= skip) g.actionDelay = 0;
		return g;
	}

	// One run of a projection: the session (frames, traces) plus what was played.
	struct GridRun {
		int skip = 0, phase = 0, steps = 0;
		std::vector<Frame> frames;
		std::vector<std::vector<Action>> played;
		std::vector<ExpectResult> expectations;   // evaluated on the SCENARIO's expectations; empty if the grid is not the scenario's
		std::vector<std::string> errors;
	};

	// Plays `m` projected to (skip, phase) on the scenario's situation. Expectations (frame-indexed in decision steps of the
	// scenario's own tick skip) are only evaluated when skip equals the scenario's tickSkip.
	inline GridRun RunGrid(const Scenario& s, const Macro& m, int skip, int phase) {
		GridRun r; r.skip = skip; r.phase = phase;
		if ((int)s.cars.size() != m.cars) { r.errors.push_back("macro has " + std::to_string(m.cars) + " car(s), scenario has " + std::to_string(s.cars.size())); return r; }
		auto steps = ProjectToGrid(m, skip, phase);
		if (steps.empty()) { r.errors.push_back("projection is empty (skip " + std::to_string(skip) + ", phase " + std::to_string(phase) + ", macro " + std::to_string(m.Ticks()) + " ticks)"); return r; }
		r.steps = (int)steps.size();
		Scenario g = WithGrid(s, skip, r.steps);
		LabSession session(g);
		if (!session.ok()) { r.errors = session.errors(); return r; }
		for (auto& a : steps) if (!session.StepOnce(a)) break;
		r.frames = session.Frames();
		r.played = steps;
		if (skip == s.tickSkip) { Scenario keep = g; keep.expect = s.expect; r.expectations = EvaluateAll(keep, r.frames); }
		return r;
	}

	// Per-run summary used by the sweep report: weighted sum per reward per car, event counts, and whether any reward paid.
	struct PhaseSummary {
		int phase = 0, steps = 0;
		std::vector<std::string> rewardNames;
		std::vector<std::vector<double>> weightedSum;   // [reward][car]
		std::map<std::string, int> eventCounts;         // "reward / event" -> count over all cars
		int expectPass = 0, expectFail = 0;
		bool paid = false;                              // some reward's weighted sum is non-zero for some car
	};

	inline PhaseSummary Summarize(const GridRun& r, size_t nCars) {
		PhaseSummary s; s.phase = r.phase; s.steps = r.steps;
		for (auto& f : r.frames) if (f.hasTrace) {
			if (s.rewardNames.empty()) {
				for (auto& rw : f.trace.rewards) s.rewardNames.push_back(rw.name);
				s.weightedSum.assign(s.rewardNames.size(), std::vector<double>(nCars, 0.0));
			}
			for (size_t i = 0; i < f.trace.rewards.size(); i++) {
				for (size_t c = 0; c < nCars && c < f.trace.rewards[i].weighted.size(); c++) s.weightedSum[i][c] += f.trace.rewards[i].weighted[c];
				for (auto& pt : f.trace.rewards[i].sink.players) for (auto& ev : pt.events) s.eventCounts[f.trace.rewards[i].name + " / " + ev]++;
			}
		}
		for (auto& row : s.weightedSum) for (double v : row) if (v != 0.0) s.paid = true;
		for (auto& e : r.expectations) (e.ok ? s.expectPass : s.expectFail)++;
		return s;
	}

	struct SweepResult {
		int skip = 0;
		std::vector<PhaseSummary> phases;
		std::vector<std::string> errors;
		int PaidCount() const { int n = 0; for (auto& p : phases) n += p.paid ? 1 : 0; return n; }
	};

	// All `skip` phases of the projection.
	inline SweepResult SweepPhases(const Scenario& s, const Macro& m, int skip) {
		SweepResult sw; sw.skip = skip;
		for (int p = 0; p < skip; p++) {
			GridRun r = RunGrid(s, m, skip, p);
			if (!r.errors.empty()) { for (auto& e : r.errors) sw.errors.push_back("phase " + std::to_string(p) + ": " + e); continue; }
			sw.phases.push_back(Summarize(r, s.cars.size()));
		}
		return sw;
	}

	inline std::string SweepText(const SweepResult& sw) {
		std::ostringstream o;
		o << "phase sweep, skip " << sw.skip << ": " << sw.PaidCount() << " / " << sw.phases.size() << " phases paid\n";
		for (auto& e : sw.errors) o << "  ERROR " << e << "\n";
		for (auto& p : sw.phases) {
			o << "  phase " << p.phase << " (" << p.steps << " steps): " << (p.paid ? "PAID" : "no pay");
			if (p.expectPass + p.expectFail) o << "  expectations " << p.expectPass << " pass / " << p.expectFail << " fail";
			o << "\n";
			for (size_t i = 0; i < p.rewardNames.size(); i++) {
				o << "      " << p.rewardNames[i] << ":";
				for (double v : p.weightedSum[i]) o << " " << detail::Num(v);
				o << "\n";
			}
			for (auto& kv : p.eventCounts) o << "      event " << kv.first << " x" << kv.second << "\n";
		}
		return o.str();
	}

} // namespace Lab
} // namespace RLGC
