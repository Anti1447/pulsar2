#pragma once
// ============================================================================
// Scenario.h - the scenario file of the Reward Lab (docs/REWARD_VIEWER_SPEC.md section 5, P1).
// 2026-11-02 | Rev 1.0
//
// A scenario is a situation plus an action script plus the rewards to run plus what a human expects. One file
// serves the headless runner (Claude reads the trace) and, later, the viewer (the human watches it). It is
// "authored": declarative and portable, with rounded numbers a person can write. (A "captured" exact-engine
// snapshot format is a later step.)
//
// Schema 1, strict: an unknown key anywhere, a missing required key, or a value out of range is an ERROR with its
// path ("cars[1].pos: ...") rather than being ignored, so a typo cannot silently run a different scenario.
//
//   {"schema":1, "name":"offside_lone_car", "description":"...", "tags":["offside"],
//    "tick_skip":8, "action_delay":0, "steps":30,                       // steps = decision steps (tick_skip engine ticks each)
//    "cars":[{"team":"blue","pos":[x,y,z],"vel":[..],"ang_vel":[..],"angle_deg":{"yaw":90,"pitch":0,"roll":0},
//             "boost":33,"has_jumped":false,"has_double_jumped":false,"has_flipped":false,"air_time_since_jump":0,"is_demoed":false}],
//    "ball":{"pos":[..],"vel":[..],"ang_vel":[..]},
//    "actions":[{"car":0,"from":0,"action":{"throttle":1,"steer":0,"pitch":0,"yaw":0,"roll":0,"jump":0,"boost":1,"handbrake":0}}],
//        // per car, "hold until the next entry": the action applies from decision step `from` on; before the first entry the car idles
//    "rewards":[{"name":"OffsidePenalty","weight":0.15},{"name":"CarEnergyPotentialReward","weight":75,"zero_sum":0.3,"gamma":0.9969}],
//    "expect":[{"kind":"reward_sum","reward":"OffsidePenalty","car":0,"steps":[1,30],"min":-1.2,"max":-0.8,
//               "intent":"a lone car 1000 uu upfield pays about -1 per step"}]}
//
// Step numbering: decision step k (0-based) is the action applied during env step k; FRAME k+1 is the state after it. Frame 0 is
// the initial state (no reward is computed there). Expectation "steps":[a,b] are FRAME numbers, inclusive.
// ============================================================================
#include "../Framework.h"
#include "../Gamestates/GameState.h"
#include "../BasicTypes/Action.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <climits>
#include <cmath>
#include <set>
#include <string>
#include <vector>

namespace RLGC {
namespace Lab {

	constexpr int SCENARIO_SCHEMA = 1;

	struct CarSpec {
		Team team = Team::BLUE;
		Vec pos = Vec(0, 0, 17), vel = Vec(0, 0, 0), angVel = Vec(0, 0, 0);
		float yawDeg = 0, pitchDeg = 0, rollDeg = 0;
		float boost = 33.33f;
		bool hasJumped = false, hasDoubleJumped = false, hasFlipped = false, isDemoed = false;
		float airTimeSinceJump = 0;
	};
	struct BallSpec { Vec pos = Vec(0, 0, 93), vel = Vec(0, 0, 0), angVel = Vec(0, 0, 0); };
	struct ActionEntry { int car = 0; int from = 0; Action action = Action(CarControls{}); };
	struct RewardSpec { std::string name; float weight = 1; float zeroSum = -1; /* < 0: not wrapped; else teamSpirit */ float gamma = 0.9969f; };

	struct Expectation {
		std::string kind;                 // reward_sum | reward_abs_max | component | gate | event
		std::string intent;               // REQUIRED: one plain-English line, written by a human, saying why this should hold
		std::string reward;               // a reward's readable name, or "*total" for the weighted stack total (reward_sum / reward_abs_max)
		std::string of = "raw";           // raw (before ZeroSum) | wrapped | weighted
		int car = 0;                      // index into the scenario's cars
		int from = 1, to = INT_MAX;       // inclusive FRAME range
		double min = -INFINITY, max = INFINITY;
		std::string gate, mode = "all";   // gate: emitted gate name; mode: all | any
		bool passed = true;               // gate: expected pass/fail
		std::string component;            // component: emitted component name (every emission in range must lie within [min, max])
		std::string event;                // event: emitted event name
		int countMin = 0, countMax = INT_MAX;   // event: how many frames in range carry it
	};

