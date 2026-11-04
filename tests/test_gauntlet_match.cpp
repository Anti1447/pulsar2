#include "TestFramework.h"

#include "../GigaLearnCPP/src/private/GigaLearnCPP/MatchLoop.h"

#include <RLGymCPP/EnvSet/EnvSet.h>
#include <RLGymCPP/TerminalConditions/NoTouchCondition.h>
#include <RLGymCPP/TerminalConditions/GoalScoreCondition.h>
#include <RLGymCPP/ObsBuilders/AdvancedObsPadded.h>
#include <RLGymCPP/StateSetters/KickoffState.h>
#include <RLGymCPP/ActionParsers/DefaultAction.h>

#include <filesystem>

using namespace RLGC;
using namespace GGL;

// The match loop shared by the sim round robin and the in-trainer gauntlet (MatchLoop.h), with scripted controllers whose
// outcome is known: a car that drives straight at the ball with boost always touches it first against a car that never
// moves. That pins down the bookkeeping the gauntlet's numbers rest on: games add up, first touch is credited to the
// right CONTROLLER (not the right colour) when the sides alternate, and the seeded scope puts the engine back.

namespace {
	void EnsureRocketSimGauntlet() {
		static bool inited = false;
		if (inited)
			return;
		for (const char* path : { "collision_meshes", "../collision_meshes", "build/collision_meshes" })
			if (std::filesystem::exists(std::filesystem::path(path) / "soccar")) {
				RocketSim::Init(path);
				break;
			}
		inited = true;
	}

	// The ball already rolling at the ORANGE goal (+y), both cars parked far from its path: blue scores, whoever controls blue
	class BallShotState : public StateSetter {
	public:
		void ResetArena(Arena* arena) override {
			arena->ResetToRandomKickoff();
			BallState bs = arena->ball->GetState();
			bs.pos = Vec(0, 1500, 93);
			bs.vel = Vec(0, 2500, 0);
			bs.angVel = Vec(0, 0, 0);
			arena->ball->SetState(bs);
			for (Car* car : arena->GetCars()) {
				CarState cs = car->GetState();
				cs.pos = Vec(3000, car->team == Team::BLUE ? -3000.f : 3000.f, 17);
				cs.vel = Vec(0, 0, 0);
				cs.angVel = Vec(0, 0, 0);
				car->SetState(cs);
			}
		}
	};

	std::unique_ptr<EnvSet> MakeKickoffEnv(int arenas, bool ballShot = false) {
		EnvSetConfig cfg = {};
		cfg.numArenas = arenas;
		cfg.tickSkip = 8;
		cfg.actionDelay = 7;
		cfg.saveRewards = false;
		cfg.envCreateFn = [ballShot](int) {
			auto arena = Arena::Create(GameMode::SOCCAR);
			arena->AddCar(Team::BLUE);
			arena->AddCar(Team::ORANGE);
			EnvCreateResult r = {};
			r.arena = arena;
			r.actionParser = new DefaultAction();
			r.obsBuilder = new AdvancedObsPadded(3);
			if (ballShot) r.stateSetter = new BallShotState(); else r.stateSetter = new KickoffState();
			r.terminalConditions = { new NoTouchCondition(10), new GoalScoreCondition() };
			return r;
		};
		return std::make_unique<EnvSet>(cfg);
	}

	// DefaultAction index of the row matching `pick`
	template<class F>
	int FindAction(F pick) {
		DefaultAction parser;
		for (int i = 0; i < parser.GetActionAmount(); i++)
			if (pick(parser.actions[i]))
				return i;
		return -1;
	}

	// Plays one fixed action index for every player it owns
	struct Fixed {
		int action;
		void BeginServe(int) {}
		void Act(const std::vector<GameState>&, const std::vector<bool>& mine, const std::vector<uint8_t>&, std::vector<int>& out) {
			for (int p = 0; p < (int)mine.size(); p++)
				if (mine[p])
					out[p] = action;
		}
	};

	// Drives at the ball with boost, steering toward it on the ground plane (a straight line misses most kickoff layouts)
	struct Chaser {
		int left, straight, right; // throttle 1 + boost, steer -1 / 0 / +1
		void BeginServe(int) {}
		void Act(const std::vector<GameState>& states, const std::vector<bool>& mine, const std::vector<uint8_t>&, std::vector<int>& out) {
			int p = 0;
			for (auto& gs : states)
				for (auto& pl : gs.players) {
					if (mine[p]) {
						Vec d = gs.ball.pos - pl.pos;
						Vec fwd = pl.rotMat.forward;
						// Unreal's axes are left-handed (x forward, y right), so a positive cross product means the ball is to the RIGHT
						float cross = fwd.x * d.y - fwd.y * d.x;
						float len = sqrtf(d.x * d.x + d.y * d.y) + 1e-3f;
						out[p] = (fabsf(cross) / len < 0.1f) ? straight : (cross > 0 ? right : left);
					}
					p++;
				}
		}
	};

