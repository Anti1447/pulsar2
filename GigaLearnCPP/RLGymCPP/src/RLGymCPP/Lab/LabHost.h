#pragma once
// ============================================================================
// LabHost.h - the live Reward Lab as a command interpreter (one LabSession, JSON in, JSON out).
// 2026-11-02 | Rev 1.0 | docs/REWARD_VIEWER_SPEC.md (P3-2)
//
// This is the whole "live host": no sockets, no threads. tools/reward_lab/reward_lab_host.cpp feeds it one JSON line per command
// on stdin; tools/reward_lab/serve.py puts HTTP in front of that for the viewer page. Everything the page shows still comes from
// the same LabSession/EnvSet/TracedRewardStack that the headless runner uses, so a live run and a batch run of the same actions
// are bit-identical (tests/test_reward_lab_host.cpp compares them).
//
// Commands (JSON objects with "cmd"); every reply has "ok". On failure: {"ok":false,"error":"..."} and the session is unchanged.
//   load        {scenario:{...}, steps?:1..100000}      new session from an inline scenario (steps overrides the length: a sandbox)
//               -> {run}                                 the full run (RunToJson)
//   state       {}                                      -> {run}
//   step        {actions:[[8 floats] per car], repeat?:1..600}   append decision steps with these actions (held)  -> delta
//   step_scripted {n?:1..600}                           append steps using the scenario's own script           -> delta
//   seek        {frame:k}                               go to frame k by re-simulating its action log; later frames are DROPPED (a branch) -> delta
//   reset       {}                                      seek 0
//   macro_record {}                                     -> {macro}   the action log as a tick-resolution macro (needs >= 1 step)
//   macro_play  {macro:{...}, skip?:n, phase?:p}        replace the session by the macro projected onto the grid -> {run}
//   page        {template:path}                         -> {html}    the viewer page with the current run embedded and live mode on
//
// delta = {ok, keep, frames:[FrameToJson...], expectations, result, finished, max_steps}
//   The client keeps the first `keep` frames of its run and appends `frames`.
// ============================================================================
#include "LabExport.h"
#include "Macro.h"
#include <memory>

namespace RLGC {
namespace Lab {

	class LabHost {
	public:
		nlohmann::json Handle(const nlohmann::json& cmd) {
			using nlohmann::json;
			try {
				if (!cmd.is_object() || !cmd.contains("cmd") || !cmd["cmd"].is_string()) return Fail("expected an object with a string \"cmd\"");
				const std::string c = cmd["cmd"].get<std::string>();
				if (c == "load") return Load(cmd);
				if (c == "state") return session ? RunReply() : Fail("nothing loaded");
				if (c == "step") return Step(cmd);
				if (c == "step_scripted") return StepScripted(cmd);
				if (c == "seek") return Seek(cmd);
				if (c == "reset") return session ? Move(0) : Fail("nothing loaded");
				if (c == "macro_record") return MacroRecord();
				if (c == "macro_play") return MacroPlay(cmd);
				if (c == "page") return Page(cmd);
				return Fail("unknown command \"" + c + "\"");
			} catch (const std::exception& e) {
				return Fail(std::string("internal error: ") + e.what());
			}
		}

		// One line in, one line out; a line that is not JSON is an error reply, never a crash.
		std::string HandleLine(const std::string& line) {
			nlohmann::json cmd;
			try { cmd = nlohmann::json::parse(line); }
			catch (const std::exception& e) { return Fail(std::string("not valid JSON: ") + e.what()).dump(); }
			return Handle(cmd).dump();
		}

		bool Loaded() const { return session != nullptr; }
		const LabSession* Session() const { return session.get(); }

	private:
		std::unique_ptr<LabSession> session;
		int maxSteps = 0;

		static nlohmann::json Fail(const std::string& msg) { return { { "ok", false }, { "error", msg } }; }

		nlohmann::json RunReply() const { return { { "ok", true }, { "run", RunToJson(*session) } }; }

