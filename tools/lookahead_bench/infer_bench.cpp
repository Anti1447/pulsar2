// Phase 0b of docs/GCO_LOOKAHEAD_TEST.md: CPU libtorch inference throughput of the live GCO architecture.
// Loads a COPY of a full checkpoint, times (policy sampled action) and (policy + critic value) at several batch sizes
// and thread counts. Random obs (cost does not depend on values), all-legal masks. CPU only, never the GPU.
//
// Usage (from build/): OMP_NUM_THREADS=4 nice -n 19 ./LookaheadInferBench <checkpoint dir copy> [obsSize=278]
#include <GigaLearnCPP/Util/Models.h>
#include <GigaLearnCPP/PPO/PPOLearner.h>
#include <RLGymCPP/ActionParsers/DefaultAction.h>

#include <chrono>
#include <cstdio>

using namespace GGL;
using namespace RLGC;

static double NowS() { return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

int main(int argc, char** argv) {
	if (argc < 2) { printf("usage: %s <checkpoint dir> [obsSize]\n", argv[0]); return 2; }
	std::filesystem::path ckpt = argv[1];
	int obsSize = argc > 2 ? atoi(argv[2]) : 278;
	const int A = DefaultAction().GetActionAmount();

	auto mk = [](std::vector<int> sizes) {
		PartialModelConfig c; c.layerSizes = sizes; c.addResiduals = true;
		c.activationType = ModelActivationType::GELU; c.addLayerNorm = true; return c;
	};
	PartialModelConfig sh = mk({ 1152, 1152 }); sh.addOutputLayer = false;
	PartialModelConfig pol = mk({ 1024, 1024, 1024, 1024 });
	PartialModelConfig crit = mk({ 1280, 1280 });
	PartialModelConfig ctrunk = mk({ 1280, 1280 }); ctrunk.addOutputLayer = false;
	ModelSet models;
	PPOLearner::MakeModels(true, obsSize, A, sh, pol, crit, ctrunk, torch::Device(torch::kCPU), models);
	models.Load(ckpt, false, false);
	printf("loaded %s (obs %d, actions %d); torch intra-op threads default = %d\n", ckpt.c_str(), obsSize, A, (int)at::get_num_threads());

	// Load sanity: all parameters finite (a shape mismatch would have thrown above)
	{
		double sumAbs = 0; bool finite = true; size_t n = 0;
		for (auto* m : { "shared_head", "policy", "critic_trunk", "critic" })
			for (auto& p : models[m]->parameters()) { finite &= torch::isfinite(p).all().item<bool>(); sumAbs += p.abs().sum().item<double>(); n += p.numel(); }
		printf("params: %zu, all finite: %s, mean |w| = %.5f\n", n, finite ? "yes" : "NO", sumAbs / n);
	}

	torch::NoGradGuard ng;
	for (int threads : { 1, 4 }) {
		at::set_num_threads(threads);
		printf("\n== %d torch thread(s) ==\n%-7s %14s %14s %16s %16s\n", threads, "batch", "policy ms", "policy+V ms", "policy rows/s", "policy+V rows/s");
		for (int B : { 1, 16, 64, 256, 1024 }) {
			torch::Tensor obs = torch::randn({ B, obsSize });
			torch::Tensor masks = torch::ones({ B, A }, torch::kUInt8);
			auto fnPolicy = [&]() { torch::Tensor tAct, tLogp; PPOLearner::InferActionsFromModels(models, obs, masks, false, 1.f, false, &tAct, &tLogp); };
			auto fnBoth = [&]() {
				fnPolicy();
				torch::Tensor h = models["shared_head"]->Forward(obs, false);
				h = models["critic_trunk"]->Forward(h, false);
				models["critic"]->Forward(h, false);
			};
			for (int i = 0; i < 3; i++) fnBoth(); // warm
			double budget = B >= 256 ? 2.0 : 1.5;
			int n = 0; double t0 = NowS(); while (NowS() - t0 < budget) { fnPolicy(); n++; } double tp = (NowS() - t0) / n;
			n = 0; t0 = NowS(); while (NowS() - t0 < budget) { fnBoth(); n++; } double tb = (NowS() - t0) / n;
			printf("%-7d %14.3f %14.3f %16.0f %16.0f\n", B, tp * 1e3, tb * 1e3, B / tp, B / tb);
		}
	}
	return 0;
}
