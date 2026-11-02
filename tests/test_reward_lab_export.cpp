#include "TestFramework.h"

#include <RLGymCPP/Lab/LabExport.h>

#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>

using namespace RLGC;
using namespace RLGC::Lab;
namespace fs = std::filesystem;

// P2 of the Reward Lab: the exported run and the self-contained page. Run from the repo root (collision meshes, the viewer template).

namespace {
	const char* TEMPLATE = "tools/reward_lab/viewer/viewer.template.html";

	Scenario LoadPack(const std::string& name) {
		std::ifstream f("tools/reward_lab/packs/core/" + name + ".scenario.json"); std::stringstream ss; ss << f.rdbuf();
		Scenario s; std::vector<std::string> errs;
		if (!ParseScenarioText(ss.str(), s, errs)) throw TestFailure{ "pack scenario " + name + " does not parse" };
		return s;
	}

	std::string ReadFile(const fs::path& p) { std::ifstream f(p); std::stringstream ss; ss << f.rdbuf(); return ss.str(); }

	// The JSON the page embeds, pulled back out of the page text.
	nlohmann::json EmbeddedRun(const std::string& html) {
		const std::string open = "<script type=\"application/json\" id=\"labdata\">";
		size_t a = html.find(open); if (a == std::string::npos) throw TestFailure{ "no labdata block" };
		a += open.size();
		size_t b = html.find("</script>", a); if (b == std::string::npos) throw TestFailure{ "labdata block not closed" };
		return nlohmann::json::parse(html.substr(a, b - a));
	}

	size_t Count(const std::string& s, const std::string& needle) { size_t n = 0, p = 0; while ((p = s.find(needle, p)) != std::string::npos) { n++; p += needle.size(); } return n; }
}

TEST(Export_run_json_has_the_documented_shape) {
	LabSession x(LoadPack("passing_setup_straight_hit")); CHECK(x.ok()); x.RunScripted();
	nlohmann::json j = RunToJson(x);
	CHECK_EQ(j["schema"].get<int>(), 1); CHECK_EQ(j["kind"].get<std::string>(), std::string("lab_run")); CHECK_EQ(j["result"].get<std::string>(), std::string("PASS"));
	CHECK_EQ(j["frames"].size(), (size_t)41);
	CHECK(j["frames"][0]["trace"].is_null() && !j["frames"][1]["trace"].is_null());
	CHECK_EQ(j["frames"][0]["actions"].size(), (size_t)0); CHECK_EQ(j["frames"][5]["actions"].size(), (size_t)4);
	CHECK_EQ(j["frames"][5]["actions"][0].size(), (size_t)8);
	const auto& p = j["frames"][5]["players"][0];
	for (const char* k : { "car", "team", "pos", "vel", "forward", "up", "boost", "on_ground", "flipping", "jumped", "double_jumped", "flipped", "demoed", "touched" }) CHECK(p.contains(k));
	CHECK_EQ(j["frames"][5]["players"].size(), (size_t)4);
	CHECK(j["frames"][5]["players"][0]["team"].get<int>() == 0 && j["frames"][5]["players"][1]["team"].get<int>() == 0 &&
	      j["frames"][5]["players"][2]["team"].get<int>() == 1 && j["frames"][5]["players"][3]["team"].get<int>() == 1);   // 0 = blue, 1 = orange, in scenario order
	CHECK(j["frames"][5]["players"][0]["car"].get<uint32_t>() == x.Frames()[5].players[0].carId);
	CHECK_EQ(j["scenario"]["name"].get<std::string>(), std::string("passing_setup_straight_hit"));
	CHECK_EQ(j["expectations"].size(), (size_t)5);
	for (auto& e : j["expectations"]) { CHECK(e["ok"].get<bool>()); CHECK(e["to"].get<int>() <= 40); CHECK(!e["intent"].get<std::string>().empty()); CHECK(!e["detail"].get<std::string>().empty()); }
	// the run's own frames are the ones the runner measured
	CHECK_EQ(j["frames"][10]["players"][0]["pos"][1].get<float>(), x.Frames()[10].players[0].pos.y);
}

TEST(Export_result_follows_the_expectations) {
	Scenario s = LoadPack("offside_goalside_car");
	{ LabSession x(s); x.RunScripted(); CHECK_EQ(RunToJson(x)["result"].get<std::string>(), std::string("PASS")); }
	s.expect[0].min = 1; s.expect[0].max = 2;                              // wrong on purpose
	{ LabSession x(s); x.RunScripted(); nlohmann::json j = RunToJson(x); CHECK_EQ(j["result"].get<std::string>(), std::string("FAIL")); CHECK(!j["expectations"][0]["ok"].get<bool>()); }
	s.expect.clear();
	{ LabSession x(s); x.RunScripted(); CHECK_EQ(RunToJson(x)["result"].get<std::string>(), std::string("NONE")); }
}

TEST(Export_the_embedded_trace_is_exactly_the_jsonl_line_for_that_step) {
	LabSession x(LoadPack("offside_2v2_surplus")); CHECK(x.ok()); x.RunScripted();
	std::ostringstream os; WriteTraceJsonl(x, os);
	std::istringstream is(os.str()); std::string line; int n = 0;
	nlohmann::json run = RunToJson(x);
	while (std::getline(is, line)) {
		nlohmann::json jl = nlohmann::json::parse(line);
		const int frame = jl["frame"].get<int>(); CHECK_EQ(frame, ++n);
		jl.erase("frame"); jl.erase("terminal");                           // the JSONL adds these two keys; the rest must be identical
		CHECK(jl == run["frames"][frame]["trace"]);
	}
	CHECK_EQ(n, x.Steps());
}

