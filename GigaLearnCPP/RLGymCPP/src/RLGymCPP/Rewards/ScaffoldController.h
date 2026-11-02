#pragma once
// ============================================================================
// ScaffoldController.h - automatic fade/re-arm of a scaffold reward's scale.
// 2026-11-02 | Rev 1.0 | docs/WAVEDASH_REWARD_SPEC.md section 9.
//
// A pure function over counters: Update(cfg, state, counts, override) -> Decision. No reward, trainer or GPU
// dependency, so it is tested offline (tests/test_scaffold_controller.cpp). The trainer's per-iteration callback
// feeds it the reward's raw counters (counted BEFORE any weight) and applies state.s as a multiplier that the
// reward snapshots in Reset() for the whole episode.
//
// It is a REGULATOR: it fades the scale while the skill is demonstrably present (success per attempt >= thetaHi),
// re-arms it when the skill is absent or decayed (< thetaLo), and holds in between (hysteresis) or whenever the
// evidence is weak or suspicious (too few attempts, the bot is not trying, the episode cap is binding).
//
// All defaults are PROVISIONAL (spec 9.4): chance level at skip 8 is ~0.25 [M]; where the real bot lands is unmeasured.
// ============================================================================
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

namespace RLGC {
namespace Scaffold {

	struct Config {
		double thetaHi = 0.50;      // fade at or above this success-per-attempt
		double thetaLo = 0.30;      // re-arm below this (just above chance 0.25); the band is the hysteresis
		double apoMin = 0.20;       // attempts per opportunity below this: the bot is not trying, do not fade
		double nMin = 500.0;        // decayed pooled attempts below this: not enough data, hold
		double capFreeze = 0.20;    // cap-hit share of episodes above this: possible farming, hold
		double dDown = 0.0014;      // multiplicative fade per iteration (halves in ~500 iterations)
		double dUp = 0.0070;        // multiplicative re-arm per iteration (doubles in ~100 iterations)
		double sMin = 0.05;         // never exactly 0: multiplicative re-arm cannot leave 0
		double countDecay = 0.905;  // per-iteration decay of the pooled sums (~10 iteration window)

		bool Valid() const {
			auto fin = [](double v) { return std::isfinite(v); };
			return fin(thetaHi) && fin(thetaLo) && fin(apoMin) && fin(nMin) && fin(capFreeze) && fin(dDown) && fin(dUp) && fin(sMin) && fin(countDecay)
				&& thetaLo >= 0 && thetaHi <= 1 && thetaLo < thetaHi
				&& apoMin >= 0 && nMin >= 0 && capFreeze >= 0 && capFreeze <= 1
				&& dDown >= 0 && dDown < 1 && dUp >= 0 && dUp < 10
				&& sMin > 0 && sMin <= 1
				&& countDecay > 0 && countDecay < 1;
		}
	};

	// Raw counts for ONE iteration, pooled over all arenas of the process.
	struct Counts {
		double opportunities = 0;   // landings with the flip still available
		double attempts = 0;        // flips pressed in the last decision before contact
		double successes = 0;       // attempts that satisfy the event rule (before the cap and cooldown)
		double episodes = 0;
		double capHits = 0;         // events suppressed by the per-episode cap
		// Finite, non-negative, and consistent: a success is an attempt, and an attempt happens at an opportunity (the flip was available at
		// the previous snapshot of that landing).
		bool Sane() const {
			for (double v : { opportunities, attempts, successes, episodes, capHits })
				if (!std::isfinite(v) || v < 0) return false;
			return successes <= attempts + 1e-9 && attempts <= opportunities + 1e-9;
		}
	};

	struct State {
		double s = 1.0;                                   // the scale applied to the scaffold terms, in [sMin, 1]
		double opp = 0, att = 0, succ = 0, eps = 0, cap = 0;   // exponentially decayed pooled sums
		long long iteration = 0;
	};

