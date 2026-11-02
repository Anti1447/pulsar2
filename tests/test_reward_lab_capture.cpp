#include "TestFramework.h"

#include <RLGymCPP/Lab/LabHost.h>

#include <iostream>

using namespace RLGC;
using namespace RLGC::Lab;
using nlohmann::json;

// P4-2 of the Reward Lab (docs/REWARD_VIEWER_SPEC.md): capture a frame of a live session as a scenario. Needs build/collision_meshes.

namespace {
	Action Act(float throttle, float boost, float jump = 0, float steer = 0, float pitch = 0) {
		Action a = Action(CarControls{});
		a.throttle = throttle; a.boost = boost; a.jump = jump; a.steer = steer; a.pitch = pitch;
		return a;
	}
	CarSpec MkCar(Team t, Vec pos, float yaw = 90, Vec vel = Vec(0, 0, 0), float boost = 100) {
		CarSpec c; c.team = t; c.pos = pos; c.yawDeg = yaw; c.vel = vel; c.boost = boost; return c;
	}
	// Blue car 0 drives at a resting ball, hits it and jumps; a blue mate and two orange cars idle.
	Scenario Drive(int steps = 30) {
		Scenario s; s.name = "Capture Drive"; s.steps = steps; s.description = "original"; s.tags = { "mine" };
		s.cars = { MkCar(Team::BLUE, Vec(0, -2200, 17), 90, Vec(0, 1400, 0)), MkCar(Team::BLUE, Vec(2500, 1500, 17)),
		           MkCar(Team::ORANGE, Vec(-3000, 3500, 17), 270), MkCar(Team::ORANGE, Vec(3000, 3500, 17), 270) };
		s.ball.pos = Vec(0, -1000, 93);
		s.rewards = { { "PassingSetupReward", 1.0f, -1, 0.9969f }, { "OffsidePenalty", 0.15f, -1, 0.9969f } };
		Expectation e; e.kind = "event"; e.intent = "x"; e.reward = "PassingSetupReward"; e.event = "paid"; e.car = 0; e.countMin = 0; e.countMax = 5;
		s.expect = { e };
		return s;
	}
	// A varied manual run: drive, hit, steer, a one-step jump, boost off.
	std::vector<Action> Varied(int step) {
		Action a0 = Act(1, 1);
		if (step >= 4 && step < 7) a0 = Act(1, 1, 0, 0.5f);
		if (step == 9) a0 = Act(1, 0, 1);
		if (step >= 10) a0 = Act(0.5f, 0, 0, -0.3f);
		return { a0, Act(0, 0), Act(0, 0), Act(0, 0) };
	}
	void RunVaried(LabSession& s, int n) { for (int i = 0; i < n; i++) s.StepOnce(Varied(i)); }
	std::vector<std::string> Prints(const std::vector<Frame>& f, size_t upTo) { std::vector<std::string> o; for (size_t i = 0; i <= upTo && i < f.size(); i++) o.push_back(Fingerprint(f[i])); return o; }
	bool Rebuild(const Scenario& c, Scenario& out) { std::vector<std::string> e; return ParseScenarioText(ToJson(c).dump(), out, e); }
	bool HasWarn(const std::vector<std::string>& w, const std::string& needle) { for (auto& x : w) if (x.find(needle) != std::string::npos) return true; return false; }
	double Dist(const Vec& a, const Vec& b) { return std::sqrt((double)(a.x - b.x) * (a.x - b.x) + (double)(a.y - b.y) * (a.y - b.y) + (double)(a.z - b.z) * (a.z - b.z)); }
}

// ---- exact replay -------------------------------------------------------------------------------------------------------