		// `keep` frames stay with the client; `frames` are everything after them.
		nlohmann::json Delta(size_t keep) const {
			using nlohmann::json;
			json frames = json::array();
			const auto& all = session->Frames();
			for (size_t i = keep; i < all.size(); i++) frames.push_back(FrameToJson(all[i]));
			return { { "ok", true }, { "keep", keep }, { "frames", frames }, { "expectations", ExpectationsToJson(*session) },
				{ "result", ResultOf(*session) }, { "finished", session->Finished() }, { "max_steps", maxSteps } };
		}

		nlohmann::json Load(const nlohmann::json& cmd) {
			if (!cmd.contains("scenario")) return Fail("load needs \"scenario\"");
			Scenario sc; std::vector<std::string> errs;
			if (!ParseScenario(cmd["scenario"], sc, errs)) { std::string m = "invalid scenario:"; for (auto& e : errs) m += " " + e + ";"; return Fail(m); }
			if (cmd.contains("steps")) {
				if (!cmd["steps"].is_number_integer() || cmd["steps"].get<long long>() < 1 || cmd["steps"].get<long long>() > 100000) return Fail("steps: expected an integer 1..100000");
				sc.steps = cmd["steps"].get<int>();
			}
			return Replace(sc);
		}

		nlohmann::json Replace(const Scenario& sc) {
			auto fresh = std::make_unique<LabSession>(sc);
			if (!fresh->ok()) { std::string m = "cannot run:"; for (auto& e : fresh->errors()) m += " " + e + ";"; return Fail(m); }
			session = std::move(fresh);
			maxSteps = sc.steps;
			return RunReply();
		}

		// An action list from the page: one [8 numbers] per car, axes in [-1,1], buttons exactly 0 or 1.
		bool ParseActions(const nlohmann::json& j, std::vector<Action>& out, std::string& err) const {
			const size_t nCars = session->GetScenario().cars.size();
			if (!j.is_array() || j.size() != nCars) { err = "actions: expected an array with one entry per car (" + std::to_string(nCars) + ")"; return false; }
			out.clear();
			for (size_t c = 0; c < nCars; c++) {
				const auto& a = j[c];
				if (!a.is_array() || a.size() != 8) { err = "actions[" + std::to_string(c) + "]: expected 8 numbers (throttle steer pitch yaw roll jump boost handbrake)"; return false; }
				Action act = Action(CarControls{});
				for (int k = 0; k < 8; k++) {
					if (!a[k].is_number()) { err = "actions[" + std::to_string(c) + "][" + std::to_string(k) + "]: expected a number"; return false; }
					const double v = a[k].get<double>();
					const bool button = k >= 5;
					if (!std::isfinite(v) || (button ? (v != 0.0 && v != 1.0) : (v < -1.0 || v > 1.0))) {
						err = "actions[" + std::to_string(c) + "][" + std::to_string(k) + "]: " + (button ? "buttons are 0 or 1" : "axes are in [-1, 1]"); return false;
					}
					act[k] = (float)v;
				}
				out.push_back(act);
			}
			return true;
		}

		static bool Repeat(const nlohmann::json& cmd, int& n, std::string& err, const char* key) {
			n = 1;
			if (!cmd.contains(key)) return true;
			if (!cmd[key].is_number_integer() || cmd[key].get<long long>() < 1 || cmd[key].get<long long>() > 600) { err = std::string(key) + ": expected an integer 1..600"; return false; }
			n = cmd[key].get<int>(); return true;
		}

		nlohmann::json Step(const nlohmann::json& cmd) {
			if (!session) return Fail("nothing loaded");
			if (!cmd.contains("actions")) return Fail("step needs \"actions\"");
			std::vector<Action> acts; std::string err; int n;
			if (!ParseActions(cmd["actions"], acts, err) || !Repeat(cmd, n, err, "repeat")) return Fail(err);
			if (session->Finished()) return Fail("the run is finished (episode ended or max steps reached); seek or reset to branch");
			const size_t keep = session->Frames().size();
			for (int i = 0; i < n && session->StepOnce(acts); i++) {}
			return Delta(keep);
		}

