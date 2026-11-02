#include "TestFramework.h"
#include "RewardTestUtil.h"

#include <RLGymCPP/Rewards/CommonRewards.h>
#include <RLGymCPP/Rewards/OffsidePenalty.h>
#include <RLGymCPP/Rewards/PassingSetupRewards.h>
#include <RLGymCPP/Rewards/ZeroSumReward.h>
#include <RLGymCPP/Rewards/RewardTraceStack.h>
#include <RLGymCPP/Rewards/RewardTraceJson.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <deque>
#include <new>
#include <random>

using namespace RLGC;
using namespace RewardTest;

// P0 of the Reward Lab (docs/REWARD_VIEWER_SPEC.md): the trace contract. The load-bearing claim is that
// attaching a sink NEVER changes what a reward returns, so the picture a human reviews is the reward that trains.
// Everything else here (consistency, once-per-step, caps, JSON) supports that.

// ---- allocation counter, to show a null sink costs no allocation -----------------------------------------------
static bool g_countAllocs = false;
static size_t g_allocs = 0;
void* operator new(size_t n) { if (g_countAllocs) g_allocs++; if (void* p = std::malloc(n ? n : 1)) return p; throw std::bad_alloc(); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, size_t) noexcept { std::free(p); }

namespace {
	constexpr float GAMMA = 0.9969f;

	uint32_t Bits(float f) { uint32_t u; std::memcpy(&u, &f, 4); return u; }

	typedef std::vector<WeightedReward> Stack;
	typedef Stack (*MakeStackFn)();

	// The three instrumented rewards, wrapped the way the live stack wraps them: Offside bare (it is individual
	// shaping), the other two under ZeroSum so the raw/wrapped distinction is exercised.
	Stack MakeStack() {
		Stack s;
		s.push_back(WeightedReward(new OffsidePenalty(), 0.15f));
		s.push_back(WeightedReward(new ZeroSumReward(new CarEnergyPotentialReward(GAMMA), 0.3f), 75));
		s.push_back(WeightedReward(new ZeroSumReward(new PassingSetupReward(), 0.3f), 5));
		return s;
	}
	void FreeStack(Stack& s) { for (auto& w : s) delete w.reward; s.clear(); }

	// ---- a seeded rollout of fabricated states with the structure EnvSet gives rewards ----------------------------
	struct Rollout {
		std::vector<GameState> states;
		std::deque<std::vector<BallState>> paths;   // gs.ballPred points into these
		std::vector<int> episodeStart;              // indices where a new episode begins (Reset before Step)
		int touches = 0;
	};

