// Reset-mix diagnostics for the GCO run (docs/GCO_RESET_MIX.md). Offline, CPU only: never touches the trainer or the GPU.
//
// Plays a checkpoint against itself (stochastic actions, as collection does) under the GCO env: tickSkip 8,
// actionDelay 7, the 6.1 obs (278 floats), GoalReward, and a 20 s no-touch timeout that is a real ending. Every
// arena has ONE fixed setter cell and team size, so each episode's source is known:
//
//   cells: BallNearCar, AirDrill solo, AirDrill contested, AirPlay wall, AirPlay flip-reset, AirPlay carry,
//          Kickoff, Random     x   modes: 1v1, 2v2, 3v3
//
// "episodes" phase: R rounds; in a round every arena plays exactly one episode (to a goal, the timeout, or the cap).
// "match" phase: kickoff resets, goal-only terminals, each arena plays `matchSeconds` of continuous game.
//
// Outputs (CSV, in outDir): episodes.csv (one row per episode), critic.csv (per cell x mode moments of V_real against
// the realised discounted return), samples.csv (state samples, about one per second per arena, both phases).
//
// Usage (repo root):
//   ./build/ResetDiag <checkpoint dir> <outDir> [arenasPerGroup=48] [rounds=6] [matchArenasPerMode=16] [matchSeconds=300]
#include <GigaLearnCPP/Util/Models.h>
#include <GigaLearnCPP/PPO/PPOLearner.h>

#include <RLGymCPP/EnvSet/EnvSet.h>
#include <RLGymCPP/Rewards/CommonRewards.h>
#include <RLGymCPP/TerminalConditions/NoTouchCondition.h>
#include <RLGymCPP/TerminalConditions/GoalScoreCondition.h>
#include <RLGymCPP/ObsBuilders/AdvancedObsPadded.h>
#include <RLGymCPP/StateSetters/KickoffState.h>
#include <RLGymCPP/StateSetters/RandomState.h>
#include <RLGymCPP/StateSetters/BallNearCarState.h>
#include <RLGymCPP/StateSetters/AirDrillState.h>
#include <RLGymCPP/StateSetters/AirPlayState.h>
#include <RLGymCPP/ActionParsers/DefaultAction.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace RLGC;
using namespace GGL;

namespace {
	constexpr int TICK_SKIP = 8, ACTION_DELAY = 7;
	constexpr float GAMMA = 0.9969f;          // TRAIN_GAMMA
	constexpr float TIMEOUT_PENALTY = 0.5f;   // GCO_TIMEOUT_PENALTY
	constexpr float CAP_SECONDS = 120.f;      // an episode still running (touches keep resetting the timer) is cut here and flagged
	constexpr int SAMPLE_EVERY = 15;          // steps (1 s)
	const int NUM_ACTIONS = DefaultAction().GetActionAmount(); // EnvSet::numActions is never assigned

	enum Cell { BNC, DRILL_SOLO, DRILL_CONTEST, AP_WALL, AP_FLIP, AP_CARRY, KICKOFF, RANDOM, NUM_CELLS };
	const char* CELL_NAMES[NUM_CELLS] = { "BallNearCar", "AirDrill_solo", "AirDrill_contest", "AirPlay_wall", "AirPlay_flip", "AirPlay_carry", "Kickoff", "Random" };

	StateSetter* MakeSetter(int cell) {
		switch (cell) {
		case BNC: return new BallNearCarState(600, 900);
		case DRILL_SOLO: { auto* s = new AirDrillState(); s->soloFrac = 1.f; return s; }
		case DRILL_CONTEST: { auto* s = new AirDrillState(); s->soloFrac = 0.f; return s; }
		case AP_WALL: return new AirPlayState(900, 0.5f, 1500, 3000, 1.0f);
		case AP_FLIP: return new AirPlayState(900, 1.0f, 1500, 3000, 0.0f);
		case AP_CARRY: return new AirPlayState(900, 0.0f, 1500, 3000, 0.0f);
		case KICKOFF: return new KickoffState();
		default: return new RandomState(true, true, false);
		}
	}

