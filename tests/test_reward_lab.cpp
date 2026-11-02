#include "TestFramework.h"

#include <RLGymCPP/Lab/LabReport.h>

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>

using namespace RLGC;
using namespace RLGC::Lab;

// P1 of the Reward Lab (docs/REWARD_VIEWER_SPEC.md): scenario files, a headless runner on the REAL EnvSet, a replay-based timeline.
// Needs build/collision_meshes (run from the repo root) and a libRLGymCPP.a built from the current headers (see the reward-harness skill).

namespace {
	Action Act(float throttle, float boost, float jump = 0, float pitch = 0) {
		Action a = Action(CarControls{});
		a.throttle = throttle; a.boost = boost; a.jump = jump; a.pitch = pitch;
		return a;
	}

	CarSpec MkCar(Team t, Vec pos, float yaw = 90, Vec vel = Vec(0, 0, 0), float boost = 100) {
		CarSpec c; c.team = t; c.pos = pos; c.yawDeg = yaw; c.vel = vel; c.boost = boost; return c;
	}

	// Blue car 0 drives into a resting ball at 1400 uu/s and hits it up-field; blue mate and two orange cars idle.
	Scenario DriveAndHit(int steps = 40) {
		Scenario s; s.name = "drive_and_hit"; s.steps = steps;
		s.cars = { MkCar(Team::BLUE, Vec(0, -2200, 17), 90, Vec(0, 1400, 0)), MkCar(Team::BLUE, Vec(2500, 1500, 17)),
		           MkCar(Team::ORANGE, Vec(-3000, 3500, 17), 270), MkCar(Team::ORANGE, Vec(3000, 3500, 17), 270) };
		s.ball.pos = Vec(0, -1000, 93);
		ActionEntry a; a.car = 0; a.from = 0; a.action = Act(1, 1); s.actions.push_back(a);
		s.rewards = { { "OffsidePenalty", 0.15f, -1, 0.9969f }, { "CarEnergyPotentialReward", 75.f, 0.3f, 0.9969f }, { "PassingSetupReward", 5.f, 0.3f, 0.9969f } };
		return s;
	}

	// Blue car 1000 uu upfield of a resting ball with nobody covering: OffsidePenalty pays -1 per step (by hand: (1000-200)/800).
	Scenario LoneOffside(int steps = 10) {
		Scenario s; s.name = "lone_offside"; s.steps = steps;
		s.cars = { MkCar(Team::BLUE, Vec(0, 1000, 17)), MkCar(Team::ORANGE, Vec(0, 3500, 17), 270) };
		s.ball.pos = Vec(500, 0, 93);
		s.rewards = { { "OffsidePenalty", 1.0f, -1, 0.9969f } };
		return s;
	}

	std::vector<std::string> Fingerprints(const std::vector<Frame>& f, size_t upTo) {
		std::vector<std::string> out;
		for (size_t i = 0; i < std::min(upTo + 1, f.size()); i++) out.push_back(Fingerprint(f[i]));
		return out;
	}

	std::string GoodJson() {
		return R"({"schema":1,"name":"x","tick_skip":8,"steps":5,
		  "cars":[{"team":"blue","pos":[0,0,17]},{"team":"orange","pos":[0,3000,17],"angle_deg":{"yaw":270}}],
		  "ball":{"pos":[0,0,93]},
		  "actions":[{"car":0,"from":2,"action":{"throttle":1,"boost":1}}],
		  "rewards":[{"name":"OffsidePenalty","weight":0.15}],
		  "expect":[{"kind":"reward_sum","reward":"OffsidePenalty","car":0,"steps":[1,5],"min":-1,"max":0,"intent":"never positive"}]})";
	}

	bool ParseHas(const std::string& text, const std::string& needle) {
		Scenario s; std::vector<std::string> errs;
		if (ParseScenarioText(text, s, errs)) return false;
		for (auto& e : errs) if (e.find(needle) != std::string::npos) return true;
		return false;
	}
	std::string Replace(std::string s, const std::string& a, const std::string& b) { size_t p = s.find(a); if (p != std::string::npos) s.replace(p, a.size(), b); return s; }
}

// ---- scenario file: strict parsing --------------------------------------------------------------------------

