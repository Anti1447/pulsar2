// L6 / patch P0-6: SimContext::SetTeamLayout. Team of car slot c is bit c of the mask (0 blue, 1 orange).
#include "GpuTestUtil.h"
using namespace rocketsim_cuda;
using namespace gputest;

static uint32_t MaskFor(const std::vector<int>& teams) {
	uint32_t m = 0;
	for (size_t i = 0; i < teams.size(); i++) m |= (uint32_t)teams[i] << i;
	return m;
}

// Default layout must equal upstream's c % 2 alternation for every car count we use (2..6).
TEST(DefaultLayoutMatchesUpstreamAlternation) {
	for (uint32_t cars = 2; cars <= 6; cars++) {
		SimContext ctx(8, cars);
		ctx.ResetToDefault();
		auto c = DownloadCars(ctx);
		for (uint32_t e = 0; e < 8; e++)
			for (uint32_t s = 0; s < cars; s++)
				CHECK_EQ((int)c[e * cars + s].team, (int)(s % 2));
	}
}

struct Layout { const char* name; std::vector<int> teams; };
static const std::vector<Layout>& Layouts() {
	static const std::vector<Layout> l = {
		{ "1v1", { 0, 1 } },
		{ "2v2", { 0, 0, 1, 1 } },
		{ "2v1", { 0, 0, 1 } },
		{ "3v3", { 0, 0, 0, 1, 1, 1 } },
		{ "3v2", { 0, 0, 0, 1, 1 } },
		{ "3v1", { 0, 0, 0, 1 } },
	};
	return l;
}

TEST(SetTeamLayoutAssignsEveryEnvAndSlot) {
	for (const Layout& L : Layouts()) {
		const uint32_t cars = (uint32_t)L.teams.size();
		SimContext ctx(16, cars);
		ctx.SetTeamLayout(MaskFor(L.teams));
		auto c = DownloadCars(ctx);
		for (uint32_t e = 0; e < 16; e++)
			for (uint32_t s = 0; s < cars; s++)
				CHECK_EQ((int)c[e * cars + s].team, L.teams[s]);
		CHECK_EQ(ctx.GetTeamLayout(), MaskFor(L.teams));
	}
}

// SetTeamLayout writes the team field only: positions and everything else stay byte-identical.
TEST(SetTeamLayoutLeavesOtherStateUntouched) {
	SimContext ctx(8, 4);
	ctx.ResetToDefault();
	auto before = DownloadCars(ctx);
	ctx.SetTeamLayout(MaskFor({ 0, 0, 0, 1 }));
	auto after = DownloadCars(ctx);
	for (size_t i = 0; i < before.size(); i++) {
		CHECK_EQ(before[i].pos.x, after[i].pos.x);
		CHECK_EQ(before[i].pos.y, after[i].pos.y);
		CHECK_EQ(before[i].boost, after[i].boost);
	}
}

// The layout survives resets, and a reset gives same-team cars DISTINCT kickoff spawn slots (no stacked cars).
TEST(LayoutSurvivesResetAndSpawnsAreDistinct) {
	for (const Layout& L : Layouts()) {
		const uint32_t cars = (uint32_t)L.teams.size();
		SimContext ctx(32, cars);
		ctx.SetTeamLayout(MaskFor(L.teams));
		ctx.ResetToDefault();
		auto c = DownloadCars(ctx);
		for (uint32_t e = 0; e < 32; e++) {
			for (uint32_t s = 0; s < cars; s++) {
				CHECK_EQ((int)c[e * cars + s].team, L.teams[s]);
				for (uint32_t t = s + 1; t < cars; t++) {
					const auto& a = c[e * cars + s].pos; const auto& b = c[e * cars + t].pos;
					const float d2 = (a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y);
					CHECK(d2 > 1.0f); // distinct spawn points
				}
			}
		}
	}
}
