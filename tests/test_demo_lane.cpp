#include "TestFramework.h"
#include "RewardTestUtil.h"

#include <RLGymCPP/Framework.h>
#include <RLGymCPP/StateSetters/DemoLaneState.h>
#include <RLGymCPP/Telemetry/DemoFunnelTelemetry.h>
#include <cmath>
#include <vector>

// DemoLaneState (reset setter) and DemoFunnelTelemetry (measurement), 2026-11-13. Run from the repo root (needs build/collision_meshes).
using namespace RLGC;
using namespace RewardTest;
namespace DF = RLGC::DemoFunnel;

namespace {
	void InitOnce() {
		static bool inited = false;
		if (inited) return;
		for (const char* p : { "build/collision_meshes", "collision_meshes", "../collision_meshes" }) { try { RocketSim::Init(p); inited = true; break; } catch (...) {} }
	}
}

TEST(DemoLane_invariants_over_many_resets) {
	InitOnce();
	struct Cfg { int blue, orange; };
	for (Cfg cfg : { Cfg{ 1, 1 }, Cfg{ 2, 2 }, Cfg{ 3, 3 } }) {
		Arena* a = Arena::Create(GameMode::SOCCAR);
		for (int i = 0; i < cfg.blue; i++) a->AddCar(Team::BLUE);
		for (int i = 0; i < cfg.orange; i++) a->AddCar(Team::ORANGE);
		DemoLaneState setter;
		int blueAtk = 0, orangeAtk = 0;
		const int N = 1500;
		for (int n = 0; n < N; n++) {
			setter.ResetArena(a);
			BallState b = a->ball->GetState();
			CHECK(b.vel.Length() < 1e-3f); CHECK(b.pos.z < 120.f);
			// the attacker is the one car moving >= 999 uu/s at a boosted state; every other car is at rest or the drifting victim (<= 601)
			int attackers = 0;
			Vec attPos, attVel; Team attTeam = Team::BLUE; float attBoost = 0;
			std::vector<CarState> states; std::vector<Team> teams;
			for (Car* c : a->_cars) {
				CarState s = c->GetState(); states.push_back(s); teams.push_back(c->team);
				CHECK(fabsf(s.pos.x) < 4096.f && fabsf(s.pos.y) < 5120.f);
				if (s.vel.Length() >= 999.f) { attackers++; attPos = s.pos; attVel = s.vel; attTeam = c->team; attBoost = s.boost; }
			}
			CHECK_EQ(attackers, 1);
			CHECK(attVel.Length() <= 2001.f); CHECK(attBoost >= 59.f);
			(attTeam == Team::BLUE ? blueAtk : orangeAtk)++;
			// exactly one opposing car lies roughly ahead of the attacker at 2000..2800 uu, within 50 deg of its velocity: the victim
			int victims = 0;
			for (size_t i = 0; i < states.size(); i++) {
				if (teams[i] == attTeam) continue;
				Vec d = states[i].pos - attPos; d.z = 0;
				float L = d.Length();
				if (L >= 1999.f && L <= 2801.f && d.Dot(Vec(attVel.x, attVel.y, 0).Normalized()) / L > cosf(0.9f)) {
					// goal-side of the ball in the attacker's attack direction (BLUE +y); a second opposing car may also fall in the cone by chance, so require >= 1
					float s = attTeam == Team::BLUE ? 1.f : -1.f;
					float dy = (states[i].pos.y - b.pos.y) * s;
					if (dy >= 499.f && dy <= 1001.f) victims++;
				}
			}
			CHECK(victims >= 1);
			CHECK((attPos - b.pos).Length() >= 999.f);
			// no two cars overlap
			for (size_t i = 0; i < states.size(); i++)
				for (size_t j = i + 1; j < states.size(); j++)
					CHECK((states[i].pos - states[j].pos).Length() > 300.f);
		}
		// both teams get the attacker role about equally (fair coin over 1500 resets: > 600 each is a 10-sigma margin)
		CHECK(blueAtk > 600 && orangeAtk > 600);
		delete a;
	}
}

// ---- funnel telemetry on fabricated states ----
namespace {
	struct Run {
		std::vector<GameState> frames;
		Run() { frames.reserve(64); }
		// a 1v1 frame: blue at x, moving +y at speed (supersonic flag as given), orange at (0, oy) at rest
		GameState& Add(float by, float speed, bool supersonic, float oy, bool demoNow = false) {
			frames.emplace_back();
			GameState& g = frames.back();
			g.deltaTime = STEP_DT;
			Player a = MakePlayer(0, 0, Team::BLUE, Vec(0, by, 17));
			a.vel = Vec(0, speed, 0); a.isSupersonic = supersonic; a.eventState.demo = demoNow;
			Player b = MakePlayer(1, 1, Team::ORANGE, Vec(0, oy, 17));
			g.players = { a, b };
			if (frames.size() > 1) {
				g.prev = &frames[frames.size() - 2];
				for (size_t i = 0; i < g.players.size(); i++) g.players[i].prev = &g.prev->players[i];
			}
			return g;
		}
	};
}

