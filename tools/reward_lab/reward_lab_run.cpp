// reward_lab_run - headless Reward Lab runner (docs/REWARD_VIEWER_SPEC.md, P1).
//
//   reward_lab_run <scenario.json | directory> [...] [--out DIR] [--html] [--viewer TEMPLATE] [--quiet]
//                  [--record-macro FILE] [--macro FILE [--skip N] [--phase P | --sweep]]
// --record-macro writes the scripted run as a tick-resolution macro (one scenario). --macro plays a macro projected onto the decision
// grid (hold-last at --skip, default the scenario's tick_skip, sampling from tick --phase) INSTEAD of the script; --sweep plays every phase.
//
// For each scenario: runs its script on the real EnvSet, prints a text report (rewards, events, gates, expectations with the
// author's intent line) and, with --out, writes <DIR>/<name>.report.txt and <DIR>/<name>.trace.jsonl (one reward trace per step).
// --html (with --out) also writes <DIR>/<name>.lab.html: one self-contained page (field view, reward HUD, timeline) to open in a browser.
// A directory runs every *.scenario.json under it. Run from the repo root (needs build/collision_meshes).
// Exit code: 0 all expectations passed, 1 some expectation failed (or a scenario had none), 2 a scenario could not be read or run.
#include <RLGymCPP/Lab/LabExport.h>
#include <RLGymCPP/Lab/Macro.h>

#include <filesystem>
#include <fstream>
#include <iostream>

using namespace RLGC::Lab;
namespace fs = std::filesystem;

