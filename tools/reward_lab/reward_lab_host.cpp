// reward_lab_host - the live Reward Lab as a line protocol (docs/REWARD_VIEWER_SPEC.md, P3-2).
//
//   reward_lab_host            reads one JSON command per line on stdin, writes one JSON reply per line on stdout
//
// Commands are documented in RLGymCPP/Lab/LabHost.h. Normally started by tools/reward_lab/serve.py, which puts HTTP in front of it.
// Run from the repo root (needs build/collision_meshes). Replies never contain a raw newline (JSON is dumped compact), so a
// reader can split on '\n'. Exits when stdin closes.
#include <RLGymCPP/Lab/LabHost.h>

#include <iostream>

int main() {
	std::ios::sync_with_stdio(false);
	RLGC::Lab::LabHost host;
	std::string line;
	while (std::getline(std::cin, line)) {
		if (line.empty()) continue;
		std::cout << host.HandleLine(line) << "\n" << std::flush;
	}
	return 0;
}
