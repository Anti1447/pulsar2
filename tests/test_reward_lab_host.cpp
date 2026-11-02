#include "TestFramework.h"

#include <RLGymCPP/Lab/LabHost.h>

using namespace RLGC;
using namespace RLGC::Lab;
using nlohmann::json;

// P3-2 of the Reward Lab (docs/REWARD_VIEWER_SPEC.md): the live host. Needs build/collision_meshes (run from the repo root).

namespace {
	Action Act(float throttle, float boost, float jump = 0, float steer = 0) {
		Action a = Action(CarControls{});
		a.throttle = throttle; a.boost = boost; a.jump = jump; a.steer = steer;
		return a;
	}
	CarSpec MkCar(Team t, Vec pos, float yaw = 90, Vec vel = Vec(0, 0, 0), float boost = 100) {
		CarSpec c; c.team = t; c.pos = pos; c.yawDeg = yaw; c.vel = vel; c.boost = boost; return c;
	}
	// Blue car 0 drives at a resting ball; blue 1 and two orange cars idle (the pack's passing scenario, shortened).
	Scenario Drive(int steps = 16) {
		Scenario s; s.name = "host_drive"; s.steps = steps;
		s.cars = { MkCar(Team::BLUE, Vec(0, -2200, 17), 90, Vec(0, 1400, 0)), MkCar(Team::BLUE, Vec(2500, 1500, 17)),
		           MkCar(Team::ORANGE, Vec(-3000, 3500, 17), 270), MkCar(Team::ORANGE, Vec(3000, 3500, 17), 270) };
		s.ball.pos = Vec(0, -1000, 93);
		ActionEntry a; a.car = 0; a.from = 0; a.action = Act(1, 1); s.actions.push_back(a);
		s.rewards = { { "PassingSetupReward", 1.0f, -1, 0.9969f }, { "OffsidePenalty", 0.15f, -1, 0.9969f } };
		Expectation e; e.kind = "event"; e.intent = "one clean hit is one payment"; e.reward = "PassingSetupReward"; e.event = "paid"; e.car = 0; e.countMin = 1; e.countMax = 1;
		s.expect = { e };
		return s;
	}
	json ActsJ(std::initializer_list<Action> list) {
		json a = json::array();
		for (auto& x : list) a.push_back(json(std::vector<float>(x.begin(), x.end())));
		return a;
	}
	json LoadCmd(const Scenario& s, int steps = 0) { json c = { { "cmd", "load" }, { "scenario", ToJson(s) } }; if (steps) c["steps"] = steps; return c; }
	json DriveActs() { return ActsJ({ Act(1, 1), Act(0, 0), Act(0, 0), Act(0, 0) }); }
	bool HasErr(const json& r, const std::string& needle) {
		return r.contains("ok") && !r["ok"].get<bool>() && r["error"].get<std::string>().find(needle) != std::string::npos;
	}
	// A client's view of the run: keep the first `keep` frames, append the rest (what the page does).
	void Apply(json& run, const json& delta) {
		json frames = json::array();
		const size_t keep = delta["keep"].get<size_t>();
		for (size_t i = 0; i < keep && i < run["frames"].size(); i++) frames.push_back(run["frames"][i]);
		for (auto& f : delta["frames"]) frames.push_back(f);
		run["frames"] = frames; run["expectations"] = delta["expectations"]; run["result"] = delta["result"];
	}
	// The same actions, run offline with the batch API.
	json Offline(const Scenario& s, const std::vector<std::vector<Action>>& log) {
		LabSession ses(s);
		for (auto& a : log) ses.StepOnce(a);
		return RunToJson(ses);
	}
	std::vector<Action> DriveVec() { return { Act(1, 1), Act(0, 0), Act(0, 0), Act(0, 0) }; }
}

TEST(Host_load_returns_exactly_the_batch_run) {
	LabHost h; Scenario s = Drive();
	json r = h.Handle(LoadCmd(s));
	CHECK(r["ok"].get<bool>());
	CHECK(r["run"] == Offline(s, {}));
	CHECK(h.Handle({ { "cmd", "state" } })["run"] == r["run"]);
}

