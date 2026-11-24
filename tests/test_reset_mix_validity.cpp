#include "TestFramework.h"

#include <RLGymCPP/Framework.h>
#include <RLGymCPP/CommonValues.h>
#include <RLGymCPP/StateSetters/KickoffState.h>
#include <RLGymCPP/StateSetters/RandomState.h>
#include <RLGymCPP/StateSetters/BallNearCarState.h>
#include <RLGymCPP/StateSetters/AirDrillState.h>
#include <RLGymCPP/StateSetters/AirPlayState.h>

#include <cmath>
#include <cstdio>
#include <functional>
#include <memory>
#include <vector>

// Validity and team symmetry of every setter in the live reset mix (MakeEnv, src/ExampleMain.cpp), in 1v1 / 2v2 / 3v3.
// Report: docs/GCO_RESET_MIX.md. Real arenas, so run from the repo root (needs build/collision_meshes).
//
// Hard invariants (zero violations allowed): finite state; ball and cars inside the pitch (walls, corner planes, floor,
// ceiling); ball speed <= 6000, car speed <= 2300; boost in [0, 100].
// Overlap (car-car closer than 150 uu, car-ball closer than 165 uu = a spawn already in contact): zero for the placed
// setters; RandomState places every body independently, so it gets a small allowance.
// The test passes on the setters as they are: defects that exist today are pinned as upper bounds in Cases().
// Symmetry: a goal-only reward is zero-sum, so a reset must not favour a colour. Checked on who starts closer to the ball
// and on the ball's mean y and y-velocity, for the setters that are meant to be symmetric. AirPlay wall mode is not (its
// setup car is always blue, by design) and is excluded; AirPlay flip-reset and carry are checked since 2026-11-17.

namespace {
	using namespace RocketSim;
	using namespace RLGC;

	void InitOnce() {
		static bool inited = false;
		if (inited) return;
		for (const char* p : { "build/collision_meshes", "collision_meshes", "../collision_meshes" }) { try { RocketSim::Init(p); inited = true; break; } catch (...) {} }
	}

	struct Stats {
		int resets = 0;
		int hard = 0;          // resets with any hard-invariant violation
		int corner = 0;        // ...of which: a car beyond a 45-degree corner wall (|x|+|y| > 8064 - 40)
		int overlap = 0;       // resets with a car-car or car-ball overlap
		int blueCloser = 0, orangeCloser = 0;
		double ballY = 0, ballVy = 0, ballAbsVy = 0;
	};