TEST(Capture_replay_reproduces_every_frame_bit_for_bit_through_a_file) {
	Scenario base = Drive();
	LabSession orig(base); CHECK(orig.ok()); RunVaried(orig, 14);
	for (int frame : { 1, 5, 9, 10, 14 }) {
		CaptureResult r; std::string err;
		CHECK(CaptureReplay(orig, frame, "cap_test", r, err));
		Scenario parsed; CHECK(Rebuild(r.scenario, parsed));                 // through the strict file format and back
		LabSession again(parsed); CHECK(again.ok());
		again.RunScripted();
		CHECK_EQ(again.Steps(), frame);
		auto a = Prints(orig.Frames(), frame), b = Prints(again.Frames(), frame);
		CHECK_EQ(a.size(), b.size());
		for (size_t i = 0; i < a.size() && i < b.size(); i++) CHECK(a[i] == b[i]);   // state, actions and the whole reward trace, no tolerance
	}
}

TEST(Capture_replay_scenario_is_minimal_labelled_and_has_no_expectations) {
	Scenario base = Drive();
	LabSession orig(base); RunVaried(orig, 12);
	CaptureResult r; std::string err;
	CHECK(CaptureReplay(orig, 12, "cap_min", r, err));
	const Scenario& c = r.scenario;
	CHECK_EQ(c.name, std::string("cap_min")); CHECK_EQ(c.steps, 12);
	CHECK(c.expect.empty());                                                  // a capture never states what should happen
	CHECK(r.warnings.empty()); CHECK_EQ(r.mode, std::string("replay"));
	CHECK(std::find(c.tags.begin(), c.tags.end(), "captured") != c.tags.end() && std::find(c.tags.begin(), c.tags.end(), "mine") != c.tags.end());
	CHECK(c.description.find("Captured from \"Capture Drive\"") != std::string::npos); CHECK(c.description.find("exact replay") != std::string::npos);
	CHECK(c.rewards.size() == base.rewards.size());
	// hold-until-next: only changes are entries. Car 0: drive@0, steer@4, drive@7, jump@9, coast@10 = 5 entries; others idle = none.
	int car0 = 0, others = 0; for (auto& e : c.actions) (e.car == 0 ? car0 : others)++;
	CHECK_EQ(car0, 5); CHECK_EQ(others, 0);
	// the ORIGINAL scenario's own script is not carried over: only what was actually fed in (the log) is
	Scenario scripted = Drive(); ActionEntry e; e.car = 3; e.from = 0; e.action = Act(0, 0, 0, 1); scripted.actions.push_back(e);
	LabSession withScript(scripted); RunVaried(withScript, 3);
	CaptureResult r2; CHECK(CaptureReplay(withScript, 3, "cap_two", r2, err));
	for (auto& x : r2.scenario.actions) CHECK(x.car != 3);
}

TEST(Capture_replay_refuses_what_it_cannot_do) {
	Scenario base = Drive();
	LabSession orig(base); RunVaried(orig, 6);
	CaptureResult r; std::string err;
	CHECK(!CaptureReplay(orig, 0, "ok_name", r, err)); CHECK(err.find("frame 0") != std::string::npos);
	CHECK(!CaptureReplay(orig, 7, "ok_name", r, err)); CHECK(err.find("1..6") != std::string::npos);
	CHECK(!CaptureReplay(orig, -1, "ok_name", r, err));
	for (const char* bad : { "", "Upper", "has space", "../x", "a/b", "_lead", "name.json", "ünï" }) { CHECK(!CaptureReplay(orig, 3, bad, r, err)); CHECK(err.find("name must be") != std::string::npos); }
	CHECK(!CaptureReplay(orig, 3, std::string(65, 'a'), r, err));
	CHECK(CaptureReplay(orig, 3, std::string(64, 'a'), r, err));
	CHECK(CaptureReplay(orig, 3, "0starts_with_digit", r, err));
	CHECK(DefaultCaptureName("Capture Drive!", 7, "replay") == "captured_capture_drive__f7_replay");
	CHECK(DefaultCaptureName("x", 2, "state") == "captured_x_f2_state");
	CHECK(ValidCaptureName(DefaultCaptureName("A very ODD name / with %% stuff", 3, "state")));
}

