#include "TestFramework.h"

#include <RLGymCPP/Lab/Macro.h>
#include <RLGymCPP/Lab/LabReport.h>

using namespace RLGC;
using namespace RLGC::Lab;

// P3 (headless part) of the Reward Lab (docs/REWARD_VIEWER_SPEC.md section 6): tick-resolution macros, projection onto the decision
// grid, phase sweep. Needs build/collision_meshes (run from the repo root).

namespace {
	Action Act(float throttle, float boost, float jump = 0) {
		Action a = Action(CarControls{});
		a.throttle = throttle; a.boost = boost; a.jump = jump;
		return a;
	}
	CarSpec MkCar(Team t, Vec pos, float yaw = 90, Vec vel = Vec(0, 0, 0), float boost = 100) {
		CarSpec c; c.team = t; c.pos = pos; c.yawDeg = yaw; c.vel = vel; c.boost = boost; return c;
	}
	ActionEntry Entry(int car, int from, Action a) { ActionEntry e; e.car = car; e.from = from; e.action = a; return e; }

	// One blue car driving up the pitch, a 1-tick jump press at tick 20 (a human can do this; a 15 Hz decision cannot, unless it lands on the grid).
	Scenario TickLevelDrive(int ticks = 64) {
		Scenario s; s.name = "tick_level_drive"; s.tickSkip = 1; s.steps = ticks;
		s.cars = { MkCar(Team::BLUE, Vec(0, -3000, 17), 90, Vec(0, 800, 0)), MkCar(Team::ORANGE, Vec(0, 3500, 17), 270) };
		s.ball.pos = Vec(2500, 2500, 93);
		s.actions = { Entry(0, 0, Act(1, 0)), Entry(0, 20, Act(1, 0, 1)), Entry(0, 21, Act(1, 0)) };
		s.rewards = { { "OffsidePenalty", 1.0f, -1, 0.9969f } };
		return s;
	}
	// The grid version of the same situation (skip 8), no script of its own.
	Scenario GridScenario() {
		Scenario s = TickLevelDrive(); s.tickSkip = 8; s.steps = 8; s.actions.clear();
		Expectation e; e.kind = "reward_sum"; e.intent = "never positive"; e.reward = "OffsidePenalty"; e.car = 0; e.from = 1; e.to = 8; e.min = -100; e.max = 0;
		s.expect = { e };
		return s;
	}
	Macro RecordTickLevel() {
		Scenario s = TickLevelDrive();
		LabSession session(s);
		session.RunScripted();
		return MacroFromLog(session.ActionLog(), 1, "tick_level");
	}
	std::vector<std::string> Prints(const std::vector<Frame>& f) { std::vector<std::string> o; for (auto& x : f) o.push_back(Fingerprint(x)); return o; }

	std::string GoodMacro() {
		return R"({"schema":1,"kind":"lab_macro","name":"m","cars":2,"ticks":20,
		  "entries":[{"car":0,"tick":3,"action":{"throttle":1}},{"car":0,"tick":10,"action":{"throttle":1,"boost":1}},{"car":1,"tick":0,"action":{"steer":-0.5,"handbrake":1}}]})";
	}
	bool MacroErr(const std::string& text, const std::string& needle) {
		Macro m; std::vector<std::string> errs;
		if (ParseMacroText(text, m, errs)) return false;
		for (auto& e : errs) if (e.find(needle) != std::string::npos) return true;
		return false;
	}
	std::string Rep(std::string s, const std::string& a, const std::string& b) { size_t p = s.find(a); if (p != std::string::npos) s.replace(p, a.size(), b); return s; }
}

// ---- file form ---------------------------------------------------------------------------------------------------------