TEST(DemoFunnel_stage_boundaries) {
	GameState g; Player a = MakePlayer(0, 0, Team::BLUE, Vec(0, 0, 17)); Player b = MakePlayer(1, 1, Team::ORANGE, Vec(0, DF::NEAR_DIST * 0.9f, 17));
	a.vel = Vec(0, DF::CLOSING_SPEED * 1.1f, 0); a.isSupersonic = true; g.players = { a, b };
	DF::Stage s = DF::Evaluate(g.players[0], g); CHECK(s.near && s.closing && s.supersonic);
	g.players[1].pos.y = DF::NEAR_DIST * 1.1f; s = DF::Evaluate(g.players[0], g); CHECK(!s.near && !s.closing && !s.supersonic); // out of range
	g.players[1].pos.y = DF::NEAR_DIST * 0.9f; g.players[0].vel.y = DF::CLOSING_SPEED * 0.9f;
	s = DF::Evaluate(g.players[0], g); CHECK(s.near && !s.closing && !s.supersonic);                                               // too slow
	g.players[0].vel.y = DF::CLOSING_SPEED * 1.1f; g.players[0].isSupersonic = false;
	s = DF::Evaluate(g.players[0], g); CHECK(s.near && s.closing && !s.supersonic);                                                // not supersonic
	g.players[0].isSupersonic = true; g.players[0].vel.y = -3000;                                                                  // driving away
	s = DF::Evaluate(g.players[0], g); CHECK(s.near && !s.closing);
	g.players[0].vel.y = 3000; g.players[1].isDemoed = true;                                                                       // victim already demoed
	s = DF::Evaluate(g.players[0], g); CHECK(!s.near);
}
TEST(DemoFunnel_attempt_converts_when_demo_lands_inside_the_window) {
	Run r; DF::ArenaMem m; int attempts = 0; std::vector<char> closed, unat;
	auto adv = [&](GameState& g) { auto o = DF::Advance(m, g); attempts += o.attempts; for (char c : o.closed) closed.push_back(c); for (char c : o.unattempted) unat.push_back(c); };
	adv(r.Add(0, 2300, true, 3000));        // far: no stage
	adv(r.Add(1800, 2300, true, 3000));     // 1200 away: edge -> attempt opens (distance == NEAR_DIST counts)
	adv(r.Add(2300, 2300, true, 3000));
	adv(r.Add(2900, 2300, true, 3000, true)); // demo credited on step 3 of the window
	CHECK_EQ(attempts, 1); CHECK_EQ((int)closed.size(), 1); CHECK_EQ((int)closed[0], 1); CHECK_EQ((int)unat.size(), 1); CHECK_EQ((int)unat[0], 0);
}
TEST(DemoFunnel_attempt_expires_without_a_demo_and_a_late_demo_is_unattempted) {
	Run r; DF::ArenaMem m; int attempts = 0; std::vector<char> closed, unat;
	auto adv = [&](GameState& g) { auto o = DF::Advance(m, g); attempts += o.attempts; for (char c : o.closed) closed.push_back(c); for (char c : o.unattempted) unat.push_back(c); };
	adv(r.Add(1800, 2300, true, 3000));
	for (int i = 0; i < DF::ATTEMPT_STEPS + 1; i++) adv(r.Add(1800, 2300, true, 3000)); // stays in stage 3 the whole time: ONE attempt, no re-edge
	CHECK_EQ(attempts, 1); CHECK_EQ((int)closed.size(), 1); CHECK_EQ((int)closed[0], 0);
	adv(r.Add(1800, 2300, true, 3000, true));                                          // demo after expiry, no open attempt
	CHECK_EQ((int)unat.size(), 1); CHECK_EQ((int)unat[0], 1);
}
TEST(DemoFunnel_first_step_of_an_episode_forgets_open_attempts) {
	Run r; DF::ArenaMem m;
	DF::Advance(m, r.Add(1800, 2300, true, 3000));
	CHECK(m.open[0] > 0);
	GameState fresh; fresh.deltaTime = STEP_DT; // no prev
	fresh.players = { MakePlayer(0, 0, Team::BLUE, Vec(0, -2000, 17)), MakePlayer(1, 1, Team::ORANGE, Vec(0, 2000, 17)) };
	auto o = DF::Advance(m, fresh);
	CHECK_EQ(m.open[0], 0); CHECK_EQ(o.attempts, 0); CHECK(o.closed.empty());
}