	Rollout MakeRollout(int teamSize, int steps, uint32_t seed) {
		std::mt19937 rng(seed);
		auto U = [&](float lo, float hi) { return std::uniform_real_distribution<float>(lo, hi)(rng); };
		auto P = [&](float p) { return std::uniform_real_distribution<float>(0, 1)(rng) < p; };
		Rollout r; r.states.reserve(steps);

		int sinceReset = 1 << 30;
		for (int i = 0; i < steps; i++) {
			GameState gs;
			const bool newEp = sinceReset >= 150 + (int)(rng() % 150) || (i > 0 && r.states.back().goalScored);
			if (i == 0 || newEp) {
				r.episodeStart.push_back(i); sinceReset = 0;
				gs.players.clear();
				for (int t = 0; t < 2; t++)
					for (int k = 0; k < teamSize; k++) {
						Player p = MakePlayer(1 + t * teamSize + k, t * teamSize + k, t == 0 ? Team::BLUE : Team::ORANGE,
							Vec(U(-3000, 3000), U(-4500, 4500), 17));
						p.vel = Vec(U(-1500, 1500), U(-1500, 1500), 0); p.boost = U(0, 100);
						gs.players.push_back(p);
					}
				gs.ball.pos = Vec(U(-2000, 2000), U(-3000, 3000), U(100, 900)); gs.ball.vel = Vec(U(-1500, 1500), U(-1500, 1500), U(-300, 600));
			} else {
				GameState& pv = r.states.back();
				gs.players = pv.players; gs.ball = pv.ball;
				gs.prev = &pv;
				for (size_t k = 0; k < gs.players.size(); k++) gs.players[k].prev = &pv.players[k];
				const float dt = STEP_DT;
				for (auto& p : gs.players) {
					p.pos = p.pos + p.vel * dt;
					p.pos.x = RS_CLAMP(p.pos.x, -4000, 4000); p.pos.y = RS_CLAMP(p.pos.y, -5000, 5000);
					p.vel = p.vel + Vec(U(-300, 300), U(-300, 300), U(-100, 100));
					{ const float b = p.boost + U(-8, 8); p.boost = RS_CLAMP(b, 0.0f, 100.0f); }   // RS_CLAMP is a macro: no RNG in its argument
					p.isOnGround = P(0.6f);
					p.pos.z = p.isOnGround ? 17.0f : U(40, 900);
					p.ballTouchedStep = false;
					if (P(0.01f)) p.isDemoed = !p.isDemoed;     // demos start and end
				}
				gs.ball.pos = pv.ball.pos + pv.ball.vel * dt; gs.ball.pos.z = RS_MAX(gs.ball.pos.z, 93.0f);
				gs.ball.pos.x = RS_CLAMP(gs.ball.pos.x, -4000, 4000); gs.ball.pos.y = RS_CLAMP(gs.ball.pos.y, -5000, 5000);
			}
			gs.deltaTime = STEP_DT;
			gs.goalScored = (i > 0 && P(0.003f));
			if (P(0.10f)) {                                      // a touch: the ball leaves fast, possibly toward a teammate
				Player& toucher = gs.players[rng() % gs.players.size()];
				if (!toucher.isDemoed) {
					toucher.ballTouchedStep = true; gs.lastTouchCarID = (int)toucher.carId; r.touches++;
					gs.ball.pos = toucher.pos + Vec(U(-100, 100), U(-100, 100), U(60, 250));
					gs.ball.vel = Vec(U(-2200, 2200), U(-2200, 2200), U(-200, 700));
					if (P(0.5f)) {
						std::vector<BallState> path = BallPath(gs.ball.pos, gs.ball.vel);
						for (auto& tm : gs.players)
							if (tm.team == toucher.team && tm.carId != toucher.carId && !tm.isDemoed && P(0.7f)) {
								Vec l = path[180].pos; tm.pos = Vec(l.x + U(-200, 200), l.y + U(-200, 200), 17); break;
							}
					}
				}
			}
			r.paths.push_back(BallPath(gs.ball.pos, gs.ball.vel));
			gs.ballPred = &r.paths.back();
			sinceReset++;
			r.states.push_back(std::move(gs));
			// fix up pointers invalidated by the move: prev points at the previous element, which is stable (reserved)
			GameState& cur = r.states.back();
			if (cur.prev) for (size_t k = 0; k < cur.players.size(); k++) cur.players[k].prev = &cur.prev->players[k];
		}
		return r;
	}

	// EnvSet's reward block, with no tracing at all: the reference the traced path is compared against.
	// Returns each reward's own output (the thing the contract is about); the weighted total is derived from them.
	std::vector<std::vector<float>> PlainStep(Stack& s, const GameState& gs, bool final) {
		for (auto& w : s) w.reward->PreStep(gs);
		std::vector<std::vector<float>> outs;
		for (auto& w : s) outs.push_back(w.reward->GetAllRewards(gs, final));
		return outs;
	}

