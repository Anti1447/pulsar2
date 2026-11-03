#pragma once
// ============================================================================
// Pack.h - sign-off for a scenario pack: what was reviewed, and whether it still holds. 2026-11-02 | Rev 1.0 | docs/REWARD_VIEWER_SPEC.md (P4-1)
//
// A pack is a folder of *.scenario.json plus manifest.json. The manifest is a person's statement: "I reviewed these scenarios, against
// THESE reward sources, on THIS engine behaviour, and they passed." Check re-derives every one of those and says whether it still holds:
//
//   scenarios   sha256 of each file (line endings normalised). Edited, added or removed scenario files make the pack STALE.
//   rewards     sha256 over every source file reachable from the reward's header through quoted #includes (line endings normalised;
//               a comment edit counts, on purpose: the check cannot tell a comment from a threshold). A changed file makes the pack STALE
//               and names the reward and the file.
//   engine      a 60-decision-step probe on the real engine (drive, boost, jump, dodge, a ball hit, a second car); its samples are stored
//               and compared with a tolerance. A different engine build or mesh set gives ENGINE_MISMATCH: the pack is refused, not re-run.
//   results     every scenario is re-run and must still PASS, as it did at sign-off.
//
// Verdicts: UNSIGNED (no manifest) | INVALID (unreadable manifest) | ENGINE_MISMATCH | STALE | CURRENT. Only CURRENT may be called "verified".
// Signing is the reviewer's act: Sign() needs a reviewer name, refuses a pack that does not fully pass, and is never run by tooling on its own.
// The Lab's own C++ (the harness that measures) is hashed too but only WARNS when it changed: it is not a reviewed reward.
// ============================================================================
#include "LabReport.h"
#include "RewardRegistry.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

namespace RLGC {
namespace Lab {

	constexpr int PACK_SCHEMA = 1;
	// uu (and uu/s) on the probe samples. MEASURED: the same probe built at -O0, -O2 and the production flags (-O3 -march=native -ffast-math) differs by up to
	// 0.0100 between the production flags and the others (0.0 between -O0 and -O2), so the tolerance sits 10x above that build noise.
	constexpr double ENGINE_TOLERANCE = 0.1;

	// ---- SHA-256 -------------------------------------------------------------------------------------------------------
	namespace sha {
		inline uint32_t R(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }
		inline std::string Hex(const std::string& data) {
			static const uint32_t K[64] = {
				0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
				0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
				0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
				0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2 };
			uint32_t h[8] = { 0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19 };
			std::string m = data; const uint64_t bits = (uint64_t)data.size() * 8;
			m += (char)0x80; while (m.size() % 64 != 56) m += (char)0;
			for (int i = 7; i >= 0; i--) m += (char)((bits >> (i * 8)) & 0xff);
			for (size_t off = 0; off < m.size(); off += 64) {
				uint32_t w[64];
				for (int i = 0; i < 16; i++) w[i] = ((uint32_t)(uint8_t)m[off + i * 4] << 24) | ((uint32_t)(uint8_t)m[off + i * 4 + 1] << 16) | ((uint32_t)(uint8_t)m[off + i * 4 + 2] << 8) | (uint32_t)(uint8_t)m[off + i * 4 + 3];
				for (int i = 16; i < 64; i++) { uint32_t s0 = R(w[i - 15], 7) ^ R(w[i - 15], 18) ^ (w[i - 15] >> 3), s1 = R(w[i - 2], 17) ^ R(w[i - 2], 19) ^ (w[i - 2] >> 10); w[i] = w[i - 16] + s0 + w[i - 7] + s1; }
				uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
				for (int i = 0; i < 64; i++) {
					uint32_t S1 = R(e, 6) ^ R(e, 11) ^ R(e, 25), ch = (e & f) ^ (~e & g), t1 = hh + S1 + ch + K[i] + w[i];
					uint32_t S0 = R(a, 2) ^ R(a, 13) ^ R(a, 22), mj = (a & b) ^ (a & c) ^ (b & c), t2 = S0 + mj;
					hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
				}
				h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
			}
			char out[65]; for (int i = 0; i < 8; i++) std::snprintf(out + i * 8, 9, "%08x", h[i]);
			return std::string(out, 64);
		}
	}

	namespace fs = std::filesystem;