TEST(Lab_scenario_parses_and_roundtrips) {
	Scenario s; std::vector<std::string> errs;
	CHECK(ParseScenarioText(GoodJson(), s, errs));
	CHECK(errs.empty());
	CHECK_EQ(s.cars.size(), (size_t)2); CHECK_EQ(s.steps, 5);
	CHECK_EQ(s.cars[1].yawDeg, 270.0f);
	nlohmann::json j = ToJson(s);
	Scenario s2; std::vector<std::string> errs2;
	CHECK(ParseScenario(j, s2, errs2));
	CHECK(ToJson(s2) == j);                 // parse(write(x)) == x
}

TEST(Lab_scenario_rejects_what_it_should_with_the_path_in_the_message) {
	const std::string g = GoodJson();
	CHECK(ParseHas(Replace(g, "\"name\":\"x\"", "\"name\":\"x\",\"nmae\":1"), "nmae: unknown key"));                       // typo'd key is an error, not ignored
	CHECK(ParseHas(Replace(g, "\"team\":\"blue\"", "\"team\":\"red\""), "cars[0].team"));
	CHECK(ParseHas(Replace(g, ",\"intent\":\"never positive\"", ""), "expect[0].intent: missing"));                        // an expectation must say why
	CHECK(ParseHas(Replace(g, "\"intent\":\"never positive\"", "\"intent\":\"\""), "expect[0].intent"));
	CHECK(ParseHas(Replace(g, "\"schema\":1", "\"schema\":2"), "schema: unsupported"));
	CHECK(ParseHas(Replace(g, "\"schema\":1,", ""), "schema: missing"));
	CHECK(ParseHas(Replace(g, "\"car\":0,\"from\":2", "\"car\":5,\"from\":2"), "actions[0].car"));                         // no such car
	CHECK(ParseHas(Replace(g, "\"throttle\":1", "\"throttle\":3"), "actions[0].action.throttle"));
	CHECK(ParseHas(Replace(g, "\"boost\":1", "\"boost\":0.5"), "buttons are 0 or 1"));
	CHECK(ParseHas(Replace(g, "\"tick_skip\":8", "\"tick_skip\":8,\"action_delay\":8"), "action_delay"));
	CHECK(ParseHas(Replace(g, "\"min\":-1,\"max\":0", "\"min\":1,\"max\":0"), "min is greater than max"));
	CHECK(ParseHas(Replace(g, "\"min\":-1,\"max\":0,", ""), "reward_sum needs min and/or max"));
	CHECK(ParseHas(Replace(g, "\"pos\":[0,0,93]", "\"pos\":[0,0]"), "ball.pos: expected an array of 3"));
	CHECK(ParseHas("{not json", "not valid JSON"));
	CHECK(ParseHas(Replace(g, "\"cars\":[", "\"cars\":[{\"team\":\"blue\",\"pos\":[0,0,17]},{\"team\":\"blue\",\"pos\":[0,0,17]},{\"team\":\"blue\",\"pos\":[0,0,17]},{\"team\":\"blue\",\"pos\":[0,0,17]},"), "at most 3 cars"));
}

TEST(Lab_scripted_action_holds_until_the_next_entry_and_idles_before_the_first) {
	Scenario s = DriveAndHit();
	s.actions.clear();
	ActionEntry a; a.car = 0; a.from = 3; a.action = Act(1, 0); s.actions.push_back(a);
	a.from = 6; a.action = Act(-1, 1); s.actions.push_back(a);
	a.car = 1; a.from = 0; a.action = Act(0.5f, 0); s.actions.push_back(a);
	CHECK_EQ(ScriptedAction(s, 0, 0).throttle, 0.0f);   // before the first entry: idle
	CHECK_EQ(ScriptedAction(s, 0, 2).throttle, 0.0f);
	CHECK_EQ(ScriptedAction(s, 0, 3).throttle, 1.0f);
	CHECK_EQ(ScriptedAction(s, 0, 5).boost, 0.0f);
	CHECK_EQ(ScriptedAction(s, 0, 6).throttle, -1.0f);
	CHECK_EQ(ScriptedAction(s, 0, 99).boost, 1.0f);
	CHECK_EQ(ScriptedAction(s, 1, 4).throttle, 0.5f);   // per car
	CHECK_EQ(ScriptedAction(s, 2, 4).throttle, 0.0f);
}

// ---- the runner on the real engine --------------------------------------------------------------------------

