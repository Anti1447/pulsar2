#pragma once
// ============================================================================
// RewardLab.h - run a scenario on the real EnvSet and record a traced timeline. 2026-11-02 | Rev 1.0 | docs/REWARD_VIEWER_SPEC.md (P1)
//
// The timeline is START STATE + ACTION LOG, not a pile of snapshots (spec section 4). ReplayTo(F) resets the arena, the
// reward stack and the obs builder, and re-simulates the first F logged actions. It therefore reproduces everything a restored
// snapshot would miss: reward-internal state (cooldowns, caps), the ball predictor, and the engine's own bookkeeping.
// Replaying TRUNCATES the future (a branch): stepping again appends new actions from frame F.
//
// The reward block mirrors EnvSet's (TracedRewardStack); the EnvSet itself is given NO rewards. Fidelity to EnvSet's own
// reward block is a test (tests/test_reward_lab.cpp), not an assumption.
// ============================================================================
#include "LabEnv.h"
#include "LabTypes.h"
#include "Expectations.h"
#include "RewardRegistry.h"

namespace RLGC {
namespace Lab {

	class LabSession {
	public:
		// On failure ok() is false and errors() says why (unknown reward, no collision meshes).
		explicit LabSession(const Scenario& scenario) : scenario(scenario) {
			if (!BuildRewardStack(scenario.rewards, rewards, errs)) return;
			stack = new TracedRewardStack(rewards);
			Reset();   // builds the environment
		}
		~LabSession() {
			delete stack;       // detaches the sinks first
			delete lab;
			FreeRewardStack(rewards);
		}
		LabSession(const LabSession&) = delete;
		LabSession& operator=(const LabSession&) = delete;

		bool ok() const { return lab && stack && errs.empty(); }
		const std::vector<std::string>& errors() const { return errs; }
		const Scenario& GetScenario() const { return scenario; }

		// Back to the scenario's initial state with empty timeline and log. Frame 0 is recorded.
		//
		// This builds a FRESH arena rather than writing the initial state into the old one. Measured (tests/test_reward_lab.cpp): a
		// second run in a reused arena differs from the first from frame 1 on (the car's height differs by 0.004 uu), because the
		// engine keeps internal suspension state that the state setter cannot reach (the facade's SetState zeroes or re-derives it;
		// the same limit VizControl's engineExtra documents). Only a fresh arena reproduces a first run exactly.
		void Reset() {
			frames.clear(); log.clear();
			if (stack) stack->Detach();   // no stale pointers while the env is rebuilt
			delete lab;
			lab = LabEnv::Create(scenario);
			if (!lab) { errs.push_back("RocketSim collision meshes not found (run from the repo root; build/collision_meshes)"); return; }
			stack->Attach();
			GameState& gs = lab->env->state.gameStates[0];
			for (auto& w : rewards) w.reward->Reset(gs);
			stack->ResetCounter();
			Frame f0;
			f0.index = 0;
			Fill(f0, gs, 0);
			frames.push_back(std::move(f0));
		}

		int Steps() const { return (int)log.size(); }
		bool Finished() const { return !frames.empty() && (frames.back().terminal != 0 || Steps() >= scenario.steps); }

		// One decision step with explicit actions. Returns false (and does nothing) once finished.
		bool StepOnce(const std::vector<Action>& actions) {
			if (!ok() || Finished() || actions.size() != scenario.cars.size()) return false;
			lab->Step(actions);
			GameState& gs = lab->env->state.gameStates[0];
			const uint8_t term = lab->env->state.terminals[0];
			Frame f;
			f.index = Steps() + 1;
			f.actions = actions;
			Fill(f, gs, term);
			f.trace = stack->Step(gs, term != 0);   // the reward block, once, after the step
			f.hasTrace = true;
			log.push_back(actions);
			frames.push_back(std::move(f));
			return true;
		}

		// The next step's actions from the scenario script.
		std::vector<Action> ScriptedActions() const {
			std::vector<Action> a;
			for (int c = 0; c < (int)scenario.cars.size(); c++) a.push_back(ScriptedAction(scenario, c, Steps()));
			return a;
		}
		bool StepScripted() { return StepOnce(ScriptedActions()); }
		void RunScripted() { while (StepScripted()) {} }

		// Go to frame `frame` by re-simulating the logged actions from the start. Drops everything after it.
		bool ReplayTo(int frame) {
			if (!ok() || frame < 0 || frame > Steps()) return false;
			std::vector<std::vector<Action>> keep(log.begin(), log.begin() + frame);
			Reset();
			for (auto& a : keep) if (!StepOnce(a)) return false;
			return true;
		}

		// The EnvSet's live state for the frame the session is at (what Capture reads for an exact state snapshot). Null if no environment.
		const GameState* LiveState() const { return lab ? &lab->env->state.gameStates[0] : nullptr; }
		const std::vector<Frame>& Frames() const { return frames; }
		const std::vector<std::vector<Action>>& ActionLog() const { return log; }
		std::vector<ExpectResult> EvaluateExpectations() const { return EvaluateAll(scenario, frames); }

	private:
		Scenario scenario;
		std::vector<std::string> errs;
		std::vector<WeightedReward> rewards;
		LabEnv* lab = nullptr;
		TracedRewardStack* stack = nullptr;
		std::vector<Frame> frames;
		std::vector<std::vector<Action>> log;

		static void Fill(Frame& f, const GameState& gs, uint8_t term) {
			for (auto& p : gs.players) f.players.push_back(SnapPlayer(p));
			f.ball.pos = gs.ball.pos; f.ball.vel = gs.ball.vel; f.ball.angVel = gs.ball.angVel;
			f.goalScored = gs.goalScored; f.terminal = term;
		}
	};

} // namespace Lab
} // namespace RLGC