	struct Override {
		enum Mode { AUTO, PIN, OFF } mode = AUTO;
		double value = 1.0;       // for PIN
		bool malformed = false;   // the file existed but could not be used: treated as AUTO and flagged
	};

	enum class Reason {
		Fade, Rearm, HoldInBand, HoldNotEnoughData, HoldCapFreeze, HoldLowApo,
		OverridePin, OverrideOff, BadInput, InvalidConfig
	};
	inline const char* ReasonName(Reason r) {
		switch (r) {
		case Reason::Fade: return "fade"; case Reason::Rearm: return "rearm"; case Reason::HoldInBand: return "hold:in-band";
		case Reason::HoldNotEnoughData: return "hold:not-enough-data"; case Reason::HoldCapFreeze: return "hold:cap-freeze";
		case Reason::HoldLowApo: return "hold:low-attempts-per-opportunity"; case Reason::OverridePin: return "override:pin";
		case Reason::OverrideOff: return "override:off"; case Reason::BadInput: return "hold:bad-input"; case Reason::InvalidConfig: return "hold:invalid-config";
		}
		return "?";
	}

	struct Decision {
		Reason reason = Reason::HoldNotEnoughData;
		double spa = 0, apo = 0, capShare = 0;   // the statistics the decision was based on (pooled, decayed)
		double attemptsWindow = 0;
	};

	inline double ClampScale(double v, double sMin) {
		if (!std::isfinite(v)) return 1.0;
		return std::min(1.0, std::max(sMin, v));
	}

	// One controller step per training iteration. state is mutated; the decision says why.
	inline Decision Update(const Config& cfg, State& st, const Counts& c, const Override& ov = Override()) {
		Decision d;
		if (!cfg.Valid()) { d.reason = Reason::InvalidConfig; return d; }   // never act on a config we cannot trust
		st.s = ClampScale(st.s, cfg.sMin);                                 // a corrupt scale is repaired, never propagated
		st.iteration++;

		// 1. pooled, decayed sums (a bad iteration is dropped, not allowed to pollute the sums)
		const bool sane = c.Sane();
		st.opp *= cfg.countDecay; st.att *= cfg.countDecay; st.succ *= cfg.countDecay; st.eps *= cfg.countDecay; st.cap *= cfg.countDecay;
		if (sane) { st.opp += c.opportunities; st.att += c.attempts; st.succ += c.successes; st.eps += c.episodes; st.cap += c.capHits; }
		d.attemptsWindow = st.att;
		d.spa = st.att > 0 ? st.succ / st.att : 0.0;
		d.apo = st.opp > 0 ? st.att / st.opp : 0.0;
		d.capShare = st.eps > 0 ? st.cap / st.eps : 0.0;

		// 2. manual override takes precedence over everything below
		if (ov.mode == Override::PIN) { st.s = ClampScale(ov.value, cfg.sMin); d.reason = Reason::OverridePin; return d; }
		if (ov.mode == Override::OFF) { d.reason = Reason::OverrideOff; return d; }

		// 3. gates: hold unless the evidence is strong and unsuspicious
		if (!sane) { d.reason = Reason::BadInput; return d; }
		if (st.att < cfg.nMin) { d.reason = Reason::HoldNotEnoughData; return d; }
		if (d.capShare > cfg.capFreeze) { d.reason = Reason::HoldCapFreeze; return d; }
		if (d.apo < cfg.apoMin) { d.reason = Reason::HoldLowApo; return d; }

		// 4. regulate
		if (d.spa >= cfg.thetaHi) { st.s = std::max(cfg.sMin, st.s * (1.0 - cfg.dDown)); d.reason = Reason::Fade; }
		else if (d.spa < cfg.thetaLo) { st.s = std::min(1.0, st.s * (1.0 + cfg.dUp)); d.reason = Reason::Rearm; }
		else d.reason = Reason::HoldInBand;
		return d;
	}