TEST(Lab_unknown_reward_name_is_an_error_not_a_skip) {
	Scenario s = LoneOffside(); s.rewards.push_back({ "NoSuchReward", 1.f, -1, 0.99f });
	LabSession session(s);
	CHECK(!session.ok());
	CHECK(session.errors().size() == 1 && session.errors()[0].find("unknown reward \"NoSuchReward\"") != std::string::npos);
}

TEST(Lab_offside_scenario_matches_the_hand_value_on_the_real_engine) {
	Scenario s = LoneOffside(10);
	Expectation e; e.kind = "reward_sum"; e.intent = "a lone car 1000 uu upfield pays -1 per step"; e.reward = "OffsidePenalty"; e.car = 0; e.from = 1; e.to = 10; e.min = -10.05; e.max = -9.95;
	Expectation g; g.kind = "gate"; g.intent = "the car is offside every step"; g.reward = "OffsidePenalty"; g.car = 0; g.gate = "offside"; g.passed = true;
	Expectation g2; g2.kind = "gate"; g2.intent = "the orange car is goalside of the ball"; g2.reward = "OffsidePenalty"; g2.car = 1; g2.gate = "offside"; g2.passed = false;
	s.expect = { e, g, g2 };
	LabSession session(s);
	CHECK(session.ok());
	session.RunScripted();
	CHECK_EQ(session.Steps(), 10);
	CHECK_EQ(session.Frames().size(), (size_t)11);
	CHECK(!session.Frames()[0].hasTrace && session.Frames()[1].hasTrace);
	for (auto& r : session.EvaluateExpectations()) { if (!r.ok) std::cerr << "    " << r.detail << "\n"; CHECK(r.ok); }
}

TEST(Lab_two_fresh_sessions_give_identical_frames) {
	Scenario s = DriveAndHit();
	std::vector<std::string> a, b;
	{ LabSession x(s); CHECK(x.ok()); x.RunScripted(); a = Fingerprints(x.Frames(), 1000); }
	{ LabSession y(s); CHECK(y.ok()); y.RunScripted(); b = Fingerprints(y.Frames(), 1000); }
	CHECK_EQ(a.size(), (size_t)41);
	CHECK(a == b);
}

TEST(Lab_reset_and_rerun_in_the_same_session_is_identical_to_the_first_run) {
	LabSession x(DriveAndHit()); CHECK(x.ok());
	x.RunScripted();
	std::vector<std::string> first = Fingerprints(x.Frames(), 1000);
	x.Reset();
	CHECK_EQ(x.Steps(), 0); CHECK_EQ(x.Frames().size(), (size_t)1);
	x.RunScripted();
	CHECK(first == Fingerprints(x.Frames(), 1000));
}

TEST(Lab_the_scenario_produces_a_paid_pass_so_the_replay_tests_below_are_not_vacuous) {
	LabSession x(DriveAndHit()); CHECK(x.ok());
	x.RunScripted();
	int paid = 0, firstPaid = -1;
	for (auto& f : x.Frames()) if (f.hasTrace) {
		const PlayerTrace* p = f.trace.rewards[2].sink.Find(f.trace.carIds[0]);
		if (p) for (auto& e : p->events) if (e == "paid") { paid++; if (firstPaid < 0) firstPaid = f.index; }
	}
	CHECK(paid >= 1);
	CHECK(firstPaid > 2);
	std::cerr << "    [info] first PassingSetup payment at frame " << firstPaid << ", " << paid << " paid frame(s)\n";
}

TEST(Lab_replay_to_frame_F_reproduces_the_original_exactly_and_continues_identically) {
	Scenario s = DriveAndHit();
	LabSession x(s); CHECK(x.ok());
	x.RunScripted();
	const std::vector<std::string> orig = Fingerprints(x.Frames(), 1000);
	const std::vector<std::vector<Action>> origLog = x.ActionLog();
	for (int F : { 0, 1, 5, 9, 17, 29, 40 }) {
		CHECK(x.ReplayTo(F));
		CHECK_EQ(x.Steps(), F);
		CHECK(Fingerprints(x.Frames(), 1000) == std::vector<std::string>(orig.begin(), orig.begin() + F + 1));   // frames 0..F equal the original
		// continue with the original actions: frames after F equal the original too (a reward that kept stale state would pay differently)
		for (int k = F; k < (int)origLog.size(); k++) CHECK(x.StepOnce(origLog[k]));
		CHECK(Fingerprints(x.Frames(), 1000) == orig);
	}
}