	inline bool ReadFileText(const fs::path& p, std::string& out) {
		std::ifstream f(p, std::ios::binary); if (!f) return false;
		std::stringstream ss; ss << f.rdbuf(); out = ss.str(); return true;
	}
	// A file edited on Windows and on Linux must hash the same: drop the CR of every CRLF.
	inline std::string NormalizeEol(const std::string& s) {
		std::string o; o.reserve(s.size());
		for (size_t i = 0; i < s.size(); i++) if (!(s[i] == '\r' && i + 1 < s.size() && s[i + 1] == '\n')) o += s[i];
		return o;
	}
	inline std::string FileSha(const fs::path& p, bool& ok) {
		std::string t; ok = ReadFileText(p, t);
		return ok ? sha::Hex(NormalizeEol(t)) : std::string();
	}

	// ---- reward sources ------------------------------------------------------------------------------------------------
	struct SourceHash {
		std::string sha;                  // over the sorted (path, file sha) pairs
		std::map<std::string, std::string> files;   // path relative to root -> file sha ("MISSING" when an include cannot be found)
	};

	inline std::vector<std::string> QuotedIncludes(const std::string& text) {
		std::vector<std::string> out; std::istringstream in(text); std::string line;
		while (std::getline(in, line)) {
			size_t i = line.find_first_not_of(" \t"); if (i == std::string::npos || line[i] != '#') continue;
			i = line.find_first_not_of(" \t", i + 1); if (i == std::string::npos || line.compare(i, 7, "include") != 0) continue;
			i = line.find_first_not_of(" \t", i + 7); if (i == std::string::npos || line[i] != '"') continue;
			size_t e = line.find('"', i + 1); if (e != std::string::npos) out.push_back(line.substr(i + 1, e - i - 1));
		}
		return out;
	}

	// Every file reachable from `entries` (paths relative to `root`) through quoted #includes, hashed together. Cycles are fine.
	// With follow = false only the listed files are hashed (no includes).
	inline SourceHash HashClosure(const fs::path& root, const std::vector<std::string>& entries, bool follow = true) {
		SourceHash h; std::vector<fs::path> todo;
		auto rel = [&](const fs::path& p) { return p.lexically_normal().lexically_relative(root.lexically_normal()).generic_string(); };
		for (auto& e : entries) todo.push_back((root / e).lexically_normal());
		while (!todo.empty()) {
			fs::path p = todo.back(); todo.pop_back();
			const std::string key = rel(p);
			if (h.files.count(key)) continue;
			std::string text;
			if (!ReadFileText(p, text)) { h.files[key] = "MISSING"; continue; }
			h.files[key] = sha::Hex(NormalizeEol(text));
			if (follow) for (auto& inc : QuotedIncludes(text)) {
				// where the compiler would look: next to the including file, then the include roots of the build (the RLGymCPP source root and its
				// parent, which is -I GigaLearnCPP/RLGymCPP/src; that is how "../RocketSimV3/compat/RocketSim.h" is found)
				fs::path near = (p.parent_path() / inc).lexically_normal(), far = (root / inc).lexically_normal(), up = (root.lexically_normal().parent_path() / inc).lexically_normal();
				std::error_code ec;
				fs::path found;
				if (fs::exists(near, ec)) found = near; else if (fs::exists(far, ec)) found = far; else if (fs::exists(up, ec)) found = up;
				if (found.empty()) { h.files[rel(near)] = "MISSING"; continue; }
				// Only the reward code itself is hashed. The engine (RocketSim, its headers and Bullet) is outside the root on purpose: its behaviour
				// is what the engine probe measures, and following the unused RocketSim v2 branch of Framework.h would hash a hundred unrelated files.
				if (rel(found).rfind("..", 0) == 0) continue;
				todo.push_back(found);
			}
		}
		std::string all;
		for (auto& kv : h.files) all += kv.first + "\n" + kv.second + "\n";
		h.sha = sha::Hex(all);
		return h;
	}