TEST(Capture_does_not_disturb_the_session_it_reads) {
	Scenario base = Drive();
	LabSession orig(base); RunVaried(orig, 10);
	const json before = RunToJson(orig);
	CaptureResult r; std::string err;
	CHECK(CaptureReplay(orig, 6, "a_name", r, err)); CHECK(CaptureState(orig, 6, "b_name", 0, r, err)); CHECK(CaptureState(orig, 10, "c_name", 0, r, err));
	CHECK(RunToJson(orig) == before);                                         // same frames, same log, same expectations
	CHECK_EQ(orig.Steps(), 10);
}

// ---- state snapshot -----------------------------------------------------------------------------------------------------

TEST(Capture_state_starts_at_the_captured_frame) {
	Scenario base = Drive();
	LabSession orig(base); RunVaried(orig, 12);
	for (int frame : { 0, 5, 8, 12 }) {
		CaptureResult r; std::string err;
		CHECK(CaptureState(orig, frame, "snap", 0, r, err));
		Scenario parsed; CHECK(Rebuild(r.scenario, parsed));
		CHECK(parsed.actions.empty()); CHECK(parsed.expect.empty());
		CHECK_EQ(parsed.steps, std::max(1, base.steps - frame));               // the original's remaining steps
		LabSession snap(parsed); CHECK(snap.ok());
		const Frame& want = orig.Frames()[frame]; const Frame& got = snap.Frames()[0];
		CHECK_EQ(got.players.size(), want.players.size());
		for (size_t i = 0; i < want.players.size(); i++) {
			const PlayerSnap& a = want.players[i]; const PlayerSnap& b = got.players[i];
			CHECK(Dist(a.pos, b.pos) < 1e-3); CHECK(Dist(a.vel, b.vel) < 1e-3); CHECK(Dist(a.forward, b.forward) < 1e-4); CHECK(Dist(a.up, b.up) < 1e-4);
			CHECK(std::fabs(a.boost - b.boost) < 1e-3); CHECK_EQ(a.team, b.team);
			CHECK_EQ(a.hasJumped, b.hasJumped); CHECK_EQ(a.hasDoubleJumped, b.hasDoubleJumped); CHECK_EQ(a.hasFlipped, b.hasFlipped);
		}
		CHECK(Dist(want.ball.pos, got.ball.pos) < 1e-3); CHECK(Dist(want.ball.vel, got.ball.vel) < 1e-3); CHECK(Dist(want.ball.angVel, got.ball.angVel) < 1e-3);
	}
	CaptureResult r; std::string err;
	CHECK(CaptureState(orig, 8, "snap", 7, r, err)); CHECK_EQ(r.scenario.steps, 7);                 // explicit length
	CHECK(!CaptureState(orig, 13, "snap", 0, r, err)); CHECK(!CaptureState(orig, -1, "snap", 0, r, err));
	CHECK(!CaptureState(orig, 3, "snap", 100001, r, err)); CHECK(!CaptureState(orig, 3, "Bad Name", 0, r, err));
}

