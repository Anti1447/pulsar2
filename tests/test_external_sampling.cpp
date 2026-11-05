#include "TestFramework.h"

#include "../GigaLearnCPP/src/private/GigaLearnCPP/ExternalSampling.h"

using namespace GGL;

// ExternalOpponent::temperature (0 = argmax, > 0 = sample from softmax(logits / T)): the picker behind it

TEST(ExternalSampling_zero_temperature_is_argmax) {
	auto logits = torch::tensor({ { 0.1f, 2.f, -1.f }, { 5.f, 0.f, 4.9f } });
	auto picks = PickFromLogits(logits, 0.f);
	CHECK_EQ(picks[0].item<int64_t>(), (int64_t)1);
	CHECK_EQ(picks[1].item<int64_t>(), (int64_t)0);
	CHECK_EQ(PickFromLogits(logits, -1.f)[0].item<int64_t>(), (int64_t)1);
}

TEST(ExternalSampling_temperature_one_follows_the_softmax) {
	torch::manual_seed(1);
	const int rows = 20000;
	// one distribution repeated: probabilities softmax([0, 1, 2]) = 0.090, 0.245, 0.665
	auto logits = torch::tensor({ 0.f, 1.f, 2.f }).repeat({ rows, 1 });
	auto picks = PickFromLogits(logits, 1.f);
	auto probs = torch::softmax(torch::tensor({ 0.f, 1.f, 2.f }), 0);
	for (int a = 0; a < 3; a++) {
		float freq = (picks == a).to(torch::kFloat32).mean().item<float>();
		CHECK(std::fabs(freq - probs[a].item<float>()) < 0.015f);
	}
}

TEST(ExternalSampling_lower_temperature_sharpens_and_higher_flattens) {
	torch::manual_seed(2);
	const int rows = 20000;
	auto logits = torch::tensor({ 0.f, 1.f, 2.f }).repeat({ rows, 1 });
	auto top = [&](float t) { return (PickFromLogits(logits, t) == 2).to(torch::kFloat32).mean().item<float>(); };
	float cold = top(0.25f), warm = top(1.f), hot = top(4.f);
	CHECK(cold > warm);
	CHECK(warm > hot);
	CHECK(cold > 0.95f);          // softmax([0,4,8]) puts 0.98 on the top logit
	CHECK(hot < 0.5f && hot > 0.33f); // close to uniform but still leaning to the top
}

TEST(ExternalSampling_is_reproducible_with_the_torch_seed) {
	auto logits = torch::tensor({ 0.f, 1.f, 2.f, 0.5f }).repeat({ 64, 1 });
	torch::manual_seed(7);
	auto a = PickFromLogits(logits, 1.f);
	torch::manual_seed(7);
	auto b = PickFromLogits(logits, 1.f);
	CHECK(torch::equal(a, b));
}
