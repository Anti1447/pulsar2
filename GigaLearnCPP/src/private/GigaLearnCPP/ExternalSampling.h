#pragma once
#include "FrameworkTorch.h"

namespace GGL {

	// One action per row of `logits` ([rows, actions]): temperature <= 0 = argmax, otherwise a draw from
	// softmax(logits / temperature) (temperature 1 = the net's own distribution). Returns int64 [rows] on the
	// logits' device. The draw uses torch's global generator, like the trainer's own sampling.
	inline torch::Tensor PickFromLogits(const torch::Tensor& logits, float temperature) {
		if (temperature <= 0.f)
			return logits.argmax(-1);
		auto probs = torch::softmax(logits.to(torch::kFloat32) / temperature, -1);
		return torch::multinomial(probs, 1, true).squeeze(-1);
	}
}
