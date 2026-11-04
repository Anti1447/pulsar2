#pragma once

#include <RLGymCPP/EnvSet/EnvSet.h>
#include "Util/MetricSender.h"
#include <atomic>
#include "Util/RenderSender.h"
#include "Util/VizControl.h"
#include "LearnerConfig.h"

namespace GGL {

	typedef std::function<void(class Learner*, const std::vector<RLGC::GameState>& states, Report& report)> StepCallbackFn;

	// Called once per training iteration at the tail of the loop, after the skill tracker /
	// guards have written into the report (so Rating/* keys are visible when they refreshed
	// this iteration). May call RequestSaveAndExit (e.g. curriculum phase triggers).
	typedef std::function<void(class Learner*, Report& report)> IterationCallbackFn;

	// https://github.com/AechPro/rlgym-ppo/blob/main/rlgym_ppo/learner.py
	class RG_IMEXPORT Learner {
	public:
		LearnerConfig config;

		RLGC::EnvSet* envSet;

		class PPOLearner* ppo;
		class PolicyVersionManager* versionMgr;

		RLGC::EnvCreateFn envCreateFn;
		MetricSender* metricSender;
		RenderSender* renderSender;
		// Render mode only (NULL in the trainer): the viewer's control panel channel.
		struct VizControl* vizControl;

		int obsSize;
		int numActions;

		struct WelfordStat* returnStat;
		struct BatchedWelfordStat* obsStat;

		std::string runID = {};

		uint64_t
			totalTimesteps = 0,
			totalIterations = 0;

		// Reachability gate anneal state: EMAs of the heads' InfoNCE accuracy and of the
		// touch-prediction agreement (persisted in the checkpoint stats)
		float reachAccEMA = 0, reachAgreeEMA = 0;

		// Last Rating/1v1, 2v2, 3v3 seen from the skill tracker (each drives its own best-checkpoint archive,
		// see BEST_ARCHIVE_MODES in Learner.cpp) and each archive's rate limiter
		float lastEvalRating[3] = { NAN, NAN, NAN };
		uint64_t lastBestArchiveTs[3] = {};

		// Kickoff-touch probe on a throwaway arena; used by Load() to reject checkpoints
		// that load structurally but are behaviorally destroyed (see LearnerConfig)
		bool BootSanityProbe();

		// Steered-practice collection state (config.steering); the direction tensor lives in the
		// PPOLearner (this header stays torch-free) and is derived LIVE from each iteration's
		// buffer inside Start() (see fnSteerUpdate there). These are fixed at startup:
		// [0, numSteeredArenas) steered practice | [numSteeredArenas, numPracticeArenas)
		// unsteered control practice | rest match.
		bool steerLoaded = false;
		int numPracticeArenas = 0;
		int numSteeredArenas = 0;


		// Churn-telemetry archive of the live steering directions (STEERING_ROADMAP
		// "continuous items"), PER MODE (index = playersPerTeam-1): the current applied
		// vector + sigma are snapshotted into every checkpoint's RUNNING_STATS
		// ("steer_vec"/"steer_sigma" for 1v1, "steer_vec_2v2"... for teams, ~5KB each) so
		// offline analysis gets the staleness curve for free and deployment can steer at
		// inference. Save-only, NEVER loaded: directions re-derive within one iteration of
		// any restart by design (a stale vector must not outlive its trunk).
		std::array<std::vector<float>, 3> steerVecSave;
		std::array<float, 3> steerSigmaSave = {};

		// FEAR PANEL (in-trainer census, FEAR_MINE.md): a frozen set of high-
		// disagreement 2v2 decline obs rows, captured ONCE when fear mining first
		// produces a full ranking, then persisted through RUNNING_STATS and re-valued
		// by the CURRENT critic + goal critic every iteration (Steer/Fear Panel zV /
		// zG / Dz panels). zV rising toward 0 across checkpoints = the critic
		// unlearning its fear = the drill deploy working. Unlike steerVecSave this IS
		// loaded: the panel must stay FIXED to be longitudinal (it is measurement
		// state, not an actuator - nothing reads it but the report).
		std::vector<float> fearPanelObs;   // flattened [k, obsSize]
		int64_t fearPanelTimestep = 0;     // totalTimesteps at freeze

		// AirDrill altitude-annealing controller state (AERIAL_GAP.md); persisted in
		// RUNNING_STATS so the curriculum never resets on restart
		float airDrillD = 0.f;             // current difficulty (mirrored into the shared knob)
		float airDrillConvEMA = -1.f;      // aerial-conversion EMA (-1 = unseeded)
		float airDrillConvRef = -1.f;      // reference EMA at the last adjustment
		int64_t airDrillLastAdjustIter = 0;

		// EMERGENCE RC2 observer: rolling sample of the miner's picked obs rows
		// (save-only, refreshed each iteration - offline inspection channel)
		std::vector<float> minerSampleObs;