TEST(Capture_state_airborne_pose_survives_the_orientation_conversion) {
	// A car in the air with pitch and roll, and the jump flags set: the angles go through Angle::FromRotMat and back
	Scenario s; s.name = "air"; s.steps = 10;
	CarSpec a = MkCar(Team::BLUE, Vec(300, -500, 900), 40, Vec(200, 300, 100)); a.pitchDeg = 50; a.rollDeg = -120; a.hasJumped = true; a.airTimeSinceJump = 0.4f; a.angVel = Vec(0.5f, -0.3f, 1.1f);
	s.cars = { a, MkCar(Team::ORANGE, Vec(2000, 2000, 17), 270) }; s.ball.pos = Vec(0, 0, 700);
	s.rewards = { { "OffsidePenalty", 1.0f, -1, 0.9969f } };
	LabSession orig(s); CHECK(orig.ok());
	for (int i = 0; i < 4; i++) orig.StepOnce({ Act(0, 0, 0, 0, 0.7f), Act(0, 0) });
	CaptureResult r; std::string err; CHECK(CaptureState(orig, 4, "air_snap", 0, r, err));
	Scenario parsed; CHECK(Rebuild(r.scenario, parsed));
	LabSession snap(parsed); CHECK(snap.ok());
	const PlayerSnap& want = orig.Frames()[4].players[0]; const PlayerSnap& got = snap.Frames()[0].players[0];
	CHECK(!want.onGround);
	CHECK(Dist(want.forward, got.forward) < 1e-4); CHECK(Dist(want.up, got.up) < 1e-4); CHECK(Dist(want.pos, got.pos) < 1e-3);
	CHECK(r.warnings.empty());                                                // airborne well above the floor: nothing lost that we know of
	// the air time ADVANCED with the run (0.4 s at the start, 4 steps of 8 ticks later about 0.67 s) and was carried over, not left at the original 0.4
	CHECK(r.scenario.cars[0].airTimeSinceJump > 0.6f); CHECK(r.scenario.cars[0].airTimeSinceJump < 0.75f);
}

TEST(Capture_state_divergence_is_measured_and_bounded) {
	// Continue the original and the snapshot with the SAME actions and see how far they drift: the honest size of "approximate".
	// Three regimes (bounds sit above what was measured; the measured numbers are printed and recorded in docs/REWARD_VIEWER_SPEC.md):
	//   on the ground, before any contact   ~0.05 uu after one step (the suspension settles), ~0.1-0.3 uu over 10 steps
	//   in the air                          0 (nothing hidden to lose)
	//   just before a jump / a ball hit     can be large: the restart changes the contact history those events depend on
	Scenario base = Drive(40);
	LabSession orig(base); RunVaried(orig, 24);
	auto drift = [&](int cap, double& car, double& ball, double& afterOne, int only = -1) {
		CaptureResult r; std::string err; CHECK(CaptureState(orig, cap, "drift", 0, r, err));
		Scenario parsed; CHECK(Rebuild(r.scenario, parsed));
		LabSession snap(parsed); CHECK(snap.ok());
		car = 0; ball = 0; afterOne = 0;
		for (int k = cap; k < cap + 10 && k < 24; k++) {
			snap.StepOnce(Varied(k));
			const Frame& a = orig.Frames()[k + 1]; const Frame& b = snap.Frames().back();
			for (size_t i = 0; i < a.players.size(); i++) if (only < 0 || (int)i == only) car = std::max(car, Dist(a.players[i].pos, b.players[i].pos));
			ball = std::max(ball, Dist(a.ball.pos, b.ball.pos));
			if (k == cap) for (size_t i = 0; i < a.players.size(); i++) if (only < 0 || (int)i == only) afterOne = std::max(afterOne, Dist(a.players[i].pos, b.players[i].pos));
		}
	};
	double car = 0, ball = 0, one = 0;
	drift(3, car, ball, one);
	std::cout << "    capture at frame 3 (ground, before contact): worst car " << car << " uu, ball " << ball << " uu over 10 steps; " << one << " uu after one step\n";
	CHECK(one < 0.2); CHECK(car < 0.5); CHECK(ball < 0.5);
	drift(11, car, ball, one, /*only car 0, the one in the air*/ 0);
	std::cout << "    capture at frame 11 (car 0 in the air): worst car " << car << " uu, ball " << ball << " uu\n";
	CHECK(car < 0.05); CHECK(ball < 0.05);
	drift(8, car, ball, one);
	std::cout << "    capture at frame 8 (just before a jump, after a ball hit): worst car " << car << " uu, ball " << ball << " uu\n";
	CHECK(car < 30.0); CHECK(ball < 30.0);
	CHECK(car > 1.0);                                   // and it is NOT small: this regime is why the capture is labelled approximate
}

