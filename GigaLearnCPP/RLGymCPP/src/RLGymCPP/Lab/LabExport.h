#pragma once
// ============================================================================
// LabExport.h - a finished run as one JSON document, and as one self-contained HTML file.
// 2026-11-02 | Rev 1.0 | docs/REWARD_VIEWER_SPEC.md (P2)
//
// The viewer (tools/reward_lab/viewer) is a static page: the run is embedded in it, so a person opens ONE file in any browser and
// Claude reads the same numbers from the page. Every number in the run's "trace" objects is the RewardTraceJson of that step,
// identical to the line the JSONL file carries for it (tests/test_reward_lab_export.cpp compares them).
//
// Run JSON, schema 1:
//   {"schema":1,"kind":"lab_run","result":"PASS"|"FAIL"|"NONE","scenario":{...Scenario.h...},
//    "frames":[{"i":0,"terminal":0,"goal":false,"ball":{"pos":[..],"vel":[..]},
//               "players":[{"car":id,"team":0|1,"pos":[..],"vel":[..],"forward":[..],"up":[..],"boost":0..100,"on_ground":b,"flipping":b,
//                           "jumped":b,"double_jumped":b,"flipped":b,"demoed":b,"touched":b}],
//               "actions":[[throttle,steer,pitch,yaw,roll,jump,boost,handbrake],...],      // one per car; [] on frame 0
//               "trace": null | {RewardTraceJson schema 1}}],
//    "expectations":[{"kind":"..","intent":"..","ok":b,"detail":"..","reward":"..","car":n,"from":n,"to":n}]}
// ============================================================================
#include "LabReport.h"
#include "FrameJson.h"   // FrameToJson
#include <filesystem>
#include <fstream>
#include <sstream>

namespace RLGC {
namespace Lab {

	inline nlohmann::json ExpectationsToJson(const LabSession& s) {
		using nlohmann::json;
		json ex = json::array();
		const int last = s.Frames().back().index;
		for (auto& r : s.EvaluateExpectations()) {
			const Expectation& e = *r.expectation;
			ex.push_back({ { "kind", e.kind }, { "intent", e.intent }, { "ok", r.ok }, { "detail", r.detail }, { "reward", e.reward }, { "car", e.car },
				{ "from", e.from }, { "to", std::min(e.to, last) } });
		}
		return ex;
	}

	inline const char* ResultOf(const LabSession& s) {
		const auto results = s.EvaluateExpectations();
		return results.empty() ? "NONE" : AllPass(results) ? "PASS" : "FAIL";
	}

	inline nlohmann::json RunToJson(const LabSession& s) {
		using nlohmann::json;
		json j;
		j["schema"] = 1; j["kind"] = "lab_run";
		j["result"] = ResultOf(s);
		j["scenario"] = ToJson(s.GetScenario());
		json frames = json::array();
		for (auto& f : s.Frames()) frames.push_back(FrameToJson(f));
		j["frames"] = frames;
		j["expectations"] = ExpectationsToJson(s);
		return j;
	}

	// JSON text that is safe inside <script type="application/json">: '<' only ever occurs inside JSON strings, so it can be written
	// as the < escape, which keeps "</script>" and "<!--" from ever appearing in the page source.
	inline std::string JsonForScript(const nlohmann::json& j) {
		std::string d = j.dump(), out; out.reserve(d.size());
		for (char c : d) { if (c == '<') out += "\\u003c"; else out += c; }
		return out;
	}

	// Replaces each <!--INLINE:name--> marker with the contents of <dir>/name. Returns false (and says which) if a file is missing.
	inline bool InlineIncludes(std::string& text, const std::filesystem::path& dir, std::string& err) {
		const std::string open = "<!--INLINE:", close = "-->";
		size_t pos = 0;
		while ((pos = text.find(open, pos)) != std::string::npos) {
			size_t end = text.find(close, pos);
			if (end == std::string::npos) { err = "unterminated INLINE marker"; return false; }
			const std::string name = text.substr(pos + open.size(), end - pos - open.size());
			std::ifstream f(dir / name);
			if (!f) { err = "cannot read inline file " + (dir / name).string(); return false; }
			std::stringstream ss; ss << f.rdbuf();
			text.replace(pos, end + close.size() - pos, ss.str());
			pos += ss.str().size();
		}
		return true;
	}

	// The template carries <!--INLINE:file--> markers and one <!--LAB_RUN_JSON--> marker for the run.
	inline bool ExportHtml(const LabSession& s, const std::filesystem::path& templatePath, std::string& out, std::string& err, const std::string& liveScript = "") {
		std::ifstream f(templatePath);
		if (!f) { err = "cannot read viewer template " + templatePath.string(); return false; }
		std::stringstream ss; ss << f.rdbuf();
		out = ss.str();
		if (!InlineIncludes(out, templatePath.parent_path(), err)) return false;
		const std::string marker = "<!--LAB_RUN_JSON-->";
		size_t p = out.find(marker);
		if (p == std::string::npos) { err = "template has no <!--LAB_RUN_JSON--> marker"; return false; }
		out.replace(p, marker.size(), JsonForScript(RunToJson(s)));
		// <!--LAB_LIVE--> is where a live host (tools/reward_lab/serve.py) injects its script; a static export leaves nothing behind.
		const std::string liveMarker = "<!--LAB_LIVE-->";
		if (size_t q = out.find(liveMarker); q != std::string::npos) out.replace(q, liveMarker.size(), liveScript);
		return true;
	}

} // namespace Lab
} // namespace RLGC