	// What the contract is NOT about: the weighted total. Under -ffast-math -march=native the compiler may fuse `total += out * weight`
	// into one FMA in one translation unit and round the product first in another, so two correct implementations of the same sum can
	// differ by 1 ulp. Each reward's own output is compared bit for bit; the total only to within float noise.
	struct Compare { int steps = 0, mismatches = 0, totalNoise = 0, totalBad = 0, nonFinite = 0; long gates = 0, events = 0; bool sawPaid = false, sawOffside = false, sawNotOffside = false, sawCooldownBlock = false; };

	// Run the same rollout through two fresh stacks: one untraced, one through TracedRewardStack.
	Compare RunBoth(MakeStackFn make, const Rollout& ro) {
		Stack plain = make(), traced = make();
		Compare c;
		{
			TracedRewardStack ts(traced);
			size_t ep = 0;
			for (int i = 0; i < (int)ro.states.size(); i++) {
				const GameState& gs = ro.states[i];
				if (ep < ro.episodeStart.size() && ro.episodeStart[ep] == i) {
					ep++;
					for (auto& w : plain) w.reward->Reset(gs);
					for (auto& w : traced) w.reward->Reset(gs);
				}
				std::vector<std::vector<float>> a = PlainStep(plain, gs, false);
				const StackStepTrace& t = ts.Step(gs, false);
				c.steps++;
				// a NaN on both sides has equal bits and would hide a divergence, so non-finite values are counted and must be zero
				std::vector<float> refTotal(gs.players.size(), 0.0f);
				for (size_t r = 0; r < a.size(); r++)
					for (size_t p = 0; p < a[r].size(); p++) {
						if (Bits(a[r][p]) != Bits(t.rewards[r].wrapped[p])) c.mismatches++;
						if (!std::isfinite(a[r][p])) c.nonFinite++;
						refTotal[p] += a[r][p] * plain[r].weight;
					}
				for (size_t p = 0; p < refTotal.size(); p++) {
					if (Bits(refTotal[p]) != Bits(t.total[p])) c.totalNoise++;
					if (std::abs(refTotal[p] - t.total[p]) > 1e-4f * (1.0f + std::abs(refTotal[p]))) c.totalBad++;
				}
				for (auto& rt : t.rewards)
					for (auto& pt : rt.sink.players) {
						c.gates += (long)pt.gates.size(); c.events += (long)pt.events.size();
						for (auto& e : pt.events) { if (e == "paid") c.sawPaid = true; if (e == "cooldown_blocked") c.sawCooldownBlock = true; }
						if (rt.name == "OffsidePenalty")
							for (auto& g : pt.gates) if (g.name == "offside") { if (g.passed) c.sawOffside = true; else c.sawNotOffside = true; }
					}
			}
		}
		FreeStack(plain); FreeStack(traced);
		return c;
	}
}

TEST(Trace_bit_identical_over_10k_step_rollouts_in_1v1_2v2_3v3) {
	Compare tot;
	for (int mode = 1; mode <= 3; mode++) {
		Rollout ro = MakeRollout(mode, 3400, 1000 + mode);
		Compare c = RunBoth(MakeStack, ro);
		tot.steps += c.steps; tot.mismatches += c.mismatches; tot.nonFinite += c.nonFinite; tot.totalBad += c.totalBad; tot.totalNoise += c.totalNoise; tot.gates += c.gates; tot.events += c.events;
		tot.sawPaid |= c.sawPaid; tot.sawOffside |= c.sawOffside; tot.sawNotOffside |= c.sawNotOffside; tot.sawCooldownBlock |= c.sawCooldownBlock;
		CHECK(ro.touches > 100);
	}
	CHECK(tot.steps >= 10000);
	CHECK_EQ(tot.mismatches, 0);   // every reward's output, bit for bit, trace on vs off
	CHECK_EQ(tot.nonFinite, 0);
	CHECK_EQ(tot.totalBad, 0);     // the weighted total agrees to float noise (see Compare)
	std::cerr << "    [info] total differs by sub-ulp noise on " << tot.totalNoise << " of " << tot.steps << " steps\n";
	// The test must not pass vacuously: the sinks really received emissions, and the interesting branches were hit.
	CHECK(tot.gates > 50000);
	CHECK(tot.sawPaid && tot.sawCooldownBlock && tot.sawOffside && tot.sawNotOffside);
}

