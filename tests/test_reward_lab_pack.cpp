#include "TestFramework.h"

#include <RLGymCPP/Lab/Pack.h>

#include <unistd.h>

using namespace RLGC;
using namespace RLGC::Lab;
using nlohmann::json;

// P4-1 of the Reward Lab (docs/REWARD_VIEWER_SPEC.md): pack manifest, sign-off, stale detection, engine-build refusal.
// Needs build/collision_meshes and the reward sources (run from the repo root).

namespace {
	const char* SRC = "GigaLearnCPP/RLGymCPP/src/RLGymCPP";
	const char* CORE = "tools/reward_lab/packs/core";

	std::string ReadAll(const fs::path& p) { std::string t; ReadFileText(p, t); return t; }
	void WriteAll(const fs::path& p, const std::string& t) { std::ofstream(p, std::ios::binary) << t; }
	void Replace(const fs::path& p, const std::string& a, const std::string& b) {
		std::string t = ReadAll(p); size_t i = t.find(a);
		if (i == std::string::npos) { std::cerr << "test setup: \"" << a << "\" not in " << p << "\n"; std::abort(); }
		t.replace(i, a.size(), b); WriteAll(p, t);
	}

	// A scratch world: a private copy of the reward sources and a pack of 2 scenarios (one offside, one passing).
	struct World {
		fs::path root, src, pack;
		World() {
			static int n = 0;
			root = fs::temp_directory_path() / ("lab_pack_test_" + std::to_string(::getpid()) + "_" + std::to_string(n++));
			fs::remove_all(root); fs::create_directories(root);
			src = root / "src"; pack = root / "pack";
			fs::copy(SRC, src, fs::copy_options::recursive);
			fs::create_directories(pack);
			for (const char* f : { "offside_goalside_car.scenario.json", "passing_setup_straight_hit.scenario.json" }) fs::copy_file(fs::path(CORE) / f, pack / f);
		}
		~World() { std::error_code ec; fs::remove_all(root, ec); }
		bool Sign(std::string& err) { return SignPack(pack, src, "Test Reviewer", "2026-11-02", err); }
		PackStatus Check() { return CheckPack(pack, src); }
		json Manifest() { return json::parse(ReadAll(pack / "manifest.json")); }
		void SetManifest(const json& m) { WriteAll(pack / "manifest.json", m.dump(1)); }
	};
	bool Has(const PackStatus& st, const std::string& needle) {
		for (auto& r : st.reasons) if (r.find(needle) != std::string::npos) return true;
		return false;
	}
	std::string Reasons(const PackStatus& st) { std::string s; for (auto& r : st.reasons) s += r + " | "; return s; }
}

// ---- hashing ---------------------------------------------------------------------------------------------------------