TEST(Macro_parses_holds_until_next_entry_and_roundtrips) {
	Macro m; std::vector<std::string> errs;
	CHECK(ParseMacroText(GoodMacro(), m, errs));
	CHECK_EQ(m.Ticks(), 20); CHECK_EQ(m.cars, 2);
	CHECK_EQ(m.At(0, 2).throttle, 0.0f);    // before the first entry: idle
	CHECK_EQ(m.At(0, 3).throttle, 1.0f);    // from the entry on
	CHECK_EQ(m.At(0, 9).boost, 0.0f);
	CHECK_EQ(m.At(0, 10).boost, 1.0f);      // the second entry replaces the whole action, it does not merge
	CHECK_EQ(m.At(0, 19).boost, 1.0f);      // held to the end
	CHECK_EQ(m.At(1, 0).steer, -0.5f); CHECK_EQ(m.At(1, 0).handbrake, 1.0f);
	CHECK_EQ(m.At(0, 20).throttle, 0.0f);   // past the end: idle, not held on
	CHECK_EQ(m.At(0, -1).throttle, 0.0f);
	CHECK_EQ(m.At(5, 0).throttle, 0.0f);    // no such car
	Macro m2; std::vector<std::string> errs2;
	CHECK(ParseMacro(MacroToJson(m), m2, errs2));
	CHECK_EQ(m2.Ticks(), m.Ticks()); CHECK_EQ(m2.At(1, 0).handbrake, 1.0f); CHECK_EQ(m2.At(0, 10).boost, 1.0f);
	for (int t = 0; t < 20; t++) for (int c = 0; c < 2; c++) CHECK(SameAction(m.At(c, t), m2.At(c, t)));
}

TEST(Macro_strict_parsing_names_the_problem) {
	CHECK(MacroErr(Rep(GoodMacro(), "\"name\":\"m\"", "\"nme\":\"m\""), "unknown key"));
	CHECK(MacroErr(Rep(GoodMacro(), "\"schema\":1", "\"schema\":2"), "unsupported"));
	CHECK(MacroErr(Rep(GoodMacro(), "\"lab_macro\"", "\"scenario\""), "lab_macro"));
	CHECK(MacroErr(Rep(GoodMacro(), "\"tick\":3", "\"tick\":25"), "inside the macro"));
	CHECK(MacroErr(Rep(GoodMacro(), "\"car\":1", "\"car\":2"), "existing car"));
	CHECK(MacroErr(Rep(GoodMacro(), "\"boost\":1", "\"boost\":0.5"), "buttons are 0 or 1"));
	CHECK(MacroErr(Rep(GoodMacro(), "\"throttle\":1}}", "\"thrttle\":1}}"), "unknown key"));
	CHECK(MacroErr(Rep(GoodMacro(), "\"steer\":-0.5", "\"steer\":-1.5"), "out of range"));
	// two entries for one car out of order would let the later-listed one silently win
	CHECK(MacroErr(Rep(GoodMacro(), "\"tick\":10", "\"tick\":2"), "out of order"));
	CHECK(MacroErr("{not json", "not valid JSON"));
}

// ---- projection, by hand ---------------------------------------------------------------------------------------------

TEST(Macro_projection_matches_a_hand_built_hold_last_sequence) {
	// car 0: throttle 1 from tick 3, boost 1 from tick 10 (throttle stays 1); 20 ticks.
	Macro m; std::vector<std::string> errs; CHECK(ParseMacroText(GoodMacro(), m, errs));
	// skip 8, phase 0 samples ticks 0, 8, 16  ->  throttle 0,1,1  boost 0,0,1
	auto g = ProjectToGrid(m, 8, 0);
	CHECK_EQ(g.size(), (size_t)3);
	const float thr0[3] = { 0, 1, 1 }, bst0[3] = { 0, 0, 1 };
	for (int k = 0; k < 3; k++) { CHECK_EQ(g[k][0].throttle, thr0[k]); CHECK_EQ(g[k][0].boost, bst0[k]); CHECK_EQ(g[k].size(), (size_t)2); }
	// phase 5 samples ticks 5, 13 (21 is past the end)  ->  throttle 1,1  boost 0,1
	auto g5 = ProjectToGrid(m, 8, 5);
	CHECK_EQ(g5.size(), (size_t)2); CHECK_EQ(g5[0][0].boost, 0.0f); CHECK_EQ(g5[1][0].boost, 1.0f); CHECK_EQ(g5[0][0].throttle, 1.0f);
	// phase 2 samples 2, 10, 18: throttle 0 (tick 2 is before the entry at 3), then 1, 1
	auto g2 = ProjectToGrid(m, 8, 2);
	CHECK_EQ(g2.size(), (size_t)3); CHECK_EQ(g2[0][0].throttle, 0.0f); CHECK_EQ(g2[1][0].throttle, 1.0f); CHECK_EQ(g2[1][0].boost, 1.0f);
	// skip 1 is the identity
	auto g1 = ProjectToGrid(m, 1, 0);
	CHECK_EQ(g1.size(), (size_t)20);
	for (int t = 0; t < 20; t++) CHECK(SameAction(g1[t][0], m.At(0, t)));
	// bad phase / skip
	CHECK(ProjectToGrid(m, 8, 8).empty()); CHECK(ProjectToGrid(m, 8, -1).empty()); CHECK(ProjectToGrid(m, 0, 0).empty());
}