		nlohmann::json StepScripted(const nlohmann::json& cmd) {
			if (!session) return Fail("nothing loaded");
			std::string err; int n;
			if (!Repeat(cmd, n, err, "n")) return Fail(err);
			if (session->Finished()) return Fail("the run is finished (episode ended or max steps reached); seek or reset to branch");
			const size_t keep = session->Frames().size();
			for (int i = 0; i < n && session->StepScripted(); i++) {}
			return Delta(keep);
		}

		nlohmann::json Move(int frame) {
			if (!session->ReplayTo(frame)) return Fail("cannot replay to frame " + std::to_string(frame));
			return Delta((size_t)frame + 1);
		}

		nlohmann::json Seek(const nlohmann::json& cmd) {
			if (!session) return Fail("nothing loaded");
			if (!cmd.contains("frame") || !cmd["frame"].is_number_integer()) return Fail("seek needs an integer \"frame\"");
			const long long k = cmd["frame"].get<long long>();
			if (k < 0 || k > session->Steps()) return Fail("frame " + std::to_string(k) + " is outside 0.." + std::to_string(session->Steps()));
			return Move((int)k);
		}

		nlohmann::json MacroRecord() const {
			if (!session) return Fail("nothing loaded");
			if (session->Steps() < 1) return Fail("nothing recorded yet: take at least one step");
			Macro m = MacroFromLog(session->ActionLog(), session->GetScenario().tickSkip, session->GetScenario().name);
			return { { "ok", true }, { "macro", MacroToJson(m) } };
		}

		nlohmann::json MacroPlay(const nlohmann::json& cmd) {
			if (!session) return Fail("nothing loaded");
			if (!cmd.contains("macro")) return Fail("macro_play needs \"macro\"");
			Macro m; std::vector<std::string> errs;
			if (!ParseMacro(cmd["macro"], m, errs)) { std::string e = "invalid macro:"; for (auto& x : errs) e += " " + x + ";"; return Fail(e); }
			const Scenario& cur = session->GetScenario();
			int skip = cur.tickSkip, phase = 0;
			if (cmd.contains("skip")) { if (!cmd["skip"].is_number_integer() || cmd["skip"].get<long long>() < 1 || cmd["skip"].get<long long>() > 120) return Fail("skip: expected an integer 1..120"); skip = cmd["skip"].get<int>(); }
			if (cmd.contains("phase")) { if (!cmd["phase"].is_number_integer() || cmd["phase"].get<long long>() < 0 || cmd["phase"].get<long long>() >= skip) return Fail("phase: expected an integer in [0, skip)"); phase = cmd["phase"].get<int>(); }
			if ((int)cur.cars.size() != m.cars) return Fail("macro has " + std::to_string(m.cars) + " car(s), the scenario has " + std::to_string(cur.cars.size()));
			auto steps = ProjectToGrid(m, skip, phase);
			if (steps.empty()) return Fail("the projection is empty");
			Scenario g = WithGrid(cur, skip, (int)steps.size());
			nlohmann::json r = Replace(g);
			if (!r["ok"].get<bool>()) return r;
			for (auto& a : steps) if (!session->StepOnce(a)) break;
			return RunReply();
		}

		nlohmann::json Page(const nlohmann::json& cmd) {
			if (!session) return Fail("nothing loaded");
			if (!cmd.contains("template") || !cmd["template"].is_string()) return Fail("page needs a \"template\" path");
			std::string html, err;
			if (!ExportHtml(*session, cmd["template"].get<std::string>(), html, err, "<script>window.LAB_LIVE = true;</script>")) return Fail(err);
			return { { "ok", true }, { "html", html } };
		}
	};

} // namespace Lab
} // namespace RLGC