TEST(Lab_seeking_back_resets_reward_state_a_replay_through_the_cooldown_pays_again) {
	// Run through the first payment (cooldown starts), replay to a frame BEFORE it, and step the same actions: the payment must
	// repeat. A reward left holding its old cooldown would pay 0 the second time.
	LabSession x(DriveAndHit()); CHECK(x.ok());
	x.RunScripted();
	int firstPaid = -1;
	for (auto& f : x.Frames()) if (f.hasTrace && firstPaid < 0) {
		const PlayerTrace* p = f.trace.rewards[2].sink.Find(f.trace.carIds[0]);
		if (p) for (auto& e : p->events) if (e == "paid") firstPaid = f.index;
	}
	CHECK(firstPaid > 2);
	const float paidValue = x.Frames()[firstPaid].trace.rewards[2].raw[0];
	CHECK(paidValue > 0.0f);
	const auto log = x.ActionLog();
	CHECK(x.ReplayTo(firstPaid - 2));
	for (int k = firstPaid - 2; k < firstPaid; k++) CHECK(x.StepOnce(log[k]));
	CHECK_EQ(x.Frames()[firstPaid].trace.rewards[2].raw[0], paidValue);
}

TEST(Lab_reset_clears_reward_state_even_when_the_payment_comes_inside_the_cooldown_window) {
	// The test above replays long enough for a stale cooldown to expire by itself (payment at frame 10, cooldown 7.5 steps). Here the car
	// starts close, so the payment comes within the first few steps: a reward that kept its cooldown across Reset would pay 0 the second time.
	Scenario s = DriveAndHit();
	s.cars[0].pos = Vec(0, -1400, 17);
	LabSession x(s); CHECK(x.ok());
	// Stop the moment the payment happens: the reward's cooldown is then still running, which is the state a leak would carry into the replay.
	int firstPaid = -1;
	while (firstPaid < 0 && x.StepScripted()) {
		const Frame& f = x.Frames().back();
		const PlayerTrace* p = f.trace.rewards[2].sink.Find(f.trace.carIds[0]);
		if (p) for (auto& e : p->events) if (e == "paid") firstPaid = f.index;
	}
	CHECK(firstPaid > 0 && firstPaid <= 6);                              // inside one cooldown (7.5 steps) of the start
	const float paid = x.Frames()[firstPaid].trace.rewards[2].raw[0];
	CHECK(paid > 0.0f);
	const auto log = x.ActionLog();
	CHECK(x.ReplayTo(0));
	for (int k = 0; k < firstPaid; k++) CHECK(x.StepOnce(log[k]));
	CHECK_EQ(x.Frames()[firstPaid].trace.rewards[2].raw[0], paid);
}

TEST(Lab_component_expectation_needs_every_emission_in_range) {
	Scenario s; s.name = "sprint"; s.steps = 30;
	s.cars = { MkCar(Team::BLUE, Vec(480, -4000, 17), 90, Vec(0, 0, 0), 100), MkCar(Team::ORANGE, Vec(-1400, 1500, 17), 270) };
	s.ball.pos = Vec(-3000, 4000, 93);
	ActionEntry a; a.car = 0; a.from = 0; a.action = Act(1, 1); s.actions.push_back(a);
	s.rewards = { { "CarEnergyPotentialReward", 1.f, -1, 0.9969f } };
	auto mk = [](double lo, double hi) { Expectation e; e.kind = "component"; e.intent = "x"; e.reward = "CarEnergyPotentialReward"; e.car = 0; e.component = "phi_next"; e.min = lo; e.max = hi; return e; };
	Expectation never = mk(0, 10); never.component = "no_such_component";
	s.expect = { mk(0.0, 5.0), mk(0.9, 1.2), never };      // phi_next starts near 1.0 (full boost) and climbs to about 1.4 as the sprinter's kinetic energy outgrows the boost it burns
	LabSession x(s); CHECK(x.ok()); x.RunScripted();
	auto r = x.EvaluateExpectations();
	CHECK(r[0].ok);                                         // every emission inside [0, 5]
	CHECK(!r[1].ok);                                        // some emissions inside [0.9, 1.2], some above it: must fail
	CHECK(r[1].detail.find("ranged over") != std::string::npos);
	CHECK(!r[2].ok && r[2].detail.find("never emitted") != std::string::npos);
}