	// ---- override file: "auto" | "pin <value>" | "off"  (value must be in [sMin, 1]; anything else is malformed -> AUTO + flag)
	inline Override ParseOverride(const std::string& text, double sMin = 0.05) {
		Override o;
		std::istringstream in(text);
		std::string word; if (!(in >> word)) { o.malformed = true; return o; }
		for (auto& ch : word) ch = (char)std::tolower((unsigned char)ch);
		std::string rest; std::getline(in, rest);
		const bool trailing = rest.find_first_not_of(" \t\r\n") != std::string::npos;
		if (word == "auto" && !trailing) return o;
		if (word == "off" && !trailing) { o.mode = Override::OFF; return o; }
		if (word == "pin") {
			char* end = nullptr; std::string num = rest; const double v = std::strtod(num.c_str(), &end);
			const bool consumed = end != num.c_str();
			const std::string tail = consumed ? std::string(end) : std::string();
			if (consumed && std::isfinite(v) && v >= sMin && v <= 1.0 && tail.find_first_not_of(" \t\r\n") == std::string::npos) { o.mode = Override::PIN; o.value = v; return o; }
		}
		o.malformed = true;      // fall back to AUTO
		return o;
	}
	// A missing file is not an error (AUTO). An unreadable or malformed one is AUTO with the malformed flag so the caller can log it.
	inline Override ReadOverrideFile(const std::string& path, double sMin = 0.05) {
		std::ifstream f(path);
		if (!f.good()) return Override();
		std::stringstream ss; ss << f.rdbuf();
		return ParseOverride(ss.str(), sMin);
	}

	// ---- persistence: flat JSON, doubles with %.17g so a round trip is bit-exact
	inline std::string Serialize(const State& st) {
		char buf[512];
		std::snprintf(buf, sizeof(buf), "{\"s\":%.17g,\"opp\":%.17g,\"att\":%.17g,\"succ\":%.17g,\"eps\":%.17g,\"cap\":%.17g,\"iteration\":%lld}",
			st.s, st.opp, st.att, st.succ, st.eps, st.cap, st.iteration);
		return buf;
	}
	namespace detail {
		inline bool Field(const std::string& js, const char* key, double& out) {
			const std::string k = std::string("\"") + key + "\":";
			const size_t p = js.find(k); if (p == std::string::npos) return false;
			char* end = nullptr; const double v = std::strtod(js.c_str() + p + k.size(), &end);
			if (end == js.c_str() + p + k.size() || !std::isfinite(v)) return false;
			out = v; return true;
		}
	}
	// Returns false (and leaves `st` untouched) if the text is missing a field, non-finite or out of range: the caller then starts from the default (s = 1).
	inline bool Parse(const std::string& js, State& st, double sMin = 0.05) {
		State t; double it = 0;
		if (!detail::Field(js, "s", t.s) || !detail::Field(js, "opp", t.opp) || !detail::Field(js, "att", t.att) || !detail::Field(js, "succ", t.succ)
			|| !detail::Field(js, "eps", t.eps) || !detail::Field(js, "cap", t.cap) || !detail::Field(js, "iteration", it)) return false;
		if (t.s < sMin || t.s > 1.0 || t.opp < 0 || t.att < 0 || t.succ < 0 || t.eps < 0 || t.cap < 0 || it < 0) return false;
		t.iteration = (long long)it; st = t; return true;
	}
	// Written next to the checkpoint: write a temp file then rename, so a crash never leaves a half-written sidecar.
	inline bool Save(const std::string& path, const State& st) {
		const std::string tmp = path + ".tmp";
		{ std::ofstream f(tmp, std::ios::trunc); if (!f.good()) return false; f << Serialize(st); if (!f.good()) return false; }
		return std::rename(tmp.c_str(), path.c_str()) == 0;
	}
	inline bool Load(const std::string& path, State& st, double sMin = 0.05) {
		std::ifstream f(path); if (!f.good()) return false;
		std::stringstream ss; ss << f.rdbuf(); return Parse(ss.str(), st, sMin);
	}

} // namespace Scaffold
} // namespace RLGC
