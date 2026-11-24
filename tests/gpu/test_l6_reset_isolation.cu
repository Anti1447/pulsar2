// L6: indexed and masked resets must leave every non-reset arena bit-identical, and the layout/teams must survive.
#include "GpuTestUtil.h"
#include <cstring>
using namespace rocketsim_cuda;
using namespace gputest;

static bool SameCar(const CarStatePOD& a, const CarStatePOD& b) { return std::memcmp(&a, &b, sizeof(CarStatePOD)) == 0; }
static bool SameBall(const BallStatePOD& a, const BallStatePOD& b) { return std::memcmp(&a, &b, sizeof(BallStatePOD)) == 0; }

static void RunSomeTicks(SimContext& ctx, int ticks) {
	for (int i = 0; i < ticks; i++) ctx.Step(ctx.GetNumEnvs());
	Sync();
}

TEST(MaskedResetLeavesOtherArenasBitIdentical) {
	const uint32_t N = 64, cars = 4;
	SimContext ctx(N, cars);
	ctx.SetTeamLayout(0x8u);
	ctx.ResetToDefault();
	RunSomeTicks(ctx, 90); // let cars/ball move so a reset is observable
	auto carsBefore = DownloadCars(ctx);
	auto ballsBefore = DownloadBalls(ctx);

	std::vector<uint8_t> mask(N, 0);
	for (uint32_t e = 0; e < N; e += 5) mask[e] = 1;
	uint8_t* dmask; CudaOk(cudaMalloc(&dmask, N), "malloc");
	CudaOk(cudaMemcpy(dmask, mask.data(), N, cudaMemcpyHostToDevice), "memcpy");
	ctx.ResetEnvironmentsMasked(dmask);
	Sync();
	cudaFree(dmask);

	auto carsAfter = DownloadCars(ctx);
	auto ballsAfter = DownloadBalls(ctx);
	int resetCount = 0;
	for (uint32_t e = 0; e < N; e++) {
		const bool reset = mask[e] != 0;
		bool ballSame = SameBall(ballsBefore[e], ballsAfter[e]);
		bool carsSame = true;
		for (uint32_t s = 0; s < cars; s++) carsSame &= SameCar(carsBefore[e * cars + s], carsAfter[e * cars + s]);
		if (reset) { CHECK(!ballSame || !carsSame); resetCount++; }
		else { CHECK(ballSame); CHECK(carsSame); }
		for (uint32_t s = 0; s < cars; s++) CHECK_EQ((int)carsAfter[e * cars + s].team, s == 3 ? 1 : 0);
	}
	CHECK(resetCount > 5);
}

TEST(IndexedResetLeavesOtherArenasBitIdentical) {
	const uint32_t N = 48, cars = 2;
	SimContext ctx(N, cars);
	ctx.ResetToDefault();
	RunSomeTicks(ctx, 60);
	auto carsBefore = DownloadCars(ctx);
	std::vector<int32_t> idx = { 3, 17, 40 };
	int32_t* didx; CudaOk(cudaMalloc(&didx, idx.size() * 4), "malloc");
	CudaOk(cudaMemcpy(didx, idx.data(), idx.size() * 4, cudaMemcpyHostToDevice), "memcpy");
	ctx.ResetEnvironmentsIndexed(didx, (uint32_t)idx.size());
	Sync();
	cudaFree(didx);
	auto carsAfter = DownloadCars(ctx);
	for (uint32_t e = 0; e < N; e++) {
		const bool reset = (e == 3 || e == 17 || e == 40);
		bool same = true;
		for (uint32_t s = 0; s < cars; s++) same &= SameCar(carsBefore[e * cars + s], carsAfter[e * cars + s]);
		CHECK_EQ(same, !reset);
	}
}

// Determinism: the same context stepped twice from the same reset gives the same state hash.
TEST(SameResetSameTicksSameState) {
	auto run = [] {
		SimContext ctx(32, 4);
		ctx.ResetToDefault();
		RunSomeTicks(ctx, 240);
		return DownloadCars(ctx);
	};
	auto a = run(); auto b = run();
	for (size_t i = 0; i < a.size(); i++) CHECK(SameCar(a[i], b[i]));
}