TEST(Host_live_steps_equal_a_batch_run_of_the_same_actions_bit_for_bit) {
	LabHost h; Scenario s = Drive();
	json run = h.Handle(LoadCmd(s))["run"];
	for (int i = 0; i < 6; i++) {
		json d = h.Handle({ { "cmd", "step" }, { "actions", DriveActs() } });
		CHECK(d["ok"].get<bool>());
		CHECK_EQ(d["keep"].get<int>(), i + 1);
		CHECK_EQ(d["frames"].size(), (size_t)1);
		Apply(run, d);
	}
	std::vector<std::vector<Action>> log(6, DriveVec());
	CHECK(run == Offline(s, log));            // every frame: state, actions, whole reward trace, expectations, result
}

TEST(Host_repeat_and_scripted_steps) {
	LabHost h; Scenario s = Drive();
	json run = h.Handle(LoadCmd(s))["run"];
	json d = h.Handle({ { "cmd", "step" }, { "actions", DriveActs() }, { "repeat", 4 } });
	CHECK_EQ(d["frames"].size(), (size_t)4); CHECK_EQ(d["keep"].get<int>(), 1);
	Apply(run, d);
	CHECK(run == Offline(s, std::vector<std::vector<Action>>(4, DriveVec())));
	// scripted: the scenario's own script (car 0 drives, others idle) for the remaining steps equals a pure scripted run
	LabHost h2; h2.Handle(LoadCmd(s));
	json d2 = h2.Handle({ { "cmd", "step_scripted" }, { "n", 16 } });
	CHECK_EQ(d2["frames"].size(), (size_t)16);
	CHECK(d2["finished"].get<bool>());
	LabSession ref(s); ref.RunScripted();
	json refRun = RunToJson(ref);
	json run2 = h2.Handle({ { "cmd", "state" } })["run"];
	CHECK(run2 == refRun);
}

TEST(Host_seek_branches_and_replays_exactly) {
	LabHost h; Scenario s = Drive();
	json run = h.Handle(LoadCmd(s))["run"];
	Apply(run, h.Handle({ { "cmd", "step" }, { "actions", DriveActs() }, { "repeat", 8 } }));
	const json original = run;
	// go back to frame 3: the 5 later frames are dropped
	json d = h.Handle({ { "cmd", "seek" }, { "frame", 3 } });
	CHECK(d["ok"].get<bool>()); CHECK_EQ(d["keep"].get<int>(), 4); CHECK_EQ(d["frames"].size(), (size_t)0);
	Apply(run, d);
	CHECK_EQ(run["frames"].size(), (size_t)4);
	CHECK(run["frames"][3] == original["frames"][3]);                    // replay lands on the same frame, no tolerance
	// a different input forks the future
	json fork = h.Handle({ { "cmd", "step" }, { "actions", ActsJ({ Act(1, 1, 0, 1), Act(0, 0), Act(0, 0), Act(0, 0) }) } });
	CHECK_EQ(fork["keep"].get<int>(), 4);
	CHECK(fork["frames"][0]["players"][0]["pos"] != original["frames"][4]["players"][0]["pos"]);
	// going back and repeating the ORIGINAL input reproduces the original future exactly (reward state was reset, not leaked)
	h.Handle({ { "cmd", "seek" }, { "frame", 3 } });
	Apply(run, json{ { "keep", 4 }, { "frames", json::array() }, { "expectations", json::array() }, { "result", "NONE" } });
	json again = h.Handle({ { "cmd", "step" }, { "actions", DriveActs() }, { "repeat", 5 } });
	for (size_t i = 0; i < 5; i++) CHECK(again["frames"][i] == original["frames"][4 + i]);
	// reset = seek 0
	json r0 = h.Handle({ { "cmd", "reset" } });
	CHECK_EQ(r0["keep"].get<int>(), 1); CHECK_EQ(r0["frames"].size(), (size_t)0);
	CHECK(h.Handle({ { "cmd", "state" } })["run"] == Offline(s, {}));
}

