#include "TestFramework.h"

#include "../GigaLearnCPP/src/private/GigaLearnCPP/NextoKickoff.h"
#include "../GigaLearnCPP/src/private/GigaLearnCPP/MatchLoop.h"

#include <RLGymCPP/EnvSet/EnvSet.h>
#include <RLGymCPP/TerminalConditions/NoTouchCondition.h>
#include <RLGymCPP/TerminalConditions/GoalScoreCondition.h>
#include <RLGymCPP/ObsBuilders/AdvancedObsPadded.h>
#include <RLGymCPP/StateSetters/KickoffState.h>
#include <RLGymCPP/ActionParsers/DefaultAction.h>

#include <filesystem>
#include <iostream>
#include <tuple>
#include <map>
#include <cmath>
#include <algorithm>

using namespace RLGC;
using namespace GGL;

// Nexto's hardcoded kickoff (NextoKickoff.h): the torch-free part, i.e. the script table, the taker decision, the step
// bookkeeping and what the script does in the real sim. The reference is rlbot-run/nexto/bot.py.

namespace {
	void EnsureRocketSimKickoff() {
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

	// A hand-built state: the ball at the centre, cars at the given (x, y), team per car
	GameState MakeState(const std::vector<std::tuple<Team, float, float>>& cars) {
		GameState gs;
		gs.ball.pos = Vec(0, 0, 93.15f);
		gs.ball.vel = Vec(0, 0, 0);
		for (size_t i = 0; i < cars.size(); i++) {
			Player p;
			p.team = std::get<0>(cars[i]);
			p.pos = Vec(std::get<1>(cars[i]), std::get<2>(cars[i]), 17);
			p.vel = Vec(0, 0, 0);
			p.index = (int)i;
			gs.players.push_back(p);
		}
		return gs;
	}

	int FindRow(DefaultAction& parser, const std::array<float, 8>& row) {
		for (int i = 0; i < parser.GetActionAmount(); i++) {
			const auto& a = parser.actions[i];
			if (a.throttle == row[0] && a.steer == row[1] && a.pitch == row[2] && a.yaw == row[3] && a.roll == row[4]
				&& a.jump == row[5] && a.boost == row[6] && a.handbrake == row[7])
				return i;
		}
		return -1;
	}

	std::unique_ptr<EnvSet> MakeKickoffEnv(int arenas) {
		EnvSetConfig cfg = {};
		cfg.numArenas = arenas;
		cfg.tickSkip = 8;
		cfg.actionDelay = 7;
		cfg.saveRewards = false;
		cfg.envCreateFn = [](int) {
			auto arena = Arena::Create(GameMode::SOCCAR);
			arena->AddCar(Team::BLUE);
			arena->AddCar(Team::ORANGE);
			EnvCreateResult r = {};
			r.arena = arena;
			r.actionParser = new DefaultAction();
			r.obsBuilder = new AdvancedObsPadded(3);
			r.stateSetter = new KickoffState();
			r.terminalConditions = { new NoTouchCondition(10), new GoalScoreCondition() };
			return r;
		};
		return std::make_unique<EnvSet>(cfg);
	}

	// Plays the script through the same NextoKickoff the opponent uses, and a plain chase of the ball (steer toward it, full
	// boost) whenever it is not scripted, standing in for the net that takes over after the 168 ticks
	struct ScriptOnly {
		NextoKickoff kickoff;
		std::array<int, NextoKickoff::NUM_KINDS> rowIdx;
		int idle, chaseLeft, chaseStraight, chaseRight;
		bool chaseAfter = true;
		int flips = 0, scriptedSteps = 0;
		std::vector<uint8_t> flipSeen;

		ScriptOnly() {
			DefaultAction parser;
			for (int k = 0; k < NextoKickoff::NUM_KINDS; k++)
				rowIdx[k] = FindRow(parser, NextoKickoff::TableRow((NextoKickoff::Kind)k));
			idle = FindRow(parser, { 0, 0, 0, 0, 0, 0, 0, 0 });
			chaseLeft = FindRow(parser, { 1, -1, 0, -1, 0, 0, 1, 0 });
			chaseStraight = FindRow(parser, { 1, 0, 0, 0, 0, 0, 1, 0 });
			chaseRight = FindRow(parser, { 1, 1, 0, 1, 0, 0, 1, 0 });
		}
		void BeginServe(int n) { kickoff.Begin(n); flipSeen.assign(n, 0); }
		void Act(const std::vector<GameState>& states, const std::vector<bool>& mine, const std::vector<uint8_t>& prevTerm, std::vector<int>& out) {
			std::vector<int> kinds;
			kickoff.Update(states, mine, prevTerm, kinds);
			int g = 0;
			for (auto& gs : states)
				for (auto& pl : gs.players) {
					if (mine[g]) {
						if (kinds[g] >= 0) {
							out[g] = rowIdx[kinds[g]];
						} else if (chaseAfter) {
							Vec d = gs.ball.pos - pl.pos;
							float cross = pl.rotMat.forward.x * d.y - pl.rotMat.forward.y * d.x; // > 0: the ball is to the right
							float len = sqrtf(d.x * d.x + d.y * d.y) + 1e-3f;
							out[g] = (fabsf(cross) / len < 0.1f) ? chaseStraight : (cross > 0 ? chaseRight : chaseLeft);
						} else {
							out[g] = idle;
						}
						scriptedSteps += kinds[g] >= 0;
						if (pl.hasFlipped && !flipSeen[g]) {
							flipSeen[g] = 1;
							flips++;
						}
					}
					g++;
				}
		}
	};

	struct Idle {
		int idle;
		void BeginServe(int) {}
		void Act(const std::vector<GameState>&, const std::vector<bool>& mine, const std::vector<uint8_t>&, std::vector<int>& out) {
			for (int p = 0; p < (int)mine.size(); p++)
				if (mine[p])
					out[p] = idle;
		}
	};
}

TEST(NextoKickoff_script_has_168_ticks_and_the_jump_is_released_before_the_flip) {
	// 44 / 16 / 8 / 4 / 4 / 52 / 40 ticks in 8-tick steps: 5.5 + 2 + 1 + .5 + .5 + 6.5 + 5 = 21
	CHECK_EQ(NextoKickoff::STEPS * 8, 168);
	int counts[NextoKickoff::NUM_KINDS] = {};
	for (int s = 0; s < NextoKickoff::STEPS; s++)
		counts[NextoKickoff::STEP_KIND[s]]++;
	CHECK_EQ(counts[NextoKickoff::STEER_LEFT], 2);   // 16 ticks
	CHECK_EQ(counts[NextoKickoff::JUMP], 1);         // 8 ticks
	CHECK_EQ(counts[NextoKickoff::FLIP], 1);
	CHECK_EQ(counts[NextoKickoff::PITCH_UP], 6);     // 52 ticks
	CHECK_EQ(counts[NextoKickoff::ROLL], 5);         // 40 ticks
	CHECK_EQ(counts[NextoKickoff::BOOST], 6);        // 5 at the start + the release step

	// A flip needs a fresh press: the step before it must have the button up, and the one before that down
	int flip = -1;
	for (int s = 0; s < NextoKickoff::STEPS; s++)
		if (NextoKickoff::STEP_KIND[s] == NextoKickoff::FLIP)
			flip = s;
	CHECK(flip >= 2);
	CHECK_EQ(NextoKickoff::TableRow(NextoKickoff::STEP_KIND[flip - 1])[5], 0.f);
	CHECK_EQ(NextoKickoff::TableRow(NextoKickoff::STEP_KIND[flip - 2])[5], 1.f);

	// The rows exist in our action table (exact tuples) and the flip is a forward-right diagonal as in the original
	DefaultAction parser;
	for (int k = 0; k < NextoKickoff::NUM_KINDS; k++)
		CHECK(FindRow(parser, NextoKickoff::TableRow((NextoKickoff::Kind)k)) >= 0);
	const auto& f = NextoKickoff::TableRow(NextoKickoff::FLIP);
	const auto& o = NextoKickoff::OriginalRow(NextoKickoff::FLIP);
	// dodge direction (-pitch, yaw + roll) in the sim: both forward (+) and right (+)
	CHECK(-f[2] > 0 && f[3] + f[4] > 0);
	CHECK(-o[2] > 0 && o[3] + o[4] > 0);
}

TEST(NextoKickoff_taker_decision_matches_the_original) {
	const Team B = Team::BLUE, O = Team::ORANGE;
	// 1v1 diagonal kickoff: equal distance, both are takers
	auto a = MakeState({ { B, -2048, -2560 }, { O, 2048, 2560 } });
	CHECK(NextoKickoff::IsTaker(a, 0));
	CHECK(NextoKickoff::IsTaker(a, 1));
	// 1v1, blue closer: only blue
	auto b = MakeState({ { B, -256, -3840 }, { O, 0, 4608 } });
	CHECK(NextoKickoff::IsTaker(b, 0));
	CHECK(!NextoKickoff::IsTaker(b, 1));
	// 2v2, blue pair on the two diagonals: one of them takes it. The original: a teammate just as close must be
	// "left" (smaller x for blue) of us, so the taker is the one with the LARGER x
	auto c = MakeState({ { B, -2048, -2560 }, { B, 2048, -2560 }, { O, -2048, 2560 }, { O, 2048, 2560 } });
	CHECK(!NextoKickoff::IsTaker(c, 0));
	CHECK(NextoKickoff::IsTaker(c, 1));
	// orange is mirrored: the teammate must have the larger x, so the taker is the one with the SMALLER x
	CHECK(NextoKickoff::IsTaker(c, 2));
	CHECK(!NextoKickoff::IsTaker(c, 3));
	// a car farther back never takes it
	auto d = MakeState({ { B, -2048, -2560 }, { B, 0, -4608 }, { O, 2048, 2560 }, { O, 0, 4608 } });
	CHECK(NextoKickoff::IsTaker(d, 0));
	CHECK(!NextoKickoff::IsTaker(d, 1));
	// within 10 uu counts as equal, beyond does not
	auto e = MakeState({ { B, -2048, -2560 }, { O, 2048, 2560 + 20 } });
	CHECK(NextoKickoff::IsTaker(e, 0));
	CHECK(!NextoKickoff::IsTaker(e, 1));
}

TEST(NextoKickoff_update_plays_the_script_once_per_episode) {
	const Team B = Team::BLUE, O = Team::ORANGE;
	std::vector<GameState> states = { MakeState({ { B, -2048, -2560 }, { O, 2048, 2560 } }) };
	NextoKickoff k;
	k.Begin(2);
	std::vector<bool> mine = { true, true };
	std::vector<uint8_t> none = { 0 }, fresh = { 1 };
	std::vector<int> kinds;

	// First Act of a serve: both take it, step 0 then 1, 2, ... in order
	for (int s = 0; s < NextoKickoff::STEPS; s++) {
		k.Update(states, mine, s == 0 ? fresh : none, kinds);
		CHECK_EQ(kinds[0], (int)NextoKickoff::STEP_KIND[s]);
		CHECK_EQ(kinds[1], (int)NextoKickoff::STEP_KIND[s]);
	}
	// After the 168 ticks the net plays
	k.Update(states, mine, none, kinds);
	CHECK_EQ(kinds[0], -1);
	CHECK_EQ(kinds[1], -1);

	// A new episode restarts it
	k.Update(states, mine, fresh, kinds);
	CHECK_EQ(kinds[0], (int)NextoKickoff::BOOST);

	// The ball being hit ends it: no override once the ball is off y == 0
	states[0].ball.pos.y = 300;
	k.Update(states, mine, none, kinds);
	CHECK_EQ(kinds[0], -1);
}

TEST(NextoKickoff_only_starts_on_a_real_kickoff_and_never_for_a_non_taker) {
	const Team B = Team::BLUE, O = Team::ORANGE;
	std::vector<bool> mine = { true, true };
	std::vector<uint8_t> none = { 0 }, fresh = { 1 };
	std::vector<int> kinds;

	// A fresh episode that does not start with the ball at rest in the centre: never scripted, even later
	{
		std::vector<GameState> states = { MakeState({ { B, -2048, -2560 }, { O, 2048, 2560 } }) };
		states[0].ball.pos = Vec(500, 800, 400);
		NextoKickoff k;
		k.Begin(2);
		k.Update(states, mine, fresh, kinds);
		CHECK_EQ(kinds[0], -1);
		states[0].ball.pos = Vec(0, 0, 93.15f); // happens to pass the centre at rest later
		k.Update(states, mine, none, kinds);
		CHECK_EQ(kinds[0], -1);
		CHECK_EQ(kinds[1], -1);
	}
	// Ball moving at the centre is not a kickoff either
	{
		std::vector<GameState> states = { MakeState({ { B, -2048, -2560 }, { O, 2048, 2560 } }) };
		states[0].ball.vel = Vec(0, 800, 0);
		NextoKickoff k;
		k.Begin(2);
		k.Update(states, mine, fresh, kinds);
		CHECK_EQ(kinds[0], -1);
	}
	// The far car of a 1v1 kickoff with unequal spawns is not a taker: it plays the net the whole time
	{
		std::vector<GameState> states = { MakeState({ { B, -256, -3840 }, { O, 0, 4608 } }) };
		NextoKickoff k;
		k.Begin(2);
		for (int s = 0; s < NextoKickoff::STEPS + 2; s++) {
			k.Update(states, mine, s == 0 ? fresh : none, kinds);
			CHECK_EQ(kinds[1], -1);
			CHECK_EQ(kinds[0], s < NextoKickoff::STEPS ? (int)NextoKickoff::STEP_KIND[s] : -1);
		}
	}
	// Players the bot does not own are left alone
	{
		std::vector<GameState> states = { MakeState({ { B, -2048, -2560 }, { O, 2048, 2560 } }) };
		NextoKickoff k;
		k.Begin(2);
		std::vector<bool> onlyOrange = { false, true };
		k.Update(states, onlyOrange, fresh, kinds);
		CHECK_EQ(kinds[0], -1);
		CHECK_EQ(kinds[1], (int)NextoKickoff::BOOST);
	}
}

TEST(NextoKickoff_script_takes_the_first_touch_in_the_real_sim) {
	EnsureRocketSimKickoff();
	const int N = 40;
	auto env = MakeKickoffEnv(N);
	ScriptOnly scripted;
	DefaultAction parser;
	Idle idle{ FindRow(parser, { 0, 0, 0, 0, 0, 0, 0, 0 }) };
	CHECK(idle.idle >= 0);

	// A is blue in the even arenas, orange in the odd ones; the idle car never moves, so any touch is the script's
	MatchConfig mc;
	mc.maxSeconds = 20.f;
	mc.alternateSides = true;
	MatchResult r = PlayMatch(*env, scripted, idle, N, mc);
	// Every kickoff layout, both colours: the script (then a plain chase) reaches the ball first every time, and each
	// taker really flipped (the released-then-pressed jump button works in the sim)
	CHECK_EQ(r.firstTouchA, N);
	CHECK_EQ(r.firstTouchB, 0);
	CHECK_EQ(scripted.flips, N);
	// the script ran in every game (finished arenas keep resetting until the slowest game ends, so there is no upper bound)
	CHECK(scripted.scriptedSteps >= N * 10);

	// The same chase WITHOUT the script, for scale (not asserted: it is how much the script changes the time to the ball)
	ScriptOnly plain;
	plain.kickoff.Begin(0);
	struct NoScript {
		ScriptOnly& inner;
		void BeginServe(int n) { inner.BeginServe(n); }
		void Act(const std::vector<GameState>& st, const std::vector<bool>& m, const std::vector<uint8_t>&, std::vector<int>& out) {
			// terminals never flagged fresh and the kickoff never armed -> never scripted
			std::vector<uint8_t> none(st.size(), 0);
			inner.kickoff.armed.assign(st.size(), 0);
			inner.Act(st, m, none, out);
		}
	} noScript{ plain };
	MatchResult rp = PlayMatch(*env, noScript, idle, N, mc);
	std::cerr << "[nexto kickoff] script+chase: first touch " << r.firstTouchA << "/" << N << ", mean game " << r.seconds / N
		<< " s;  chase only: first touch " << rp.firstTouchA << "/" << N << ", mean game " << rp.seconds / N << " s\n";
	env->Sync();
}

namespace {
	CarControls Ctl(float throttle, float steer, float pitch, float yaw, float roll, bool jump, bool boost) {
		CarControls c = {};
		c.throttle = throttle; c.steer = steer; c.pitch = pitch; c.yaw = yaw; c.roll = roll; c.jump = jump; c.boost = boost;
		return c;
	}