	struct Scenario {
		int schema = SCENARIO_SCHEMA;
		std::string name, description;
		std::vector<std::string> tags;
		int tickSkip = 8, actionDelay = 0, steps = 30;
		std::vector<CarSpec> cars;
		BallSpec ball;
		std::vector<ActionEntry> actions;
		std::vector<RewardSpec> rewards;
		std::vector<Expectation> expect;
	};

	namespace detail {
		struct Reader {
			std::vector<std::string>& errs;
			void Err(const std::string& path, const std::string& msg) { errs.push_back(path + ": " + msg); }

			// Reject any key not in `allowed`.
			void Strict(const nlohmann::json& j, const std::string& path, std::initializer_list<const char*> allowed) {
				std::set<std::string> ok(allowed.begin(), allowed.end());
				for (auto it = j.begin(); it != j.end(); ++it)
					if (!ok.count(it.key())) Err(path.empty() ? it.key() : path + "." + it.key(), "unknown key");
			}
			bool Num(const nlohmann::json& j, const std::string& path, double& out, double lo = -INFINITY, double hi = INFINITY) {
				if (!j.is_number()) { Err(path, "expected a number"); return false; }
				double v = j.get<double>();
				if (!std::isfinite(v) || v < lo || v > hi) { Err(path, "out of range"); return false; }
				out = v; return true;
			}
			bool Int(const nlohmann::json& j, const std::string& path, int& out, int lo = INT_MIN, int hi = INT_MAX) {
				if (!j.is_number_integer()) { Err(path, "expected an integer"); return false; }
				long long v = j.get<long long>();
				if (v < lo || v > hi) { Err(path, "out of range"); return false; }
				out = (int)v; return true;
			}
			bool Str(const nlohmann::json& j, const std::string& path, std::string& out) {
				if (!j.is_string()) { Err(path, "expected a string"); return false; }
				out = j.get<std::string>(); return true;
			}
			bool Bool(const nlohmann::json& j, const std::string& path, bool& out) {
				if (!j.is_boolean()) { Err(path, "expected true or false"); return false; }
				out = j.get<bool>(); return true;
			}
			bool V3(const nlohmann::json& j, const std::string& path, Vec& out) {
				if (!j.is_array() || j.size() != 3) { Err(path, "expected an array of 3 numbers"); return false; }
				double v[3];
				for (int i = 0; i < 3; i++) if (!Num(j[i], path + "[" + std::to_string(i) + "]", v[i])) return false;
				out = Vec((float)v[0], (float)v[1], (float)v[2]); return true;
			}
		};
		inline nlohmann::json V3J(const Vec& v) { return nlohmann::json::array({ v.x, v.y, v.z }); }
	}