TEST(Lab_branching_after_a_replay_diverges_only_after_the_branch_point) {
	LabSession x(DriveAndHit()); CHECK(x.ok());
	x.RunScripted();
	const std::vector<std::string> orig = Fingerprints(x.Frames(), 1000);
	auto log = x.ActionLog();
	CHECK(x.ReplayTo(10));
	std::vector<Action> other = log[10]; other[0] = Act(0, 0);          // car 0 lifts off the throttle at step 10
	CHECK(x.StepOnce(other));
	CHECK_EQ(x.Steps(), 11);
	const auto now = Fingerprints(x.Frames(), 1000);
	CHECK(std::vector<std::string>(now.begin(), now.begin() + 11) == std::vector<std::string>(orig.begin(), orig.begin() + 11));   // frames 0..10 untouched
	CHECK(now[11] != orig[11]);                                          // frame 11 differs
	CHECK_EQ(x.ActionLog().size(), (size_t)11);                          // the discarded future is gone
}

TEST(Lab_reward_block_agrees_with_the_real_EnvSet_reward_block) {
	// The Lab runs its own copy of EnvSet's reward block; here the SAME rewards run inside a second EnvSet and the per-player totals are compared.
	// (Sub-ulp noise is allowed: the two blocks are compiled with different contraction.)
	Scenario s = DriveAndHit(40);
	LabSession lab(s); CHECK(lab.ok());
	lab.RunScripted();

	std::vector<WeightedReward> inside; std::vector<std::string> errs;
	CHECK(BuildRewardStack(s.rewards, inside, errs));
	LabEnv* ref = LabEnv::Create(s, inside); CHECK(ref != nullptr);
	for (auto& w : inside) w.reward->Reset(ref->env->state.gameStates[0]);   // EnvSet already did this once; harmless and explicit
	double maxDiff = 0; int nonzero = 0;
	for (int k = 0; k < lab.Steps(); k++) {
		ref->Step(lab.ActionLog()[k]);
		const Frame& f = lab.Frames()[k + 1];
		for (size_t p = 0; p < f.trace.total.size(); p++) {
			const float envRew = ref->env->state.rewards[p];
			maxDiff = std::max(maxDiff, (double)std::fabs(envRew - f.trace.total[p]) / (1.0 + std::fabs(envRew)));
			if (std::fabs(envRew) > 1e-6f) nonzero++;
		}
		// same world, too
		for (size_t p = 0; p < f.players.size(); p++) CHECK(ref->env->state.gameStates[0].players[p].pos.x == f.players[p].pos.x);
	}
	delete ref;
	FreeRewardStack(inside);
	CHECK(nonzero > 40);
	CHECK(maxDiff < 1e-4);
}

TEST(Lab_a_goal_ends_the_episode_and_the_final_flag_reaches_the_rewards) {
	Scenario s; s.name = "goal"; s.steps = 60;
	s.cars = { MkCar(Team::BLUE, Vec(0, -3000, 17)), MkCar(Team::ORANGE, Vec(3000, 3000, 17), 270) };
	s.ball.pos = Vec(0, 4600, 93); s.ball.vel = Vec(0, 3500, 0);      // straight into the orange goal
	s.rewards = { { "CarEnergyPotentialReward", 1.f, -1, 0.9969f } };
	LabSession x(s); CHECK(x.ok());
	x.RunScripted();
	CHECK(x.Finished());
	CHECK(x.Steps() < 60);
	CHECK(x.Frames().back().terminal != 0 && x.Frames().back().goalScored);
	CHECK(!x.StepScripted());                                            // nothing happens after the end
	// on the goal step CarEnergy's Phi(s') is 0 (its gate says so)
	const PlayerTrace* p = x.Frames().back().trace.rewards[0].sink.Find(x.Frames().back().trace.carIds[0]);
	bool sawGoalGate = false;
	for (auto& g : p->gates) if (g.name == "not_goal_step" && !g.passed) sawGoalGate = true;
	CHECK(sawGoalGate);
}

namespace {
	// Emits an event on the step the terminal flag is set: the only way to see that the Lab hands `isFinal` to the rewards.
	class FinalFlagProbe : public Reward {
	public:
		virtual float GetReward(const Player& p, const GameState&, bool isFinal) override {
			if (trace) { trace->Gate(p.carId, "is_final", isFinal); if (isFinal) trace->Event(p.carId, "final"); }
			return 0.0f;
		}
	};
}