	// bot.py KICKOFF_CONTROLS, tick by tick
	std::vector<CarControls> ExactScript() {
		std::vector<CarControls> v;
		auto add = [&](int ticks, CarControls c) { for (int i = 0; i < ticks; i++) v.push_back(c); };
		add(44, Ctl(1, 0, 0, 0, 0, false, true));
		add(16, Ctl(1, -1, 0, 0, 0, false, true));
		add(8, Ctl(1, 0, 0, 0, 0, true, true));
		add(4, Ctl(1, 0, 0, 0, 0, false, true));
		add(4, Ctl(1, 0, -0.7f, 0.8f, 0, true, true));
		add(52, Ctl(1, 0, 1, 0, 0, false, true));
		add(40, Ctl(1, 0, 0.5f, 0, 1, false, false));
		return v;
	}

	CarControls ChaseControls(const CarState& cs, Vec ball) {
		Vec d = ball - cs.pos;
		float cross = cs.rotMat.forward.x * d.y - cs.rotMat.forward.y * d.x;
		float len = sqrtf(d.x * d.x + d.y * d.y) + 1e-3f;
		float steer = (fabsf(cross) / len < 0.1f) ? 0.f : (cross > 0 ? 1.f : -1.f);
		return Ctl(1, steer, 0, steer, 0, false, true);
	}