TEST(Capture_state_warns_about_what_it_loses) {
	GameState gs; Scenario sc = Drive();
	Player wall; wall.carId = 0; wall.team = Team::BLUE; wall.isOnGround = true; wall.pos = Vec(4000, 0, 500);
	Player floorAir; floorAir.carId = 1; floorAir.team = Team::BLUE; floorAir.isOnGround = false; floorAir.pos = Vec(0, 0, 10);
	Player flipping; flipping.carId = 2; flipping.team = Team::ORANGE; flipping.isFlipping = true; flipping.isOnGround = false; flipping.pos = Vec(0, 0, 400);
	Player jumping; jumping.carId = 3; jumping.team = Team::ORANGE; jumping.isJumping = true; jumping.isOnGround = false; jumping.pos = Vec(0, 0, 400);
	Player demo; demo.carId = 4; demo.team = Team::ORANGE; demo.isDemoed = true; demo.isOnGround = true; demo.pos = Vec(0, 0, 17);
	gs.players = { wall, floorAir, flipping, jumping, demo };
	gs.boostPads[3] = false; gs.boostPads[9] = false;
	sc.actionDelay = 2;
	auto w = CaptureWarnings(gs, sc);
	CHECK(HasWarn(w, "car 0 is on a wall or the ceiling")); CHECK(HasWarn(w, "AIRBORNE"));
	CHECK(HasWarn(w, "car 1 is airborne below z 20")); CHECK(HasWarn(w, "ON THE GROUND"));
	CHECK(HasWarn(w, "car 2 is mid-flip")); CHECK(HasWarn(w, "car 3 is mid-jump"));
	CHECK(HasWarn(w, "car 4 is demolished"));
	CHECK(HasWarn(w, "2 boost pad(s)")); CHECK(HasWarn(w, "action_delay is 2"));
	CHECK_EQ(w.size(), (size_t)7);
	// a plain car on the floor and nothing else: no warnings
	GameState calm; Player ok; ok.carId = 0; ok.team = Team::BLUE; ok.isOnGround = true; ok.pos = Vec(0, 0, 17); calm.players = { ok };
	Scenario sc2 = Drive(); sc2.actionDelay = 0;
	CHECK(CaptureWarnings(calm, sc2).empty());
	// the warnings are also written into a state capture's description
	Scenario base = Drive(); LabSession orig(base); RunVaried(orig, 3);
	CaptureResult r; std::string err; CHECK(CaptureState(orig, 3, "w_test", 0, r, err));
	CHECK(r.scenario.description.find("APPROXIMATE") != std::string::npos);
	// a capture that DOES lose something says so in the file: action_delay > 0 loses the actions in flight
	Scenario delayed = Drive(); delayed.actionDelay = 2;
	LabSession od(delayed); RunVaried(od, 3);
	CaptureResult rd; CHECK(CaptureState(od, 3, "w_delay", 0, rd, err));
	CHECK_EQ(rd.warnings.size(), (size_t)1); CHECK(rd.warnings[0].find("action_delay is 2") != std::string::npos);
	CHECK(rd.scenario.description.find("NOTE: " + rd.warnings[0]) != std::string::npos);
}

// ---- through the host -----------------------------------------------------------------------------------------------------