TEST(Macro_a_one_tick_press_survives_only_in_the_phase_that_samples_it) {
	Macro m = RecordTickLevel();
	CHECK_EQ(m.Ticks(), 64);
	CHECK_EQ(m.At(0, 19).jump, 0.0f); CHECK_EQ(m.At(0, 20).jump, 1.0f); CHECK_EQ(m.At(0, 21).jump, 0.0f);   // recorded exactly
	int caught = 0, phaseCaught = -1;
	for (int p = 0; p < 8; p++) {
		bool any = false;
		for (auto& step : ProjectToGrid(m, 8, p)) if (step[0].jump == 1.0f) any = true;
		if (any) { caught++; phaseCaught = p; }
	}
	CHECK_EQ(caught, 1);            // 1 of 8 phases: the 12.5% the 15 Hz decision rate leaves a 1-tick press
	CHECK_EQ(phaseCaught, 4);       // tick 20 = 4 mod 8
	// skip 4: tick 20 = 0 mod 4, so one phase of four
	int caught4 = 0;
	for (int p = 0; p < 4; p++) { bool any = false; for (auto& step : ProjectToGrid(m, 4, p)) if (step[0].jump == 1.0f) any = true; caught4 += any; }
	CHECK_EQ(caught4, 1);
}

// ---- the exit gates, on the real engine -----------------------------------------------------------------------------

TEST(Macro_recorded_tick_level_run_replays_bit_exact_through_a_file) {
	Scenario s = TickLevelDrive();
	LabSession original(s); CHECK(original.ok());
	original.RunScripted();
	CHECK_EQ(original.Steps(), 64);
	Macro rec = MacroFromLog(original.ActionLog(), 1, "tick_level");
	// through the file form and back: floats must survive the text
	Macro loaded; std::vector<std::string> errs;
	CHECK(ParseMacroText(MacroToJson(rec).dump(), loaded, errs));
	GridRun r = RunGrid(s, loaded, 1, 0);
	CHECK(r.errors.empty());
	CHECK_EQ(r.steps, 64);
	auto a = Prints(original.Frames()), b = Prints(r.frames);
	CHECK_EQ(a.size(), b.size());
	for (size_t i = 0; i < a.size() && i < b.size(); i++) CHECK(a[i] == b[i]);   // state, actions and the whole reward trace, no tolerance
	// and the recorded jump really happened in the run
	CHECK(r.frames.back().players[0].hasJumped);
}

TEST(Macro_decision_grid_run_of_a_grid_recording_equals_the_original) {
	// A run at the training skip (8): expand to ticks, project back at phase 0 - the identity - and replay.
	Scenario s = GridScenario(); s.actions = { Entry(0, 0, Act(1, 1)), Entry(0, 3, Act(0.5f, 0, 1)), Entry(0, 4, Act(1, 1)) };
	LabSession original(s); original.RunScripted();
	Macro rec = MacroFromLog(original.ActionLog(), 8);
	CHECK_EQ(rec.Ticks(), 64);
	auto back = ProjectToGrid(rec, 8, 0);
	CHECK_EQ(back.size(), original.ActionLog().size());
	for (size_t k = 0; k < back.size(); k++) for (size_t c = 0; c < back[k].size(); c++) CHECK(SameAction(back[k][c], original.ActionLog()[k][c]));
	GridRun r = RunGrid(s, rec, 8, 0);
	auto a = Prints(original.Frames()), b = Prints(r.frames);
	CHECK_EQ(a.size(), b.size());
	for (size_t i = 0; i < a.size() && i < b.size(); i++) CHECK(a[i] == b[i]);
	// the scenario's expectations are evaluated on the grid it was written for
	CHECK_EQ(r.expectations.size(), (size_t)1); CHECK(r.expectations[0].ok);
}