namespace {
	// A reward that VIOLATES the contract: its trace branch changes the value. The comparison above must catch it.
	class LeakyReward : public Reward {
	public:
		virtual float GetReward(const Player& player, const GameState& state, bool) override {
			float v = state.ball.pos.x * 0.001f;
			if (trace) { trace->Component(player.carId, "v", v); v += 1e-6f; }
			return v;
		}
	};
	Stack MakeLeaky() { Stack s; s.push_back(WeightedReward(new LeakyReward(), 1.0f)); return s; }
}

TEST(Trace_detector_catches_a_reward_whose_emission_changes_its_value) {
	Rollout ro = MakeRollout(2, 300, 7);
	CHECK(RunBoth(MakeLeaky, ro).mismatches > 0);
}

TEST(Trace_null_sink_allocates_nothing_in_the_instrumented_hot_path) {
	Rollout ro = MakeRollout(2, 200, 11);
	OffsidePenalty off; CarEnergyPotentialReward en(GAMMA);
	CHECK(off.trace == nullptr && en.trace == nullptr);
	for (auto& gs : ro.states) for (auto& p : gs.players) { off.GetReward(p, gs, false); en.GetReward(p, gs, false); }   // warm up
	g_allocs = 0; g_countAllocs = true;
	float sink = 0;
	for (auto& gs : ro.states) for (auto& p : gs.players) { sink += off.GetReward(p, gs, false); sink += en.GetReward(p, gs, false); }
	g_countAllocs = false;
	CHECK_EQ(g_allocs, (size_t)0);
	(void)sink;
	// and with a sink attached, it does allocate (so the counter is live and the test above is meaningful)
	TraceSink s; off.SetTrace(&s);
	g_allocs = 0; g_countAllocs = true;
	for (auto& gs : ro.states) for (auto& p : gs.players) off.GetReward(p, gs, false);
	g_countAllocs = false;
	CHECK(g_allocs > 0);
}

namespace {
	class CountingReward : public Reward {
	public:
		int pre = 0, calls = 0;
		virtual void PreStep(const GameState&) override { pre++; }
		virtual float GetReward(const Player&, const GameState&, bool) override { calls++; return 0.0f; }
	};
}

TEST(Trace_each_reward_is_called_exactly_once_per_step) {
	Stack s; auto* a = new CountingReward(); auto* b = new CountingReward();
	s.push_back(WeightedReward(a, 1.0f)); s.push_back(WeightedReward(new ZeroSumReward(b, 0.0f), 1.0f));
	Rollout ro = MakeRollout(3, 50, 5);
	{
		TracedRewardStack ts(s);
		for (auto& gs : ro.states) ts.Step(gs, false);
	}
	CHECK_EQ(a->pre, 50); CHECK_EQ(a->calls, 50 * 6);
	CHECK_EQ(b->pre, 50); CHECK_EQ(b->calls, 50 * 6);
	FreeStack(s);
}

TEST(Trace_attach_reaches_the_wrapped_child_and_detach_clears_it) {
	Stack s = MakeStack();
	auto* zs = dynamic_cast<ZeroSumReward*>(s[1].reward);
	CHECK(zs != nullptr && zs->child->trace == nullptr);
	{
		TracedRewardStack ts(s);
		CHECK(zs->child->trace != nullptr && s[0].reward->trace != nullptr);
		CHECK(zs->child->trace == &ts.Last().rewards[1].sink);   // the child writes into ITS reward's sink
		// readable names come from the innermost reward; the W&B key (Reward::GetName, mangled under gcc) is carried unchanged
		CHECK_EQ(ts.Last().rewards[0].name, std::string("OffsidePenalty"));
		CHECK_EQ(ts.Last().rewards[1].name, std::string("CarEnergyPotentialReward"));
		CHECK_EQ(ts.Last().rewards[2].name, std::string("PassingSetupReward"));
		CHECK_EQ(ts.Last().rewards[1].wandbKey, s[1].reward->GetName());
	}
	CHECK(zs->child->trace == nullptr && s[0].reward->trace == nullptr && zs->trace == nullptr);
	FreeStack(s);
}

