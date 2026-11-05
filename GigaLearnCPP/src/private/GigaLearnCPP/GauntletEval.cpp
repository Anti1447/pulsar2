#include "GauntletEval.h"

#include "Util/Models.h"
#include <private/GigaLearnCPP/PPO/PPOLearner.h>

#include <RLGymCPP/StateSetters/KickoffState.h>
#include <RLGymCPP/TerminalConditions/GoalScoreCondition.h>
#include <RLGymCPP/TerminalConditions/NoTouchCondition.h>

namespace {
	// Our policy as a match-loop controller: the current weights, argmax actions, the obs and masks the arenas built
	class PolicyController {
	public:
		RLGC::EnvSet* env = nullptr;
		GGL::PPOLearner* ppo = nullptr;

		void BeginServe(int numPlayers) {}

		void Act(const std::vector<RLGC::GameState>&, const std::vector<bool>& isOurs,
			const std::vector<uint8_t>&, std::vector<int>& outActionIdx) {
			RG_NO_GRAD;
			std::vector<int64_t> rows = {};
			for (int p = 0; p < (int)isOurs.size(); p++)
				if (isOurs[p])
					rows.push_back(p);
			if (rows.empty())
				return;

			torch::Tensor tRows = torch::tensor(rows);
			torch::Tensor tObs = GGL::DIMLIST2_TO_TENSOR<float>(env->state.obs).index_select(0, tRows).to(ppo->device, true);
			torch::Tensor tMasks = GGL::DIMLIST2_TO_TENSOR<uint8_t>(env->state.actionMasks).index_select(0, tRows).to(ppo->device, true);
			torch::Tensor tActions, tLogProbs;
			GGL::PPOLearner::InferActionsFromModels(
				ppo->models, tObs, tMasks,
				true, ppo->config.policyTemperature, ppo->config.useHalfPrecision,
				&tActions, &tLogProbs);
			auto actions = GGL::TENSOR_TO_VEC<int>(tActions);
			for (size_t i = 0; i < rows.size(); i++)
				outActionIdx[rows[i]] = actions[i];
		}
	};
}

GGL::GauntletEval::GauntletEval(const RLGC::EnvSetConfig& baseConfig, const GauntletConfig& cfg, int obsSize) : config(cfg) {
	if (!config.envCreateFn)
		RG_ERR_CLOSE("GauntletEval: config.gauntlet.envCreateFn is not set (it lays out the 1v1 arenas with the trainer's obs builder)");
	if (config.gamesPerBot < 2)
		RG_ERR_CLOSE("GauntletEval: config.gauntlet.gamesPerBot must be at least 2 (one game per side)");

	RLGC::EnvSetConfig ec = baseConfig;
	ec.numArenas = config.gamesPerBot;
	ec.envCreateFn = config.envCreateFn;
	envSet = new RLGC::EnvSet(ec);

	// Real kickoffs, a goal or 10 s without a touch ends the game, no rewards (same treatment as the skill tracker's
	// and the exploiter eval's arenas)
	for (int i = 0; i < envSet->arenas.size(); i++) {
		envSet->rewards[i].clear();
		envSet->stateSetters[i] = { new RLGC::KickoffState() };
		envSet->terminalConditions[i] = { new RLGC::NoTouchCondition(10), new RLGC::GoalScoreCondition() };
		if (envSet->state.gameStates[i].players.size() != 2)
			RG_ERR_CLOSE("GauntletEval: arena " << i << " is not 1v1 (config.gauntlet.envCreateFn must lay out 1v1 arenas)");
	}
	if (envSet->state.obs.size[1] != obsSize)
		RG_ERR_CLOSE("GauntletEval: the arenas' obs size is " << envSet->state.obs.size[1] << " but the trainer's is " << obsSize
			<< " (config.gauntlet.envCreateFn must use the trainer's obs builder)");
}

GGL::GauntletEval::~GauntletEval() {
	delete envSet;
}

GGL::MatchResult GGL::GauntletEval::Run(PPOLearner* ppo, ExternalOpponent& bot) {
	// The gauntlet is the fixed yardstick: the bots play as shipped (argmax) whatever the training serves use
	struct RestoreTemperature {
		ExternalOpponent& b;
		float saved;
		~RestoreTemperature() { b.temperature = saved; }
	} restoreTemperature{ bot, bot.temperature };
	bot.temperature = 0.f;
	PolicyController ours;
	ours.env = envSet;
	ours.ppo = ppo;

	MatchConfig mc;
	mc.playersPerTeam = 1;
	mc.maxSeconds = config.maxGameTime;
	mc.alternateSides = true;

	// Same seed for every bot and every run: the same kickoff layouts, so the series only moves when the policy does.
	// The training's own random stream is put back afterwards.
	ScopedRandSeed seed(config.seed);
	MatchResult res = PlayMatch(*envSet, ours, bot, config.gamesPerBot, mc);

	// Leave the arenas on fresh kickoffs
	std::fill(envSet->state.terminals.begin(), envSet->state.terminals.end(), 1);
	return res;
}