	Chaser MakeChaser() {
		auto find = [](float steer) {
			return FindAction([steer](const auto& a) { return a.throttle == 1.f && a.boost && a.steer == steer && a.yaw == steer && !a.jump && a.pitch == 0.f && a.roll == 0.f; });
		};
		return Chaser{ find(-1.f), find(0.f), find(1.f) };
	}
}

TEST(Gauntlet_match_loop_credits_the_controller_not_the_colour) {
	EnsureRocketSimGauntlet();
	int idle = FindAction([](const auto& a) { return a.throttle == 0.f && !a.boost && a.steer == 0.f && !a.jump && a.pitch == 0.f && a.yaw == 0.f && a.roll == 0.f; });
	CHECK(idle >= 0);

	const int N = 8;
	auto env = MakeKickoffEnv(N);
	Chaser chaser = MakeChaser();
	Fixed still{ idle };

	// Blue everywhere, then alternating sides, then the roles swapped: first touch must follow the controller each time
	for (int mode = 0; mode < 3; mode++) {
		MatchConfig mc;
		mc.maxSeconds = 40.f;
		mc.alternateSides = (mode == 1);
		MatchResult r = (mode == 2) ? PlayMatch(*env, still, chaser, N, mc) : PlayMatch(*env, chaser, still, N, mc);
		int chaserTouches = (mode == 2) ? r.firstTouchB : r.firstTouchA;
		int stillTouches = (mode == 2) ? r.firstTouchA : r.firstTouchB;
		CHECK_EQ(r.games, N);
		CHECK_EQ(r.winsA + r.winsB + r.draws, N);          // every game is exactly one of win / loss / draw
		CHECK_EQ(chaserTouches, N);                        // the car that drives at the ball always gets there first...
		CHECK_EQ(stillTouches, 0);                         // ...and the parked one never does
		CHECK_EQ(r.firstTouchBoth, 0);
		CHECK((mode == 2 ? r.touchStepsB : r.touchStepsA) > 0.0);
		CHECK(r.seconds > 0.0);
	}
	env->Sync();
}

TEST(Gauntlet_match_loop_goal_goes_to_the_controller_that_scored) {
	EnsureRocketSimGauntlet();
	int idle = FindAction([](const auto& a) { return a.throttle == 0.f && !a.boost && a.steer == 0.f && !a.jump && a.pitch == 0.f && a.yaw == 0.f && a.roll == 0.f; });
	const int N = 8;
	auto env = MakeKickoffEnv(N, true);
	Fixed a{ idle }, b{ idle };

	// Every game is the same: blue scores (the ball rolls into the orange goal) and nobody touches anything.
	// Blue everywhere: A is blue, so A wins all of them.
	MatchConfig mc;
	mc.maxSeconds = 30.f;
	MatchResult fixedSides = PlayMatch(*env, a, b, N, mc);
	CHECK_EQ(fixedSides.winsA, N);
	CHECK_EQ(fixedSides.winsB, 0);
	CHECK_EQ(fixedSides.draws, 0);

	// Alternating: A is blue in the even arenas (wins) and orange in the odd ones (loses to the same goal)
	mc.alternateSides = true;
	MatchResult alternating = PlayMatch(*env, a, b, N, mc);
	CHECK_EQ(alternating.winsA, N / 2);
	CHECK_EQ(alternating.winsB, N / 2);
	CHECK_EQ(alternating.draws, 0);
	// No car ever touched the ball, so there is no first touch to credit
	CHECK_EQ(alternating.firstTouchA + alternating.firstTouchB + alternating.firstTouchBoth, 0);
	env->Sync();
}

TEST(Gauntlet_seeded_scope_restores_the_random_engine) {
	auto& eng = RocketSim::Math::GetRandEngine();
	eng.seed(12345);
	eng.discard(7);
	const std::default_random_engine before = eng;

	std::vector<unsigned> first, second;
	{
		ScopedRandSeed s(99);
		for (int i = 0; i < 5; i++) first.push_back((unsigned)eng());
	}
	CHECK(eng == before); // the training's stream is back exactly where it was

	{
		ScopedRandSeed s(99);
		for (int i = 0; i < 5; i++) second.push_back((unsigned)eng());
	}
	CHECK(first == second); // same seed, same sequence: the gauntlet's games repeat
	CHECK(eng == before);

	unsigned continued = (unsigned)eng();
	std::default_random_engine ref = before;
	CHECK_EQ(continued, (unsigned)ref()); // and the next draw is what it would have been without the gauntlet
}
