#include "TestFramework.h"

#include <RLGymCPP/Rewards/ScaffoldController.h>

#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

using namespace RLGC::Scaffold;

// Pre-registered controller tests (docs/WAVEDASH_REWARD_SPEC.md section 9.9). Pure function, no training, no engine.

namespace {
	// One iteration's raw counts with a chosen success-per-attempt. 1000 attempts, half the chances used, no cap hits.
	Counts Iter(double spa, double attempts = 1000, double apo = 0.5, double capShare = 0.0) {
		Counts c; c.attempts = attempts; c.successes = attempts * spa; c.opportunities = attempts / apo; c.episodes = 1000; c.capHits = 1000 * capShare; return c;
	}
	// runs n iterations of the same counts, returns the scale trajectory
	std::vector<double> Run(const Config& cfg, State& st, const Counts& c, int n, const Override& ov = Override()) {
		std::vector<double> s; for (int i = 0; i < n; i++) { Update(cfg, st, c, ov); s.push_back(st.s); } return s;
	}
}

// 1. sustained SPA >= thetaHi fades s monotonically at dDown per iteration, down to sMin, never below
TEST(Scaffold_sustained_high_success_fades_to_the_floor) {
	Config cfg; State st;
	auto s = Run(cfg, st, Iter(0.70), 3000);
	double prev = 1.0;
	for (int i = 0; i < 3000; i++) {
		CHECK(s[i] <= prev + 1e-15);                       // monotone non-increasing
		CHECK(s[i] >= cfg.sMin - 1e-15);                   // never below the floor
		if (s[i] > cfg.sMin * 1.0001) CHECK_NEAR(s[i], prev * (1.0 - cfg.dDown), 1e-12);   // exact per-iteration rate while above the floor
		prev = s[i];
	}
	CHECK_NEAR(s.back(), cfg.sMin, 1e-12);
	CHECK(s[499] < 0.52 && s[499] > 0.48);                // halves in ~500 iterations (spec 9.4)
}

// 2. SPA < thetaLo grows s at dUp per iteration up to 1, never above
TEST(Scaffold_low_success_rearms_to_one) {
	Config cfg; State st; st.s = 0.10;
	auto s = Run(cfg, st, Iter(0.10), 600);
	double prev = 0.10;
	for (double v : s) { CHECK(v >= prev - 1e-15); CHECK(v <= 1.0 + 1e-15); prev = v; }
	CHECK_NEAR(s.back(), 1.0, 1e-12);
	CHECK_NEAR(s[0], 0.10 * (1.0 + cfg.dUp), 1e-12);
}

// 3. inside the band holds exactly; a noisy statistic straddling thetaHi does not chatter
TEST(Scaffold_band_holds_and_noise_does_not_chatter) {
	Config cfg;
	{ State st; auto s = Run(cfg, st, Iter(0.40), 100); for (double v : s) CHECK_EQ(v, 1.0); }
	{ State st; std::vector<double> s;
	  for (int i = 0; i < 50; i++) { Update(cfg, st, Iter(i % 2 ? 0.54 : 0.46)); s.push_back(st.s); }
	  CHECK(s.back() > 0.95);                              // moved by less than 5% over 50 iterations
	  CHECK(s.back() <= 1.0); }
}

// 4. each gate holds s regardless of a high SPA
TEST(Scaffold_gates_hold_regardless_of_spa) {
	Config cfg;
	{ State st; Decision d{}; for (int i = 0; i < 100; i++) d = Update(cfg, st, Iter(0.90, /*attempts*/ 10));   // too few attempts (steady state ~105 < 500)
	  CHECK_EQ(st.s, 1.0); CHECK(d.reason == Reason::HoldNotEnoughData); }
	{ State st; Decision d{}; for (int i = 0; i < 100; i++) d = Update(cfg, st, Iter(0.90, 1000, /*apo*/ 0.10));  // not trying
	  CHECK_EQ(st.s, 1.0); CHECK(d.reason == Reason::HoldLowApo); }
	{ State st; Decision d{}; for (int i = 0; i < 100; i++) d = Update(cfg, st, Iter(0.90, 1000, 0.5, /*cap share*/ 0.30));   // cap binding
	  CHECK_EQ(st.s, 1.0); CHECK(d.reason == Reason::HoldCapFreeze); }
	{ State st; Decision d{}; for (int i = 0; i < 100; i++) d = Update(cfg, st, Counts());                         // zero counts
	  CHECK_EQ(st.s, 1.0); CHECK(d.reason == Reason::HoldNotEnoughData); }
	{ State st; for (int i = 0; i < 100; i++) Update(cfg, st, Iter(0.90)); CHECK(st.s < 1.0); }                  // control: the same counts without a gate do fade
}