TEST(Lab_isFinal_is_true_on_the_terminal_step_only) {
	RewardRegistry()["FinalFlagProbe"] = [](const RewardSpec&) -> Reward* { return new FinalFlagProbe(); };
	Scenario s; s.name = "goal_probe"; s.steps = 60;
	s.cars = { MkCar(Team::BLUE, Vec(0, -3000, 17)), MkCar(Team::ORANGE, Vec(3000, 3000, 17), 270) };
	s.ball.pos = Vec(0, 4600, 93); s.ball.vel = Vec(0, 3500, 0);
	s.rewards = { { "FinalFlagProbe", 1.f, -1, 0.9969f } };
	LabSession x(s); CHECK(x.ok());
	x.RunScripted();
	CHECK(x.Frames().back().terminal != 0);
	int finals = 0;
	for (auto& f : x.Frames()) if (f.hasTrace) {
		const PlayerTrace* p = f.trace.rewards[0].sink.Find(f.trace.carIds[0]);
		bool fin = false; for (auto& e : p->events) if (e == "final") fin = true;
		CHECK_EQ(fin, f.index == x.Frames().back().index);       // true on the last frame, false on every earlier one
		finals += fin;
	}
	CHECK_EQ(finals, 1);
	RewardRegistry().erase("FinalFlagProbe");
}

TEST(Lab_gate_mode_all_versus_any_on_a_gate_that_changes_during_the_run) {
	Scenario s = DriveAndHit(40);
	s.rewards = { { "PassingSetupReward", 1.f, -1, 0.9969f } };
	auto mk = [](const char* mode, bool passed) { Expectation e; e.kind = "gate"; e.intent = "x"; e.reward = "PassingSetupReward"; e.car = 0; e.gate = "touched_ball"; e.mode = mode; e.passed = passed; return e; };
	s.expect = { mk("all", true), mk("any", true), mk("all", false), mk("any", false) };
	LabSession x(s); CHECK(x.ok()); x.RunScripted();
	auto r = x.EvaluateExpectations();
	// touched_ball fails before the hit and passes on it: "all passed" is false, "any passed" true, "all failed" false, "any failed" true
	CHECK(!r[0].ok); CHECK(r[1].ok); CHECK(!r[2].ok); CHECK(r[3].ok);
}

// ---- the shipped scenario pack, report and trace ------------------------------------------------------------

namespace {
	std::vector<std::string> PackFiles() {
		std::vector<std::string> out;
		for (auto& e : std::filesystem::directory_iterator("tools/reward_lab/packs/core"))
			if (e.path().string().find(".scenario.json") != std::string::npos) out.push_back(e.path().string());
		std::sort(out.begin(), out.end());
		return out;
	}
	bool Load(const std::string& path, Scenario& s, std::vector<std::string>& errs) {
		std::ifstream f(path); std::stringstream ss; ss << f.rdbuf();
		return ParseScenarioText(ss.str(), s, errs);
	}
}

TEST(Lab_core_pack_every_scenario_parses_has_intents_and_passes_on_the_real_engine) {
	const auto files = PackFiles();
	CHECK(files.size() >= 9);
	std::set<std::string> names; int totalExpect = 0;
	for (auto& file : files) {
		Scenario s; std::vector<std::string> errs;
		if (!Load(file, s, errs)) { for (auto& e : errs) std::cerr << "    " << file << ": " << e << "\n"; }
		CHECK(errs.empty());
		CHECK(names.insert(s.name).second);                                   // unique names
		CHECK(!s.expect.empty());                                             // a scenario with no expectation proves nothing
		LabSession x(s); CHECK(x.ok());
		x.RunScripted();
		for (auto& r : x.EvaluateExpectations()) {
			if (!r.ok) std::cerr << "    " << s.name << ": " << r.expectation->intent << " -> " << r.detail << "\n";
			CHECK(r.ok);
			totalExpect++;
		}
	}
	CHECK(totalExpect >= 25);
}

