// L1 / patch P0-7: per-step bump/bumped/demo/demoed event flags from the car-car resolver.
// Semantics must match RLGymCPP's _BumpCallback: cross-team only; a demo also counts as a bump; flags accumulate over
// the ticks of one agent step and are cleared by ClearStepEvents().
#include "GpuTestUtil.h"
using namespace rocketsim_cuda;
using namespace gputest;

struct Flags { uint8_t bump, bumped, demo, demoed; };

static Flags ReadFlags(SimContext& ctx, uint32_t car) {
	Sync();
	Flags f{};
	const CarStateSoA& s = ctx.GetCarState();
	CudaOk(cudaMemcpy(&f.bump, s.step_bump + car, 1, cudaMemcpyDeviceToHost), "bump");
	CudaOk(cudaMemcpy(&f.bumped, s.step_bumped + car, 1, cudaMemcpyDeviceToHost), "bumped");
	CudaOk(cudaMemcpy(&f.demo, s.step_demo + car, 1, cudaMemcpyDeviceToHost), "demo");
	CudaOk(cudaMemcpy(&f.demoed, s.step_demoed + car, 1, cudaMemcpyDeviceToHost), "demoed");
	return f;
}

// Car A drives into stationary car B along +x, far from the ball, in the blue half.
static void SetupHeadOn(SimContext& ctx, float speed, bool supersonicFlag, int teamA, int teamB) {
	BallStatePOD ball; ball.pos = Vec3(3000.f, 4000.f, 93.15f);
	ctx.CopyBallStateToDevice(&ball, 0, 1);
	CarStatePOD a, b;
	a.pos = Vec3(-900.f, -3000.f, 17.f); a.vel = Vec3(speed, 0.f, 0.f); a.is_supersonic = supersonicFlag ? 1 : 0; a.team = (uint8_t)teamA;
	b.pos = Vec3(-200.f, -3000.f, 17.f); b.vel = Vec3(0.f, 0.f, 0.f); b.team = (uint8_t)teamB;
	CarStatePOD both[2] = { a, b };
	ctx.CopyCarStateToDevice(both, 0, 2);
	Sync();
}

static void Run(SimContext& ctx, int ticks) { for (int i = 0; i < ticks; i++) ctx.Step(1); Sync(); }

TEST(SupersonicHeadOnCrossTeamIsADemoAndABump) {
	SimContext ctx(1, 2);
	ctx.SetTeamLayout(0x2u); // slot 0 blue, slot 1 orange
	SetupHeadOn(ctx, 2300.f, true, 0, 1);
	ctx.ClearStepEvents();
	Run(ctx, 50);
	auto cars = DownloadCars(ctx);
	CHECK(cars[1].is_demoed == 1);
	Flags a = ReadFlags(ctx, 0), b = ReadFlags(ctx, 1);
	CHECK_EQ((int)a.bump, 1);  CHECK_EQ((int)a.demo, 1);  CHECK_EQ((int)a.bumped, 0); CHECK_EQ((int)a.demoed, 0);
	CHECK_EQ((int)b.bumped, 1); CHECK_EQ((int)b.demoed, 1); CHECK_EQ((int)b.bump, 0);  CHECK_EQ((int)b.demo, 0);
}

TEST(FlagsAccumulateUntilCleared) {
	SimContext ctx(1, 2);
	ctx.SetTeamLayout(0x2u);
	SetupHeadOn(ctx, 2300.f, true, 0, 1);
	ctx.ClearStepEvents();
	Run(ctx, 20);
	Run(ctx, 60); // many ticks after the contact: the step flags must still be set (no per-tick clearing)
	CHECK_EQ((int)ReadFlags(ctx, 0).demo, 1);
	ctx.ClearStepEvents();
	Sync();
	Flags a = ReadFlags(ctx, 0), b = ReadFlags(ctx, 1);
	CHECK_EQ((int)(a.bump | a.bumped | a.demo | a.demoed), 0);
	CHECK_EQ((int)(b.bump | b.bumped | b.demo | b.demoed), 0);
}

TEST(SameTeamContactSetsNoFlags) {
	SimContext ctx(1, 2);
	ctx.SetTeamLayout(0x0u); // both blue
	SetupHeadOn(ctx, 2300.f, true, 0, 0);
	ctx.ClearStepEvents();
	Run(ctx, 50);
	for (uint32_t c = 0; c < 2; c++) {
		Flags f = ReadFlags(ctx, c);
		CHECK_EQ((int)(f.bump | f.bumped | f.demo | f.demoed), 0);
	}
}

// Property: over a speed sweep, demoed-flag <=> the victim's is_demoed went 0 -> 1, demo => bump, demoed => bumped.
TEST(DemoFlagsAgreeWithDemolitionStateAcrossSpeeds) {
	int demos = 0, bumpsOnly = 0;
	for (float speed : { 300.f, 600.f, 900.f, 1200.f, 1600.f, 2000.f, 2200.f, 2300.f }) {
		for (int sup = 0; sup < 2; sup++) {
			SimContext ctx(1, 2);
			ctx.SetTeamLayout(0x2u);
			SetupHeadOn(ctx, speed, sup != 0, 0, 1);
			ctx.ClearStepEvents();
			Run(ctx, 60);
			auto cars = DownloadCars(ctx);
			Flags a = ReadFlags(ctx, 0), b = ReadFlags(ctx, 1);
			CHECK_EQ((int)b.demoed, (int)cars[1].is_demoed);
			CHECK_EQ((int)a.demo, (int)b.demoed);
			if (a.demo) { CHECK_EQ((int)a.bump, 1); CHECK_EQ((int)b.bumped, 1); demos++; }
			else if (a.bump) { CHECK_EQ((int)b.bumped, 1); bumpsOnly++; }
			CHECK_EQ((int)cars[0].is_demoed, 0);
		}
	}
	std::cout << "  [info] demos=" << demos << " bump-only=" << bumpsOnly << " of 16 setups" << std::endl;
	CHECK(demos >= 1); // the sweep must actually exercise the demo path
}
