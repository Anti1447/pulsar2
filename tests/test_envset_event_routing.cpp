#include "TestFramework.h"

#include <RLGymCPP/Framework.h>
#include <RLGymCPP/EnvSet/EnvSet.h>
#include <RLGymCPP/Lab/LabEnv.h>
#include <RLGymCPP/StateSetters/KickoffState.h>
#include <RLGymCPP/Telemetry/DemoTelemetry.h>
#include <chrono>
#include <thread>
#include <vector>

// Do game events land in the arena they happened in? (2026-11-11, after the RLGymBot_v3 EnvSet bug report: arenas are built on the thread pool,
// and a per-arena index taken from the JOB number instead of the arena's slot routes every goal/demo/save/shot/bump flag to another match.)
// A REAL EnvSet with 48 arenas (a blue and an orange car each, idle). One arena at a time gets a forced demo (a supersonic blue car into a
// stationary orange car 180 uu ahead); one decision step later the demo/demoed flags must be on those two cars in THAT arena's GameState and
// on nobody in any other arena. Creation sleeps in reverse job order so a completion-order append would scramble the slots.
// Also asserts the structural invariant behind it: arenas[k] is the arena the create function built for job k.
// Run from the repo root (needs build/collision_meshes). Mutation check: see docs/reward_scenarios/DemoCredit_2026-11-11.md.

namespace {
	using namespace RLGC;
	using namespace RocketSim;

	bool AnyEventFlag(const GameState& gs) {
		for (const Player& p : gs.players) {
			const PlayerEventState& e = p.eventState;
			if (e.goal || e.save || e.assist || e.shot || e.shotPass || e.bump || e.bumped || e.demo || e.demoed) return true;
		}
		return false;
	}
}

TEST(EnvSetRouting_events_land_in_the_arena_they_happened_in) {
	CHECK(Lab::InitEngine());
	const int N = 48;
	std::vector<Arena*> created(N, nullptr);
	EnvSetConfig cfg = {};
	cfg.numArenas = N; cfg.tickSkip = 8; cfg.actionDelay = 0; cfg.saveRewards = false;
	cfg.envCreateFn = [&](int idx) {
		std::this_thread::sleep_for(std::chrono::microseconds((N - 1 - idx) * 400)); // job N-1 finishes first, job 0 last
		Arena* a = Arena::Create(GameMode::SOCCAR);
		a->AddCar(Team::BLUE); a->AddCar(Team::ORANGE);
		created[idx] = a;
		EnvCreateResult r = {};
		r.arena = a;
		r.terminalConditions = { new GoalScoreCondition() };
		r.obsBuilder = new AdvancedObsPadded(3, /*shuffleSlots=*/false, 0);
		r.actionParser = new DefaultAction();
		r.stateSetter = new KickoffState();
		r.userInfo = NULL;
		return r;
	};
	EnvSet env(cfg);

	for (int k = 0; k < N; k++) CHECK(env.arenas[k] == created[k]);               // slot == job index
	for (int k = 0; k < N; k++) CHECK_EQ(env.eventCallbackInfos[k]->arenaIdx, k); // and the callbacks know their slot

	for (int k : { 0, 1, 17, 30, 47 }) {
		Arena* a = env.arenas[k];
		Car* bumper = nullptr; Car* victim = nullptr; int bumperPlayer = -1;
		for (size_t i = 0; i < a->_cars.size(); i++) {
			Car* c = a->_cars[i];
			if (c->team == Team::BLUE) { bumper = c; bumperPlayer = env.state.arenaPlayerStartIdx[k] + (int)i; } else victim = c;
		}
		CHECK(bumper && victim);

		// Blue car 450 uu behind a stationary orange car, driving at it at supersonic speed. It takes a few steps to arrive, so the demo
		// step has a genuine previous state (a teleport on the demo step itself would leave `prev` showing the old positions).
		CarState b = bumper->GetState();
		b.pos = Vec(0, -1270, 17); b.vel = Vec(0, 2300, 0); b.angVel = Vec(0, 0, 0);
		b.rotMat = Angle(1.57079633f, 0, 0).ToRotMat(); b.isOnGround = true; b.isSupersonic = true; b.boost = 100; bumper->SetState(b);
		CarState v = victim->GetState();
		v.pos = Vec(0, -820, 17); v.vel = Vec(0, 0, 0); v.angVel = Vec(0, 0, 0);
		v.rotMat = Angle(-1.57079633f, 0, 0).ToRotMat(); v.isOnGround = true; v.isDemoed = false; victim->SetState(v);
		BallState bs = BallState(); bs.pos = Vec(2500, 3500, 93); a->ball->SetState(bs);

		const int n = env.state.numPlayers;
		env.controlOverrideMask.assign(n, 1);
		env.controlOverrides.assign(n, Action());
		env.controlOverrides[bumperPlayer].throttle = 1.0f; env.controlOverrides[bumperPlayer].boost = 1.0f; // hold supersonic speed

		const Player *pb = nullptr, *pv = nullptr;
		int demoStep = -1;
		for (int step = 0; step < 12 && demoStep < 0; step++) {
			env.StepFirstHalf(false);
			env.StepSecondHalf(IList(n, 0), false);
			const GameState& gs = env.state.gameStates[k];
			pb = pv = nullptr;
			for (const Player& p : gs.players) (p.team == Team::BLUE ? pb : pv) = &p;
			if (pv && pv->eventState.demoed) demoStep = step;
			if (demoStep < 0) for (int j = 0; j < N; j++) CHECK(!AnyEventFlag(env.state.gameStates[j]));   // nothing flagged anywhere before the hit
		}
		CHECK(demoStep >= 1);                     // it needed a few steps to arrive
		CHECK(pb && pv);
		if (!(pb && pv && demoStep >= 0)) continue;
		const GameState& gs = env.state.gameStates[k];
		CHECK(pb->eventState.demo);               // credited to the bumper
		CHECK(pv->eventState.demoed);             // and the victim
		CHECK(pv->isDemoed);                      // the car state agrees
		CHECK(!pv->eventState.demo && !pb->eventState.demoed);
		for (int j = 0; j < N; j++)
			if (j != k) CHECK(!AnyEventFlag(env.state.gameStates[j]));   // no other arena got a flag

		// the new telemetry sees a clean, plausible credit
		DemoTelemetry::CreditCheck chk = DemoTelemetry::StateEventCheck(gs);
		CHECK(chk.evaluable && chk.anything && !chk.mismatch);
		CHECK(DemoTelemetry::DemoPlausible(*pb, *pv));
	}
}