TEST(Host_recorded_macro_replays_the_manual_session_bit_for_bit) {
	LabHost h; Scenario s = Drive();
	h.Handle(LoadCmd(s));
	CHECK(HasErr(h.Handle({ { "cmd", "macro_record" } }), "at least one step"));
	h.Handle({ { "cmd", "step" }, { "actions", DriveActs() }, { "repeat", 3 } });
	h.Handle({ { "cmd", "step" }, { "actions", ActsJ({ Act(1, 0, 1), Act(0, 0), Act(0, 0), Act(0, 0) }) }, { "repeat", 2 } });
	h.Handle({ { "cmd", "step" }, { "actions", ActsJ({ Act(1, 1, 0, -0.37f), Act(0, 0), Act(0, 0), Act(0, 0) }) } });
	json manual = h.Handle({ { "cmd", "state" } })["run"];
	json rec = h.Handle({ { "cmd", "macro_record" } });
	CHECK(rec["ok"].get<bool>());
	CHECK_EQ(rec["macro"]["ticks"].get<int>(), 6 * 8);
	// through a file-sized round trip, into a fresh host
	LabHost h2; h2.Handle(LoadCmd(s));
	json played = h2.Handle({ { "cmd", "macro_play" }, { "macro", json::parse(rec["macro"].dump()) } });
	CHECK(played["ok"].get<bool>());
	json b = played["run"];
	CHECK(manual["frames"] == b["frames"]);               // state, actions and the whole reward trace of every frame
	CHECK(manual["expectations"] == b["expectations"]); CHECK(manual["result"] == b["result"]);
	// the played scenario is the same situation with no script, as many steps as were played
	CHECK_EQ(b["scenario"]["steps"].get<int>(), 6); CHECK(b["scenario"]["actions"].empty());
	CHECK(manual["scenario"]["cars"] == b["scenario"]["cars"] && manual["scenario"]["ball"] == b["scenario"]["ball"]);
	// another skip/phase is accepted and plays a shorter or equal run; a phase outside [0, skip) is refused
	json p4 = h2.Handle({ { "cmd", "macro_play" }, { "macro", rec["macro"] }, { "skip", 4 }, { "phase", 2 } });
	CHECK(p4["ok"].get<bool>());
	CHECK_EQ(p4["run"]["scenario"]["tick_skip"].get<int>(), 4);
	CHECK_EQ(p4["run"]["frames"].size(), (size_t)(48 / 4 + 1));
	CHECK(HasErr(h2.Handle({ { "cmd", "macro_play" }, { "macro", rec["macro"] }, { "skip", 4 }, { "phase", 4 } }), "phase"));
}

TEST(Host_refuses_bad_input_and_keeps_the_session) {
	LabHost h;
	CHECK(HasErr(h.Handle({ { "cmd", "step" }, { "actions", DriveActs() } }), "nothing loaded"));
	CHECK(HasErr(h.Handle({ { "cmd", "state" } }), "nothing loaded"));
	CHECK(HasErr(h.Handle({ { "cmd", "bogus" } }), "unknown command"));
	CHECK(HasErr(h.Handle(json::array()), "expected an object"));
	CHECK(HasErr(h.Handle({ { "nocmd", 1 } }), "\"cmd\""));
	CHECK(HasErr(h.Handle({ { "cmd", "load" } }), "scenario"));
	CHECK(HasErr(h.Handle({ { "cmd", "load" }, { "scenario", { { "schema", 1 }, { "bogus", 1 } } } }), "invalid scenario"));
	Scenario s = Drive();
	CHECK(HasErr(h.Handle(LoadCmd(s, 0 + 200000)), "steps"));
	h.Handle(LoadCmd(s));
	const json before = h.Handle({ { "cmd", "state" } })["run"];
	CHECK(HasErr(h.Handle({ { "cmd", "step" } }), "actions"));
	CHECK(HasErr(h.Handle({ { "cmd", "step" }, { "actions", ActsJ({ Act(1, 1) }) } }), "one entry per car"));
	json bad = DriveActs(); bad[0][0] = 1.5;  CHECK(HasErr(h.Handle({ { "cmd", "step" }, { "actions", bad } }), "[-1, 1]"));
	bad = DriveActs(); bad[0][6] = 0.5;       CHECK(HasErr(h.Handle({ { "cmd", "step" }, { "actions", bad } }), "0 or 1"));
	bad = DriveActs(); bad[2][3] = "x";       CHECK(HasErr(h.Handle({ { "cmd", "step" }, { "actions", bad } }), "expected a number"));
	bad = DriveActs(); bad[1] = json::array({ 1, 2 }); CHECK(HasErr(h.Handle({ { "cmd", "step" }, { "actions", bad } }), "8 numbers"));
	CHECK(HasErr(h.Handle({ { "cmd", "step" }, { "actions", DriveActs() }, { "repeat", 0 } }), "repeat"));
	CHECK(HasErr(h.Handle({ { "cmd", "step" }, { "actions", DriveActs() }, { "repeat", 601 } }), "repeat"));
	CHECK(HasErr(h.Handle({ { "cmd", "seek" }, { "frame", 1 } }), "outside"));
	CHECK(HasErr(h.Handle({ { "cmd", "seek" }, { "frame", -1 } }), "outside"));
	CHECK(HasErr(h.Handle({ { "cmd", "seek" } }), "frame"));
	CHECK(HasErr(h.Handle({ { "cmd", "macro_play" }, { "macro", { { "schema", 1 } } } }), "invalid macro"));
	CHECK(HasErr(h.Handle({ { "cmd", "page" } }), "template"));
	// a macro for a different number of cars is refused, not silently played as nothing
	Macro oneCar; oneCar.cars = 1; oneCar.perTick.assign(16, std::vector<Action>(1, IdleAction()));
	CHECK(HasErr(h.Handle({ { "cmd", "macro_play" }, { "macro", MacroToJson(oneCar) } }), "car(s)"));
	CHECK(h.Handle({ { "cmd", "state" } })["run"] == before);          // nothing above changed the session
	// a line that is not JSON is an error reply
	json r = json::parse(h.HandleLine("{oops"));
	CHECK(!r["ok"].get<bool>());
}

