#include "TestFramework.h"

#include <RLGymCPP/TeamLayout.h>
#include <RLGymCPP/EnvSet/EnvSet.h>
#include <RLGymCPP/Rewards/CommonRewards.h>
#include <RLGymCPP/Rewards/ZeroSumReward.h>
#include <RLGymCPP/Rewards/KickoffOutcomeReward.h>
#include <RLGymCPP/Rewards/Match/TeamPressureReward.h>
#include <RLGymCPP/TerminalConditions/NoTouchCondition.h>
#include <RLGymCPP/TerminalConditions/GoalScoreCondition.h>
#include <RLGymCPP/ObsBuilders/AdvancedObsPadded.h>
#include <RLGymCPP/StateSetters/KickoffState.h>
#include <RLGymCPP/StateSetters/RandomState.h>
#include <RLGymCPP/StateSetters/BallNearCarState.h>
#include <RLGymCPP/StateSetters/AirDrillState.h>
#include <RLGymCPP/StateSetters/AirPlayState.h>
#include <RLGymCPP/StateSetters/CombinedState.h>
#include <RLGymCPP/ActionParsers/DefaultAction.h>

#include <cmath>
#include <filesystem>
#include <map>
#include <set>

using namespace RLGC;

// Uneven training arenas (2v1 / 3v1 / 3v2, RLGymCPP/TeamLayout.h): the layout, the engine's kickoffs, the padded obs, the
// zero-sum wrapper's per-team balance, and a mixed fleet stepped through the live reset mix.

namespace {
	void InitRocketSimUneven() {
		static bool done = false;
		if (done) return;
		for (const char* path : { "collision_meshes", "../collision_meshes", "build/collision_meshes" })
			if (std::filesystem::exists(std::filesystem::path(path) / "soccar")) { RocketSim::Init(path); break; }
		done = true;
	}

	// Car order as ExampleMain's MakeEnv adds them
	Arena* MakeSized(TeamSizes s) {
		Arena* arena = Arena::Create(GameMode::SOCCAR);
		for (int i = 0; i < RS_MAX(s.blue, s.orange); i++) {
			if (i < s.blue) arena->AddCar(Team::BLUE);
			if (i < s.orange) arena->AddCar(Team::ORANGE);
		}
		return arena;
	}

	struct IndexReward : public Reward { // a different value for every player
		float GetReward(const Player& player, const GameState&, bool) override { return 1.f + player.index * player.index; }
	};

	struct OpenZeroSum : public ZeroSumReward { // the per-player call is protected
		using ZeroSumReward::ZeroSumReward;
		using ZeroSumReward::GetAllRewards;
	};
}

TEST(Uneven_layout_without_uneven_arenas_is_the_old_one) {
	const int N = 1024, n2 = 341, n3 = 341;
	for (int i = 0; i < N; i++) {
		int old = i >= N - n3 ? 3 : (i >= N - n3 - n2 ? 2 : 1);
		TeamSizes s = ArenaTeamSizes(i, N, n2, n3, 0);
		CHECK_EQ(s.blue, old);
		CHECK_EQ(s.orange, old);
	}
}

TEST(Uneven_layout_counts_and_colour_balance) {
	const int N = 1024;
	const int nU = UnevenArenaCount(N, 0.15f);
	CHECK_EQ(nU, 150);
	CHECK_EQ(UnevenArenaCount(N, 0.f), 0);
	CHECK_EQ(UnevenArenaCount(N, 0.9f), 510); // capped at half
	const int n2 = (int)((N - nU) * 0.3333f), n3 = n2;
	std::map<std::pair<int, int>, int> count;
	for (int i = 0; i < N; i++) {
		TeamSizes s = ArenaTeamSizes(i, N, n2, n3, nU);
		count[{ s.blue, s.orange }]++;
	}
	CHECK_EQ((count[{ 1, 1 }]), N - nU - n2 - n3);
	CHECK_EQ((count[{ 2, 2 }]), n2);
	CHECK_EQ((count[{ 3, 3 }]), n3);
	for (auto shape : { std::pair<int, int>{2, 1}, {1, 2}, {3, 1}, {1, 3}, {3, 2}, {2, 3} })
		CHECK_EQ(count[shape], 25);
	TeamSizes first = ArenaTeamSizes(0, N, n2, n3, nU);
	CHECK(first.blue == 1 && first.orange == 1); // arena 0 stays 1v1
}