	// Parse and validate. Returns false (and fills `errors`) on any problem; `out` is only meaningful on success.
	inline bool ParseScenario(const nlohmann::json& j, Scenario& out, std::vector<std::string>& errors) {
		using nlohmann::json;
		detail::Reader R{ errors };
		const size_t errsBefore = errors.size();
		Scenario s;
		if (!j.is_object()) { errors.push_back("$: expected an object"); return false; }
		R.Strict(j, "", { "schema", "name", "description", "tags", "tick_skip", "action_delay", "steps", "cars", "ball", "actions", "rewards", "expect" });

		if (!j.contains("schema")) R.Err("schema", "missing");
		else if (R.Int(j["schema"], "schema", s.schema) && s.schema != SCENARIO_SCHEMA) R.Err("schema", "unsupported schema " + std::to_string(s.schema) + " (this reader knows " + std::to_string(SCENARIO_SCHEMA) + ")");
		if (!j.contains("name")) R.Err("name", "missing"); else if (R.Str(j["name"], "name", s.name) && s.name.empty()) R.Err("name", "must not be empty");
		if (j.contains("description")) R.Str(j["description"], "description", s.description);
		if (j.contains("tags")) {
			if (!j["tags"].is_array()) R.Err("tags", "expected an array of strings");
			else for (size_t i = 0; i < j["tags"].size(); i++) { std::string t; if (R.Str(j["tags"][i], "tags[" + std::to_string(i) + "]", t)) s.tags.push_back(t); }
		}
		if (j.contains("tick_skip")) R.Int(j["tick_skip"], "tick_skip", s.tickSkip, 1, 120);
		if (j.contains("action_delay")) R.Int(j["action_delay"], "action_delay", s.actionDelay, 0, 119);
		if (s.actionDelay >= s.tickSkip) R.Err("action_delay", "must be smaller than tick_skip");
		if (!j.contains("steps")) R.Err("steps", "missing"); else R.Int(j["steps"], "steps", s.steps, 1, 100000);

		// cars
		if (!j.contains("cars") || !j["cars"].is_array() || j["cars"].empty()) R.Err("cars", "expected a non-empty array");
		else {
			int blue = 0, orange = 0;
			for (size_t i = 0; i < j["cars"].size(); i++) {
				const json& c = j["cars"][i]; const std::string p = "cars[" + std::to_string(i) + "]";
				CarSpec cs;
				if (!c.is_object()) { R.Err(p, "expected an object"); continue; }
				R.Strict(c, p, { "team", "pos", "vel", "ang_vel", "angle_deg", "boost", "has_jumped", "has_double_jumped", "has_flipped", "air_time_since_jump", "is_demoed" });
				if (!c.contains("team")) R.Err(p + ".team", "missing");
				else { std::string t; if (R.Str(c["team"], p + ".team", t)) { if (t == "blue") cs.team = Team::BLUE; else if (t == "orange") cs.team = Team::ORANGE; else R.Err(p + ".team", "expected \"blue\" or \"orange\""); } }
				(cs.team == Team::BLUE ? blue : orange)++;
				if (c.contains("pos")) R.V3(c["pos"], p + ".pos", cs.pos); else R.Err(p + ".pos", "missing");
				if (c.contains("vel")) R.V3(c["vel"], p + ".vel", cs.vel);
				if (c.contains("ang_vel")) R.V3(c["ang_vel"], p + ".ang_vel", cs.angVel);
				if (c.contains("angle_deg")) {
					const json& a = c["angle_deg"];
					if (!a.is_object()) R.Err(p + ".angle_deg", "expected an object");
					else {
						R.Strict(a, p + ".angle_deg", { "yaw", "pitch", "roll" });
						double v;
						if (a.contains("yaw") && R.Num(a["yaw"], p + ".angle_deg.yaw", v, -360, 360)) cs.yawDeg = (float)v;
						if (a.contains("pitch") && R.Num(a["pitch"], p + ".angle_deg.pitch", v, -90, 90)) cs.pitchDeg = (float)v;
						if (a.contains("roll") && R.Num(a["roll"], p + ".angle_deg.roll", v, -180, 180)) cs.rollDeg = (float)v;
					}
				}
				double v;
				if (c.contains("boost") && R.Num(c["boost"], p + ".boost", v, 0, 100)) cs.boost = (float)v;
				if (c.contains("has_jumped")) R.Bool(c["has_jumped"], p + ".has_jumped", cs.hasJumped);
				if (c.contains("has_double_jumped")) R.Bool(c["has_double_jumped"], p + ".has_double_jumped", cs.hasDoubleJumped);
				if (c.contains("has_flipped")) R.Bool(c["has_flipped"], p + ".has_flipped", cs.hasFlipped);
				if (c.contains("is_demoed")) R.Bool(c["is_demoed"], p + ".is_demoed", cs.isDemoed);
				if (c.contains("air_time_since_jump") && R.Num(c["air_time_since_jump"], p + ".air_time_since_jump", v, 0, 10)) cs.airTimeSinceJump = (float)v;
				s.cars.push_back(cs);
			}
			if (blue > 3 || orange > 3) R.Err("cars", "at most 3 cars per team");
		}

		// ball
		if (!j.contains("ball")) R.Err("ball", "missing");
		else if (!j["ball"].is_object()) R.Err("ball", "expected an object");
		else {
			R.Strict(j["ball"], "ball", { "pos", "vel", "ang_vel" });
			if (j["ball"].contains("pos")) R.V3(j["ball"]["pos"], "ball.pos", s.ball.pos); else R.Err("ball.pos", "missing");
			if (j["ball"].contains("vel")) R.V3(j["ball"]["vel"], "ball.vel", s.ball.vel);
			if (j["ball"].contains("ang_vel")) R.V3(j["ball"]["ang_vel"], "ball.ang_vel", s.ball.angVel);
		}

		// actions
		if (j.contains("actions")) {
			if (!j["actions"].is_array()) R.Err("actions", "expected an array");
			else for (size_t i = 0; i < j["actions"].size(); i++) {
				const json& e = j["actions"][i]; const std::string p = "actions[" + std::to_string(i) + "]";
				ActionEntry ae;
				if (!e.is_object()) { R.Err(p, "expected an object"); continue; }
				R.Strict(e, p, { "car", "from", "action" });
				if (e.contains("car")) R.Int(e["car"], p + ".car", ae.car, 0, (int)s.cars.size() - 1); else R.Err(p + ".car", "missing");
				if (e.contains("from")) R.Int(e["from"], p + ".from", ae.from, 0, 100000); else R.Err(p + ".from", "missing");
				if (!e.contains("action") || !e["action"].is_object()) R.Err(p + ".action", "expected an object");
				else {
					const json& a = e["action"];
					R.Strict(a, p + ".action", { "throttle", "steer", "pitch", "yaw", "roll", "jump", "boost", "handbrake" });
					Action act = Action(CarControls{});
					static const char* axes[5] = { "throttle", "steer", "pitch", "yaw", "roll" };
					static const char* btns[3] = { "jump", "boost", "handbrake" };
					double v;
					for (int k = 0; k < 5; k++) if (a.contains(axes[k]) && R.Num(a[axes[k]], p + ".action." + axes[k], v, -1, 1)) act[k] = (float)v;
					for (int k = 0; k < 3; k++) if (a.contains(btns[k]) && R.Num(a[btns[k]], p + ".action." + btns[k], v, 0, 1)) {
						if (v != 0 && v != 1) R.Err(p + ".action." + btns[k], "buttons are 0 or 1"); else act[5 + k] = (float)v;
					}
					ae.action = act;
				}
				s.actions.push_back(ae);
			}
		}

		// rewards
		if (j.contains("rewards")) {
			if (!j["rewards"].is_array()) R.Err("rewards", "expected an array");
			else for (size_t i = 0; i < j["rewards"].size(); i++) {
				const json& r = j["rewards"][i]; const std::string p = "rewards[" + std::to_string(i) + "]";
				RewardSpec rs;
				if (!r.is_object()) { R.Err(p, "expected an object"); continue; }
				R.Strict(r, p, { "name", "weight", "zero_sum", "gamma" });
				if (r.contains("name")) R.Str(r["name"], p + ".name", rs.name); else R.Err(p + ".name", "missing");
				double v;
				if (r.contains("weight") && R.Num(r["weight"], p + ".weight", v)) rs.weight = (float)v;
				if (r.contains("zero_sum") && R.Num(r["zero_sum"], p + ".zero_sum", v, 0, 1)) rs.zeroSum = (float)v;
				if (r.contains("gamma") && R.Num(r["gamma"], p + ".gamma", v, 0, 1)) rs.gamma = (float)v;
				s.rewards.push_back(rs);
			}
		}

		// expectations
		if (j.contains("expect")) {
			if (!j["expect"].is_array()) R.Err("expect", "expected an array");
			else for (size_t i = 0; i < j["expect"].size(); i++) {
				const json& e = j["expect"][i]; const std::string p = "expect[" + std::to_string(i) + "]";
				Expectation ex;
				if (!e.is_object()) { R.Err(p, "expected an object"); continue; }
				R.Strict(e, p, { "kind", "intent", "reward", "of", "car", "steps", "min", "max", "gate", "mode", "passed", "component", "event", "count" });
				if (e.contains("kind")) R.Str(e["kind"], p + ".kind", ex.kind); else R.Err(p + ".kind", "missing");
				if (ex.kind != "reward_sum" && ex.kind != "reward_abs_max" && ex.kind != "component" && ex.kind != "gate" && ex.kind != "event" && !ex.kind.empty())
					R.Err(p + ".kind", "expected reward_sum, reward_abs_max, component, gate or event");
				if (e.contains("intent") && R.Str(e["intent"], p + ".intent", ex.intent) && ex.intent.empty()) R.Err(p + ".intent", "must not be empty");
				if (!e.contains("intent")) R.Err(p + ".intent", "missing: every expectation carries one plain-English line saying why it should hold");
				if (e.contains("reward")) R.Str(e["reward"], p + ".reward", ex.reward); else R.Err(p + ".reward", "missing");
				if (e.contains("of") && R.Str(e["of"], p + ".of", ex.of) && ex.of != "raw" && ex.of != "wrapped" && ex.of != "weighted") R.Err(p + ".of", "expected raw, wrapped or weighted");
				if (e.contains("car")) R.Int(e["car"], p + ".car", ex.car, 0, (int)s.cars.size() - 1);
				if (e.contains("steps")) {
					const json& st = e["steps"];
					if (!st.is_array() || st.size() != 2) R.Err(p + ".steps", "expected [from, to] frame numbers");
					else { R.Int(st[0], p + ".steps[0]", ex.from, 1, 100000); R.Int(st[1], p + ".steps[1]", ex.to, 1, 100000); if (ex.to < ex.from) R.Err(p + ".steps", "to must be >= from"); }
				}
				double v;
				if (e.contains("min") && R.Num(e["min"], p + ".min", v)) ex.min = v;
				if (e.contains("max") && R.Num(e["max"], p + ".max", v)) ex.max = v;
				if (ex.min > ex.max) R.Err(p, "min is greater than max");
				if (ex.kind == "reward_sum" && !e.contains("min") && !e.contains("max")) R.Err(p, "reward_sum needs min and/or max");
				if (ex.kind == "reward_abs_max" && !e.contains("max")) R.Err(p, "reward_abs_max needs max");
				if (ex.kind == "component") {
					if (e.contains("component")) R.Str(e["component"], p + ".component", ex.component); else R.Err(p + ".component", "missing");
					if (!e.contains("min") && !e.contains("max")) R.Err(p, "component needs min and/or max");
				}
				if (ex.kind == "gate") {
					if (e.contains("gate")) R.Str(e["gate"], p + ".gate", ex.gate); else R.Err(p + ".gate", "missing");
					if (e.contains("passed")) R.Bool(e["passed"], p + ".passed", ex.passed);
					if (e.contains("mode") && R.Str(e["mode"], p + ".mode", ex.mode) && ex.mode != "all" && ex.mode != "any") R.Err(p + ".mode", "expected all or any");
				}
				if (ex.kind == "event") {
					if (e.contains("event")) R.Str(e["event"], p + ".event", ex.event); else R.Err(p + ".event", "missing");
					if (e.contains("count")) {
						const json& c = e["count"];
						if (!c.is_array() || c.size() != 2) R.Err(p + ".count", "expected [min, max]");
						else { R.Int(c[0], p + ".count[0]", ex.countMin, 0, 1000000); R.Int(c[1], p + ".count[1]", ex.countMax, 0, 1000000); if (ex.countMax < ex.countMin) R.Err(p + ".count", "max must be >= min"); }
					} else { ex.countMin = 1; }
				}
				s.expect.push_back(ex);
			}
		}

		if (errors.size() != errsBefore) return false;
		out = std::move(s);
		return true;
	}

