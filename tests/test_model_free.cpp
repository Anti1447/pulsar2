#include "TestFramework.h"

#include "../GigaLearnCPP/src/private/GigaLearnCPP/Util/Models.h"

using namespace GGL;

// 2026-11-27 GPU memory leak: ~Model did not delete its optimizer, which holds references to the parameters, so every
// freed policy version / rating reference left its weights allocated. Runs on the CPU: after Free() our handle on a
// parameter must be the only one left.

TEST(ModelFree_releases_parameters_for_every_optimizer_type) {
	for (ModelOptimType type : { ModelOptimType::ADAM, ModelOptimType::MUON }) {
		ModelConfig config = PartialModelConfig();
		config.numInputs = 8;
		config.numOutputs = 4;
		config.layerSizes = { 16, 16 };
		config.optimType = type;

		ModelSet original = {};
		original.Add(new Model("policy", config, torch::Device(torch::kCPU)));
		ModelSet clone = original.CloneAll();

		std::vector<torch::Tensor> cloneParams = clone["policy"]->parameters();
		std::vector<torch::Tensor> originalParams = original["policy"]->parameters();
		CHECK(!cloneParams.empty());
		clone.Free();
		for (auto& p : cloneParams)
			CHECK_EQ(p.use_count(), (size_t)1);

		// Freeing a clone leaves the original usable
		for (auto& p : originalParams)
			CHECK(p.use_count() > 1);
		CHECK_EQ(original["policy"]->parameters().size(), originalParams.size());

		original.Free();
		for (auto& p : originalParams)
			CHECK_EQ(p.use_count(), (size_t)1);
	}
}