	struct Nets {
		ModelSet models;
		void Build(int obsSize, int numActions, const std::filesystem::path& ckpt) {
			// MUST mirror ExampleMain.cpp for the 6.1 lineage (g_BallPredObs): GELU, LayerNorm, residual flag on.
			auto mk = [](std::vector<int> sizes) {
				PartialModelConfig c;
				c.layerSizes = sizes;
				c.addResiduals = true;
				c.activationType = ModelActivationType::GELU;
				c.addLayerNorm = true;
				return c;
			};
			PartialModelConfig sh = mk({ 1152, 1152 }); sh.addOutputLayer = false;
			PartialModelConfig pol = mk({ 1024, 1024, 1024, 1024 });
			PartialModelConfig crit = mk({ 1280, 1280 });
			PartialModelConfig ctrunk = mk({ 1280, 1280 }); ctrunk.addOutputLayer = false;
			PPOLearner::MakeModels(true, obsSize, numActions, sh, pol, crit, ctrunk, torch::Device(torch::kCPU), models);
			models.Load(ckpt, false, false);
		}
		// rows -> actions (sampled) and V_real (critic units)
		void Infer(torch::Tensor obs, torch::Tensor masks, bool wantValue, std::vector<int>& actions, std::vector<float>& values) {
			torch::NoGradGuard ng;
			torch::Tensor tAct, tLogp;
			PPOLearner::InferActionsFromModels(models, obs, masks, /*deterministic=*/false, 1.f, false, &tAct, &tLogp);
			tAct = tAct.to(torch::kInt32).contiguous();
			actions.assign(tAct.data_ptr<int>(), tAct.data_ptr<int>() + tAct.numel());
			if (wantValue) {
				torch::Tensor h = models["shared_head"]->Forward(obs, false);
				h = models["critic_trunk"]->Forward(h, false);
				torch::Tensor v = models["critic"]->Forward(h, false).flatten().to(torch::kFloat32).contiguous();
				values.assign(v.data_ptr<float>(), v.data_ptr<float>() + v.numel());
			}
		}
	};

	struct Group { int cell, perTeam; };

	std::unique_ptr<EnvSet> MakeEnv(const std::vector<Group>& arenaGroup, bool matchMode) {
		EnvSetConfig cfg = {};
		cfg.numArenas = (int)arenaGroup.size();
		cfg.tickSkip = TICK_SKIP;
		cfg.actionDelay = ACTION_DELAY;
		cfg.saveRewards = false;
		cfg.envCreateFn = [&arenaGroup, matchMode](int idx) {
			const Group& g = arenaGroup[idx];
			auto arena = Arena::Create(GameMode::SOCCAR);
			for (int i = 0; i < g.perTeam; i++) { arena->AddCar(Team::BLUE); arena->AddCar(Team::ORANGE); }
			EnvCreateResult r = {};
			r.arena = arena;
			r.actionParser = new DefaultAction();
			r.obsBuilder = new AdvancedObsPadded(3, /*shuffleSlots=*/true, 8);
			r.stateSetter = MakeSetter(g.cell);
			if (matchMode)
				r.terminalConditions = { new GoalScoreCondition() };
			else
				r.terminalConditions = { new NoTouchCondition(20, /*truncate=*/false), new GoalScoreCondition() };
			r.rewards = { { new GoalReward(), 1.f } };
			return r;
		};
		return std::make_unique<EnvSet>(cfg);
	}

	void WriteSample(FILE* f, const char* phase, const Group& g, long long ep, int step, const GameState& gs) {
		// one row per car; ball columns repeat
		int closest = -1; float best = 1e9f;
		for (int i = 0; i < (int)gs.players.size(); i++) {
			float d = (gs.players[i].pos - gs.ball.pos).Length();
			if (d < best) { best = d; closest = i; }
		}
		for (int i = 0; i < (int)gs.players.size(); i++) {
			const Player& p = gs.players[i];
			fprintf(f, "%s,%s,%d,%lld,%d,%.0f,%.0f,%.0f,%.0f,%.0f,%.0f,%d,%.0f,%.0f,%.0f,%.0f,%.1f,%d,%d,%d\n",
				phase, CELL_NAMES[g.cell], g.perTeam, ep, step,
				gs.ball.pos.x, gs.ball.pos.y, gs.ball.pos.z, gs.ball.vel.x, gs.ball.vel.y, gs.ball.vel.z,
				(int)p.team, p.pos.x, p.pos.y, p.pos.z, p.vel.Length(), p.boost, (int)p.isOnGround, (int)(i == closest), (int)p.isDemoed);
		}
	}

	// obs / masks of the rows of the listed arenas
	void Gather(EnvSet& env, const std::vector<int>& arenas, torch::Tensor& obs, torch::Tensor& masks, std::vector<int>& rows) {
		rows.clear();
		for (int a : arenas) {
			int start = env.state.arenaPlayerStartIdx[a];
			int n = (int)env.state.gameStates[a].players.size();
			for (int i = 0; i < n; i++) rows.push_back(start + i);
		}
		const int D = env.obsSize, A = NUM_ACTIONS;
		obs = torch::empty({ (int64_t)rows.size(), D }, torch::kFloat32);
		masks = torch::empty({ (int64_t)rows.size(), A }, torch::kUInt8);
		float* po = obs.data_ptr<float>();
		uint8_t* pm = masks.data_ptr<uint8_t>();
		for (size_t r = 0; r < rows.size(); r++) {
			memcpy(po + r * D, &env.state.obs.data[(size_t)rows[r] * D], sizeof(float) * D);
			memcpy(pm + r * A, &env.state.actionMasks.data[(size_t)rows[r] * A], A);
		}
	}
}

