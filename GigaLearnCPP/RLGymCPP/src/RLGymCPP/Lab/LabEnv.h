#pragma once
// ============================================================================
// LabEnv.h - a real EnvSet (one arena) that starts from a Scenario. 2026-11-02 | Rev 1.0 | docs/REWARD_VIEWER_SPEC.md (P1)
//
// The Lab does not re-implement physics, action parsing, state building, ball prediction or terminals: it asks the same EnvSet
// that trains to do them, with the scenario's cars and a ScenarioStateSetter. Cars are driven through EnvSet's raw control
// override, so a scripted Action reaches the engine exactly as a parsed one would.
// ============================================================================
#include "Scenario.h"
#include "../EnvSet/EnvSet.h"
#include "../ObsBuilders/AdvancedObsPadded.h"
#include "../ActionParsers/DefaultAction.h"
#include "../TerminalConditions/GoalScoreCondition.h"
#include <filesystem>

namespace RLGC {
namespace Lab {

	constexpr int LAB_BALL_PRED_SLICES = 8;   // as the live 6.0 obs (2.0 s of per-tick prediction)

	// RocketSim needs the collision meshes once per process. Tries the repo-relative locations used by the tests.
	inline bool InitEngine() {
		static bool ok = false;
		if (ok) return true;
		for (const char* path : { "build/collision_meshes", "collision_meshes", "../collision_meshes", "../build/collision_meshes" }) {
			try {
				if (!std::filesystem::exists(path)) continue;
				RocketSim::Init(path, true); ok = true; break;
			} catch (...) {}
		}
		return ok;
	}

	// Writes a Scenario's initial situation into an arena. Holds a pointer to the scenario (which must outlive it).
	class ScenarioStateSetter : public StateSetter {
	public:
		const Scenario* scenario;
		explicit ScenarioStateSetter(const Scenario* s) : scenario(s) {}

		virtual void ResetArena(Arena* arena) override {
			constexpr float DEG = 3.14159265358979f / 180.0f;
			size_t i = 0;
			for (Car* car : arena->_cars) {
				const CarSpec& c = scenario->cars[i++];
				CarState cs = CarState();
				cs.pos = c.pos; cs.vel = c.vel; cs.angVel = c.angVel;
				cs.rotMat = Angle(c.yawDeg * DEG, c.pitchDeg * DEG, c.rollDeg * DEG).ToRotMat();
				cs.boost = c.boost;
				cs.hasJumped = c.hasJumped; cs.hasDoubleJumped = c.hasDoubleJumped; cs.hasFlipped = c.hasFlipped;
				cs.airTimeSinceJump = c.airTimeSinceJump; cs.isDemoed = c.isDemoed;
				cs.isOnGround = c.pos.z < 20.0f;   // the engine re-derives contact on the first step
				car->SetState(cs);
				car->controls = CarControls();
			}
			BallState bs = BallState();
			bs.pos = scenario->ball.pos; bs.vel = scenario->ball.vel; bs.angVel = scenario->ball.angVel;
			arena->ball->SetState(bs);
			for (BoostPad* pad : arena->_boostPads) {
				BoostPadState ps = BoostPadState();   // active, no cooldown, nobody locked
				pad->SetState(ps);
			}
		}
	};

	// Owns an EnvSet and the pieces EnvSet itself does not delete.
	struct LabEnv {
		EnvSet* env = nullptr;
		ObsBuilder* obsBuilder = nullptr;
		ActionParser* actionParser = nullptr;
		ScenarioStateSetter* setter = nullptr;
		std::vector<TerminalCondition*> terminals;

		// `envRewards` lets a test hand the EnvSet real rewards (to compare against the Lab's own reward block); the Lab proper passes none.
		static LabEnv* Create(const Scenario& scenario, std::vector<WeightedReward> envRewards = {}) {
			if (!InitEngine()) return nullptr;
			LabEnv* le = new LabEnv();
			le->setter = new ScenarioStateSetter(&scenario);
			EnvSetConfig cfg = {};
			cfg.numArenas = 1; cfg.tickSkip = scenario.tickSkip; cfg.actionDelay = scenario.actionDelay; cfg.saveRewards = false;
			cfg.envCreateFn = [&](int) {
				Arena* arena = Arena::Create(GameMode::SOCCAR);
				for (auto& c : scenario.cars) arena->AddCar(c.team);
				EnvCreateResult r = {};
				r.arena = arena;
				r.rewards = envRewards;
				le->terminals = { new GoalScoreCondition() };
				r.terminalConditions = le->terminals;
				le->obsBuilder = new AdvancedObsPadded(3, /*shuffleSlots=*/false, LAB_BALL_PRED_SLICES);
				r.obsBuilder = le->obsBuilder;
				le->actionParser = new DefaultAction();
				r.actionParser = le->actionParser;
				r.stateSetter = le->setter;
				r.userInfo = NULL;
				return r;
			};
			le->env = new EnvSet(cfg);
			return le;
		}

		~LabEnv() {
			delete env;   // deletes the arena
			delete obsBuilder; delete actionParser; delete setter;
			for (auto* t : terminals) delete t;
		}

		// One decision step with explicit actions (one per car, in scenario order).
		void Step(const std::vector<Action>& actions) {
			const size_t n = env->state.gameStates[0].players.size();
			env->controlOverrideMask.assign(n, 1);
			env->controlOverrides = actions;
			env->StepFirstHalf(false);
			env->StepSecondHalf(IList(n, 0), false);
		}
	};

} // namespace Lab
} // namespace RLGC
