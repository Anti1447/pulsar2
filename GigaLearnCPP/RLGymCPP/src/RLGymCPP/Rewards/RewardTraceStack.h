#pragma once
// ============================================================================
// RewardTraceStack.h - run a reward stack exactly as EnvSet does, and collect one trace per step.
// 2026-11-02 | Rev 1.0 | docs/REWARD_VIEWER_SPEC.md section 3.1 (P0).
//
// TracedRewardStack attaches one TraceSink to each reward in a stack, then Step() mirrors EnvSet's reward
// block line for line (PreStep every reward, then GetAllRewards for each, multiplied by its weight and summed).
// It adds only bookkeeping around those calls: each reward is called exactly once per step, in the same order,
// so stateful rewards (cooldowns, caps, windows) behave as in training. The destructor detaches the sinks.
//
// Links ZeroSumReward.cpp (dynamic_cast + _lastRewards for the pre-ZeroSum value), like EnvSet does.
// ============================================================================
#include "Reward.h"
#include "RewardTrace.h"
#include "ZeroSumReward.h"
#include <string>
#include <typeinfo>
#include <vector>
#if defined(__GNUC__) || defined(__clang__)
#include <cxxabi.h>
#include <cstdlib>
#endif

namespace RLGC {

	// Reward::GetName() is the W&B key and is the MANGLED type name under gcc ("N4RLGC14OffsidePenaltyE"); it is pinned by the
	// panel (docs/REWARD_SHAPING.md) so it is left alone. The viewer wants a readable name, taken from the innermost reward under
	// any wrapper: "ZeroSumReward(CarEnergyPotentialReward)" shows as "CarEnergyPotentialReward".
	inline std::string ReadableRewardName(Reward* r) {
		while (auto* w = dynamic_cast<RewardWrapper*>(r)) r = w->child;
		std::string name = typeid(*r).name();
#if defined(__GNUC__) || defined(__clang__)
		int status = 0;
		if (char* d = abi::__cxa_demangle(name.c_str(), nullptr, nullptr, &status)) { if (status == 0) name = d; std::free(d); }
#endif
		if (size_t i = name.rfind("::"); i != std::string::npos) name.erase(0, i + 2);
		if (size_t i = name.rfind(' '); i != std::string::npos) name.erase(0, i + 1);   // MSVC "class X"
		return name;
	}

	struct RewardStepTrace {
		std::string name;      // readable, e.g. "OffsidePenalty"
		std::string wandbKey;  // Reward::GetName(), the key the "Rewards/*" panel uses
		float weight = 1;
		bool zeroSum = false;
		// All three are indexed like state.players. `raw` is the child's value before ZeroSum (equal to `wrapped`
		// when the reward is not wrapped), `wrapped` is what GetAllRewards returned, `weighted` is wrapped * weight.
		std::vector<float> raw, wrapped, weighted;
		TraceSink sink;
	};

	struct StackStepTrace {
		uint64_t step = 0;
		std::vector<uint32_t> carIds;        // state.players order
		std::vector<float> total;            // sum of weighted over the stack, per player
		std::vector<RewardStepTrace> rewards;
	};

	class TracedRewardStack {
	public:
		// Non-owning. The stack must outlive this object and must not be resized while it is attached.
		explicit TracedRewardStack(std::vector<WeightedReward>& stack) : stack(stack) { Attach(); }
		~TracedRewardStack() { Detach(); }
		TracedRewardStack(const TracedRewardStack&) = delete;
		TracedRewardStack& operator=(const TracedRewardStack&) = delete;

		void Attach() {
			trace.rewards.resize(stack.size());
			for (size_t i = 0; i < stack.size(); i++) {
				trace.rewards[i].name = ReadableRewardName(stack[i].reward);
				trace.rewards[i].wandbKey = stack[i].reward->GetName();
				trace.rewards[i].weight = stack[i].weight;
				trace.rewards[i].zeroSum = dynamic_cast<ZeroSumReward*>(stack[i].reward) != nullptr;
				stack[i].reward->SetTrace(&trace.rewards[i].sink);
			}
		}
		void Detach() {
			for (auto& w : stack)
				w.reward->SetTrace(nullptr);
		}

		// One environment step. `isFinal` is what EnvSet passes as the terminal flag.
		const StackStepTrace& Step(const GameState& gs, bool isFinal) {
			trace.step = steps++;
			trace.carIds.clear();
			for (auto& p : gs.players) trace.carIds.push_back(p.carId);
			trace.total.assign(gs.players.size(), 0.0f);

			for (auto& w : stack)
				w.reward->PreStep(gs);

			for (size_t r = 0; r < stack.size(); r++) {
				RewardStepTrace& rt = trace.rewards[r];
				rt.sink.Clear();
				rt.weight = stack[r].weight;
				std::vector<float> output = stack[r].reward->GetAllRewards(gs, isFinal);
				rt.wrapped = output;
				rt.weighted.resize(output.size());
				for (size_t i = 0; i < output.size(); i++) {
					rt.weighted[i] = output[i] * stack[r].weight;
					trace.total[i] += rt.weighted[i];
				}
				if (rt.zeroSum) {
					ZeroSumReward* zs = dynamic_cast<ZeroSumReward*>(stack[r].reward);
					rt.raw = zs->_lastRewards;
				} else {
					rt.raw = output;
				}
			}
			return trace;
		}

		const StackStepTrace& Last() const { return trace; }
		void ResetCounter() { steps = 0; }

	private:
		std::vector<WeightedReward>& stack;
		StackStepTrace trace;
		uint64_t steps = 0;
	};

} // namespace RLGC