int main(int argc, char** argv) {
	std::cout << std::unitbuf;
	if (argc < 3) { std::cerr << "usage: ResetDiag <checkpoint dir> <outDir> [arenasPerGroup=48] [rounds=6] [matchArenasPerMode=16] [matchSeconds=300]\n"; return 2; }
	const std::filesystem::path ckpt = argv[1], outDir = argv[2];
	const int perGroup = argc > 3 ? std::atoi(argv[3]) : 48;
	const int rounds = argc > 4 ? std::atoi(argv[4]) : 6;
	const int matchArenas = argc > 5 ? std::atoi(argv[5]) : 16;
	const float matchSeconds = argc > 6 ? (float)std::atof(argv[6]) : 300.f;
	std::filesystem::create_directories(outDir);

	bool inited = false;
	for (const char* d : { "collision_meshes", "build/collision_meshes", "../collision_meshes" })
		if (std::filesystem::exists(std::filesystem::path(d) / "soccar")) { RocketSim::Init(d); inited = true; break; }
	if (!inited) { std::cerr << "collision_meshes not found\n"; return 2; }

	FILE* fS = fopen((outDir / "samples.csv").string().c_str(), "w");
	fprintf(fS, "phase,cell,mode,ep,step,bx,by,bz,bvx,bvy,bvz,team,cx,cy,cz,cspeed,boost,onground,closest,demoed\n");

	Nets nets;
	bool netsBuilt = false;
	auto t0 = std::chrono::steady_clock::now();
	auto elapsed = [&] { return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count(); };

	// ---------------- episodes phase ----------------
	if (rounds > 0 && perGroup > 0) {
		std::vector<Group> groups;
		for (int cell = 0; cell < NUM_CELLS; cell++)
			for (int pt = 1; pt <= 3; pt++)
				for (int i = 0; i < perGroup; i++)
					groups.push_back({ cell, pt });
		auto env = MakeEnv(groups, false);
		nets.Build(env->obsSize, NUM_ACTIONS, ckpt); netsBuilt = true;
		const int N = (int)groups.size();
		const int capSteps = (int)(CAP_SECONDS * 120.f / TICK_SKIP);

		FILE* fE = fopen((outDir / "episodes.csv").string().c_str(), "w");
		fprintf(fE, "cell,mode,ep,steps,outcome,first_touch_step,first_touch_team,blue_closer,min_car_ball,min_car_car,oob,v0_blue,v0_orange,"
			"b0x,b0y,b0z,b0speed\n");
		// critic moments per (cell, mode): n, sum G, sum G^2, sum V, sum V^2, sum GV    (G = realised discounted return, raw units)
		double M[NUM_CELLS][3][6] = {};

		long long epId = 0;
		for (int round = 0; round < rounds; round++) {
			std::fill(env->state.terminals.begin(), env->state.terminals.end(), 1);
			struct Ep {
				bool done = false; int steps = 0, firstTouchStep = -1, firstTouchTeam = -1; long long id;
				std::vector<std::vector<float>> v; // [player][step]
				float v0[2] = {}; int blueCloser = 0; float minCarBall = 0, minCarCar = 0; int oob = 0; Vec b0, b0v;
			};
			std::vector<Ep> eps(N);
			int remaining = N;
			for (int step = 0; remaining > 0; step++) {
				env->Reset();
				if (step == 0) {
					for (int a = 0; a < N; a++) {
						Ep& e = eps[a];
						e.id = epId++;
						const GameState& gs = env->state.gameStates[a];
						e.v.resize(gs.players.size());
						float dBest[2] = { 1e9f, 1e9f }; e.minCarBall = 1e9f; e.minCarCar = 1e9f;
						for (size_t i = 0; i < gs.players.size(); i++) {
							const Player& p = gs.players[i];
							float d = (p.pos - gs.ball.pos).Length();
							dBest[(int)p.team] = std::min(dBest[(int)p.team], d);
							e.minCarBall = std::min(e.minCarBall, d);
							if (fabsf(p.pos.x) > 4096 - 60 || fabsf(p.pos.y) > 5120 - 60 || fabsf(p.pos.x) + fabsf(p.pos.y) > 8064 - 60 || p.pos.z < 16 || p.pos.z > 2044 - 40
								|| p.boost < 0 || p.boost > 100.01f || !std::isfinite(p.pos.x + p.vel.x) || p.vel.Length() > 2301)
								e.oob++;
							for (size_t j = i + 1; j < gs.players.size(); j++)
								e.minCarCar = std::min(e.minCarCar, (p.pos - gs.players[j].pos).Length());
						}
						e.blueCloser = dBest[0] < dBest[1] ? 1 : (dBest[0] > dBest[1] ? -1 : 0);
						e.b0 = gs.ball.pos; e.b0v = gs.ball.vel;
						WriteSample(fS, "reset0", groups[a], e.id, 0, gs);
					}
				}
				env->StepFirstHalf(false);

				std::vector<int> active;
				for (int a = 0; a < N; a++) if (!eps[a].done) active.push_back(a);
				torch::Tensor obs, masks; std::vector<int> rows, acts; std::vector<float> vals;
				Gather(*env, active, obs, masks, rows);
				nets.Infer(obs, masks, true, acts, vals);
				IList actions(env->state.numPlayers, 0);
				for (size_t r = 0; r < rows.size(); r++) actions[rows[r]] = acts[r];
				// finished arenas keep stepping (EnvSet has no pause); give them their first valid action so nothing asserts
				for (int a = 0; a < N; a++)
					if (eps[a].done) {
						int start = env->state.arenaPlayerStartIdx[a];
						for (size_t i = 0; i < env->state.gameStates[a].players.size(); i++) {
							int pick = 0;
							for (int k = 0; k < NUM_ACTIONS; k++) if (env->state.actionMasks.At(start + i, k)) { pick = k; break; }
							actions[start + i] = pick;
						}
					}
				{
					size_t r = 0;
					for (int a : active) {
						Ep& e = eps[a];
						const GameState& gs = env->state.gameStates[a];
						for (size_t i = 0; i < gs.players.size(); i++, r++) {
							e.v[i].push_back(vals[r]);
							if (step == 0) e.v0[(int)gs.players[i].team] += vals[r] / (gs.players.size() / 2);
						}
					}
				}
				env->StepSecondHalf(actions, false);

				for (int a : active) {
					Ep& e = eps[a];
					const GameState& gs = env->state.gameStates[a];
					e.steps++;
					if (e.firstTouchStep < 0) {
						bool t[2] = {};
						for (auto& p : gs.players) if (p.ballTouchedStep) t[(int)p.team] = true;
						if (t[0] || t[1]) { e.firstTouchStep = e.steps; e.firstTouchTeam = (t[0] && t[1]) ? 2 : (t[0] ? 0 : 1); }
					}
					bool terminal = env->state.terminals[a] != 0;
					if (!terminal && (e.steps + a) % SAMPLE_EVERY == 0)
						WriteSample(fS, "reset", groups[a], e.id, e.steps, gs);
					if (terminal || e.steps >= capSteps) {
						e.done = true; remaining--;
						// outcome: 0 blue scored, 1 orange scored, 2 no-touch timeout, 3 cap
						int outcome = 3;
						if (terminal) outcome = gs.goalScored ? ((int)RS_TEAM_FROM_Y(gs.ball.pos.y) == (int)Team::ORANGE ? 0 : 1) : 2;
						fprintf(fE, "%s,%d,%lld,%d,%d,%d,%d,%d,%.0f,%.0f,%d,%.4f,%.4f,%.0f,%.0f,%.0f,%.0f\n", CELL_NAMES[groups[a].cell], groups[a].perTeam, e.id, e.steps, outcome,
							e.firstTouchStep, e.firstTouchTeam, e.blueCloser, e.minCarBall, e.minCarCar, e.oob, e.v0[0], e.v0[1], e.b0.x, e.b0.y, e.b0.z, e.b0v.Length());
						if (outcome != 3) {
							double* m = M[groups[a].cell][groups[a].perTeam - 1];
							for (size_t i = 0; i < gs.players.size(); i++) {
								float R = outcome == 2 ? -TIMEOUT_PENALTY : (((int)gs.players[i].team == outcome) ? 1.f : -1.f);
								const auto& v = e.v[i];
								double g = R; // return at the last obs
								for (int t = (int)v.size() - 1; t >= 0; t--) {
									m[0] += 1; m[1] += g; m[2] += g * g; m[3] += v[t]; m[4] += (double)v[t] * v[t]; m[5] += g * v[t];
									g *= GAMMA;
								}
							}
						}
						e.v.clear(); e.v.shrink_to_fit();
					}
				}
				if (step % 100 == 0)
					std::cerr << "round " << round + 1 << "/" << rounds << " step " << step << " active " << remaining << " (" << (int)elapsed() << " s)\n";
			}
			fflush(fE); fflush(fS);
		}
		fclose(fE);
		FILE* fC = fopen((outDir / "critic.csv").string().c_str(), "w");
		fprintf(fC, "cell,mode,n,sumG,sumG2,sumV,sumV2,sumGV\n");
		for (int c = 0; c < NUM_CELLS; c++)
			for (int m = 0; m < 3; m++)
				fprintf(fC, "%s,%d,%.0f,%.6f,%.6f,%.6f,%.6f,%.6f\n", CELL_NAMES[c], m + 1, M[c][m][0], M[c][m][1], M[c][m][2], M[c][m][3], M[c][m][4], M[c][m][5]);
		fclose(fC);
		env->Sync();
	}

	// ---------------- match phase ----------------
	if (matchArenas > 0 && matchSeconds > 0) {
		std::vector<Group> groups;
		for (int pt = 1; pt <= 3; pt++)
			for (int i = 0; i < matchArenas; i++)
				groups.push_back({ KICKOFF, pt });
		auto env = MakeEnv(groups, true);
		if (!netsBuilt) nets.Build(env->obsSize, NUM_ACTIONS, ckpt);
		const int N = (int)groups.size();
		const int steps = (int)(matchSeconds * 120.f / TICK_SKIP);
		FILE* fM = fopen((outDir / "match.csv").string().c_str(), "w");
		fprintf(fM, "mode,arena,goals_blue,goals_orange,stall20_count,max_no_touch_s,kickoffs,kickoff_first_touch_s_sum,kickoff_touched\n");
		struct Mt { int gb = 0, go = 0, stalls = 0, kickoffs = 0, kTouched = 0; float noTouch = 0, maxNoTouch = 0, kSum = 0, sinceKick = 0; bool awaitingKickTouch = true, stallCounted = false; };
		std::vector<Mt> mt(N);
		std::vector<int> all(N);
		for (int a = 0; a < N; a++) { all[a] = a; mt[a].kickoffs = 1; }
		std::fill(env->state.terminals.begin(), env->state.terminals.end(), 1);
		const float dt = TICK_SKIP / 120.f;
		for (int step = 0; step < steps; step++) {
			env->Reset();
			env->StepFirstHalf(false);
			torch::Tensor obs, masks; std::vector<int> rows, acts; std::vector<float> vals;
			Gather(*env, all, obs, masks, rows);
			nets.Infer(obs, masks, false, acts, vals);
			IList actions(acts.begin(), acts.end());
			env->StepSecondHalf(actions, false);
			for (int a = 0; a < N; a++) {
				Mt& m = mt[a];
				const GameState& gs = env->state.gameStates[a];
				bool touched = false;
				for (auto& p : gs.players) touched |= p.ballTouchedStep;
				m.sinceKick += dt;
				if (touched) {
					if (m.awaitingKickTouch) { m.awaitingKickTouch = false; m.kTouched++; m.kSum += m.sinceKick; }
					m.noTouch = 0; m.stallCounted = false;
				} else {
					m.noTouch += dt;
					m.maxNoTouch = std::max(m.maxNoTouch, m.noTouch);
					if (m.noTouch >= 20 && !m.stallCounted) { m.stalls++; m.stallCounted = true; }
				}
				if (env->state.terminals[a]) {
					if (gs.goalScored) ((int)RS_TEAM_FROM_Y(gs.ball.pos.y) == (int)Team::ORANGE ? m.gb : m.go)++;
					m.kickoffs++; m.awaitingKickTouch = true; m.sinceKick = 0; m.noTouch = 0; m.stallCounted = false;
				} else if ((step + a) % SAMPLE_EVERY == 0)
					WriteSample(fS, "match", groups[a], a, step, gs);
			}
			if (step % 200 == 0) std::cerr << "match step " << step << "/" << steps << " (" << (int)elapsed() << " s)\n";
		}
		for (int a = 0; a < N; a++)
			fprintf(fM, "%d,%d,%d,%d,%d,%.1f,%d,%.2f,%d\n", groups[a].perTeam, a, mt[a].gb, mt[a].go, mt[a].stalls, mt[a].maxNoTouch, mt[a].kickoffs, mt[a].kSum, mt[a].kTouched);
		fclose(fM);
		env->Sync();
	}
	fclose(fS);
	std::cerr << "done in " << (int)elapsed() << " s\n";
	return 0;
}
