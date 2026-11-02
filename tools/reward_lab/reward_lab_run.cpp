// reward_lab_run - headless Reward Lab runner (docs/REWARD_VIEWER_SPEC.md, P1).
//
//   reward_lab_run <scenario.json | directory> [...] [--out DIR] [--html] [--viewer TEMPLATE] [--quiet]
//
// For each scenario: runs its script on the real EnvSet, prints a text report (rewards, events, gates, expectations with the
// author's intent line) and, with --out, writes <DIR>/<name>.report.txt and <DIR>/<name>.trace.jsonl (one reward trace per step).
// --html (with --out) also writes <DIR>/<name>.lab.html: one self-contained page (field view, reward HUD, timeline) to open in a browser.
// A directory runs every *.scenario.json under it. Run from the repo root (needs build/collision_meshes).
// Exit code: 0 all expectations passed, 1 some expectation failed (or a scenario had none), 2 a scenario could not be read or run.
#include <RLGymCPP/Lab/LabExport.h>

#include <filesystem>
#include <fstream>
#include <iostream>

using namespace RLGC::Lab;
namespace fs = std::filesystem;

int main(int argc, char** argv) {
	std::vector<std::string> inputs; std::string outDir, viewer = "tools/reward_lab/viewer/viewer.template.html"; bool quiet = false, html = false;
	for (int i = 1; i < argc; i++) {
		std::string a = argv[i];
		if (a == "--out" && i + 1 < argc) outDir = argv[++i];
		else if (a == "--quiet") quiet = true;
		else if (a == "--html") html = true;
		else if (a == "--viewer" && i + 1 < argc) viewer = argv[++i];
		else if (a == "-h" || a == "--help") { std::cout << "usage: reward_lab_run <scenario.json|dir>... [--out DIR] [--html] [--viewer TEMPLATE] [--quiet]\n"; return 0; }
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
		session.RunScripted();
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