// 5. override: pin forces, off freezes, auto resumes, precedence, malformed falls back to auto
TEST(Scaffold_override_semantics) {
	Config cfg; State st;
	{ Override o = ParseOverride("pin 0.5"); CHECK(o.mode == Override::PIN); CHECK_NEAR(o.value, 0.5, 1e-12); CHECK(!o.malformed); }
	{ Override o = ParseOverride("  OFF \n"); CHECK(o.mode == Override::OFF); CHECK(!o.malformed); }
	{ Override o = ParseOverride("auto\n"); CHECK(o.mode == Override::AUTO); CHECK(!o.malformed); }
	for (const char* bad : { "", "pin", "pin x", "pin 7", "pin 0.01", "pin -1", "pin 0.5 extra", "fade", "off now", "pin nan" }) {
		Override o = ParseOverride(bad); CHECK(o.mode == Override::AUTO); CHECK(o.malformed);
	}
	// pin beats a high SPA and survives many iterations
	Override pin = ParseOverride("pin 0.5");
	for (int i = 0; i < 50; i++) { Decision d = Update(cfg, st, Iter(0.90), pin); CHECK_EQ(st.s, 0.5); CHECK(d.reason == Reason::OverridePin); }
	// auto resumes FROM the pinned value
	Update(cfg, st, Iter(0.90), Override()); CHECK_NEAR(st.s, 0.5 * (1.0 - cfg.dDown), 1e-12);
	// off freezes whatever s is, even under a strong fade signal
	double frozen = st.s; Override off = ParseOverride("off");
	for (int i = 0; i < 50; i++) Update(cfg, st, Iter(0.90), off);
	CHECK_EQ(st.s, frozen);
	// a malformed file behaves like auto (the controller keeps regulating)
	Override mal = ParseOverride("garbage"); Update(cfg, st, Iter(0.90), mal); CHECK(st.s < frozen);
}

// 5b. the override FILE: missing = auto (not malformed), present = parsed, malformed = auto + flag
TEST(Scaffold_override_file) {
	const std::string path = "scaffold_override_test.tmp";
	std::remove(path.c_str());
	{ Override o = ReadOverrideFile(path); CHECK(o.mode == Override::AUTO); CHECK(!o.malformed); }
	{ FILE* f = std::fopen(path.c_str(), "w"); std::fputs("pin 0.25\n", f); std::fclose(f); }
	{ Override o = ReadOverrideFile(path); CHECK(o.mode == Override::PIN); CHECK_NEAR(o.value, 0.25, 1e-12); }
	{ FILE* f = std::fopen(path.c_str(), "w"); std::fputs("nonsense", f); std::fclose(f); }
	{ Override o = ReadOverrideFile(path); CHECK(o.mode == Override::AUTO); CHECK(o.malformed); }
	std::remove(path.c_str());
}

// 6. persistence: a round trip, and a restart mid-fade, are bit-exact
TEST(Scaffold_persistence_round_trip_and_restart_mid_fade) {
	Config cfg; State a;
	Run(cfg, a, Iter(0.70), 100);
	const std::string text = Serialize(a);
	State b; CHECK(Parse(text, b));
	CHECK_EQ(a.s, b.s); CHECK_EQ(a.att, b.att); CHECK_EQ(a.succ, b.succ); CHECK_EQ(a.opp, b.opp); CHECK_EQ(a.iteration, b.iteration);
	for (int i = 0; i < 50; i++) { Update(cfg, a, Iter(0.70)); Update(cfg, b, Iter(0.70)); CHECK_EQ(a.s, b.s); }
	// through a real file (atomic temp + rename)
	const std::string path = "scaffold_state_test.json";
	std::remove(path.c_str());
	CHECK(Save(path, a));
	State c; CHECK(Load(path, c)); CHECK_EQ(a.s, c.s); CHECK_EQ(a.iteration, c.iteration);
	std::remove(path.c_str()); std::remove((path + ".tmp").c_str());
	// missing / corrupt sidecar: Load/Parse fail and leave the state alone, so the caller starts from s = 1
	State d; d.s = 0.7; CHECK(!Load("does_not_exist.json", d)); CHECK_EQ(d.s, 0.7);
	CHECK(!Parse("", d)); CHECK(!Parse("{\"s\":0.5}", d)); CHECK(!Parse("{\"s\":2,\"opp\":0,\"att\":0,\"succ\":0,\"eps\":0,\"cap\":0,\"iteration\":0}", d)); CHECK(!Parse("{\"s\":nan,\"opp\":0,\"att\":0,\"succ\":0,\"eps\":0,\"cap\":0,\"iteration\":0}", d));
	CHECK_EQ(d.s, 0.7);
}