TEST(Uneven_kickoffs_are_on_own_half_distinct_and_facing_forward) {
	InitRocketSimUneven();
	for (int k = 0; k < UNEVEN_SHAPES; k++) {
		TeamSizes s = UnevenShape(k);
		Arena* arena = MakeSized(s);
		std::set<int> firstBlueSpots;
		for (int seed = 1; seed <= 200; seed++) {
			arena->ResetToRandomKickoff(seed * 7919);
			int blue = 0, orange = 0;
			std::vector<Vec> spots;
			for (Car* car : arena->_cars) {
				CarState cs = car->GetState();
				(car->team == Team::BLUE ? blue : orange)++;
				CHECK(car->team == Team::BLUE ? cs.pos.y < -2000.f : cs.pos.y > 2000.f);
				CHECK(car->team == Team::BLUE ? cs.rotMat.forward.y > 0.f : cs.rotMat.forward.y < 0.f);
				CHECK(cs.vel.Length() < 1.f);
				for (const Vec& other : spots)
					CHECK((cs.pos - other).Length() > 200.f);
				spots.push_back(cs.pos);
				if (car->team == Team::BLUE && blue == 1) firstBlueSpots.insert((int)cs.pos.x);
			}
			CHECK_EQ(blue, s.blue);
			CHECK_EQ(orange, s.orange);
			// the shared spots are mirrored: the i-th cars of both teams stand point-symmetric
			std::vector<Vec> b, o;
			for (Car* car : arena->_cars)
				(car->team == Team::BLUE ? b : o).push_back(car->GetState().pos);
			for (int i = 0; i < RS_MIN(s.blue, s.orange); i++) {
				CHECK_NEAR(b[i].x, -o[i].x, 1.f);
				CHECK_NEAR(b[i].y, -o[i].y, 1.f);
			}
		}
		CHECK((int)firstBlueSpots.size() >= 4); // the spot choice still varies
		delete arena;
	}
}

TEST(Uneven_zero_sum_wrapper_balances_per_team_means) {
	GameState state = {};
	state.players.resize(4); // 3 blue against 1 orange
	for (int i = 0; i < 4; i++) {
		state.players[i].index = i;
		state.players[i].team = i < 3 ? Team::BLUE : Team::ORANGE;
	}
	for (float spirit : { 0.f, 0.3f, 0.6f }) {
		OpenZeroSum wrapped(new IndexReward(), spirit);
		std::vector<float> r = wrapped.GetAllRewards(state, false);
		float blueMean = (r[0] + r[1] + r[2]) / 3, orangeMean = r[3];
		CHECK_NEAR(blueMean, -orangeMean, 1e-4f);   // per car, one side's average gain is the other's loss
		// ...but the sum over all cars is NOT zero in uneven games: it is (3 - 1) * blueMean
		CHECK_NEAR(r[0] + r[1] + r[2] + r[3], 2 * blueMean, 1e-4f);
	}
}

