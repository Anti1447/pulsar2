#pragma once
// ============================================================================
// Expectations.h - judge a recorded run against what the scenario's author said should happen.
// 2026-11-02 | Rev 1.0 | docs/REWARD_VIEWER_SPEC.md section 5 (P1).
//
// Each Expectation carries a human-written `intent` line; the result repeats it next to the measured value, so a failure reads
// as "you said X should hold because Y, and the run measured Z". A result is never silently skipped: an expectation that
// names a reward, car, gate or frame range that does not exist FAILS with a message saying what it looked for.
// ============================================================================
#include "LabTypes.h"
#include "Scenario.h"
#include <cstdio>
#include <string>
#include <vector>

namespace RLGC {
namespace Lab {

	struct ExpectResult {
		const Expectation* expectation = nullptr;
		bool ok = false;
		std::string detail;   // measured value and what was looked for
	};

	namespace detail {
		inline std::string Num(double v) { char b[48]; std::snprintf(b, sizeof(b), "%.6g", v); return b; }

		inline int FindReward(const StackStepTrace& t, const std::string& name) {
			for (size_t i = 0; i < t.rewards.size(); i++)
				if (t.rewards[i].name == name || t.rewards[i].wandbKey == name) return (int)i;
			return -1;
		}
		inline std::string RewardNames(const StackStepTrace& t) {
			std::string s;
			for (auto& r : t.rewards) s += (s.empty() ? "" : ", ") + r.name;
			return s.empty() ? "(none)" : s;
		}
		// Value of reward `name` (or "*total") for player index `car` in one frame; false if not found.
		inline bool Value(const Frame& f, const Expectation& e, double& out) {
			const StackStepTrace& t = f.trace;
			if (e.car < 0 || e.car >= (int)t.carIds.size()) return false;
			if (e.reward == "*total") { out = t.total[e.car]; return true; }
			int ri = FindReward(t, e.reward);
			if (ri < 0) return false;
			const RewardStepTrace& r = t.rewards[ri];
			const std::vector<float>& v = e.of == "weighted" ? r.weighted : e.of == "wrapped" ? r.wrapped : r.raw;
			if (e.car >= (int)v.size()) return false;
			out = v[e.car]; return true;
		}
	}

	inline ExpectResult Evaluate(const Expectation& e, const std::vector<Frame>& frames) {
		ExpectResult res; res.expectation = &e;
		const int last = frames.empty() ? 0 : frames.back().index;
		const int from = e.from, to = std::min(e.to, last);
		const std::string where = "frames " + std::to_string(from) + ".." + std::to_string(to) + ", car " + std::to_string(e.car);
		if (frames.size() < 2 || from > to) { res.detail = "no frames in range (" + where + ")"; return res; }

		// the reward must exist in the traces at all
		const StackStepTrace* any = nullptr;
		for (auto& f : frames) if (f.hasTrace) { any = &f.trace; break; }
		if (!any) { res.detail = "the run produced no traced frames"; return res; }
		if (e.reward != "*total" && detail::FindReward(*any, e.reward) < 0) { res.detail = "reward \"" + e.reward + "\" is not in the stack (stack: " + detail::RewardNames(*any) + ")"; return res; }
		if (e.car >= (int)any->carIds.size()) { res.detail = "car " + std::to_string(e.car) + " does not exist"; return res; }

		if (e.kind == "reward_sum" || e.kind == "reward_abs_max") {
			double sum = 0, peak = 0; int n = 0;
			for (int i = from; i <= to; i++) {
				const Frame& f = frames[i];
				double v;
				if (!f.hasTrace || !detail::Value(f, e, v)) continue;
				sum += v; peak = std::max(peak, std::fabs(v)); n++;
			}
			if (n == 0) { res.detail = "no traced frames in range (" + where + ")"; return res; }
			if (e.kind == "reward_sum") {
				res.ok = sum >= e.min && sum <= e.max;
				res.detail = e.reward + " (" + e.of + ") summed to " + detail::Num(sum) + " over " + std::to_string(n) + " frames; expected within [" + detail::Num(e.min) + ", " + detail::Num(e.max) + "]";
			} else {
				res.ok = peak <= e.max;
				res.detail = e.reward + " (" + e.of + ") peaked at |" + detail::Num(peak) + "| over " + std::to_string(n) + " frames; expected <= " + detail::Num(e.max);
			}
			return res;
		}

		const int ri = e.reward == "*total" ? -1 : detail::FindReward(*any, e.reward);
		if (ri < 0) { res.detail = "gate/event expectations name one reward, not \"*total\""; return res; }
		const uint32_t carId = any->carIds[e.car];

		if (e.kind == "component") {
			int seen = 0, inside = 0; double lo = INFINITY, hi = -INFINITY;
			for (int i = from; i <= to; i++) {
				const Frame& f = frames[i];
				if (!f.hasTrace) continue;
				const PlayerTrace* pt = f.trace.rewards[ri].sink.Find(carId);
				if (!pt) continue;
				for (auto& c : pt->components) if (c.name == e.component) {
					seen++; lo = std::min(lo, (double)c.value); hi = std::max(hi, (double)c.value);
					if (c.value >= e.min && c.value <= e.max) inside++;
				}
			}
			if (seen == 0) { res.detail = "component \"" + e.component + "\" was never emitted by " + e.reward + " for this car in range (" + where + ")"; return res; }
			res.ok = inside == seen;
			res.detail = "component \"" + e.component + "\" ranged over [" + detail::Num(lo) + ", " + detail::Num(hi) + "] in " + std::to_string(seen) + " emissions; expected every one within [" + detail::Num(e.min) + ", " + detail::Num(e.max) + "]";
			return res;
		}

		if (e.kind == "gate") {
			int seen = 0, match = 0;
			for (int i = from; i <= to; i++) {
				const Frame& f = frames[i];
				if (!f.hasTrace) continue;
				const PlayerTrace* pt = f.trace.rewards[ri].sink.Find(carId);
				if (!pt) continue;
				for (auto& g : pt->gates) if (g.name == e.gate) { seen++; if (g.passed == e.passed) match++; }
			}
			if (seen == 0) { res.detail = "gate \"" + e.gate + "\" was never emitted by " + e.reward + " for this car in range (" + where + ")"; return res; }
			res.ok = e.mode == "all" ? match == seen : match > 0;
			res.detail = "gate \"" + e.gate + "\" " + (e.passed ? "passed" : "failed") + " in " + std::to_string(match) + " of " + std::to_string(seen) + " emissions; expected " + e.mode;
			return res;
		}

		if (e.kind == "event") {
			int count = 0;
			for (int i = from; i <= to; i++) {
				const Frame& f = frames[i];
				if (!f.hasTrace) continue;
				const PlayerTrace* pt = f.trace.rewards[ri].sink.Find(carId);
				if (!pt) continue;
				for (auto& ev : pt->events) if (ev == e.event) { count++; break; }
			}
			res.ok = count >= e.countMin && count <= e.countMax;
			res.detail = "event \"" + e.event + "\" occurred on " + std::to_string(count) + " frames; expected within [" + std::to_string(e.countMin) + ", " + (e.countMax == INT_MAX ? std::string("inf") : std::to_string(e.countMax)) + "]";
			return res;
		}

		res.detail = "unknown kind \"" + e.kind + "\"";
		return res;
	}

	inline std::vector<ExpectResult> EvaluateAll(const Scenario& s, const std::vector<Frame>& frames) {
		std::vector<ExpectResult> out;
		for (auto& e : s.expect) out.push_back(Evaluate(e, frames));
		return out;
	}

} // namespace Lab
} // namespace RLGC