int main(int argc, char** argv) {
	std::vector<std::string> inputs; std::string outDir, viewer = "tools/reward_lab/viewer/viewer.template.html"; bool quiet = false, html = false, sweep = false; std::string recordMacro, macroFile; int gridSkip = 0, gridPhase = 0;
	for (int i = 1; i < argc; i++) {
		std::string a = argv[i];
		if (a == "--out" && i + 1 < argc) outDir = argv[++i];
		else if (a == "--quiet") quiet = true;
		else if (a == "--html") html = true;
		else if (a == "--viewer" && i + 1 < argc) viewer = argv[++i];
		else if (a == "--record-macro" && i + 1 < argc) recordMacro = argv[++i];
		else if (a == "--macro" && i + 1 < argc) macroFile = argv[++i];
		else if (a == "--skip" && i + 1 < argc) gridSkip = std::atoi(argv[++i]);
		else if (a == "--phase" && i + 1 < argc) gridPhase = std::atoi(argv[++i]);
		else if (a == "--sweep") sweep = true;
		else if (a == "-h" || a == "--help") {
			std::cout << "usage: reward_lab_run <scenario.json|dir>... [--out DIR] [--html] [--viewer TEMPLATE] [--quiet]\n"
				"  [--record-macro FILE]                         write the scripted run as a tick-resolution macro (one scenario)\n"
				"  [--macro FILE] [--skip N] [--phase P|--sweep]  play a macro projected onto the decision grid instead of the script\n";
			return 0;
		}
		else inputs.push_back(a);
	}
	if (inputs.empty()) { std::cerr << "no scenarios given (try --help)\n"; return 2; }

	std::vector<fs::path> files;
	for (auto& in : inputs) {
		if (fs::is_directory(in)) { for (auto& e : fs::recursive_directory_iterator(in)) if (e.is_regular_file() && e.path().string().find(".scenario.json") != std::string::npos) files.push_back(e.path()); }
		else files.push_back(in);
	}
	std::sort(files.begin(), files.end());
	if (files.empty()) { std::cerr << "no *.scenario.json files found\n"; return 2; }
	if (!outDir.empty()) fs::create_directories(outDir);
	if (!recordMacro.empty() && files.size() != 1) { std::cerr << "--record-macro needs exactly one scenario\n"; return 2; }
	Macro macro;
	if (!macroFile.empty()) {
		std::ifstream mf(macroFile); std::stringstream ms; ms << mf.rdbuf(); std::vector<std::string> merrs;
		if (!mf || !ParseMacroText(ms.str(), macro, merrs)) { std::cerr << macroFile << ": INVALID MACRO\n"; for (auto& e : merrs) std::cerr << "  " << e << "\n"; return 2; }
	}
	if (html && outDir.empty()) { std::cerr << "--html needs --out DIR\n"; return 2; }

	int exitCode = 0, pass = 0, fail = 0, broken = 0;
	for (auto& file : files) {
		std::ifstream f(file);
		if (!f) { std::cerr << file << ": cannot open\n"; broken++; exitCode = 2; continue; }
		std::stringstream ss; ss << f.rdbuf();
		Scenario sc; std::vector<std::string> errs;
		if (!ParseScenarioText(ss.str(), sc, errs)) {
			std::cerr << file << ": INVALID SCENARIO\n"; for (auto& e : errs) std::cerr << "  " << e << "\n";
			broken++; exitCode = 2; continue;
		}
		LabSession session(sc);
		if (!session.ok()) {
			std::cerr << file << ": cannot run\n"; for (auto& e : session.errors()) std::cerr << "  " << e << "\n";
			broken++; exitCode = 2; continue;
		}
		if (!macroFile.empty()) {
			const int skip = gridSkip > 0 ? gridSkip : sc.tickSkip;
			std::cout << "macro " << macroFile << " on " << sc.name << " (" << macro.Ticks() << " ticks, skip " << skip << ")\n";
			if (sweep) { SweepResult sw = SweepPhases(sc, macro, skip); std::cout << SweepText(sw); if (!sw.errors.empty()) exitCode = 2; }
			else {
				GridRun gr = RunGrid(sc, macro, skip, gridPhase);
				if (!gr.errors.empty()) { for (auto& e : gr.errors) std::cerr << "  " << e << "\n"; exitCode = 2; continue; }
				SweepResult one; one.skip = skip; one.phases.push_back(Summarize(gr, sc.cars.size()));
				std::cout << SweepText(one);
				for (auto& r : gr.expectations) std::cout << "  [" << (r.ok ? "PASS" : "FAIL") << "] " << r.expectation->intent << "\n         " << r.detail << "\n";
			}
			continue;
		}
		session.RunScripted();
		if (!recordMacro.empty()) {
			Macro rec = MacroFromLog(session.ActionLog(), sc.tickSkip, sc.name);
			std::ofstream(recordMacro) << MacroToJson(rec).dump(1) << "\n";
			std::cout << "recorded " << rec.Ticks() << " ticks (" << session.Steps() << " steps x skip " << sc.tickSkip << ") to " << recordMacro << "\n";
		}
		const std::string report = TextReport(session);
		const auto results = session.EvaluateExpectations();
		const bool ok = !results.empty() && AllPass(results);
		(ok ? pass : fail)++;
		if (!ok && exitCode == 0) exitCode = 1;
		if (!quiet || !ok) std::cout << report << "\n";
		else std::cout << "PASS " << sc.name << " (" << results.size() << " expectations)\n";
		if (!outDir.empty()) {
			std::ofstream(fs::path(outDir) / (sc.name + ".report.txt")) << report;
			std::ofstream tr(fs::path(outDir) / (sc.name + ".trace.jsonl")); WriteTraceJsonl(session, tr);
			if (html) {
				std::string page, err;
				if (!ExportHtml(session, viewer, page, err)) { std::cerr << file << ": cannot export html: " << err << "\n"; exitCode = 2; }
				else std::ofstream(fs::path(outDir) / (sc.name + ".lab.html")) << page;
			}
		}
	}
	std::cout << "reward_lab_run: " << pass << " passed, " << fail << " failed, " << broken << " unreadable of " << files.size() << " scenarios\n";
	return exitCode;
}