TEST(Uneven_envset_obs_slots_and_finite_rewards) {
	InitRocketSimUneven();
	EnvSetConfig cfg = {};
	cfg.numArenas = UNEVEN_SHAPES;
	cfg.tickSkip = 4;
	cfg.actionDelay = 0;
	cfg.saveRewards = false;
	cfg.envCreateFn = [](int index) {
		EnvCreateResult result = {};
		result.arena = MakeSized(UnevenShape(index));
		result.actionParser = new DefaultAction();
		result.obsBuilder = new AdvancedObsPadded(3, true, 8);
		result.stateSetter = new CombinedState({
			{ new BallNearCarState(600, 900), 0.2f }, { new AirDrillState(), 0.2f }, { new AirPlayState(), 0.2f },
			{ new KickoffState(), 0.2f }, { new RandomState(true, true, false), 0.2f } });
		result.terminalConditions = { new NoTouchCondition(3), new GoalScoreCondition() };
		const float g = 0.998f;
		result.rewards = {
			{ new BallToGoalPotentialReward(g), 75.f }, { new ZeroSumReward(new TouchAccelReward(), 0.6f), 10.f },
			{ new ZeroSumReward(new BallProximityPotentialReward(g), 0.6f), 4.f },
			{ new ZeroSumReward(new TeamPressureReward(), 0.6f), 0.15f }, { new KickoffOutcomeReward(), 30.f },
			{ new GoalReward(), 150 } };
		return result;
	};
	EnvSet envSet(cfg);
	CHECK_EQ(envSet.state.numPlayers, 3 + 3 + 4 + 4 + 5 + 5);
	CHECK_EQ(envSet.obsSize, 278);

	const int numActions = DefaultAction().GetActionAmount();
	std::vector<int> resets(cfg.numArenas, 0);
	for (int step = 0; step < 900; step++) {
		envSet.StepFirstHalf(false);
		IList actions(envSet.state.numPlayers, 0);
		for (int p = 0; p < envSet.state.numPlayers; p++) {
			if (!envSet.state.actionMasks.Defined()) continue;
			std::vector<int> valid;
			for (int a = 0; a < numActions; a++)
				if (envSet.state.actionMasks.At(p, a)) valid.push_back(a);
			if (!valid.empty()) actions[p] = valid[RocketSim::Math::RandInt(0, (int)valid.size())];
		}
		envSet.StepSecondHalf(actions, false);
		for (int a = 0; a < cfg.numArenas; a++) {
			TeamSizes s = UnevenShape(a);
			int start = envSet.state.arenaPlayerStartIdx[a];
			auto& gs = envSet.state.gameStates[a];
			CHECK_EQ((int)gs.players.size(), s.blue + s.orange);
			for (int j = 0; j < (int)gs.players.size(); j++) {
				CHECK(std::isfinite(envSet.state.rewards[start + j]));
				// presence flags sit after the 51-float header and six 29-float slots: 2 teammate then 3 opponent flags
				int mine = gs.players[j].team == Team::BLUE ? s.blue : s.orange;
				int theirs = s.blue + s.orange - mine;
				float mates = 0, foes = 0;
				for (int f = 0; f < 2; f++) mates += envSet.state.obs.At(start + j, 225 + f);
				for (int f = 2; f < 5; f++) foes += envSet.state.obs.At(start + j, 225 + f);
				CHECK_EQ((int)mates, mine - 1);
				CHECK_EQ((int)foes, theirs);
				for (int k = 0; k < envSet.obsSize; k++)
					CHECK(std::isfinite(envSet.state.obs.At(start + j, k)));
			}
			if (envSet.state.terminals[a]) resets[a]++;
		}
	}
	envSet.Sync();
	for (int a = 0; a < cfg.numArenas; a++)
		CHECK(resets[a] >= 1); // every shape went through at least one reset with the setters
}

// ---------------------------------------------------------------- external bots and statistics in uneven arenas
#include "../GigaLearnCPP/src/private/GigaLearnCPP/ExternalPlayMonitor.h"
#include "../GigaLearnCPP/src/private/GigaLearnCPP/GGLBotOpponent.h"
#include "../GigaLearnCPP/src/private/GigaLearnCPP/NextoOpponent.h"

using namespace GGL;

namespace {
	// One random game state per shape (plus 1v1, 2v2, 3v3), through the live obs path
	struct Fleet {
		std::unique_ptr<EnvSet> set;
		std::vector<TeamSizes> sizes;
		Fleet() {
			sizes = { {1, 1}, {2, 2}, {3, 3} };
			for (int k = 0; k < UNEVEN_SHAPES; k++) sizes.push_back(UnevenShape(k));
			static std::vector<TeamSizes> s_sizes;
			s_sizes = sizes;
			EnvSetConfig cfg = {};
			cfg.numArenas = (int)sizes.size();
			cfg.tickSkip = 4;
			cfg.saveRewards = false;
			cfg.envCreateFn = [](int index) {
				EnvCreateResult r = {};
				r.arena = MakeSized(s_sizes[index]);
				r.actionParser = new DefaultAction();
				r.obsBuilder = new AdvancedObsPadded(3, false, 8);
				r.stateSetter = new RandomState(true, true, false);
				r.terminalConditions = { new GoalScoreCondition() };
				return r;
			};
			set = std::make_unique<EnvSet>(cfg);
		}
	};
}