TEST(Trace_zero_sum_reports_raw_wrapped_and_weighted_separately) {
	// 1v1, teamSpirit 0: wrapped_i = raw_i - raw_other, so wrapped sums to 0 while raw does not.
	Stack s; s.push_back(WeightedReward(new ZeroSumReward(new CarEnergyPotentialReward(GAMMA), 0.0f), 10));
	Rollout ro = MakeRollout(1, 80, 21);
	TracedRewardStack ts(s);
	int checked = 0;
	for (auto& gs : ro.states) {
		const StackStepTrace& t = ts.Step(gs, false);
		const RewardStepTrace& r = t.rewards[0];
		CHECK(r.zeroSum);
		CHECK_NEAR(r.wrapped[0], r.raw[0] - r.raw[1], 1e-5f);
		CHECK_NEAR(r.wrapped[0] + r.wrapped[1], 0.0f, 1e-5f);
		CHECK_NEAR(r.weighted[0], r.wrapped[0] * 10.0f, 1e-5f);
		if (std::abs(r.raw[0]) > 1e-4f) checked++;
	}
	CHECK(checked > 20);
	ts.Detach(); FreeStack(s);
}

// ---- the traces say what the reward did ---------------------------------------------------------------------

namespace {
	const PlayerTrace* PT(const StackStepTrace& t, size_t reward, uint32_t car) { return t.rewards[reward].sink.Find(car); }
	const TraceGate* G(const PlayerTrace* p, const char* name) { for (auto& g : p->gates) if (g.name == name) return &g; return nullptr; }
	float C(const PlayerTrace* p, const char* name) { for (auto& c : p->components) if (c.name == name) return c.value; return -9999.0f; }
	bool HasEvent(const PlayerTrace* p, const char* e) { for (auto& s : p->events) if (s == e) return true; return false; }
}

TEST(Trace_offside_value_matches_its_components_and_gates) {
	// Blue car 1000 uu upfield of the ball, no teammate, orange all onside: severity 1.0, team factor 1, value -1.
	GameState gs; gs.deltaTime = STEP_DT; gs.ball.pos = Vec(500, 0, 93); gs.ball.vel = Vec(1000, 0, 0);
	gs.players = { MakePlayer(1, 0, Team::BLUE, Vec(0, 1000, 17)), MakePlayer(2, 1, Team::ORANGE, Vec(0, 3000, 17)) };
	Stack s; s.push_back(WeightedReward(new OffsidePenalty(), 1.0f));
	TracedRewardStack ts(s);
	const StackStepTrace& t = ts.Step(gs, false);
	const PlayerTrace* p = PT(t, 0, 1);
	CHECK(p != nullptr);
	CHECK(G(p, "offside") && G(p, "offside")->passed);
	CHECK_NEAR(C(p, "severity") * C(p, "team_factor"), 1.0f, 1e-5f);
	CHECK_NEAR(t.rewards[0].raw[0], -C(p, "severity") * C(p, "team_factor"), 1e-5f);
	CHECK_NEAR(t.rewards[0].raw[0], -1.0f, 1e-5f);
	// the threshold plane is drawn at ball.y + dead zone for blue
	bool plane = false;
	for (auto& q : p->prims) if (q.kind == TracePrimKind::Line && q.role == TraceRole::Threshold && std::abs(q.a.y - 200.0f) < 1e-3f) plane = true;
	CHECK(plane);
	// goalside car: the gate fails and nothing is paid
	gs.players[0].pos = Vec(0, -1000, 17);
	const StackStepTrace& t2 = ts.Step(gs, false);
	CHECK(G(PT(t2, 0, 1), "offside") && !G(PT(t2, 0, 1), "offside")->passed);
	CHECK_EQ(t2.rewards[0].raw[0], 0.0f);
	ts.Detach(); FreeStack(s);
}

