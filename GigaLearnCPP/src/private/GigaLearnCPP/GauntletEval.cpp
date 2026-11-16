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

	// Real kickoffs, a goal or 10 s without a touch ends the game, no rewards (same treatment as the skill tracker's
	// and the exploiter eval's arenas)
	auto build = [&](RLGC::EnvCreateFn fn, int teamSize) {
		RLGC::EnvSetConfig ec = baseConfig;
		ec.numArenas = config.gamesPerBot;
		ec.envCreateFn = fn;
		RLGC::EnvSet* es = new RLGC::EnvSet(ec);
		for (int i = 0; i < es->arenas.size(); i++) {
			es->rewards[i].clear();
			es->stateSetters[i] = { new RLGC::KickoffState() };
			es->terminalConditions[i] = { new RLGC::NoTouchCondition(10), new RLGC::GoalScoreCondition() };
			if (es->state.gameStates[i].players.size() != (size_t)teamSize * 2)
				RG_ERR_CLOSE("GauntletEval: arena " << i << " is not " << teamSize << "v" << teamSize
					<< " (the env create function must lay out " << teamSize << "v" << teamSize << " arenas)");
		}
		if (es->state.obs.size[1] != obsSize)
			RG_ERR_CLOSE("GauntletEval: the arenas' obs size is " << es->state.obs.size[1] << " but the trainer's is " << obsSize
				<< " (the env create function must use the trainer's obs builder)");
		return es;
	};
	envSet = build(config.envCreateFn, 1);
	for (int ts : config.extraTeamSizes) {
		if (ts < 2 || ts > 3 || extraEnvSets.count(ts))
			RG_ERR_CLOSE("GauntletEval: extraTeamSizes entries must be distinct and 2 or 3 (got " << ts << ")");
		if (!config.teamEnvCreateFn)
			RG_ERR_CLOSE("GauntletEval: extraTeamSizes needs config.gauntlet.teamEnvCreateFn");
		extraEnvSets[ts] = build(config.teamEnvCreateFn(ts), ts);
	}
}

GGL::GauntletEval::~GauntletEval() {
	delete envSet;
	for (auto& kv : extraEnvSets)
		delete kv.second;
}

GGL::MatchResult GGL::GauntletEval::Run(PPOLearner* ppo, ExternalOpponent& bot, int teamSize) {
	RLGC::EnvSet* env = (teamSize == 1) ? envSet : (extraEnvSets.count(teamSize) ? extraEnvSets[teamSize] : nullptr);
	if (!env)
		RG_ERR_CLOSE("GauntletEval: no arenas for team size " << teamSize);
	if (teamSize > bot.MaxTeamSize())
		RG_ERR_CLOSE("GauntletEval: " << bot.name << " cannot field " << teamSize << " per team");
	// The gauntlet is the fixed yardstick: the bots play as shipped (argmax) whatever the training serves use
	struct RestoreTemperature {
		ExternalOpponent& b;
		float saved;
		~RestoreTemperature() { b.temperature = saved; }
	} restoreTemperature{ bot, bot.temperature };
	bot.temperature = 0.f;
	PolicyController ours;
	ours.env = env;
	ours.ppo = ppo;

	MatchConfig mc;
	mc.playersPerTeam = teamSize;
	mc.maxSeconds = config.maxGameTime;
	mc.alternateSides = true;

	// Same seed for every bot and every run: the same kickoff layouts, so the series only moves when the policy does.
	// The training's own random stream is put back afterwards.
	ScopedRandSeed seed(config.seed);
	MatchResult res = PlayMatch(*env, ours, bot, config.gamesPerBot, mc);

	// Leave the arenas on fresh kickoffs
	std::fill(env->state.terminals.begin(), env->state.terminals.end(), 1);
	return res;
}