// 7. robustness: bad inputs never propagate
TEST(Scaffold_bad_inputs_never_propagate) {
	Config cfg; State st;
	Run(cfg, st, Iter(0.70), 20);
	const State before = st; const double nan = std::numeric_limits<double>::quiet_NaN(), inf = std::numeric_limits<double>::infinity();
	for (Counts bad : { [&] { Counts c = Iter(0.7); c.attempts = nan; return c; }(), [&] { Counts c = Iter(0.7); c.successes = -5; return c; }(),
	                    [&] { Counts c = Iter(0.7); c.opportunities = inf; return c; }(), [&] { Counts c = Iter(0.7); c.successes = c.attempts * 2; return c; }() }) {
		State s2 = before; Decision d = Update(cfg, s2, bad);
		CHECK(d.reason == Reason::BadInput);
		CHECK(std::isfinite(s2.s) && std::isfinite(s2.att) && std::isfinite(s2.succ));
		CHECK_EQ(s2.s, before.s);                          // s is held; the bad iteration adds nothing to the sums
	}
	// invalid configs are rejected: s untouched
	for (int k = 0; k < 6; k++) {
		Config bad; State s2; s2.s = 0.8;
		if (k == 0) { bad.thetaLo = 0.6; bad.thetaHi = 0.5; }
		if (k == 1) bad.sMin = 0.0;
		if (k == 2) bad.sMin = 1.5;
		if (k == 3) bad.dDown = -0.1;
		if (k == 4) bad.countDecay = 1.0;
		if (k == 5) bad.thetaHi = nan;
		Decision d = Update(bad, s2, Iter(0.9)); CHECK(d.reason == Reason::InvalidConfig); CHECK_EQ(s2.s, 0.8);
	}
	// a corrupt scale in the state is repaired into [sMin, 1]
	{ State s2; s2.s = nan; Update(cfg, s2, Iter(0.4)); CHECK_EQ(s2.s, 1.0); }
	{ State s2; s2.s = 7.0; Update(cfg, s2, Iter(0.4)); CHECK_EQ(s2.s, 1.0); }
	{ State s2; s2.s = -1.0; Update(cfg, s2, Iter(0.4)); CHECK_EQ(s2.s, cfg.sMin); }
	// the scale stays in [sMin, 1] for any random walk of inputs
	{ State s2; unsigned x = 12345u; for (int i = 0; i < 5000; i++) { x = x * 1664525u + 1013904223u; double spa = (x >> 8) / 16777216.0; Update(cfg, s2, Iter(spa)); CHECK(s2.s >= cfg.sMin - 1e-15 && s2.s <= 1.0 + 1e-15); } }
}

// 8. determinism
TEST(Scaffold_is_deterministic) {
	Config cfg; State a, b;
	std::vector<double> ta, tb; unsigned x = 99u;
	for (int i = 0; i < 400; i++) { x = x * 1664525u + 1013904223u; double spa = 0.25 + 0.5 * ((x >> 8) / 16777216.0); Update(cfg, a, Iter(spa)); ta.push_back(a.s); }
	x = 99u;
	for (int i = 0; i < 400; i++) { x = x * 1664525u + 1013904223u; double spa = 0.25 + 0.5 * ((x >> 8) / 16777216.0); Update(cfg, b, Iter(spa)); tb.push_back(b.s); }
	CHECK(ta == tb);
}

// regulator behavior end to end: fade while skilled, the skill decays, the scale comes back (no one-way ratchet)
TEST(Scaffold_regulates_both_ways) {
	Config cfg; State st;
	Run(cfg, st, Iter(0.70), 400);   double faded = st.s; CHECK(faded < 0.85);
	Run(cfg, st, Iter(0.10), 400);   CHECK(st.s > faded);                       // skill collapsed: re-armed
	Run(cfg, st, Iter(0.10), 600);   CHECK_NEAR(st.s, 1.0, 1e-12);
}