	// The header each registered reward lives in, relative to RLGymCPP/src/RLGymCPP. Kept next to the registry; a test checks they agree.
	inline const std::map<std::string, std::string>& RewardHeaders() {
		static const std::map<std::string, std::string> m = {
			{ "OffsidePenalty", "Rewards/OffsidePenalty.h" },
			{ "CarEnergyPotentialReward", "Rewards/CommonRewards.h" },   // shared with the other CommonRewards: an edit to any of them stales this one
			{ "PassingSetupReward", "Rewards/PassingSetupRewards.h" },
			{ "GoalReward", "Rewards/CommonRewards.h" },                  // shared with the other CommonRewards, like CarEnergyPotentialReward
			{ "AerialTouchReward", "Rewards/CommonRewards.h" },           // shared with the other CommonRewards
			{ "BallToGoalPotentialReward", "Rewards/CommonRewards.h" },   // shared with the other CommonRewards
			{ "AirInterceptPotentialReward", "Rewards/CommonRewards.h" }, // shared with the other CommonRewards
			{ "WallJumpToBallReward", "Rewards/CommonRewards.h" },       // shared with the other CommonRewards
			{ "KickoffRaceReward", "Rewards/CommonRewards.h" },          // shared with the other CommonRewards
			{ "KickoffOutcomeReward", "Rewards/KickoffOutcomeReward.h" },
			{ "ConsecutiveAirTouchReward", "Rewards/CommonRewards.h" },   // shared with the other CommonRewards
			{ "MultiAirTouchReward", "Rewards/MultiAirTouchReward.h" },
			{ "DemoReward", "Rewards/CommonRewards.h" },                  // shared with the other CommonRewards
			{ "TouchAccelReward", "Rewards/CommonRewards.h" },                // shared with the other CommonRewards
			{ "GuardedPickupBoostReward", "Rewards/CommonRewards.h" },        // shared with the other CommonRewards
			{ "TeamPressureReward", "Rewards/CommonRewards.h" },           // shared with the other CommonRewards
			{ "BallProximityPotentialReward", "Rewards/CommonRewards.h" },   // shared with the other CommonRewards
			{ "OpposedSaveReward", "Rewards/CommonRewards.h" },           // shared with the other CommonRewards
		};
		return m;
	}

	// ---- engine probe ---------------------------------------------------------------------------------------------------
	inline Scenario ProbeScenario() {
		Scenario s; s.name = "engine_probe"; s.tickSkip = 8; s.steps = 60;
		CarSpec a; a.team = Team::BLUE; a.pos = Vec(0, -2800, 17); a.yawDeg = 90; a.boost = 100;
		CarSpec b; b.team = Team::ORANGE; b.pos = Vec(1500, 2500, 17); b.yawDeg = 270; b.boost = 100;
		s.cars = { a, b };
		s.ball.pos = Vec(0, -300, 93);
		auto act = [](float thr, float steer, float pitch, float jump, float boost) { Action x = Action(CarControls{}); x.throttle = thr; x.steer = steer; x.pitch = pitch; x.jump = jump; x.boost = boost; return x; };
		auto add = [&](int car, int from, Action x) { ActionEntry e; e.car = car; e.from = from; e.action = x; s.actions.push_back(e); };
		add(0, 0, act(1, 0, 0, 0, 1)); add(0, 10, act(1, 0, 0, 1, 1)); add(0, 11, act(1, 0, -1, 1, 1)); add(0, 12, act(1, 0.3f, 0, 0, 1));
		add(1, 0, act(1, 0.3f, 0, 0, 0)); add(1, 20, act(0.5f, -0.6f, 0, 0, 1));
		return s;
	}

	// 12 samples (every 5th step) x car0 pos, vel; car1 pos; ball pos, vel = 15 numbers, rounded to 1e-3.
	inline bool EngineProbe(std::vector<double>& out, std::string& err, const Scenario* custom = nullptr) {
		Scenario s = custom ? *custom : ProbeScenario();
		LabSession ses(s);
		if (!ses.ok()) { for (auto& e : ses.errors()) err += e + "; "; return false; }
		ses.RunScripted();
		if (ses.Steps() != s.steps) { err = "the probe ended early (step " + std::to_string(ses.Steps()) + ")"; return false; }
		out.clear();
		auto put = [&](const Vec& v) { for (float c : { v.x, v.y, v.z }) out.push_back(std::round((double)c * 1000.0) / 1000.0); };
		for (int k = 5; k <= s.steps; k += 5) {
			const Frame& f = ses.Frames()[k];
			put(f.players[0].pos); put(f.players[0].vel); put(f.players[1].pos); put(f.ball.pos); put(f.ball.vel);
		}
		return true;
	}

