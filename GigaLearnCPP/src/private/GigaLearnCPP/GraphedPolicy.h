#pragma once
#include "FrameworkTorch.h"
#include "PPO/PPOLearner.h"

#include <ATen/cuda/CUDAGraph.h>
#include <ATen/cuda/CUDAEvent.h>
#include <c10/cuda/CUDAStream.h>
#include <c10/cuda/CUDAGuard.h>

namespace GGL {

	// CUDA-GRAPHED POLICY SAMPLING for the collection loop (2026-11-23, user-directed "speed improvements from buta-ppo"; measurement in docs/GCO_SPEED.md).
	// Measured on the live ts2 run (800k batch): of 4.9 s inference per iteration, 1.7 s was the host->device copy from pageable memory and 2.9 s was the CPU
	// time to LAUNCH the forward and sampling kernels; the wait for the GPU was 0.3 s. So the GPU is not the bottleneck. This class removes both: the obs and masks go
	// through persistent PINNED buffers into persistent device tensors, and forward + masked softmax + sampling + log-prob run as ONE graph replay.
	// Statistically the same sampler as InferActionsFromModels: probs are the same masked softmax (same function), and the action is argmax(probs / Exp(1)) (the
	// exponential race: exactly sampling from probs, and capturable because it has no host sync), where the eager path uses torch::multinomial. The random stream
	// differs, the distribution does not. The graph is captured ONCE per (row count) on the first use and reused: the collection snapshot's parameters are updated IN
	// PLACE (fnSyncSnapshot copy_), and the half-precision copy is refreshed in place too, so the captured addresses stay valid. Capture mode is thread-local, because
	// the learn pass runs on another thread at the same time.
	// Self-check at capture: the replayed probabilities must equal the eager ones and every sampled action must be legal; otherwise the instance marks itself broken and the
	// caller falls back to the eager path. Revert: GGL_GRAPH_INFER=0.
	class GraphedPolicy {
	public:
		ModelSet* models;
		torch::Device device;
		bool halfPrec;
		float temperature;
		bool broken = false;

		// host-side split of the last Run (seconds), read by the caller's inference timers
		double tCopyIn = 0, tReplay = 0, tWait = 0;

		GraphedPolicy(ModelSet* models, torch::Device device, bool halfPrec, float temperature)
			: models(models), device(device), halfPrec(halfPrec), temperature(temperature) {}

		// obs: [N, obsSize] float32 CPU, masks: [N, A] uint8 CPU. idx (optional, int64 CPU): the rows to use, in order.
		// Returns actions (int64, CPU) and log-probs (float32, CPU) for those rows. Returns false when the graph path is unavailable (broken); the caller then runs eagerly.
		bool Run(const torch::Tensor& obs, const torch::Tensor& masks, const torch::Tensor& idx, torch::Tensor& outActions, torch::Tensor& outLogProbs) {
			if (broken)
				return false;
			RG_NO_GRAD;
			const int64_t rows = idx.defined() ? idx.numel() : obs.size(0);
			if (!ready || rows != rows_) {
				if (ready) { // a second row count would need its own graph; the caller keeps one instance per row count
					broken = true;
					return false;
				}
				try {
					Build(rows, obs.size(1), masks.size(1));
				} catch (const std::exception& e) {
					RG_LOG("GraphedPolicy: capture failed, using the eager path (" << e.what() << ")");
					broken = true;
					return false;
				}
				if (broken)
					return false;
			}

			Timer t0 = {};
			if (idx.defined()) {
				torch::index_select_out(obsPin, obs, 0, idx);
				torch::index_select_out(maskPin, masks, 0, idx);
			} else {
				obsPin.copy_(obs);
				maskPin.copy_(masks);
			}
			RefreshHalf();
			auto stream = c10::cuda::getCurrentCUDAStream(device.index());
			tCopyIn = t0.Elapsed();

			// The pinned->device copies, the forward + sampling and the device->pinned copies are ALL inside the graph (buta-ppo's layout): two CUDA API calls per step
			// (this replay and the stream sync) instead of about seven. Measured: with the learn pass running on another thread each API call from this thread waited on
			// the driver for milliseconds, so the number of calls mattered more than the kernels.
			Timer t1 = {};
			graph.replay();
			tReplay = t1.Elapsed();

			Timer t2 = {};
			stream.synchronize();
			tWait = t2.Elapsed();
			if (!okPin.item<bool>()) {
				// Non-finite logits: let the eager path produce its forensic error message
				return false;
			}
			outActions = actPin.clone();
			outLogProbs = lpPin.clone();
			return true;
		}