	// One solo kickoff (a lone blue car, so only the script matters). `exact`: the original's tick-level script, delayed by the
	// sim's 7-tick action latency like the discretized one; otherwise the 21-step table. Runs 168 + 7 ticks (the whole script)
	// and returns where the car is then.
	CarState SoloScriptEnd(Arena* arena, bool exact, const std::vector<CarControls>& script, Vec* startPos) {
		arena->ResetToRandomKickoff();
		Car* car = arena->GetCars()[0];
		*startPos = car->GetState().pos;
		CarControls held = {};
		const int L = 7;
		for (int t = 0; t < 168 + L; t++) {
			if (exact) {
				if (t - L >= 0)
					held = script[t - L];
			} else if (t >= L && ((t - L) % 8) == 0) {
				const auto& r = NextoKickoff::TableRow(NextoKickoff::STEP_KIND[(t - L) / 8]);
				held = Ctl(r[0], r[1], r[2], r[3], r[4], r[5] > 0, r[6] > 0);
			}
			car->controls = held;
			arena->Step(1);
		}
		return car->GetState();
	}
}

TEST(NextoKickoff_discretized_script_ends_where_the_tick_level_original_does) {
	EnsureRocketSimKickoff();
	auto arena = std::unique_ptr<Arena>(Arena::Create(GameMode::SOCCAR));
	arena->AddCar(Team::BLUE);
	auto script = ExactScript();
	CHECK_EQ((int)script.size(), 168);

	// Physics is deterministic per spawn point: one run of each variant per layout, over enough random kickoffs to see them all
	std::map<std::pair<int, int>, std::pair<CarState, CarState>> seen;
	for (int i = 0; i < 80; i++) {
		Vec p0, p1;
		CarState e = SoloScriptEnd(arena.get(), true, script, &p0);
		CarState d = SoloScriptEnd(arena.get(), false, script, &p1);
		(void)d;
		seen[{ (int)lroundf(p0.x), (int)lroundf(p0.y) }].first = e;
		// the discretized run used a (possibly different) random spawn: file it under ITS spawn
		seen[{ (int)lroundf(p1.x), (int)lroundf(p1.y) }].second = d;
	}
	int compared = 0;
	for (auto& [pos, e] : seen) {
		if (e.first.pos.Length() == 0 || e.second.pos.Length() == 0)
			continue; // a layout one of the two variants never drew
		float dpos = (e.first.pos - e.second.pos).Length();
		float dspeed = fabsf(e.first.vel.Length() - e.second.vel.Length());
		std::cerr << "[nexto kickoff fidelity] spawn (" << pos.first << "," << pos.second << "): end of script, position gap " << (int)dpos
			<< " uu, speed exact " << (int)e.first.vel.Length() << " vs discretized " << (int)e.second.vel.Length() << "\n";
		// Measured 77-79 uu (about one car length) and 2% in speed on every layout, after 1.4 s at ~2000 uu/s. What is left is the
		// flip: the sim's dodge has a 45 deg diagonal where the original's yaw .8 / pitch -.7 is 49 deg.
		CHECK(dpos <= 150.f);
		CHECK(dspeed <= 100.f);
		compared++;
	}
	CHECK(compared >= 3);
}