TEST(Uneven_external_bots_serve_either_side) {
	InitRocketSimUneven();
	if (!std::filesystem::exists("rlbot-run/galactus_v2/POLICY.lt")) return; // models are not in the repository
	Fleet fleet;
	auto& states = fleet.set->state.gameStates;
	torch::Device cpu(torch::kCPU);
	std::vector<std::shared_ptr<ExternalOpponent>> bots = {
		std::make_shared<NextoOpponent>("rlbot-run/nexto/nexto-model.pt", cpu),
		std::make_shared<GGLBotOpponent>(GGLBotKind::GALACTUS_V2, "rlbot-run/galactus_v2", cpu),
		std::make_shared<GGLBotOpponent>(GGLBotKind::CHRONOS_V2, "rlbot-run/chronos_v2", cpu),
		std::make_shared<GGLBotOpponent>(GGLBotKind::DICE, "rlbot-run/dice", cpu),
		std::make_shared<GGLBotOpponent>(GGLBotKind::PULSAR2, "rlbot-run/releases/v1.0.28/checkpoint", cpu),
	};
	const std::vector<uint8_t> fresh(states.size(), 1);
	for (auto& bot : bots) {
		for (Team side : { Team::BLUE, Team::ORANGE }) {
			std::vector<bool> served;
			for (auto& gs : states) {
				int n[2] = {};
				for (auto& p : gs.players) n[(int)p.team]++;
				for (auto& p : gs.players)
					served.push_back(p.team == side && RS_MAX(n[0], n[1]) <= bot->MaxTeamSize());
			}
			std::vector<int> actions(served.size(), -1);
			bot->BeginServe((int)served.size());
			bot->Act(states, served, fresh, actions);
			for (size_t i = 0; i < served.size(); i++) {
				if (served[i]) CHECK(actions[i] >= 0 && actions[i] < 90);
				else CHECK_EQ(actions[i], -1);
			}
		}
	}
}

TEST(Uneven_nexto_batching_gives_each_arena_its_own_answer) {
	InitRocketSimUneven();
	if (!std::filesystem::exists("rlbot-run/nexto/nexto-model.pt")) return;
	Fleet fleet;
	auto& states = fleet.set->state.gameStates;
	NextoOpponent nexto("rlbot-run/nexto/nexto-model.pt", torch::Device(torch::kCPU));
	std::vector<bool> all;
	for (auto& gs : states) all.insert(all.end(), gs.players.size(), true);
	std::vector<int> together(all.size(), -1);
	nexto.BeginServe((int)all.size());
	nexto.Act(states, all, std::vector<uint8_t>(states.size(), 1), together);
	// each arena served on its own must pick the same actions (4-player 2v2 and 3v1 share a batch above)
	size_t at = 0;
	for (auto& gs : states) {
		std::vector<bool> mine(gs.players.size(), true);
		std::vector<int> alone(gs.players.size(), -1);
		nexto.BeginServe((int)mine.size());
		nexto.Act({ gs }, mine, { 1 }, alone);
		for (size_t i = 0; i < alone.size(); i++)
			CHECK_EQ(alone[i], together[at + i]);
		at += gs.players.size();
	}
}

TEST(Uneven_arenas_stay_out_of_the_symmetric_bot_statistics) {
	GameState gs = {};
	for (int i = 0; i < 3; i++) {
		Player p = {};
		p.team = i < 2 ? Team::BLUE : Team::ORANGE;
		gs.players.push_back(p);
	}
	gs.goalScored = true;
	gs.ball.pos = Vec(0, 5200, 100);
	ExternalPlayStats st;
	ExternalPlayMonitor m;
	m.BeginServe(1);
	m.Step({ gs }, { 1 }, { 1 }, Team::ORANGE, 4, st);
	for (int k = 0; k < ExternalPlayStats::NUM_COUNTERS; k++)
		CHECK_EQ(st.Get((ExternalPlayStats::Counter)k), (int64_t)0);
}

TEST(Uneven_wall_setup_car_takes_both_colours_only_in_uneven_arenas) {
	InitRocketSimUneven();
	for (TeamSizes s : { TeamSizes{2, 2}, TeamSizes{2, 1}, TeamSizes{1, 3} }) {
		Arena* arena = MakeSized(s);
		AirPlayState setter(900, 0.5f, 1500, 3000, 1.0f); // wall mode only
		int blue = 0, orange = 0;
		for (int i = 0; i < 400; i++) {
			setter.ResetArena(arena);
			// the setup car is the one behind the rolling ball: grounded, within 900 uu, with its velocity along the ball's
			BallState bs = arena->ball->GetState();
			Vec dir = bs.vel.Normalized();
			for (Car* car : arena->_cars) {
				CarState cs = car->GetState();
				Vec offset = bs.pos - cs.pos;
				offset.z = 0;
				if (cs.vel.Length() > 1.f && offset.Length() < 900.f && offset.Normalized().Dot(dir) > 0.99f)
					(car->team == Team::BLUE ? blue : orange)++;
			}
		}
		if (s.Uneven()) {
			CHECK(blue > 120);
			CHECK(orange > 120);
		} else {
			CHECK(blue > 300);
			CHECK_EQ(orange, 0);
		}
		delete arena;
	}
}