TEST(Trace_car_energy_value_is_gamma_phi_next_minus_phi_prev) {
	GameState prev; prev.deltaTime = STEP_DT; prev.ball.pos = Vec(0, 0, 93);
	prev.players = { MakePlayer(1, 0, Team::BLUE, Vec(0, -1000, 17)) };
	prev.players[0].vel = Vec(0, 800, 0);
	GameState gs = prev; gs.prev = &prev;
	gs.players[0].prev = &prev.players[0];
	gs.players[0].vel = Vec(0, 1200, 0); gs.players[0].pos = Vec(0, -950, 17);
	Stack s; s.push_back(WeightedReward(new CarEnergyPotentialReward(GAMMA), 1.0f));
	TracedRewardStack ts(s);
	const StackStepTrace& t = ts.Step(gs, false);
	const PlayerTrace* p = PT(t, 0, 1);
	CHECK(G(p, "has_prev")->passed && G(p, "not_goal_step")->passed);
	CHECK_NEAR(t.rewards[0].raw[0], GAMMA * C(p, "phi_next") - C(p, "phi_prev"), 1e-6f);
	CHECK(t.rewards[0].raw[0] > 0.0f);                            // sped up: positive
	// goal step: Phi(s') = 0, so the gate says so and the value is exactly -Phi(prev)
	gs.goalScored = true;
	const StackStepTrace& t2 = ts.Step(gs, false);
	CHECK(!G(PT(t2, 0, 1), "not_goal_step")->passed);
	CHECK_EQ(C(PT(t2, 0, 1), "phi_next"), 0.0f);
	CHECK_NEAR(t2.rewards[0].raw[0], -C(PT(t2, 0, 1), "phi_prev"), 1e-6f);
	// no prev (first step of an episode): gate fails, value 0
	GameState first = gs; first.prev = nullptr; first.players[0].prev = nullptr; first.goalScored = false;
	const StackStepTrace& t3 = ts.Step(first, false);
	CHECK(!G(PT(t3, 0, 1), "has_prev")->passed); CHECK_EQ(t3.rewards[0].raw[0], 0.0f);
	ts.Detach(); FreeStack(s);
}