	inline bool ParseScenarioText(const std::string& text, Scenario& out, std::vector<std::string>& errors) {
		nlohmann::json j = nlohmann::json::parse(text, nullptr, /*allow_exceptions=*/false);
		if (j.is_discarded()) { errors.push_back("$: not valid JSON"); return false; }
		return ParseScenario(j, out, errors);
	}

	// Inverse of ParseScenario for everything it reads: Parse(ToJson(s)) reproduces s (floats are written as floats).
	inline nlohmann::json ToJson(const Scenario& s) {
		using nlohmann::json;
		json j;
		j["schema"] = s.schema; j["name"] = s.name; j["description"] = s.description; j["tags"] = s.tags;
		j["tick_skip"] = s.tickSkip; j["action_delay"] = s.actionDelay; j["steps"] = s.steps;
		j["cars"] = json::array();
		for (auto& c : s.cars)
			j["cars"].push_back({ { "team", c.team == Team::BLUE ? "blue" : "orange" }, { "pos", detail::V3J(c.pos) }, { "vel", detail::V3J(c.vel) }, { "ang_vel", detail::V3J(c.angVel) },
				{ "angle_deg", { { "yaw", c.yawDeg }, { "pitch", c.pitchDeg }, { "roll", c.rollDeg } } }, { "boost", c.boost }, { "has_jumped", c.hasJumped },
				{ "has_double_jumped", c.hasDoubleJumped }, { "has_flipped", c.hasFlipped }, { "air_time_since_jump", c.airTimeSinceJump }, { "is_demoed", c.isDemoed } });
		j["ball"] = { { "pos", detail::V3J(s.ball.pos) }, { "vel", detail::V3J(s.ball.vel) }, { "ang_vel", detail::V3J(s.ball.angVel) } };
		j["actions"] = json::array();
		for (auto& a : s.actions)
			j["actions"].push_back({ { "car", a.car }, { "from", a.from }, { "action", { { "throttle", a.action.throttle }, { "steer", a.action.steer }, { "pitch", a.action.pitch },
				{ "yaw", a.action.yaw }, { "roll", a.action.roll }, { "jump", a.action.jump }, { "boost", a.action.boost }, { "handbrake", a.action.handbrake } } } });
		j["rewards"] = json::array();
		for (auto& r : s.rewards) {
			json jr = { { "name", r.name }, { "weight", r.weight }, { "gamma", r.gamma } };
			if (r.zeroSum >= 0) jr["zero_sum"] = r.zeroSum;
			j["rewards"].push_back(jr);
		}
		j["expect"] = json::array();
		for (auto& e : s.expect) {
			json je = { { "kind", e.kind }, { "intent", e.intent }, { "reward", e.reward }, { "of", e.of }, { "car", e.car } };
			if (e.from != 1 || e.to != INT_MAX) je["steps"] = json::array({ e.from, e.to == INT_MAX ? 100000 : e.to });
			if (std::isfinite(e.min)) je["min"] = e.min;
			if (std::isfinite(e.max)) je["max"] = e.max;
			if (e.kind == "component") je["component"] = e.component;
			if (e.kind == "gate") { je["gate"] = e.gate; je["passed"] = e.passed; je["mode"] = e.mode; }
			if (e.kind == "event") { je["event"] = e.event; je["count"] = json::array({ e.countMin, e.countMax == INT_MAX ? 1000000 : e.countMax }); }
			j["expect"].push_back(je);
		}
		return j;
	}

	// The action a car holds at decision step `step`: the latest entry with from <= step, else idle.
	inline Action ScriptedAction(const Scenario& s, int car, int step) {
		Action a = Action(CarControls{});
		int best = -1;
		for (auto& e : s.actions)
			if (e.car == car && e.from <= step && e.from >= best) { best = e.from; a = e.action; }
		return a;
	}

} // namespace Lab
} // namespace RLGC