TEST(Capture_host_command_defaults_and_errors) {
	LabHost h; Scenario base = Drive();
	json bad = h.Handle({ { "cmd", "capture" }, { "mode", "replay" } });
	CHECK(!bad["ok"].get<bool>()); CHECK(bad["error"].get<std::string>().find("nothing loaded") != std::string::npos);
	h.Handle({ { "cmd", "load" }, { "scenario", ToJson(base) } });
	json acts = json::array(); for (auto& a : Varied(0)) acts.push_back(json(std::vector<float>(a.begin(), a.end())));
	h.Handle({ { "cmd", "step" }, { "actions", acts }, { "repeat", 6 } });
	const json before = h.Handle({ { "cmd", "state" } })["run"];
	// default frame is where the session is; default name is derived
	json r = h.Handle({ { "cmd", "capture" }, { "mode", "replay" } });
	CHECK(r["ok"].get<bool>()); CHECK_EQ(r["name"].get<std::string>(), std::string("captured_capture_drive_f6_replay"));
	CHECK_EQ(r["scenario"]["steps"].get<int>(), 6); CHECK_EQ(r["mode"].get<std::string>(), std::string("replay")); CHECK(r["warnings"].empty());
	CHECK(r["scenario"]["expect"].empty());
	Scenario parsed; std::vector<std::string> errs; CHECK(ParseScenarioText(r["scenario"].dump(), parsed, errs));
	json s = h.Handle({ { "cmd", "capture" }, { "mode", "state" }, { "frame", 3 }, { "name", "my_snap" }, { "steps", 9 } });
	CHECK(s["ok"].get<bool>()); CHECK_EQ(s["name"].get<std::string>(), std::string("my_snap")); CHECK_EQ(s["scenario"]["steps"].get<int>(), 9);
	CHECK(s["scenario"]["actions"].empty());
	CHECK(h.Handle({ { "cmd", "state" } })["run"] == before);                  // capturing changes nothing
	auto err = [&](json c) { json x = h.Handle(c); return !x["ok"].get<bool>() ? x["error"].get<std::string>() : std::string(); };
	CHECK(err({ { "cmd", "capture" } }).find("mode") != std::string::npos);
	CHECK(err({ { "cmd", "capture" }, { "mode", "bogus" } }).find("mode") != std::string::npos);
	CHECK(err({ { "cmd", "capture" }, { "mode", "replay" }, { "frame", 99 } }).find("outside") != std::string::npos);
	CHECK(err({ { "cmd", "capture" }, { "mode", "replay" }, { "frame", 0 } }).find("frame 0") != std::string::npos);
	CHECK(err({ { "cmd", "capture" }, { "mode", "replay" }, { "frame", "x" } }).find("integer") != std::string::npos);
	CHECK(err({ { "cmd", "capture" }, { "mode", "state" }, { "name", "Bad Name" } }).find("name must be") != std::string::npos);
	CHECK(err({ { "cmd", "capture" }, { "mode", "state" }, { "name", 5 } }).find("string") != std::string::npos);
	CHECK(err({ { "cmd", "capture" }, { "mode", "state" }, { "steps", -1 } }).find("steps") != std::string::npos);
	CHECK(err({ { "cmd", "capture" }, { "mode", "state" }, { "steps", "x" } }).find("integer") != std::string::npos);
	CHECK(h.Handle({ { "cmd", "state" } })["run"] == before);
}

TEST(Capture_a_captured_scenario_loads_into_a_host_and_replays_the_same_frames) {
	LabHost h; Scenario base = Drive();
	h.Handle({ { "cmd", "load" }, { "scenario", ToJson(base) } });
	json acts = json::array(); for (auto& a : Varied(0)) acts.push_back(json(std::vector<float>(a.begin(), a.end())));
	h.Handle({ { "cmd", "step" }, { "actions", acts }, { "repeat", 5 } });
	const json run5 = h.Handle({ { "cmd", "state" } })["run"];
	json cap = h.Handle({ { "cmd", "capture" }, { "mode", "replay" } });
	LabHost h2; json loaded = h2.Handle({ { "cmd", "load" }, { "scenario", cap["scenario"] } });
	CHECK(loaded["ok"].get<bool>());
	h2.Handle({ { "cmd", "step_scripted" }, { "n", 5 } });
	json run5b = h2.Handle({ { "cmd", "state" } })["run"];
	CHECK(run5["frames"] == run5b["frames"]);                                 // the saved file replays the manual session exactly
}