	bool InPitch(const Vec& p, float radius, float zMin) {
		using namespace CommonValues;
		return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z)
			&& fabsf(p.x) <= SIDE_WALL_X - radius && fabsf(p.y) <= BACK_WALL_Y - radius
			&& fabsf(p.x) + fabsf(p.y) <= 8064.f - radius
			&& p.z >= zMin && p.z <= CEILING_Z - radius;
	}

	Stats Run(StateSetter& setter, int perTeam, int N) {
		InitOnce();
		Arena* a = Arena::Create(GameMode::SOCCAR);
		for (int i = 0; i < perTeam; i++) { a->AddCar(Team::BLUE); a->AddCar(Team::ORANGE); }
		Stats s;
		for (int n = 0; n < N; n++) {
			setter.ResetArena(a);
			BallState b = a->ball->GetState();
			bool hard = false, overlap = false, cornerHit = false;
			hard |= !InPitch(b.pos, CommonValues::BALL_RADIUS - 1.f, CommonValues::BALL_RADIUS - 1.f);
			hard |= !(b.vel.Length() <= CommonValues::BALL_MAX_SPEED + 1.f);
			std::vector<CarState> cs;
			std::vector<Team> teams;
			for (Car* c : a->_cars) { cs.push_back(c->GetState()); teams.push_back(c->team); }
			float best[2] = { 1e9f, 1e9f };
			for (size_t i = 0; i < cs.size(); i++) {
				hard |= !InPitch(cs[i].pos, 40.f, 16.f);
				cornerHit |= fabsf(cs[i].pos.x) + fabsf(cs[i].pos.y) > 8064.f - 40.f;
				hard |= !(cs[i].vel.Length() <= CommonValues::CAR_MAX_SPEED + 1.f);
				hard |= !(cs[i].boost >= 0.f && cs[i].boost <= 100.f);
				float d = (cs[i].pos - b.pos).Length();
				overlap |= d < 165.f;
				best[(int)teams[i]] = std::min(best[(int)teams[i]], d);
				for (size_t j = i + 1; j < cs.size(); j++)
					overlap |= (cs[i].pos - cs[j].pos).Length() < 150.f;
			}
			s.resets++;
			s.hard += hard; s.overlap += overlap; s.corner += cornerHit;
			if (best[0] < best[1] - 1.f) s.blueCloser++;
			else if (best[1] < best[0] - 1.f) s.orangeCloser++;
			s.ballY += b.pos.y; s.ballVy += b.vel.y; s.ballAbsVy += fabsf(b.vel.y);
		}
		delete a;
		return s;
	}

	// |blue share - 0.5| within 5 sigma of a fair coin over the decided resets
	bool Fair(const Stats& s) {
		int n = s.blueCloser + s.orangeCloser;
		if (n < 100) return true; // equidistant by construction (kickoffs)
		double share = (double)s.blueCloser / n;
		return fabs(share - 0.5) <= 5.0 * 0.5 / sqrt((double)n);
	}

	// maxHard / maxOverlap: allowed share of resets with a hard violation / an overlap. Zero unless a KNOWN DEFECT is pinned.
	// Fixed 2026-11-17 (docs/GCO_RESET_MIX.md P2, P3), now held at 0 here:
	//   corner escapes: support / climber / air cars were bounds-checked against the side and back walls only (0.3..1.8% of
	//     resets beyond a 45-degree corner wall); placement now also rejects |x|+|y| > CORNER_SPAWN_MAX (StateSetter.h).
	//   AirPlay flip-reset / carry: the second air-player was lifted 350 uu off the first one's spot (37% inside the ball,
	//     orange closer 95% / blue closer 67%); the two now take mirrored slots, so both modes are checked for colour fairness.
	// Still pinned: AirPlay wall, the setup car is always blue (by design, so not symmetric); rare wall-mode overlaps.
	struct Case { const char* name; std::function<std::unique_ptr<StateSetter>()> make; float maxHard, maxOverlap; bool symmetric; };
	std::vector<Case> Cases() {
		return {
			{ "BallNearCar", [] { return std::make_unique<BallNearCarState>(600, 900); }, 0.f, 0.f, true },
			{ "AirDrill_contest", [] { auto s = std::make_unique<AirDrillState>(); s->soloFrac = 0.f; return s; }, 0.f, 0.f, true },
			{ "AirDrill_solo", [] { auto s = std::make_unique<AirDrillState>(); s->soloFrac = 1.f; return s; }, 0.f, 0.f, true },
			{ "AirPlay_flip", [] { return std::make_unique<AirPlayState>(900, 1.0f, 1500, 3000, 0.0f); }, 0.f, 0.f, true },
			{ "AirPlay_carry", [] { return std::make_unique<AirPlayState>(900, 0.0f, 1500, 3000, 0.0f); }, 0.f, 0.f, true },
			{ "AirPlay_wall", [] { return std::make_unique<AirPlayState>(900, 0.5f, 1500, 3000, 1.0f); }, 0.f, 0.002f, false },
			{ "Kickoff", [] { return std::make_unique<KickoffState>(); }, 0.f, 0.f, true },
			{ "Random", [] { return std::make_unique<RandomState>(true, true, false); }, 0.f, 0.01f, true }, // independent placement: rare overlaps
		};
	}
}

TEST(ResetMix_states_are_valid_in_every_mode) {
	const int N = 4000;
	bool ok = true;
	for (auto& c : Cases())
		for (int perTeam = 1; perTeam <= 3; perTeam++) {
			auto setter = c.make();
			Stats s = Run(*setter, perTeam, N);
			bool fine = s.hard <= (int)(c.maxHard * N) && s.overlap <= (int)(c.maxOverlap * N) && s.hard == s.corner;
			printf("  [reset-mix] %-17s %dv%d  hard %d (corner %d)  overlap %d  blue-closer %.3f (n=%d)%s\n", c.name, perTeam, perTeam,
				s.hard, s.corner, s.overlap, (s.blueCloser + s.orangeCloser) ? (double)s.blueCloser / (s.blueCloser + s.orangeCloser) : 0.5,
				s.blueCloser + s.orangeCloser, fine ? "" : "   <-- above the allowed share");
			ok &= fine; // s.hard == s.corner: the only hard violation ever allowed is the pinned corner escape
		}
	CHECK(ok);
}

TEST(ResetMix_setters_do_not_favour_a_colour) {
	const int N = 6000;
	for (auto& c : Cases())
		for (int perTeam = 1; perTeam <= 3; perTeam++) {
			auto setter = c.make();
			Stats s = Run(*setter, perTeam, N);
			if (c.symmetric) {
				if (!Fair(s)) printf("  [reset-mix] UNFAIR %s %dv%d blue %d orange %d\n", c.name, perTeam, perTeam, s.blueCloser, s.orangeCloser);
				CHECK(Fair(s));
				// the ball does not sit or travel toward one end on average (5 sigma; y is within +-4800, |vy| per setter)
				CHECK(fabs(s.ballY / N) <= 5.0 * 4800.0 / sqrt(3.0 * N));
				double vyScale = std::max(1.0, 2.0 * s.ballAbsVy / N);
				CHECK(fabs(s.ballVy / N) <= 5.0 * vyScale / sqrt((double)N));
			}
		}
}