TEST(Host_stops_at_the_end_of_the_run_and_a_sandbox_can_be_longer) {
	LabHost h; Scenario s = Drive(4);
	h.Handle(LoadCmd(s));
	json d = h.Handle({ { "cmd", "step" }, { "actions", DriveActs() }, { "repeat", 10 } });
	CHECK_EQ(d["frames"].size(), (size_t)4);                           // stopped at max steps, not an error
	CHECK(d["finished"].get<bool>()); CHECK_EQ(d["max_steps"].get<int>(), 4);
	CHECK(HasErr(h.Handle({ { "cmd", "step" }, { "actions", DriveActs() } }), "finished"));
	CHECK(h.Handle({ { "cmd", "seek" }, { "frame", 2 } })["ok"].get<bool>());   // a branch is allowed from the end
	json sandbox = h.Handle(LoadCmd(s, 50));
	CHECK(sandbox["ok"].get<bool>());
	json d2 = h.Handle({ { "cmd", "step" }, { "actions", DriveActs() }, { "repeat", 30 } });
	CHECK_EQ(d2["frames"].size(), (size_t)30); CHECK(!d2["finished"].get<bool>()); CHECK_EQ(d2["max_steps"].get<int>(), 50);
}

TEST(Host_expectations_follow_the_manual_run) {
	LabHost h; Scenario s = Drive(20);
	h.Handle(LoadCmd(s));
	json idle = h.Handle({ { "cmd", "step" }, { "actions", ActsJ({ Act(0, 0), Act(0, 0), Act(0, 0), Act(0, 0) }) }, { "repeat", 10 } });
	CHECK(idle["result"].get<std::string>() == "FAIL");                // nobody touched the ball: "one payment" does not hold
	h.Handle({ { "cmd", "seek" }, { "frame", 0 } });
	json hit = h.Handle({ { "cmd", "step" }, { "actions", DriveActs() }, { "repeat", 16 } });
	CHECK(hit["result"].get<std::string>() == "PASS");                 // driving into the ball does
	CHECK(hit["expectations"][0]["ok"].get<bool>());
}

TEST(Host_page_embeds_the_current_run_in_live_mode) {
	LabHost h; Scenario s = Drive();
	h.Handle(LoadCmd(s));
	h.Handle({ { "cmd", "step" }, { "actions", DriveActs() }, { "repeat", 2 } });
	const std::string tmpl = "tools/reward_lab/viewer/viewer.template.html";
	json p = h.Handle({ { "cmd", "page" }, { "template", tmpl } });
	CHECK(p["ok"].get<bool>());
	const std::string html = p["html"].get<std::string>();
	CHECK(html.find("window.LAB_LIVE = true;") != std::string::npos);
	CHECK(html.find("<!--INLINE") == std::string::npos); CHECK(html.find("<!--LAB_") == std::string::npos);
	CHECK(html.find("host_drive") != std::string::npos);
	// the static export of the same session carries no live script and no leftover marker
	std::string stat, err;
	CHECK(ExportHtml(*h.Session(), tmpl, stat, err));
	CHECK(stat.find("window.LAB_LIVE = true") == std::string::npos);
	CHECK(stat.find("<!--LAB_") == std::string::npos);
	CHECK(HasErr(h.Handle({ { "cmd", "page" }, { "template", "no/such/template.html" } }), "cannot read"));
}