TEST(Export_page_embeds_the_run_and_resolves_every_marker) {
	LabSession x(LoadPack("energy_sprint_from_rest")); CHECK(x.ok()); x.RunScripted();
	std::string html, err;
	CHECK(ExportHtml(x, TEMPLATE, html, err)); CHECK(err.empty());
	CHECK(html.find("<!--INLINE:") == std::string::npos && html.find("<!--LAB_RUN_JSON-->") == std::string::npos);
	CHECK(html.find("const LabCore") != std::string::npos);                   // viewer_core.js inlined
	CHECK(html.find("window.LAB") != std::string::npos);                      // viewer_ui.js inlined
	CHECK(html.find("--bg:") != std::string::npos);                           // viewer.css inlined
	CHECK(html.find("src=\"http") == std::string::npos && html.find("href=\"http") == std::string::npos);   // no network dependency
	CHECK(EmbeddedRun(html) == RunToJson(x));                                 // what the page holds is the run, exactly
}

TEST(Export_text_in_the_data_cannot_break_out_of_the_script_block) {
	Scenario s = LoadPack("offside_goalside_car");
	s.description = "</script><script>window.pwned=1</script><!-- x";
	s.expect[0].intent = "a </script> tag in an intent line";
	LabSession x(s); CHECK(x.ok()); x.RunScripted();
	std::string html, err; CHECK(ExportHtml(x, TEMPLATE, html, err));
	// the number of closing script tags is the page's own (3 script blocks), not one more per injected string
	std::string plain; { LabSession y(LoadPack("offside_goalside_car")); y.RunScripted(); CHECK(ExportHtml(y, TEMPLATE, plain, err)); }
	CHECK_EQ(Count(html, "</script>"), Count(plain, "</script>"));
	CHECK(html.find("window.pwned") != std::string::npos);                    // the text is still there, as data
	nlohmann::json emb = EmbeddedRun(html);
	CHECK_EQ(emb["scenario"]["description"].get<std::string>(), s.description);   // and round-trips unchanged
	CHECK_EQ(emb["expectations"][0]["intent"].get<std::string>(), s.expect[0].intent);
	CHECK(JsonForScript(nlohmann::json{ { "a", "<b>" } }).find('<') == std::string::npos);
}

TEST(Export_template_errors_are_reported_not_ignored) {
	LabSession x(LoadPack("offside_goalside_car")); CHECK(x.ok()); x.RunScripted();
	const fs::path dir = fs::temp_directory_path() / "lab_export_test"; fs::create_directories(dir);
	std::string html, err;
	CHECK(!ExportHtml(x, dir / "missing.html", html, err) && err.find("cannot read viewer template") != std::string::npos);
	{ std::ofstream(dir / "a.html") << "<html><!--INLINE:nope.js--></html>"; }
	err.clear(); CHECK(!ExportHtml(x, dir / "a.html", html, err) && err.find("cannot read inline file") != std::string::npos);
	{ std::ofstream(dir / "b.html") << "<html>no run marker</html>"; }
	err.clear(); CHECK(!ExportHtml(x, dir / "b.html", html, err) && err.find("no <!--LAB_RUN_JSON--> marker") != std::string::npos);
	{ std::ofstream(dir / "c.html") << "<html><!--INLINE:broken"; }
	err.clear(); CHECK(!ExportHtml(x, dir / "c.html", html, err) && err.find("unterminated") != std::string::npos);
	{ std::ofstream(dir / "part.txt") << "PART"; std::ofstream(dir / "d.html") << "[<!--INLINE:part.txt-->|<!--INLINE:part.txt-->]<!--LAB_RUN_JSON-->"; }
	err.clear(); CHECK(ExportHtml(x, dir / "d.html", html, err)); CHECK(html.rfind("[PART|PART]{", 0) == 0);   // two markers, both replaced, then the run
	// an inlined file may MENTION the marker text (a comment, documentation): it is data, not another include
	{ std::ofstream(dir / "lit.txt") << "see <!--INLINE:lit.txt--> here"; std::ofstream(dir / "e.html") << "<!--INLINE:lit.txt--><!--LAB_RUN_JSON-->"; }
	err.clear(); CHECK(ExportHtml(x, dir / "e.html", html, err)); CHECK(html.rfind("see <!--INLINE:lit.txt--> here{", 0) == 0);
	fs::remove_all(dir);
}

TEST(Export_every_pack_scenario_exports_a_page) {
	int n = 0;
	for (auto& e : fs::directory_iterator("tools/reward_lab/packs/core")) {
		if (e.path().string().find(".scenario.json") == std::string::npos) continue;
		std::stringstream ss; ss << std::ifstream(e.path()).rdbuf();
		Scenario s; std::vector<std::string> errs; CHECK(ParseScenarioText(ss.str(), s, errs));
		LabSession x(s); CHECK(x.ok()); x.RunScripted();
		std::string html, err; CHECK(ExportHtml(x, TEMPLATE, html, err));
		CHECK(EmbeddedRun(html)["frames"].size() == x.Frames().size());
		n++;
	}
	CHECK(n >= 10);
}
