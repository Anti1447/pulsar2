#include "TestFramework.h"

#include <RLGymCPP/Framework.h>
#include <RLGymCPP/CommonValues.h>
#include <RLGymCPP/StateSetters/NearGoalState.h>

#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

// NearGoalState (docs/GCO_PURE_SELFPLAY_ARM.md section 9): validity in 1v1 / 2v2 / 3v3, colour symmetry, geometry, and how often a goal is scored
// by pure ball motion with every car idle (a "free goal" the setter must not hand out). Real arenas: run from the repo root (needs build/collision_meshes).

namespace {
	using namespace RocketSim;
	using namespace RLGC;

	void InitOnce() {
		static bool inited = false;
		if (inited) return;
		for (const char* p : { "build/collision_meshes", "collision_meshes", "../collision_meshes" }) { try { RocketSim::Init(p); inited = true; break; } catch (...) {} }
	}

	bool InPitch(const Vec& p, float radius, float zMin) {
		using namespace CommonValues;
		return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z)
			&& fabsf(p.x) <= SIDE_WALL_X - radius && fabsf(p.y) <= BACK_WALL_Y - radius
			&& fabsf(p.x) + fabsf(p.y) <= 8064.f - radius
			&& p.z >= zMin && p.z <= CEILING_Z - radius;
	}

	struct Stats {
		int resets = 0, hard = 0, overlap = 0;
		int ballOnOrangeHalf = 0;      // ball y > 0 (blue attacking)
		int attackerClose = 0;         // some car within 1200 uu of the ball (3D)
		int attackerCloseXY = 0;       // same, horizontal distance only (the ball may be airborne)
		int defenderInZone = 0;        // a car of the defending side in the goal mouth zone
		double meanDepth = 0, meanSpeed = 0;
	};

	Stats Run(NearGoalState& setter, int perTeam, int N) {
		InitOnce();
		Arena* a = Arena::Create(GameMode::SOCCAR);
		for (int i = 0; i < perTeam; i++) { a->AddCar(Team::BLUE); a->AddCar(Team::ORANGE); }
		Stats s;
		for (int n = 0; n < N; n++) {
			setter.ResetArena(a);
			BallState b = a->ball->GetState();
			bool hard = !InPitch(b.pos, CommonValues::BALL_RADIUS - 1.f, CommonValues::BALL_RADIUS - 1.f) || !(b.vel.Length() <= CommonValues::BALL_MAX_SPEED + 1.f);
			bool overlap = false, close = false, closeXY = false;
			std::vector<CarState> cs;
			for (Car* c : a->_cars) cs.push_back(c->GetState());
			for (size_t i = 0; i < cs.size(); i++) {
				hard |= !InPitch(cs[i].pos, 40.f, 16.f);
				hard |= !(cs[i].boost >= 0.f && cs[i].boost <= 100.f);
				float d = (cs[i].pos - b.pos).Length();
				overlap |= d < 165.f;
				close |= d <= 1200.f;
				closeXY |= (Vec(cs[i].pos.x, cs[i].pos.y, 0) - Vec(b.pos.x, b.pos.y, 0)).Length() <= 1200.f;
				for (size_t j = i + 1; j < cs.size(); j++) overlap |= (cs[i].pos - cs[j].pos).Length() < 150.f;
			}
			s.resets++; s.hard += hard; s.overlap += overlap; s.attackerClose += close; s.attackerCloseXY += closeXY;
			s.ballOnOrangeHalf += b.pos.y > 0;
			s.meanDepth += CommonValues::GOAL_LINE_Y - fabsf(b.pos.y);
			s.meanSpeed += b.vel.Length();
		}
		delete a;
		s.meanDepth /= s.resets; s.meanSpeed /= s.resets;
		return s;
	}
}

TEST(NearGoal_states_are_valid_in_every_mode) {
	for (int perTeam = 1; perTeam <= 3; perTeam++) {
		NearGoalState setter;
		Stats s = Run(setter, perTeam, 4000);
		printf("  [near-goal] %dv%d  hard %d  overlap %d  contact-range %.3f  mean depth %.0f  mean ball speed %.0f\n", perTeam, perTeam, s.hard, s.overlap,
			(double)s.attackerClose / s.resets, s.meanDepth, s.meanSpeed);
		CHECK_EQ(s.hard, 0);
		CHECK_EQ(s.overlap, 0);
		CHECK(s.attackerClose >= (int)(0.95 * s.resets)); // the attacker's contester is within 500..1100 uu of the ball
		CHECK(s.meanDepth > 600.f && s.meanDepth < 2200.f);
	}
}

TEST(NearGoal_far_variant_is_valid_and_deeper) {
	// The "far" sibling slice: same setter, ball 1600..3200 uu in front of the attacked goal line (the near setter is 600..2200).
	for (int perTeam = 1; perTeam <= 3; perTeam++) {
		NearGoalState setter(1600, 3200);
		Stats s = Run(setter, perTeam, 4000);
		printf("  [near-goal far] %dv%d  hard %d  overlap %d  contact-range 3D %.3f horizontal %.3f  mean depth %.0f\n", perTeam, perTeam, s.hard, s.overlap,
			(double)s.attackerClose / s.resets, (double)s.attackerCloseXY / s.resets, s.meanDepth);
		CHECK_EQ(s.hard, 0);
		CHECK_EQ(s.overlap, 0);
		CHECK(s.attackerCloseXY >= (int)(0.95 * s.resets)); // horizontal: the ball is airborne in 45% of resets, which inflates the 3D distance
		CHECK(s.meanDepth > 1600.f && s.meanDepth < 3200.f);
	}
}

TEST(NearGoal_does_not_favour_a_colour) {
	NearGoalState setter;
	Stats s = Run(setter, 1, 6000);
	double share = (double)s.ballOnOrangeHalf / s.resets; // blue attacks +y
	printf("  [near-goal] blue-attacking share %.3f (n=%d)\n", share, s.resets);
	CHECK(fabs(share - 0.5) <= 5.0 * 0.5 / sqrt((double)s.resets));
}

TEST(NearGoal_idle_cars_rarely_get_a_free_goal) {
	InitOnce();
	Arena* a = Arena::Create(GameMode::SOCCAR);
	a->AddCar(Team::BLUE); a->AddCar(Team::ORANGE);
	NearGoalState setter;
	const int N = 600;
	int scoredFree = 0, scoredAny = 0;
	for (int n = 0; n < N; n++) {
		setter.ResetArena(a);
		for (Car* c : a->_cars) c->controls = {};
		bool scored = false;
		for (int t = 0; t < 45 && !scored; t++) { // 45 decision steps of 8 ticks = 3 s
			a->Step(8);
			scored = a->IsBallScored();
		}
		scoredFree += scored;
	}
	(void)scoredAny;
	printf("  [near-goal] idle cars, goal within 3 s of the reset: %d/%d = %.3f\n", scoredFree, N, (double)scoredFree / N);
	CHECK((double)scoredFree / N < 0.25);
	delete a;
}