TEST(Pack_sha256_matches_published_and_reference_vectors) {
	CHECK_EQ(sha::Hex(""), std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
	CHECK_EQ(sha::Hex("abc"), std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
	CHECK_EQ(sha::Hex(std::string(1000000, 'a')), std::string("cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));
	// padding boundaries (reference values from Python hashlib)
	struct V { size_t n; const char* h; } v[] = {
		{ 1, "ca978112ca1bbdcafac231b39a23dc4da786eff8147c4e72b9807785afee48bb" }, { 55, "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318" },
		{ 56, "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a" }, { 57, "f13b2d724659eb3bf47f2dd6af1accc87b81f09f59f2b75e5c0bed6589dfe8c6" },
		{ 63, "7d3e74a05d7db15bce4ad9ec0658ea98e3f06eeecf16b4c6fff2da457ddc2f34" }, { 64, "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb" },
		{ 65, "635361c48bb9eab14198e76ea8ab7f1a41685d6ad62aa9146d301d4f17eb0ae0" }, { 119, "31eba51c313a5c08226adf18d4a359cfdfd8d2e816b13f4af952f7ea6584dcfb" },
		{ 120, "2f3d335432c70b580af0e8e1b3674a7c020d683aa5f73aaaedfdc55af904c21c" }, { 128, "6836cf13bac400e9105071cd6af47084dfacad4e5e302c94bfed24e013afb73e" },
		{ 1000, "41edece42d63e8d9bf515a9ba6932e1c20cbc9f5a5d134645adb5db1b9737ea3" } };
	for (auto& x : v) CHECK_EQ(sha::Hex(std::string(x.n, 'a')), std::string(x.h));
}

TEST(Pack_line_endings_do_not_change_a_hash_but_content_does) {
	CHECK_EQ(NormalizeEol("a\r\nb\r\n"), std::string("a\nb\n"));
	CHECK_EQ(NormalizeEol("a\rb"), std::string("a\rb"));                 // a lone CR is content
	CHECK_EQ(NormalizeEol("a\r"), std::string("a\r"));
	CHECK(sha::Hex(NormalizeEol("x\r\ny\r\n")) == sha::Hex(NormalizeEol("x\ny\n")));
	CHECK(sha::Hex(NormalizeEol("x\ny\n")) != sha::Hex(NormalizeEol("x\ny \n")));
}

TEST(Pack_quoted_includes_are_found_and_the_rest_ignored) {
	auto inc = QuotedIncludes("#include \"a.h\"\n  #  include \"sub/b.h\"\n#include <vector>\n// #include \"c.h\" is text\n#include\"d.h\"\n#define X \"e.h\"\n#include \"unterminated\n");
	CHECK_EQ(inc.size(), (size_t)3);
	CHECK_EQ(inc[0], std::string("a.h")); CHECK_EQ(inc[1], std::string("sub/b.h")); CHECK_EQ(inc[2], std::string("d.h"));
}

TEST(Pack_closure_follows_includes_and_notices_any_change) {
	World w; const fs::path r = w.root / "tree";
	fs::create_directories(r / "sub");
	WriteAll(r / "a.h", "#include \"b.h\"\n#include \"sub/c.h\"\nint a;\n");
	WriteAll(r / "b.h", "#include \"a.h\"\nint b;\n");                      // a cycle
	WriteAll(r / "sub/c.h", "#include \"d.h\"\n#include \"../e.h\"\nint c;\n");   // d.h next to c.h, e.h one level up
	WriteAll(r / "sub/d.h", "int d;\n"); WriteAll(r / "e.h", "int e;\n"); WriteAll(r / "unrelated.h", "int u;\n");
	SourceHash h = HashClosure(r, { "a.h" });
	CHECK_EQ(h.files.size(), (size_t)5);
	CHECK(h.files.count("a.h") && h.files.count("b.h") && h.files.count("sub/c.h") && h.files.count("sub/d.h") && h.files.count("e.h"));
	CHECK(!h.files.count("unrelated.h"));
	auto edited = [&](const std::string& file, const std::string& add) { WriteAll(r / file, ReadAll(r / file) + add); SourceHash x = HashClosure(r, { "a.h" }); return x.sha != h.sha; };
	CHECK(edited("sub/d.h", "// c\n"));                // a dependency of a dependency
	WriteAll(r / "sub/d.h", "int d;\n");
	CHECK(HashClosure(r, { "a.h" }).sha == h.sha);     // restoring restores the hash
	WriteAll(r / "unrelated.h", "int changed;\n");
	CHECK(HashClosure(r, { "a.h" }).sha == h.sha);     // an unreachable file does not matter
	WriteAll(r / "e.h", "int e;\r\n");                 // CRLF only
	CHECK(HashClosure(r, { "a.h" }).sha == h.sha);
	// a missing include is part of the hash, not silently dropped
	WriteAll(r / "b.h", "#include \"a.h\"\n#include \"gone.h\"\nint b;\n");
	SourceHash m = HashClosure(r, { "a.h" });
	CHECK(m.sha != h.sha); CHECK_EQ(m.files["gone.h"], std::string("MISSING"));
	// the order of the entries does not matter
	CHECK(HashClosure(r, { "a.h", "e.h" }).sha == HashClosure(r, { "e.h", "a.h" }).sha);
	// an include that only resolves from the source root (like -I does), not next to the including file
	fs::create_directories(r / "deep/er");
	WriteAll(r / "deep/er/f.h", "#include \"e.h\"\n");                  // e.h is at the root, two levels up from here, spelled root-relative
	SourceHash rootRel = HashClosure(r, { "deep/er/f.h" });
	CHECK(rootRel.files.count("e.h") == 1); CHECK(rootRel.files["e.h"] != "MISSING");
	// the same content under another file name is a different closure: the path is part of what is hashed
	WriteAll(r / "m.h", "int same;\n"); WriteAll(r / "n.h", "int same;\n");
	CHECK(HashClosure(r, { "m.h" }).sha != HashClosure(r, { "n.h" }).sha);
	// code outside the root (the engine) is not followed: changing it does not change the hash, and it is not listed
	fs::create_directories(w.root / "engine");
	WriteAll(w.root / "engine/eng.h", "int eng;\n"); WriteAll(r / "x.h", "#include \"../engine/eng.h\"\nint x;\n");
	SourceHash ex = HashClosure(r, { "x.h" });
	CHECK_EQ(ex.files.size(), (size_t)1);
	WriteAll(w.root / "engine/eng.h", "int eng_changed;\n");
	CHECK(HashClosure(r, { "x.h" }).sha == ex.sha);
}

TEST(Pack_every_registered_reward_has_a_header_that_contains_it) {
	for (auto& kv : RewardRegistry()) {
		auto it = RewardHeaders().find(kv.first);
		CHECK(it != RewardHeaders().end());
		if (it == RewardHeaders().end()) continue;
		std::string t; CHECK(ReadFileText(fs::path(SRC) / it->second, t));
		CHECK(t.find("class " + kv.first) != std::string::npos || t.find("struct " + kv.first) != std::string::npos);
	}
	for (auto& kv : RewardHeaders()) CHECK(RewardRegistry().count(kv.first) == 1);   // no header entry for an unregistered reward
}

// ---- the engine probe ---------------------------------------------------------------------------------------------------

TEST(Pack_engine_probe_is_repeatable_and_exercises_a_ball_hit) {
	std::vector<double> a, b; std::string err;
	CHECK(EngineProbe(a, err)); CHECK(EngineProbe(b, err));
	CHECK_EQ(a.size(), (size_t)(12 * 15));
	CHECK(a == b);
	// the ball starts at rest and is moving by the end: the car reached it
	double firstBallSpeed = 0, lastBallSpeed = 0;
	auto speed = [&](size_t s) { return std::sqrt(a[s * 15 + 12] * a[s * 15 + 12] + a[s * 15 + 13] * a[s * 15 + 13] + a[s * 15 + 14] * a[s * 15 + 14]); };
	firstBallSpeed = speed(0); lastBallSpeed = speed(11);
	CHECK(firstBallSpeed < 100.0); CHECK(lastBallSpeed > 500.0);
	// the car jumped: it was off the ground at some sample
	bool air = false; for (size_t s = 0; s < 12; s++) if (a[s * 15 + 2] > 60.0) air = true;
	CHECK(air);
}

// ---- signing ---------------------------------------------------------------------------------------------------------

TEST(Pack_sign_refuses_what_a_reviewer_could_not_have_reviewed) {
	World w; std::string err;
	CHECK(!SignPack(w.pack, w.src, "", "2026-11-02", err)); CHECK(err.find("reviewer") != std::string::npos);
	CHECK(!SignPack(w.pack, w.src, "R", "11/02/2026", err)); CHECK(err.find("YYYY-MM-DD") != std::string::npos);
	CHECK(!SignPack(w.root / "nonexistent", w.src, "R", "2026-11-02", err)); CHECK(err.find("no *.scenario.json") != std::string::npos);
	// a scenario with no expectations: nothing was asserted
	json j = json::parse(ReadAll(w.pack / "passing_setup_straight_hit.scenario.json")); j["expect"] = json::array();
	WriteAll(w.pack / "passing_setup_straight_hit.scenario.json", j.dump());
	CHECK(!w.Sign(err)); CHECK(err.find("no expectations") != std::string::npos);
	// a failing expectation
	j = json::parse(ReadAll(fs::path(CORE) / "passing_setup_straight_hit.scenario.json")); j["expect"][0]["count"] = json::array({ 5, 5 });
	WriteAll(w.pack / "passing_setup_straight_hit.scenario.json", j.dump());
	CHECK(!w.Sign(err)); CHECK(err.find("cannot be signed off") != std::string::npos);
	CHECK(!fs::exists(w.pack / "manifest.json"));          // nothing half-written
	// a scenario that does not parse
	WriteAll(w.pack / "passing_setup_straight_hit.scenario.json", "{\"schema\":1,\"bogus\":1}");
	CHECK(!w.Sign(err)); CHECK(err.find("unknown key") != std::string::npos);
}

TEST(Pack_signed_pack_is_current_and_records_who_and_when) {
	World w; std::string err;
	CHECK_EQ(w.Check().verdict, std::string("UNSIGNED"));
	CHECK(w.Sign(err));
	json m = w.Manifest();
	CHECK_EQ(m["reviewer"].get<std::string>(), std::string("Test Reviewer")); CHECK_EQ(m["date"].get<std::string>(), std::string("2026-11-02"));
	CHECK_EQ(m["scenarios"].size(), (size_t)2); CHECK_EQ(m["rewards"].size(), (size_t)2);
	CHECK_EQ(m["engine"]["probe"].size(), (size_t)180);
	for (auto& s : m["scenarios"]) { CHECK_EQ(s["result"].get<std::string>(), std::string("PASS")); CHECK(s["sha256"].get<std::string>().size() == 64); }
	PackStatus st = w.Check();
	CHECK_EQ(st.verdict, std::string("CURRENT")); CHECK(st.reasons.empty()); CHECK(st.warnings.empty());
	CHECK_EQ(st.reviewer, std::string("Test Reviewer"));
	const std::string text = PackStatusText(st);
	CHECK(text.find("may be called verified") != std::string::npos); CHECK(text.find("Test Reviewer") != std::string::npos);
}

// ---- stale detection --------------------------------------------------------------------------------------------------

TEST(Pack_editing_a_scenario_file_makes_the_pack_stale) {
	World w; std::string err; CHECK(w.Sign(err));
	Replace(w.pack / "offside_goalside_car.scenario.json", "\"intent\": \"", "\"intent\": \"(reworded) ");
	PackStatus st = w.Check();
	CHECK_EQ(st.verdict, std::string("STALE")); CHECK(Has(st, "offside_goalside_car.scenario.json was edited"));
	CHECK(!Has(st, "passing_setup"));                    // only the edited file is named
	CHECK(PackStatusText(st).find("not verified") != std::string::npos);
}

TEST(Pack_adding_or_removing_a_scenario_makes_the_pack_stale) {
	World w; std::string err; CHECK(w.Sign(err));
	fs::copy_file(fs::path(CORE) / "offside_lone_upfield_car.scenario.json", w.pack / "offside_lone_upfield_car.scenario.json");
	PackStatus st = w.Check();
	CHECK_EQ(st.verdict, std::string("STALE")); CHECK(Has(st, "offside_lone_upfield_car.scenario.json was added"));
	fs::remove(w.pack / "offside_lone_upfield_car.scenario.json");
	CHECK_EQ(w.Check().verdict, std::string("CURRENT"));
	fs::remove(w.pack / "offside_goalside_car.scenario.json");
	st = w.Check();
	CHECK_EQ(st.verdict, std::string("STALE")); CHECK(Has(st, "offside_goalside_car.scenario.json was signed off but is gone"));
}

TEST(Pack_editing_a_reward_source_names_the_reward_and_the_file) {
	World w; std::string err; CHECK(w.Sign(err));
	// a COMMENT edit in the reward's own header: counts on purpose
	WriteAll(w.src / "Rewards/OffsidePenalty.h", ReadAll(w.src / "Rewards/OffsidePenalty.h") + "\n// a comment\n");
	PackStatus st = w.Check();
	CHECK_EQ(st.verdict, std::string("STALE"));
	CHECK(Has(st, "reward OffsidePenalty changed after sign-off: Rewards/OffsidePenalty.h")); CHECK(!Has(st, "reward PassingSetupReward"));
}

TEST(Pack_editing_a_dependency_of_a_reward_makes_it_stale) {
	World w; std::string err; CHECK(w.Sign(err));
	WriteAll(w.src / "Rewards/RewardTiming.h", ReadAll(w.src / "Rewards/RewardTiming.h") + "\n// edited\n");   // included by PassingSetupRewards.h, not by OffsidePenalty.h
	PackStatus st = w.Check();
	CHECK_EQ(st.verdict, std::string("STALE"));
	CHECK(Has(st, "reward PassingSetupReward changed after sign-off")); CHECK(Has(st, "Rewards/RewardTiming.h"));
	CHECK(!Has(st, "reward OffsidePenalty"));
}

TEST(Pack_an_unrelated_edit_or_line_endings_do_not_stale_the_pack) {
	World w; std::string err; CHECK(w.Sign(err));
	WriteAll(w.src / "Rewards/GoalSpeedReward.h", "// nothing reachable from the two rewards\n");     // exists in the tree; not in either closure
	WriteAll(w.src / "Rewards/NoSuchRewardFile.h", "int x;\n");
	CHECK_EQ(w.Check().verdict, std::string("CURRENT"));
	// the same header saved with Windows line endings
	std::string t = ReadAll(w.src / "Rewards/OffsidePenalty.h"), crlf;
	for (char c : NormalizeEol(t)) { if (c == '\n') crlf += '\r'; crlf += c; }
	WriteAll(w.src / "Rewards/OffsidePenalty.h", crlf);
	CHECK(crlf != t);
	CHECK_EQ(w.Check().verdict, std::string("CURRENT"));
	// the scenario file likewise
	t = ReadAll(w.pack / "offside_goalside_car.scenario.json"); crlf.clear();
	for (char c : NormalizeEol(t)) { if (c == '\n') crlf += '\r'; crlf += c; }
	WriteAll(w.pack / "offside_goalside_car.scenario.json", crlf);
	CHECK_EQ(w.Check().verdict, std::string("CURRENT"));
}

TEST(Pack_zero_sum_wrapper_is_part_of_a_scenario_that_uses_it) {
	World w; std::string err;
	json j = json::parse(ReadAll(w.pack / "passing_setup_straight_hit.scenario.json"));
	j["rewards"][0]["zero_sum"] = 0.3; j["expect"] = json::array({ { { "kind", "event" }, { "reward", "PassingSetupReward" }, { "car", 0 }, { "event", "paid" }, { "count", json::array({ 1, 1 }) }, { "intent", "one payment" } } });
	WriteAll(w.pack / "passing_setup_straight_hit.scenario.json", j.dump());
	if (!w.Sign(err)) { std::cerr << err << "\n"; }
	CHECK(fs::exists(w.pack / "manifest.json"));
	json m = w.Manifest(); bool hasZs = false;
	for (auto& r : m["rewards"]) if (r["name"] == "PassingSetupReward") hasZs = r["files"].contains("Rewards/ZeroSumReward.h") && r["files"].contains("Rewards/ZeroSumReward.cpp");
	CHECK(hasZs);
	CHECK_EQ(w.Check().verdict, std::string("CURRENT"));
	WriteAll(w.src / "Rewards/ZeroSumReward.cpp", ReadAll(w.src / "Rewards/ZeroSumReward.cpp") + "\n// edit\n");
	PackStatus st = w.Check();
	CHECK_EQ(st.verdict, std::string("STALE")); CHECK(Has(st, "ZeroSumReward.cpp"));
}

TEST(Pack_a_scenario_that_stops_passing_is_stale_even_if_its_hash_was_refreshed) {
	World w; std::string err; CHECK(w.Sign(err));
	// someone edits an expectation so it fails AND rewrites the manifest hash to match: only the re-run catches it
	json j = json::parse(ReadAll(w.pack / "passing_setup_straight_hit.scenario.json")); j["expect"][0]["count"] = json::array({ 5, 5 });
	WriteAll(w.pack / "passing_setup_straight_hit.scenario.json", j.dump());
	json d; std::vector<std::string> errs; CHECK(Describe(w.pack, w.src, d, errs));
	json m = w.Manifest();
	for (auto& s : m["scenarios"]) for (auto& n : d["scenarios"]) if (s["file"] == n["file"]) s["sha256"] = n["sha256"];
	w.SetManifest(m);
	PackStatus st = w.Check();
	CHECK_EQ(st.verdict, std::string("STALE")); CHECK(Has(st, "passing_setup_straight_hit.scenario.json: no longer passes"));
	CHECK(CheckPack(w.pack, w.src, /*rerun=*/false).verdict == "CURRENT");   // the cheap check alone would have said CURRENT: that is why it re-runs by default
}

// ---- engine ------------------------------------------------------------------------------------------------------------

TEST(Pack_a_different_engine_is_refused_not_rerun) {
	World w; std::string err; CHECK(w.Sign(err));
	json m = w.Manifest();
	m["engine"]["probe"][7] = m["engine"]["probe"][7].get<double>() + 0.5;
	w.SetManifest(m);
	PackStatus st = w.Check();
	CHECK_EQ(st.verdict, std::string("ENGINE_MISMATCH"));
	CHECK(Has(st, "behaves differently")); CHECK(Has(st, "probe sample 7")); CHECK(Has(st, "sign it off again"));
	// scenarios were not re-run: even a failing scenario does not get reported in this verdict
	json j = json::parse(ReadAll(w.pack / "passing_setup_straight_hit.scenario.json")); j["expect"][0]["count"] = json::array({ 5, 5 });
	WriteAll(w.pack / "passing_setup_straight_hit.scenario.json", j.dump());
	st = w.Check();
	CHECK_EQ(st.verdict, std::string("ENGINE_MISMATCH")); CHECK(!Has(st, "no longer passes"));
	// noise below the tolerance is not a different engine
	m["engine"]["probe"][7] = m["engine"]["probe"][7].get<double>() - 0.5 + 0.004;
	fs::copy_file(fs::path(CORE) / "passing_setup_straight_hit.scenario.json", w.pack / "passing_setup_straight_hit.scenario.json", fs::copy_options::overwrite_existing);
	w.SetManifest(m);
	CHECK_EQ(w.Check().verdict, std::string("CURRENT"));
	// a probe of a different length is a different engine too
	m["engine"]["probe"].erase(m["engine"]["probe"].end() - 1); w.SetManifest(m);
	st = w.Check(); CHECK_EQ(st.verdict, std::string("ENGINE_MISMATCH")); CHECK(Has(st, "samples"));
}

TEST(Pack_engine_tolerance_edge_is_inclusive_below_and_refused_above) {
	World w; std::string err; CHECK(w.Sign(err));
	json m = w.Manifest(); const double base = m["engine"]["probe"][3].get<double>();
	m["engine"]["probe"][3] = base + ENGINE_TOLERANCE * 0.9; w.SetManifest(m); CHECK_EQ(w.Check().verdict, std::string("CURRENT"));
	m["engine"]["probe"][3] = base + ENGINE_TOLERANCE * 1.5; w.SetManifest(m); CHECK_EQ(w.Check().verdict, std::string("ENGINE_MISMATCH"));
	m["engine"]["probe"][3] = base - ENGINE_TOLERANCE * 1.5; w.SetManifest(m); CHECK_EQ(w.Check().verdict, std::string("ENGINE_MISMATCH"));   // either direction
}

TEST(Pack_harness_change_only_warns) {
	World w; std::string err; CHECK(w.Sign(err));
	json m = w.Manifest(); m["harness_sha256"] = std::string(64, '0'); w.SetManifest(m);
	PackStatus st = w.Check();
	CHECK_EQ(st.verdict, std::string("CURRENT")); CHECK_EQ(st.warnings.size(), (size_t)1); CHECK(st.warnings[0].find("Lab harness") != std::string::npos);
	CHECK(PackStatusText(st).find("! the Lab harness") != std::string::npos);
}

TEST(Pack_the_harness_warning_follows_the_lab_files_not_the_rewards) {
	World w; std::string err; CHECK(w.Sign(err));
	WriteAll(w.src / "Rewards/OffsidePenalty.h", ReadAll(w.src / "Rewards/OffsidePenalty.h") + "\n// edit\n");
	PackStatus st = w.Check();
	CHECK_EQ(st.verdict, std::string("STALE")); CHECK(st.warnings.empty());          // a reward edit is a stale reward, not also a harness change
	World v; CHECK(v.Sign(err));
	WriteAll(v.src / "Lab/Expectations.h", ReadAll(v.src / "Lab/Expectations.h") + "\n// edit\n");
	st = v.Check();
	CHECK_EQ(st.verdict, std::string("CURRENT")); CHECK_EQ(st.warnings.size(), (size_t)1);   // a Lab edit is only a warning
	WriteAll(v.src / "Rewards/RewardTraceStack.h", ReadAll(v.src / "Rewards/RewardTraceStack.h") + "\n// edit\n");
	CHECK(v.Check().warnings.size() == 1);
}

// ---- the manifest itself ------------------------------------------------------------------------------------------------

TEST(Pack_bad_manifests_are_invalid_with_a_reason) {
	World w; std::string err; CHECK(w.Sign(err));
	const json good = w.Manifest();
	auto verdictOf = [&](const json& m, std::string& why) { w.SetManifest(m); PackStatus st = w.Check(); why = Reasons(st); return st.verdict; };
	std::string why;
	WriteAll(w.pack / "manifest.json", "{not json"); PackStatus st = w.Check();
	CHECK_EQ(st.verdict, std::string("INVALID")); CHECK(Has(st, "not JSON"));
	json m = good; m["schema"] = 2; CHECK_EQ(verdictOf(m, why), std::string("INVALID")); CHECK(why.find("schema") != std::string::npos);
	m = good; m.erase("schema"); CHECK_EQ(verdictOf(m, why), std::string("INVALID"));
	m = good; m["kind"] = "scenario"; CHECK_EQ(verdictOf(m, why), std::string("INVALID")); CHECK(why.find("kind") != std::string::npos);
	for (const char* k : { "reviewer", "date", "engine", "scenarios", "rewards", "harness_sha256" }) { m = good; m.erase(k); CHECK_EQ(verdictOf(m, why), std::string("INVALID")); CHECK(why.find(k) != std::string::npos); }
	m = good; m["engine"].erase("probe"); CHECK_EQ(verdictOf(m, why), std::string("INVALID"));
	m = good; m["engine"]["probe"][0] = "x"; CHECK_EQ(verdictOf(m, why), std::string("INVALID"));
	m = good; m["scenarios"] = "x"; CHECK_EQ(verdictOf(m, why), std::string("INVALID"));
	m = good; m["scenarios"][0].erase("sha256"); CHECK_EQ(verdictOf(m, why), std::string("INVALID"));
	m = good; m["rewards"][0].erase("files"); CHECK_EQ(verdictOf(m, why), std::string("INVALID"));
	m = json::array(); CHECK_EQ(verdictOf(m, why), std::string("INVALID"));
	w.SetManifest(good); CHECK_EQ(w.Check().verdict, std::string("CURRENT"));      // the fixture itself was fine
}

TEST(Pack_a_reward_used_but_not_signed_off_is_stale) {
	World w; std::string err; CHECK(w.Sign(err));
	json m = w.Manifest(); json keep = json::array();
	for (auto& r : m["rewards"]) if (r["name"] != "PassingSetupReward") keep.push_back(r);
	m["rewards"] = keep; w.SetManifest(m);
	PackStatus st = w.Check();
	CHECK_EQ(st.verdict, std::string("STALE")); CHECK(Has(st, "reward PassingSetupReward is used by a scenario but was not signed off"));
}

TEST(Pack_the_core_pack_can_be_described_with_every_reward_it_uses_known) {
	// (whether it is signed is the user's call and not asserted here)
	json d; std::vector<std::string> errs;
	CHECK(Describe(CORE, SRC, d, errs)); CHECK(errs.empty());
	CHECK(d["scenarios"].size() >= 10); CHECK(d["rewards"].size() >= 3);
	for (auto& r : d["rewards"]) for (auto it = r["files"].begin(); it != r["files"].end(); ++it) CHECK(it.value() != "MISSING");
}

TEST(Pack_engine_probe_is_sensitive_to_small_changes_well_above_the_tolerance) {
	// A probe that cannot tell a slightly different world from this one would pass a changed engine. Perturb the START and look at the samples.
	std::vector<double> base, moved, boosted;  /* boosted = ball moved */ std::string err;
	CHECK(EngineProbe(base, err));
	Scenario a = ProbeScenario(); a.cars[0].pos.x += 0.5f;
	Scenario b = ProbeScenario(); b.ball.pos.x += 5.0f;
	CHECK(EngineProbe(moved, err, &a)); CHECK(EngineProbe(boosted, err, &b));
	auto worst = [&](const std::vector<double>& x) { double w = 0; for (size_t i = 0; i < x.size(); i++) w = std::max(w, std::fabs(x[i] - base[i])); return w; };
	CHECK(worst(moved) > 10 * ENGINE_TOLERANCE);       // half a unit at the start becomes far more than the tolerance by the end
	CHECK(worst(boosted) > 10 * ENGINE_TOLERANCE);     // the ball 5 uu to the side changes the hit
	// and the probe does not move when nothing changed
	std::vector<double> again; CHECK(EngineProbe(again, err, nullptr)); CHECK(again == base);
}