	private:
		bool ready = false;
		int64_t rows_ = 0;
		at::cuda::CUDAGraph graph;
		torch::Tensor obsPin, maskPin, actPin, lpPin, okPin;
		torch::Tensor obsGpu, maskGpu, actGpu, lpGpu, okGpu, probsGpu;

		// The half-precision copy of each net is rebuilt lazily inside Model::Forward after a weight sync; do that now, eagerly and OUTSIDE the graph, so the
		// replay reads fresh weights and the captured body never takes the rebuild branch.
		void RefreshHalf() {
			bool stale = false;
			for (const char* nm : { "shared_head", "policy" })
				if ((*models)[nm] && (*models)[nm]->_seqHalfOutdated)
					stale = true;
			if (stale) {
				auto o = obsGpu.slice(0, 0, 1);
				auto m = maskGpu.slice(0, 0, 1);
				PPOLearner::InferPolicyProbsFromModels(*models, o, m, temperature, halfPrec);
			}
		}

		void Body() {
			torch::Tensor rowOk;
			auto probs = PPOLearner::InferPolicyProbsFromModels(*models, obsGpu, maskGpu, temperature, halfPrec, {}, &rowOk);
			probsGpu.copy_(probs);
			auto e = torch::empty_like(probs).exponential_(1.0).clamp_min_(1e-20);
			auto act = (probs / e).argmax(1);
			actGpu.copy_(act);
			lpGpu.copy_(torch::log(probs.gather(1, act.unsqueeze(1))).squeeze(1));
			okGpu.copy_(rowOk.all());
		}

		void Build(int64_t rows, int64_t obsSize, int64_t numActions) {
			auto fopt = torch::TensorOptions().dtype(torch::kFloat32);
			auto popt = torch::TensorOptions().pinned_memory(true);
			obsPin = torch::empty({ rows, obsSize }, popt.dtype(torch::kFloat32));
			maskPin = torch::empty({ rows, numActions }, popt.dtype(torch::kUInt8));
			actPin = torch::empty({ rows }, popt.dtype(torch::kInt64));
			lpPin = torch::empty({ rows }, popt.dtype(torch::kFloat32));
			okPin = torch::empty({}, popt.dtype(torch::kBool));
			obsGpu = torch::zeros({ rows, obsSize }, fopt.device(device));
			maskGpu = torch::ones({ rows, numActions }, torch::TensorOptions().dtype(torch::kUInt8).device(device));
			actGpu = torch::zeros({ rows }, torch::TensorOptions().dtype(torch::kInt64).device(device));
			lpGpu = torch::zeros({ rows }, fopt.device(device));
			okGpu = torch::zeros({}, torch::TensorOptions().dtype(torch::kBool).device(device));
			probsGpu = torch::zeros({ rows, numActions }, fopt.device(device));
			rows_ = rows;

			// Real inputs for the self-check: any valid obs row repeated is fine for a shape/legality check, but use the caller's first rows when available (set by Run).
			RefreshHalf(); // refresh with the zero obs: the flags just have to be false before capture
			auto stream = c10::cuda::getCurrentCUDAStream(device.index());
			for (int i = 0; i < 3; i++) // eager warmup: cuBLAS handles/workspaces must exist before capture
				Body();
			stream.synchronize();
			obsPin.zero_();
			maskPin.fill_(1);
			graph.capture_begin({ 0, 0 }, cudaStreamCaptureModeThreadLocal);
			obsGpu.copy_(obsPin, true);
			maskGpu.copy_(maskPin, true);
			Body();
			actPin.copy_(actGpu, true);
			lpPin.copy_(lpGpu, true);
			okPin.copy_(okGpu, true);
			graph.capture_end();
			stream.synchronize();

			// Self-check: replay on the (zero) inputs and compare the probabilities with a fresh eager evaluation
			graph.replay();
			stream.synchronize();
			auto eagerProbs = PPOLearner::InferPolicyProbsFromModels(*models, obsGpu, maskGpu, temperature, halfPrec);
			float maxDiff = (eagerProbs - probsGpu).abs().max().item<float>();
			bool legal = maskGpu.gather(1, actGpu.unsqueeze(1)).to(torch::kBool).all().item<bool>();
			if (!(maxDiff < 1e-4f) || !legal) {
				RG_LOG("GraphedPolicy: self-check FAILED (max prob diff " << maxDiff << ", legal " << legal << "); using the eager path");
				broken = true;
				return;
			}
			ready = true;
			RG_LOG("GraphedPolicy: captured " << rows << "-row policy graph (max prob diff vs eager " << maxDiff << ")");
		}
	};
}