TEST(Trace_passing_setup_cooldown_gate_and_events) {
	const Vec BALL(0, -1000, 300), VEL(900, 700, 350), FAR_OPP(-3000, 3000, 17);
	std::vector<BallState> path = BallPath(BALL, VEL);
	Vec l = path[180].pos; Vec mate = l + Vec(150, 0, -l.z + 17);
	GameState gs; gs.deltaTime = STEP_DT; gs.ball.pos = BALL; gs.ball.vel = VEL;
	gs.players = { MakePlayer(1, 0, Team::BLUE, Vec(BALL.x - 100, BALL.y - 100, 17)), MakePlayer(2, 1, Team::BLUE, mate), MakePlayer(3, 2, Team::ORANGE, FAR_OPP) };
	gs.players[0].ballTouchedStep = true; gs.lastTouchCarID = 1; gs.ballPred = &path;
	Stack s; s.push_back(WeightedReward(new PassingSetupReward(), 1.0f));
	s[0].reward->Reset(gs);
	TracedRewardStack ts(s);
	const StackStepTrace& t = ts.Step(gs, false);
	const PlayerTrace* p = PT(t, 0, 1);
	CHECK(t.rewards[0].raw[0] > 2.5f);
	CHECK(HasEvent(p, "paid") && HasEvent(p, "cooldown_started"));
	CHECK(G(p, "not_on_cooldown") && G(p, "not_on_cooldown")->passed);
	CHECK(C(p, "primary") > 0.0f && C(p, "primary_plus_trajectory") >= C(p, "primary"));
	CHECK_NEAR(C(p, "primary_plus_trajectory") + C(p, "zone_bonus"), t.rewards[0].raw[0], 1e-5f);
	// the landing target is drawn at the point the reward reads: the cache's AtTime(1.5). (Which tick that is depends on the cache's
	// rounding of 1.5 / slice length; the test pins "trace == what the reward read", not the tick.)
	BallPredCache cache; cache.Update(gs);
	bool target = false;
	for (auto& q : p->prims) if (q.kind == TracePrimKind::Point && q.role == TraceRole::Target && (q.a - cache.AtTime(1.5f).pos).Length() < 1e-4f) target = true;
	CHECK(target);
	// the very next touch is inside the cooldown: gate fails, event says so, value 0
	const StackStepTrace& t2 = ts.Step(gs, false);
	const PlayerTrace* p2 = PT(t2, 0, 1);
	CHECK(G(p2, "not_on_cooldown") && !G(p2, "not_on_cooldown")->passed);
	CHECK(HasEvent(p2, "cooldown_blocked") && !HasEvent(p2, "paid"));
	CHECK_EQ(t2.rewards[0].raw[0], 0.0f);
	// no prediction on the state: the gate says why it pays nothing
	GameState nopred = gs; nopred.ballPred = nullptr;
	Stack s2; s2.push_back(WeightedReward(new PassingSetupReward(), 1.0f)); s2[0].reward->Reset(nopred);
	TracedRewardStack ts2(s2);
	const StackStepTrace& t3 = ts2.Step(nopred, false);
	CHECK(G(PT(t3, 0, 1), "ball_prediction_valid") && !G(PT(t3, 0, 1), "ball_prediction_valid")->passed);
	CHECK_EQ(t3.rewards[0].raw[0], 0.0f);
	ts.Detach(); ts2.Detach(); FreeStack(s); FreeStack(s2);
}

TEST(Trace_stateful_cooldown_sequence_is_identical_with_dense_touches) {
	// The random rollouts rarely produce consecutive blocked touches, so a leak that only fires inside the cooldown branch would hide
	// there. Here the passer touches EVERY step in the golden geometry: pays at step 0 and again each time the 0.5 s cooldown expires.
	const Vec BALL(0, -1000, 300), VEL(900, 700, 350), FAR_OPP(-3000, 3000, 17);
	std::vector<BallState> path = BallPath(BALL, VEL);
	Vec l = path[180].pos; Vec mate = l + Vec(150, 0, -l.z + 17);
	GameState gs; gs.deltaTime = STEP_DT; gs.ball.pos = BALL; gs.ball.vel = VEL;
	gs.players = { MakePlayer(1, 0, Team::BLUE, Vec(BALL.x - 100, BALL.y - 100, 17)), MakePlayer(2, 1, Team::BLUE, mate), MakePlayer(3, 2, Team::ORANGE, FAR_OPP) };
	gs.players[0].ballTouchedStep = true; gs.lastTouchCarID = 1; gs.ballPred = &path;
	Stack plain, traced;
	plain.push_back(WeightedReward(new PassingSetupReward(), 1.0f)); traced.push_back(WeightedReward(new PassingSetupReward(), 1.0f));
	plain[0].reward->Reset(gs); traced[0].reward->Reset(gs);
	TracedRewardStack ts(traced);
	int paid = 0, blocked = 0, mismatches = 0;
	for (int i = 0; i < 80; i++) {
		auto a = PlainStep(plain, gs, false);
		const StackStepTrace& t = ts.Step(gs, false);
		if (Bits(a[0][0]) != Bits(t.rewards[0].wrapped[0])) mismatches++;
		const PlayerTrace* p = PT(t, 0, 1);
		if (HasEvent(p, "paid")) paid++;
		if (HasEvent(p, "cooldown_blocked")) blocked++;
	}
	CHECK_EQ(mismatches, 0);
	CHECK_EQ(paid, 10);            // steps 0, 8, 16, ... 72: a 0.5 s cooldown is 7.5 steps at 1/15 s
	CHECK_EQ(blocked, 70);         // every other step is inside the cooldown
	ts.Detach(); FreeStack(plain); FreeStack(traced);
}