TEST(Lab_report_names_the_intent_the_measured_value_events_and_gates_and_the_trace_is_valid_jsonl) {
	Scenario s; std::vector<std::string> errs;
	CHECK(Load("tools/reward_lab/packs/core/passing_setup_straight_hit.scenario.json", s, errs));
	LabSession x(s); CHECK(x.ok()); x.RunScripted();
	const std::string rep = TextReport(x);
	CHECK(rep.find("SCENARIO passing_setup_straight_hit") != std::string::npos);
	CHECK(rep.find("RESULT: PASS (5/5 expectations)") != std::string::npos);
	CHECK(rep.find("one clean hit is one payment") != std::string::npos);        // the author's intent line
	CHECK(rep.find("event \"paid\" occurred on 1 frames") != std::string::npos); // the measured value
	CHECK(rep.find("PassingSetupReward  paid") != std::string::npos);            // the event timeline
	CHECK(rep.find("PassingSetupReward / car 0 / touched_ball:") != std::string::npos);   // the gate table
	std::ostringstream os; WriteTraceJsonl(x, os);
	std::istringstream is(os.str()); std::string line; int n = 0;
	while (std::getline(is, line)) {
		nlohmann::json j = nlohmann::json::parse(line);
		CHECK_EQ(j["schema"].get<int>(), REWARD_TRACE_SCHEMA);
		CHECK_EQ(j["frame"].get<int>(), ++n);
	}
	CHECK_EQ(n, x.Steps());
	// a failing run says FAIL
	s.expect[0].countMin = 5; s.expect[0].countMax = 5;
	LabSession y(s); y.RunScripted();
	CHECK(TextReport(y).find("RESULT: FAIL (4/5 expectations)") != std::string::npos);
	CHECK(TextReport(y).find("[FAIL] one clean hit is one payment") != std::string::npos);
}

// ---- expectations say what they measured --------------------------------------------------------------------

TEST(Lab_expectations_pass_fail_and_never_silently_skip) {
	Scenario s = LoneOffside(10);
	auto mk = [](const char* kind, const char* reward) { Expectation e; e.kind = kind; e.intent = "x"; e.reward = reward; e.from = 1; e.to = 10; return e; };
	Expectation sumOk = mk("reward_sum", "OffsidePenalty"); sumOk.min = -10.05; sumOk.max = -9.95;
	Expectation sumBad = mk("reward_sum", "OffsidePenalty"); sumBad.min = -5; sumBad.max = 0;                   // wrong on purpose
	Expectation absOk = mk("reward_abs_max", "OffsidePenalty"); absOk.max = 1.01;
	Expectation absBad = mk("reward_abs_max", "OffsidePenalty"); absBad.max = 0.5;
	Expectation total = mk("reward_sum", "*total"); total.min = -10.05; total.max = -9.95;
	Expectation weighted = mk("reward_sum", "OffsidePenalty"); weighted.of = "weighted"; weighted.min = -10.05; weighted.max = -9.95;
	Expectation noReward = mk("reward_sum", "Typo"); noReward.min = -1; noReward.max = 1;
	Expectation noGate = mk("gate", "OffsidePenalty"); noGate.gate = "no_such_gate";
	Expectation noRange = mk("reward_sum", "OffsidePenalty"); noRange.from = 50; noRange.to = 60; noRange.min = -1; noRange.max = 1;
	Expectation evNone = mk("event", "OffsidePenalty"); evNone.event = "paid"; evNone.countMin = 0; evNone.countMax = 0;
	Expectation evWant = mk("event", "OffsidePenalty"); evWant.event = "paid"; evWant.countMin = 1;
	Expectation gateAny = mk("gate", "OffsidePenalty"); gateAny.gate = "offside"; gateAny.passed = false; gateAny.mode = "any";
	s.expect = { sumOk, sumBad, absOk, absBad, total, weighted, noReward, noGate, noRange, evNone, evWant, gateAny };
	LabSession x(s); CHECK(x.ok()); x.RunScripted();
	auto r = x.EvaluateExpectations();
	CHECK_EQ(r.size(), (size_t)12);
	const bool want[12] = { true, false, true, false, true, true, false, false, false, true, false, false };
	for (int i = 0; i < 12; i++) { if (r[i].ok != want[i]) std::cerr << "    expectation " << i << ": " << r[i].detail << "\n"; CHECK_EQ(r[i].ok, want[i]); }
	CHECK(r[1].detail.find("summed to -10") != std::string::npos);                       // the failure says what was measured
	CHECK(r[6].detail.find("not in the stack (stack: OffsidePenalty)") != std::string::npos);
	CHECK(r[7].detail.find("never emitted") != std::string::npos);
	CHECK(r[8].detail.find("no frames in range") != std::string::npos);
}
