// reward_lab_pack - sign-off for a scenario pack (docs/REWARD_VIEWER_SPEC.md, P4-1). See RLGymCPP/Lab/Pack.h for what is checked.
//
//   reward_lab_pack check DIR [--src DIR]                     verdict of a pack against its manifest.json
//   reward_lab_pack sign  DIR --reviewer NAME [--date YYYY-MM-DD] [--src DIR]   write manifest.json (a person's act; refuses a failing pack)
//   reward_lab_pack probe                                     print the engine probe samples (what is stored under "engine")
//
// --src is the RLGymCPP source root the reward headers are hashed from (default GigaLearnCPP/RLGymCPP/src/RLGymCPP). Run from the repo root.
// check exits 0 CURRENT, 1 STALE or ENGINE_MISMATCH, 2 INVALID or a usage error, 3 UNSIGNED. sign exits 0 or 2.
#include <RLGymCPP/Lab/Pack.h>

#include <iostream>

using namespace RLGC::Lab;

int main(int argc, char** argv) {
	if (argc < 2) { std::cerr << "usage: reward_lab_pack check|sign|probe ... (see the header of this file)\n"; return 2; }
	const std::string cmd = argv[1];
	std::string dir, reviewer, date, src = "GigaLearnCPP/RLGymCPP/src/RLGymCPP";
	for (int i = 2; i < argc; i++) {
		std::string a = argv[i];
		if (a == "--reviewer" && i + 1 < argc) reviewer = argv[++i];
		else if (a == "--date" && i + 1 < argc) date = argv[++i];
		else if (a == "--src" && i + 1 < argc) src = argv[++i];
		else if (dir.empty()) dir = a;
		else { std::cerr << "unexpected argument " << a << "\n"; return 2; }
	}
	if (cmd == "probe") {
		std::vector<double> p; std::string err;
		if (!EngineProbe(p, err)) { std::cerr << "probe failed: " << err << "\n"; return 2; }
		for (size_t i = 0; i < p.size(); i++) std::cout << p[i] << ((i + 1) % 15 == 0 ? "\n" : " ");
		return 0;
	}
	if (dir.empty()) { std::cerr << cmd << " needs a pack directory\n"; return 2; }
	if (cmd == "check") {
		PackStatus st = CheckPack(dir, src);
		std::cout << PackStatusText(st);
		return st.verdict == "CURRENT" ? 0 : st.verdict == "UNSIGNED" ? 3 : st.verdict == "INVALID" ? 2 : 1;
	}
	if (cmd == "sign") {
		if (date.empty()) { char b[16]; std::time_t t = std::time(nullptr); std::strftime(b, sizeof(b), "%Y-%m-%d", std::localtime(&t)); date = b; }
		std::string err;
		if (!SignPack(dir, src, reviewer, date, err)) { std::cerr << "not signed: " << err << "\n"; return 2; }
		std::cout << "signed " << dir << " as " << reviewer << " on " << date << "\n";
		return 0;
	}
	std::cerr << "unknown command " << cmd << "\n"; return 2;
}