// ---- sink limits and wire format ------------------------------------------------------------------------------

TEST(Trace_sink_caps_primitives_per_player_and_counts_the_drops) {
	TraceSink s;
	for (int i = 0; i < 300; i++) s.Line(7, TraceRole::Info, Vec(0, 0, 0), Vec(1, 1, 1));
	s.Line(8, TraceRole::Info, Vec(0, 0, 0), Vec(1, 1, 1));
	CHECK_EQ(s.Find(7)->prims.size(), TraceSink::MAX_PRIMS_PER_PLAYER);
	CHECK_EQ(s.Find(7)->primsDropped, 300 - (int)TraceSink::MAX_PRIMS_PER_PLAYER);
	CHECK_EQ(s.Find(8)->prims.size(), (size_t)1);       // one player's flood does not starve another
	s.Clear(); CHECK(s.Find(7) == nullptr);
}

TEST(Trace_json_schema_shape_roundtrip_and_nonfinite_values) {
	Rollout ro = MakeRollout(2, 120, 3);
	Stack s = MakeStack();
	TracedRewardStack ts(s);
	size_t lines = 0; bool sawGate = false, sawPrim = false, sawEvent = false;
	for (int i = 0; i < (int)ro.states.size(); i++) {
		if (i == 0) for (auto& w : s) w.reward->Reset(ro.states[0]);
		const std::string line = ToJson(ts.Step(ro.states[i], false)).dump();
		CHECK(line.find('\n') == std::string::npos);                // one object per line (JSONL)
		nlohmann::json j = nlohmann::json::parse(line);
		CHECK_EQ(j["schema"].get<int>(), REWARD_TRACE_SCHEMA);
		const size_t n = j["cars"].size();
		CHECK_EQ(n, (size_t)4); CHECK_EQ(j["total"].size(), n);
		CHECK_EQ(j["rewards"].size(), (size_t)3);
		for (auto& r : j["rewards"]) {
			CHECK_EQ(r["raw"].size(), n); CHECK_EQ(r["wrapped"].size(), n); CHECK_EQ(r["weighted"].size(), n);
			for (auto& p : r["players"]) {
				for (auto& g : p["gates"]) { sawGate = true; CHECK(g["passed"].is_boolean() && g["name"].is_string()); }
				for (auto& q : p["prims"]) { sawPrim = true; CHECK(q["a"].size() == 3 && q["b"].size() == 3 && q["kind"].is_string() && q["role"].is_string()); }
				if (!p["events"].empty()) sawEvent = true;
			}
		}
		CHECK(nlohmann::json::parse(j.dump()) == j);
		lines++;
	}
	CHECK(lines == 120 && sawGate && sawPrim && sawEvent);
	// a non-finite component must serialise (as null) rather than throw
	ts.Detach(); FreeStack(s);
	Stack s2; s2.push_back(WeightedReward(new LeakyReward(), 1.0f));
	TracedRewardStack ts2(s2);
	GameState gs = ro.states[0]; gs.ball.pos.x = std::numeric_limits<float>::quiet_NaN();
	nlohmann::json j = nlohmann::json::parse(ToJson(ts2.Step(gs, false)).dump());   // null is what the wire carries
	CHECK(j["rewards"][0]["players"][0]["components"][0]["value"].is_null());
	ts2.Detach(); FreeStack(s2);
}