	// ---- manifest ------------------------------------------------------------------------------------------------------
	struct PackStatus {
		std::string verdict;                  // UNSIGNED | INVALID | ENGINE_MISMATCH | STALE | CURRENT
		std::vector<std::string> reasons;     // why it is not CURRENT
		std::vector<std::string> warnings;    // things worth knowing that do not stale the pack
		std::string reviewer, date;
	};

	inline std::vector<std::string> ListScenarioFiles(const fs::path& dir) {
		std::vector<std::string> out;
		std::error_code ec;
		for (auto it = fs::recursive_directory_iterator(dir, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
			if (it->is_regular_file() && it->path().string().find(".scenario.json") != std::string::npos)
				out.push_back(it->path().lexically_relative(dir).generic_string());
		std::sort(out.begin(), out.end());
		return out;
	}

	// The reward-source entries a scenario needs: each reward's header, plus the ZeroSum wrapper when one is used.
	inline void RewardEntries(const Scenario& sc, std::map<std::string, std::string>& nameToHeader, bool& usesZeroSum) {
		for (auto& r : sc.rewards) {
			auto it = RewardHeaders().find(r.name);
			nameToHeader[r.name] = it == RewardHeaders().end() ? "" : it->second;
			if (r.zeroSum >= 0) usesZeroSum = true;
		}
	}

	// The Lab's own C++ that measures (only those files: following includes would pull in every registered reward and stale the warning on a reward edit).
	inline std::string HarnessHash(const fs::path& srcRoot) { return HashClosure(srcRoot, { "Lab/RewardLab.h", "Lab/LabEnv.h", "Lab/Expectations.h", "Lab/LabTypes.h", "Lab/Scenario.h", "Lab/RewardRegistry.h", "Rewards/RewardTraceStack.h" }, false).sha; }

	// Everything Sign records and Check re-derives, as JSON.
	inline bool Describe(const fs::path& dir, const fs::path& srcRoot, nlohmann::json& out, std::vector<std::string>& errs) {
		using nlohmann::json;
		json scen = json::array(); std::map<std::string, std::string> names; bool zs = false;
		for (auto& rel : ListScenarioFiles(dir)) {
			std::string text; if (!ReadFileText(dir / rel, text)) { errs.push_back(rel + ": cannot read"); continue; }
			Scenario sc; std::vector<std::string> pe;
			if (!ParseScenarioText(text, sc, pe)) { for (auto& e : pe) errs.push_back(rel + ": " + e); continue; }
			bool ok; scen.push_back({ { "file", rel }, { "sha256", FileSha(dir / rel, ok) }, { "name", sc.name } });
			RewardEntries(sc, names, zs);
		}
		json rw = json::array();
		for (auto& kv : names) {
			if (kv.second.empty()) { errs.push_back("reward \"" + kv.first + "\" has no known source header (Lab/Pack.h RewardHeaders)"); continue; }
			std::vector<std::string> entries = { kv.second };
			if (zs) { entries.push_back("Rewards/ZeroSumReward.h"); entries.push_back("Rewards/ZeroSumReward.cpp"); }
			SourceHash h = HashClosure(srcRoot, entries);
			json files = json::object(); for (auto& f : h.files) files[f.first] = f.second;
			rw.push_back({ { "name", kv.first }, { "header", kv.second }, { "sha256", h.sha }, { "files", files } });
		}
		out = { { "scenarios", scen }, { "rewards", rw }, { "harness_sha256", HarnessHash(srcRoot) } };
		return errs.empty();
	}

	// Writes <dir>/manifest.json. Refuses unless every scenario passes and has at least one expectation. `date` is YYYY-MM-DD.
	inline bool SignPack(const fs::path& dir, const fs::path& srcRoot, const std::string& reviewer, const std::string& date, std::string& err) {
		using nlohmann::json;
		if (reviewer.empty()) { err = "a reviewer name is required: sign-off is a person's statement"; return false; }
		if (date.size() != 10 || date[4] != '-' || date[7] != '-') { err = "date must be YYYY-MM-DD"; return false; }
		json d; std::vector<std::string> errs;
		if (!Describe(dir, srcRoot, d, errs)) { for (auto& e : errs) err += e + "; "; return false; }
		if (d["scenarios"].empty()) { err = "no *.scenario.json under " + dir.string(); return false; }
		for (auto& s : d["scenarios"]) {
			std::string text; ReadFileText(dir / s["file"].get<std::string>(), text);
			Scenario sc; std::vector<std::string> pe; ParseScenarioText(text, sc, pe);
			LabSession ses(sc);
			if (!ses.ok()) { err = s["file"].get<std::string>() + ": cannot run"; return false; }
			ses.RunScripted();
			auto results = ses.EvaluateExpectations();
			if (results.empty()) { err = s["file"].get<std::string>() + ": has no expectations, nothing to sign off"; return false; }
			if (!AllPass(results)) { err = s["file"].get<std::string>() + ": an expectation fails; a failing pack cannot be signed off"; return false; }
			s["result"] = "PASS"; s["expectations"] = (int)results.size();
		}
		std::vector<double> probe; std::string pe;
		if (!EngineProbe(probe, pe)) { err = "engine probe failed: " + pe; return false; }
		json m = { { "schema", PACK_SCHEMA }, { "kind", "lab_pack_manifest" }, { "name", dir.filename().string() }, { "reviewer", reviewer }, { "date", date },
			{ "engine", { { "tolerance", ENGINE_TOLERANCE }, { "probe", probe } } }, { "scenarios", d["scenarios"] }, { "rewards", d["rewards"] }, { "harness_sha256", d["harness_sha256"] } };
		std::ofstream f(dir / "manifest.json"); if (!f) { err = "cannot write manifest.json"; return false; }
		f << m.dump(1) << "\n";
		return true;
	}

	// Compares the pack as it is now with its manifest. `ranScenarios` false skips the re-run (a cheap check of files only).
	inline PackStatus CheckPack(const fs::path& dir, const fs::path& srcRoot, bool rerun = true) {
		using nlohmann::json;
		PackStatus st;
		std::string text;
		if (!ReadFileText(dir / "manifest.json", text)) { st.verdict = "UNSIGNED"; st.reasons.push_back("no manifest.json: nobody has signed this pack off"); return st; }
		json m;
		try { m = json::parse(text); } catch (const std::exception& e) { st.verdict = "INVALID"; st.reasons.push_back(std::string("manifest.json is not JSON: ") + e.what()); return st; }
		auto bad = [&](const std::string& why) { st.verdict = "INVALID"; st.reasons.push_back("manifest.json: " + why); return st; };
		if (!m.is_object() || !m.contains("schema") || m["schema"] != PACK_SCHEMA) return bad("unsupported or missing schema (this build reads " + std::to_string(PACK_SCHEMA) + ")");
		if (!m.contains("kind") || m["kind"] != "lab_pack_manifest") return bad("kind must be lab_pack_manifest");
		for (const char* k : { "reviewer", "date", "engine", "scenarios", "rewards", "harness_sha256" }) if (!m.contains(k)) return bad(std::string("missing \"") + k + "\"");
		if (!m["engine"].is_object() || !m["engine"].contains("probe") || !m["engine"]["probe"].is_array() || !m["engine"].contains("tolerance")) return bad("engine needs probe and tolerance");
		if (!m["scenarios"].is_array() || !m["rewards"].is_array() || !m["reviewer"].is_string() || !m["date"].is_string()) return bad("scenarios and rewards must be lists, reviewer and date strings");
		st.reviewer = m["reviewer"]; st.date = m["date"];

		// 1. engine: refuse, do not re-run, when the physics is not what the reviewer saw
		std::vector<double> now; std::string pe;
		if (!EngineProbe(now, pe)) { st.verdict = "ENGINE_MISMATCH"; st.reasons.push_back("the engine probe could not run here: " + pe); return st; }
		const json& was = m["engine"]["probe"]; const double tol = m["engine"]["tolerance"].is_number() ? m["engine"]["tolerance"].get<double>() : ENGINE_TOLERANCE;
		if (was.size() != now.size()) { st.verdict = "ENGINE_MISMATCH"; st.reasons.push_back("the engine probe has " + std::to_string(now.size()) + " samples, the manifest has " + std::to_string(was.size())); return st; }
		double worst = 0; size_t at = 0;
		for (size_t i = 0; i < now.size(); i++) { if (!was[i].is_number()) return bad("engine.probe holds a non-number"); double d = std::fabs(was[i].get<double>() - now[i]); if (d > worst) { worst = d; at = i; } }
		if (worst > tol) {
			st.verdict = "ENGINE_MISMATCH";
			st.reasons.push_back("this engine behaves differently from the one the pack was signed off on: probe sample " + std::to_string(at) + " differs by " + detail::Num(worst) + " (tolerance " + detail::Num(tol) + "). The results are not comparable; re-run the pack and sign it off again on this engine.");
			return st;
		}

		// 2. files and sources
		json d; std::vector<std::string> errs;
		if (!Describe(dir, srcRoot, d, errs)) { st.verdict = "STALE"; for (auto& e : errs) st.reasons.push_back(e); return st; }
		std::map<std::string, std::string> signedFiles, nowFiles;
		for (auto& s : m["scenarios"]) { if (!s.is_object() || !s.contains("file") || !s.contains("sha256")) return bad("a scenario entry needs file and sha256"); signedFiles[s["file"]] = s["sha256"]; }
		for (auto& s : d["scenarios"]) nowFiles[s["file"]] = s["sha256"];
		for (auto& kv : nowFiles) {
			if (!signedFiles.count(kv.first)) st.reasons.push_back("scenario " + kv.first + " was added after sign-off");
			else if (signedFiles[kv.first] != kv.second) st.reasons.push_back("scenario " + kv.first + " was edited after sign-off");
		}
		for (auto& kv : signedFiles) if (!nowFiles.count(kv.first)) st.reasons.push_back("scenario " + kv.first + " was signed off but is gone");
		std::map<std::string, const json*> signedRewards;
		for (auto& r : m["rewards"]) { if (!r.is_object() || !r.contains("name") || !r.contains("sha256") || !r.contains("files")) return bad("a reward entry needs name, sha256 and files"); signedRewards[r["name"]] = &r; }
		for (auto& r : d["rewards"]) {
			const std::string name = r["name"];
			if (!signedRewards.count(name)) { st.reasons.push_back("reward " + name + " is used by a scenario but was not signed off"); continue; }
			const json& was = *signedRewards[name];
			if (was["sha256"] == r["sha256"]) continue;
			std::string changed;
			for (auto it = r["files"].begin(); it != r["files"].end(); ++it) {
				if (!was["files"].contains(it.key())) changed += (changed.empty() ? "" : ", ") + it.key() + " (new dependency)";
				else if (was["files"][it.key()] != it.value()) changed += (changed.empty() ? "" : ", ") + it.key();
			}
			for (auto it = was["files"].begin(); it != was["files"].end(); ++it) if (!r["files"].contains(it.key())) changed += (changed.empty() ? "" : ", ") + it.key() + " (no longer included)";
			st.reasons.push_back("reward " + name + " changed after sign-off: " + changed);
		}
		if (m["harness_sha256"] != d["harness_sha256"]) st.warnings.push_back("the Lab harness code changed since sign-off (Lab/*.h): the measurements may differ; re-running is recommended");

		// 3. results: every scenario must still pass
		if (rerun) for (auto& rel : ListScenarioFiles(dir)) {
			std::string t; ReadFileText(dir / rel, t); Scenario sc; std::vector<std::string> pe2;
			if (!ParseScenarioText(t, sc, pe2)) continue;
			LabSession ses(sc);
			if (!ses.ok()) { st.reasons.push_back(rel + ": cannot run any more: " + (ses.errors().empty() ? "" : ses.errors()[0])); continue; }
			ses.RunScripted();
			auto results = ses.EvaluateExpectations();
			if (results.empty() || !AllPass(results)) st.reasons.push_back(rel + ": no longer passes (it did at sign-off)");
		}
		st.verdict = st.reasons.empty() ? "CURRENT" : "STALE";
		return st;
	}

	inline std::string PackStatusText(const PackStatus& st) {
		std::ostringstream o;
		o << "pack: " << st.verdict;
		if (!st.reviewer.empty()) o << "  (signed off by " << st.reviewer << " on " << st.date << ")";
		o << "\n";
		for (auto& r : st.reasons) o << "  - " << r << "\n";
		for (auto& w : st.warnings) o << "  ! " << w << "\n";
		if (st.verdict == "CURRENT") o << "  every scenario, reward source and the engine behaviour match the sign-off; the pack may be called verified.\n";
		else o << "  not verified: do not describe these rewards as verified until the pack is CURRENT again.\n";
		return o.str();
	}

} // namespace Lab
} // namespace RLGC