		// V_exp, the return-level expectile twin (opaque; GAP_EXP.lt). Measurement only -
		// see the struct in Learner.cpp. Actuation lives with the composition critic.
		std::shared_ptr<struct GapState> gapSensor;

		// External fixed opponents (config.externalOpponent; opaque - torch types
		// stay out of this header). Frozen weights: collection-side only, nothing
		// to snapshot or checkpoint. Cumulative goal counters per bot = the fixed external
		// yardstick (persisted so the series survives restarts).
		struct ExternalOpponentStats {
			std::atomic<int64_t> goalsFor = 0, goalsAgainst = 0, serveIters = 0;
		};
		std::vector<std::shared_ptr<class ExternalOpponent>> externalOpps;
		std::vector<ExternalOpponentStats> externalStats; // Parallel to externalOpps
		// Per-bot play statistics from serve iterations (ExternalPlayMonitor.h): GD per 1k steps by mode/side, kickoff
		// outcomes, touch/half/shot/demo shares. Parallel to externalOpps; persisted like the goal counters.
		std::vector<std::shared_ptr<struct ExternalPlayStats>> externalPlay;

		// Gauntlet of OUR policy against the external bots (config.gauntlet; GauntletEval.h). The latest result per bot
		// (parallel to externalOpps) is persisted in RUNNING_STATS; the history is GAUNTLET_LOG.jsonl.
		struct GauntletResult {
			int64_t iteration = 0, timesteps = 0, games = 0, wins = 0, losses = 0, draws = 0;
			int64_t firstTouchUs = 0, firstTouchBot = 0, firstTouchBoth = 0;
			double meanSeconds = 0;
		};
		std::shared_ptr<struct GauntletEval> gauntlet;
		std::vector<GauntletResult> gauntletLast;
		int64_t gauntletLastIter = 0; // totalIterations at the last run
		// Runs the gauntlet if it is due and writes its Gauntlet/* keys into `report`. Barrier zone only (no collection worker).
		void RunGauntlet(Report& report);

		// Exploiter stack (docs/EXPLOITERS.md; opaque here like the members above). exploiterRun: this process is an
		// exploiter run (config.exploiter) - its frozen opponents and eval arenas. exploiterPool: this is the main run
		// and imports finished exploiters (config.exploiterPool). At most one of the two is set.
		std::shared_ptr<struct ExploiterRunState> exploiterRun;
		std::shared_ptr<struct ExploiterPool> exploiterPool;
		int64_t exploiterRampStartTs = -1; // Exploiter run: when the critic first explained enough (policy-LR ramp start)
		// Gated policy-LR steps (ExploiterConfig::policyLRStepInterval): steps taken so far, the last check's timestep,
		// and the explained variance of each iteration since then
		int lrStepLevel = 0;
		int64_t lrLastCheckTs = -1;
		std::vector<float> lrWindowEV;
		// Exploiter run: deterministic eval vs the role's target, logged to EVAL_LOG.jsonl; returns the headline win rate
		float RunExploiterEval(Report& report, int games);
		// Exploiter run, fresh start: the adaptive restart (RESTART.json)
		void ChooseExploiterStart();


		StepCallbackFn stepCallback = NULL;
		IterationCallbackFn iterationCallback = NULL; // optional; assign after construction

		// Programmatic save-and-exit, honored at the same safe point as the Q key (worker
		// joined, checkpoint saved first). A nonzero code makes tools/run_trainer.sh RESTART
		// the trainer (its crash-restart path) - the self-deploy vehicle for boot-time config
		// changes like a curriculum phase flip; 0 stops it like a clean quit.
		std::atomic<bool> exitRequested = false;
		std::atomic<int> requestedExitCode = 0;
		void RequestSaveAndExit(int exitCode) {
			requestedExitCode = exitCode;
			exitRequested = true;
		}

		Learner(RLGC::EnvCreateFn envCreateFunc, LearnerConfig config, StepCallbackFn stepCallback = NULL);
		void Start();


		void StartQuitKeyThread(std::atomic<bool>& quitPressed, std::thread& outThread);

		void Save();
		void TrackEvalRatings(const Report& report);
		void Load();
		void SaveStats(std::filesystem::path path);
		void LoadStats(std::filesystem::path path);

		// Render-mode live reload: if a numbered checkpoint newer than loadedTimesteps has appeared
		// in config.checkpointFolder and looks fully written, hot-swap its policy weights in and
		// update loadedTimesteps. Returns true on a successful swap. Guarded so a mid-write race
		// against the (uncontrolled) training process can only cost a retry, never the process.
		bool ReloadNewestCheckpointForRender(int64_t& loadedTimesteps);
		// Render mode only: repopulate the viewer panel's opponent dropdown from disk.
		void RefreshVizOpponentList();

		RG_NO_COPY(Learner);

		~Learner();
	};
}