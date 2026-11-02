#pragma once
// ============================================================================
// LabReport.h - the text a person (or Claude) reads after a scenario run, and the JSONL trace.
// 2026-11-02 | Rev 1.0 | docs/REWARD_VIEWER_SPEC.md (P1)
//
// Everything printed here is read back from the recorded Frames, i.e. from what the reward itself reported on each step.
// ============================================================================
#include "RewardLab.h"
#include <map>
#include <ostream>
#include <sstream>

namespace RLGC {
namespace Lab {

	inline bool AllPass(const std::vector<ExpectResult>& r) {
		for (auto& e : r) if (!e.ok) return false;
		return true;
	}

	// One JSON object per recorded step (frames 1..N), RewardTraceJson schema 1 plus the frame number and terminal flag.
	inline void WriteTraceJsonl(const LabSession& s, std::ostream& os) {
		for (auto& f : s.Frames()) {
			if (!f.hasTrace) continue;
			nlohmann::json j = ToJson(f.trace);
			j["frame"] = f.index; j["terminal"] = f.terminal;
			os << j.dump() << "\n";
		}
	}

	inline std::string TextReport(const LabSession& s) {
		using detail::Num;
		const Scenario& sc = s.GetScenario();
		const auto& frames = s.Frames();
		const auto results = s.EvaluateExpectations();
		int passed = 0; for (auto& r : results) passed += r.ok;
		std::ostringstream o;
		o << "SCENARIO " << sc.name << "  (" << s.Steps() << " of " << sc.steps << " steps, tick_skip " << sc.tickSkip << ", " << sc.cars.size() << " cars)  RESULT: "
		  << (results.empty() ? "NO EXPECTATIONS" : AllPass(results) ? "PASS" : "FAIL") << " (" << passed << "/" << results.size() << " expectations)\n";
		if (!sc.description.empty()) o << "  " << sc.description << "\n";
		if (frames.back().terminal) o << "  episode ended at frame " << frames.back().index << (frames.back().goalScored ? " (goal)" : "") << "\n";
		if (frames.size() < 2) { o << "  (no steps were run)\n"; return o.str(); }

		const StackStepTrace& t0 = frames[1].trace;
		const size_t nCars = t0.carIds.size();
		// per-reward, per-car sums of the three views
		o << "rewards (sum over the run; raw = before ZeroSum, weighted = what the stack adds):\n";
		for (size_t r = 0; r < t0.rewards.size(); r++) {
			o << "  " << t0.rewards[r].name << "  weight " << Num(t0.rewards[r].weight) << (t0.rewards[r].zeroSum ? "  [ZeroSum]" : "") << "\n";
			for (size_t c = 0; c < nCars; c++) {
				double raw = 0, wr = 0, we = 0;
				for (auto& f : frames) if (f.hasTrace) { raw += f.trace.rewards[r].raw[c]; wr += f.trace.rewards[r].wrapped[c]; we += f.trace.rewards[r].weighted[c]; }
				o << "    car " << c << ": raw " << Num(raw) << "  wrapped " << Num(wr) << "  weighted " << Num(we) << "\n";
			}
		}
		o << "  stack total per car:";
		for (size_t c = 0; c < nCars; c++) { double t = 0; for (auto& f : frames) if (f.hasTrace) t += f.trace.total[c]; o << "  car " << c << " " << Num(t); }
		o << "\n";

		// events, in time order
		std::ostringstream ev; int nev = 0;
		for (auto& f : frames) if (f.hasTrace)
			for (size_t r = 0; r < f.trace.rewards.size(); r++)
				for (auto& pt : f.trace.rewards[r].sink.players)
					for (auto& e : pt.events) {
						size_t car = 0; for (size_t c = 0; c < nCars; c++) if (f.trace.carIds[c] == pt.carId) car = c;
						if (nev++ < 40) ev << "  frame " << f.index << "  car " << car << "  " << f.trace.rewards[r].name << "  " << e << "\n";
					}
		o << "events (" << nev << (nev > 40 ? ", first 40 shown" : "") << "):\n" << (nev ? ev.str() : "  (none)\n");

		// gates: per reward / car / gate, pass and fail counts and the first failure's reason
		struct GateStat { int pass = 0, fail = 0, firstFailFrame = -1; std::string reason; };
		std::map<std::string, GateStat> gates;
		for (auto& f : frames) if (f.hasTrace)
			for (size_t r = 0; r < f.trace.rewards.size(); r++)
				for (auto& pt : f.trace.rewards[r].sink.players) {
					size_t car = 0; for (size_t c = 0; c < nCars; c++) if (f.trace.carIds[c] == pt.carId) car = c;
					for (auto& g : pt.gates) {
						GateStat& st = gates[f.trace.rewards[r].name + " / car " + std::to_string(car) + " / " + g.name];
						if (g.passed) st.pass++; else { if (st.fail++ == 0) { st.firstFailFrame = f.index; st.reason = g.reason; } }
					}
				}
		o << "gates (passed / failed over the run):\n";
		if (gates.empty()) o << "  (none emitted)\n";
		for (auto& kv : gates) {
			o << "  " << kv.first << ": " << kv.second.pass << " / " << kv.second.fail;
			if (kv.second.fail) o << "   first failure frame " << kv.second.firstFailFrame << (kv.second.reason.empty() ? "" : ": " + kv.second.reason);
			o << "\n";
		}

		o << "expectations:\n";
		if (results.empty()) o << "  (none)\n";
		for (auto& r : results) o << "  [" << (r.ok ? "PASS" : "FAIL") << "] " << r.expectation->intent << "\n         " << r.detail << "\n";
		return o.str();
	}

} // namespace Lab
} // namespace RLGC