TEST(Macro_engine_loses_the_press_in_seven_of_eight_phases) {
	Scenario s = GridScenario();
	Macro m = RecordTickLevel();
	int jumped = 0, jumpedPhase = -1;
	for (int p = 0; p < 8; p++) {
		GridRun r = RunGrid(s, m, 8, p);
		CHECK(r.errors.empty()); CHECK_EQ(r.steps, 8);
		if (r.frames.back().players[0].hasJumped) { jumped++; jumpedPhase = p; }
	}
	CHECK_EQ(jumped, 1);
	CHECK_EQ(jumpedPhase, 4);
	// the 120 Hz version (the ceiling) jumps
	GridRun ceiling = RunGrid(s, m, 1, 0);
	CHECK(ceiling.frames.back().players[0].hasJumped);
	// expectations are written for skip 8 and are not evaluated at another skip
	CHECK(ceiling.expectations.empty());
}

TEST(Macro_sweep_covers_every_phase_and_reports_pay_per_phase) {
	// An idle macro under an always-on penalty: every phase plays 8 steps and every phase pays the same.
	// Blue car 1000 uu upfield of a resting ball, nobody covering: OffsidePenalty pays (1000-200)/800 = -1 per step, so -8 over 8 steps.
	Scenario s = GridScenario(); s.expect.clear();
	s.cars[0] = MkCar(Team::BLUE, Vec(0, 1000, 17)); s.ball.pos = Vec(500, 0, 93);
	Macro idle; idle.cars = 2; idle.perTick.assign(64, std::vector<Action>(2, IdleAction()));
	SweepResult sw = SweepPhases(s, idle, 8);
	CHECK(sw.errors.empty());
	CHECK_EQ(sw.phases.size(), (size_t)8);
	CHECK_EQ(sw.PaidCount(), 8);
	for (int p = 0; p < 8; p++) { CHECK_EQ(sw.phases[p].phase, p); CHECK_EQ(sw.phases[p].steps, 8); CHECK(sw.phases[p].paid); }
	const double ref = sw.phases[0].weightedSum[0][0];
	CHECK_NEAR(ref, -8.0, 0.01);
	for (int p = 1; p < 8; p++) CHECK(sw.phases[p].weightedSum[0][0] == ref);   // identical situation, identical pay: phase must not matter here
	const std::string text = SweepText(sw);
	CHECK(text.find("8 / 8 phases paid") != std::string::npos);
}

TEST(Macro_run_refuses_a_mismatched_or_empty_projection) {
	Scenario s = GridScenario();
	Macro one; one.cars = 1; one.perTick.assign(16, std::vector<Action>(1, IdleAction()));
	GridRun r = RunGrid(s, one, 8, 0);
	CHECK(!r.errors.empty()); CHECK(r.errors[0].find("car(s)") != std::string::npos);
	Macro none; none.cars = 2;
	GridRun r2 = RunGrid(s, none, 8, 0);
	CHECK(!r2.errors.empty()); CHECK(r2.errors[0].find("empty") != std::string::npos);
	GridRun r3 = RunGrid(s, RecordTickLevel(), 8, 9);   // phase outside [0, skip)
	CHECK(!r3.errors.empty());
}

TEST(Macro_every_button_and_axis_change_is_written_and_read_back) {
	// each field alone, so a change in one of them cannot hide behind a change in another
	for (int field = 0; field < 8; field++) {
		Macro m; m.cars = 1; m.perTick.assign(6, std::vector<Action>(1, IdleAction()));
		for (int t = 3; t < 6; t++) m.perTick[t][0][field] = field < 5 ? 0.5f : 1.0f;
		Macro back; std::vector<std::string> errs;
		CHECK(ParseMacroText(MacroToJson(m).dump(), back, errs));
		for (int t = 0; t < 6; t++) for (int k = 0; k < 8; k++) CHECK_EQ(back.At(0, t)[k], m.At(0, t)[k]);
	}
}
