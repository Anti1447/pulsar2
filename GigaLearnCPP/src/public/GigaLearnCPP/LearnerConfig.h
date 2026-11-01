#pragma once
#include <RLGymCPP/BasicTypes/Lists.h>
#include <RLGymCPP/StateSetters/AirDrillState.h>
#include "PPO/PPOLearnerConfig.h"
#include "SkillTrackerConfig.h"

#include <filesystem>
#include <memory>

namespace GGL {
	enum class LearnerDeviceType {
		AUTO, // CUDA if available, else MPS (Apple Metal), else CPU
		CPU,
		GPU_CUDA,
		GPU_MPS
	};


	// Steered-practice collection ("optimism surgery"). During COLLECTION ONLY, current-policy
	// rows belonging to the steered practice arenas get alpha*sigma*v added to the shared-trunk
	// output feeding the POLICY head, where v is a behavior-derived "commitment" direction.
	// Never steered: the learn pass, GAE value preds, skill-tracker evals, old-version
	// opponents. Stored logProbs come from the steered distribution, so PPO's clipped
	// importance ratio absorbs the bounded behavior/target divergence - the same mechanism
	// that absorbs pipelinedCollection's one-iteration lag.
	//
	// The direction is derived LIVE, every iteration, from the just-collected buffer (no
	// external file, no sidecar): airborne-ball readings from MATCH arenas are labeled by
	// ball-only landing sims + within-episode lookahead (went to the landing / declined it),
	// matched on (distance, flight time), and the went-declined difference of trunk means is
	// EMA-folded into the active vector. Derivation uses match rows only so the steered data
	// never feeds its own direction (no self-reinforcing loop). Directions go stale FAST as
	// the trunk trains (measured: a vector that gained +7pp engagement was 75M steps later
	// losing 11pp) - which is exactly why this is live instead of a file.
	//
	// Causal auto-gate: a slice of practice arenas runs UNSTEERED as controls (same
	// resolution-termination), and the steered-vs-control landing-engagement delta is EMA'd;
	// if steering stops helping, alpha drops to 0 automatically (derivation continues, and
	// steering re-engages when the delta recovers). This replaces any offline validation.
	//
	// Arena layout: [0, numSteered) steered practice | [numSteered, numPractice) control
	// practice | rest match. Pair ALL practice arenas (steered + control) with
	struct GapSensorConfig {
		bool enabled = false;
		float tau = 0.8f;        // expectile: "returns when it goes well"
		float lr = 1e-4f;
		int trainRows = 98304;
	};

	// External fixed opponent (Nexto; NextoOpponent.h has the full rationale):
	// on serveFrac of collection iterations, the non-self team across the whole
	// fleet is played by a frozen external TorchScript bot instead of self/pool/
	// league. Forces contact with its style (aerial play, the point) and gives a
	// FIXED yardstick (Nexto/Goals For/Against) immune to pool inflation. Rows
	// are excluded from training exactly like old-version opponents; eval paths
	// never see it (Rating semantics unchanged). Latch-covered like every other
	// data-distribution intervention.
	struct ExternalOpponentConfig {
		bool enabled = false;
		std::string modelPath = {};  // TorchScript module (Nexto's nexto-model.pt)
		float serveFrac = 0.15f;     // per-iteration serve probability
	};

	// EXPLOITER RUN (ported from RLGymBot_v3, docs/EXPLOITERS.md): this process trains only against frozen opponents
	// and reports how well it beats them. Set up by ConfigureExploiter() in ExampleMain.cpp; the launcher there fills
	// role, leaguePool and the restart candidates.
	struct ExploiterConfig {
		bool enabled = false;                          // True = this process is an exploiter run
		std::filesystem::path snapshotFolder = {};     // Main checkpoint to exploit (copied to <checkpointFolder>/snapshot on first start)
		std::filesystem::path sourceCheckpointFolder = {}; // Where the launcher takes new snapshots from; unused by the Learner

		// "main" plays the snapshot only; "league" plays the snapshot plus every checkpoint in leaguePool
		std::string role = "main";
		std::vector<std::filesystem::path> leaguePool = {};
		std::vector<std::string> slotRoles = { "main", "league" }; // One process per slot (tools/run_exploiter.sh --slot N)
		float mainComputeShare = 0.5f;                 // Running together: main gets this share of numGames, the slots split the rest
		float togetherGamesScale = 1.0f;               // Running together: numGames is scaled by this before the split
		// Running together: cap on each process's share of GPU memory (LearnerConfig::cudaMemoryFraction). Without it three
		// trainers can overfill VRAM, and on WSL/WDDM that doesn't OOM - it spills to system RAM and everything stalls.
		float mainVramFraction = 0;                    // 0 = no cap
		float slotVramFraction = 0;
		int leagueMaxMain = 16;                        // League capacity: newest main policy versions...
		int leagueMaxExploiters = 16;                  // ...plus newest finished exploiters (both roles)

		// Policy LR starts at policyLR * policyLRStartScale; once the critic's explained variance first reaches
		// explainedVarThreshold it ramps linearly to policyLR over policyLRRampSteps
		float policyLRStartScale = 0.1f;
		float explainedVarThreshold = 0.5f;
		int64_t policyLRRampSteps = 100'000'000;
		// > 0: gated steps instead of the continuous ramp. The LR goes up one step (1/20 of the way from the start
		// scale to 100%) at a time, and only at a check: every policyLRStepInterval steps, if the MEDIAN explained
		// variance over the iterations since the previous check is above explainedVarThreshold
		int64_t policyLRStepInterval = 0;

		// Deterministic evals (ExploiterEval): games split evenly across modes, headline = mean of the per-mode rates
		// (league: uniform mean over opponents). Training goals stay stochastic (Exploiter/Rolling Win Rate).
		int evalArenas = 12;                           // Eval arenas, laid out by evalEnvCreateFn (a multiple of the mode count)
		RLGC::EnvCreateFn evalEnvCreateFn = nullptr;   // Set by ExampleMain: equal arenas per mode
		float evalMaxGameTime = 300;                   // Seconds before an eval game without a goal counts as a draw
		int evalGames = 504;                           // Final / periodic eval, total across modes
		int64_t tsPerEval = 500'000'000;
		int rollingWinRateGoals = 2000;                // Training goals in the rolling (stochastic) win rate window
		int64_t maxTimesteps = 500'000'000;            // Step cap: final eval, save, finish (0 = run until stopped)

		float opponentLossFloor = 0.05f;               // League sampling weight = loss rate + this, so none drops to 0
		float opponentLossAlpha = 0.1f;                // How fast an opponent's loss rate follows new results

		// Early stop: every earlyStopInterval steps, a deterministic eval of earlyStopGames vs the target; the run ends
		// (FINISHED, reason "win_rate") once the headline reaches earlyStopWinRate. 0 = off.
		float earlyStopWinRate = 0.70f;
		int earlyStopGames = 192;                      // 64 per mode
		int64_t earlyStopInterval = 25'000'000;

		// Adaptive restart (fresh runs only): each candidate (an unused, finished same-role exploiter) plays restartGames
		// vs the target; the one inside [restartMinWinRate, restartMaxWinRate] closest to the max becomes the starting
		// weights, otherwise the snapshot
		std::vector<std::filesystem::path> restartCandidates = {};
		std::vector<std::string> restartCandidateNames = {};
		int restartGames = 96;
		int restartMaxCandidates = 8;                  // Newest ones only, so a long history can't stall a start
		float restartMinWinRate = 0.2f, restartMaxWinRate = 0.5f;
	};

	// Main run: train and evaluate against finished exploiters from exploitersFolder (ExploiterPool.h).
	// Unused by exploiter runs.
	struct ExploiterPoolConfig {
		bool enabled = false;
		std::filesystem::path exploitersFolder = {};   // Where exploiter runs live; CONTROL.json there can change trainShare live

		// Share of ALL iterations played against a training exploiter (when there is one). Taken out of the old-version
		// share, so total non-self exposure is unchanged (see the opponent cascade in Learner.cpp)
		float trainShare = 0.15f;
		float admitMinWinRate = 0.0f;   // Exploiter's final eval vs its target needed to join (0 = all join)
		float retireWinRate = 0.60f;    // Main's eval win rate vs a training exploiter that counts toward retiring it...
		int retireConsecutive = 0;      // ...this many evals in a row (0 = never retire)
		int maxTraining = 0;            // Training exploiters at once (0 = no limit)
		int heldOutEvery = 3;           // Every Nth admitted exploiter is held out (never trained against)
		float lossFloor = 0.05f;        // Training exploiters are picked with weight = main's loss rate vs it + this
		float lossAlpha = 0.1f;
		// "Mastered": main beat it at least this often in its last eval (0 = off): its sampling weight is scaled by
		// masteredWeightScale and it's evaluated every tsPerRetiredEval until main drops below the bar again
		float masteredWinRate = 0.60f;
		float masteredWeightScale = 0.25f;

		int scanInterval = 50;          // Iterations between scans for newly finished exploiters (and CONTROL.json re-reads)
		int evalArenas = 12;
		RLGC::EnvCreateFn evalEnvCreateFn = nullptr;
		int evalGames = 252;            // Deterministic games per eval, per exploiter, total across modes
		float evalMaxGameTime = 300;
		int64_t tsPerEval = 500'000'000;          // Evals vs training and held-out exploiters
		int64_t tsPerRetiredEval = 2'000'000'000; // Forgetting checks vs retired and mastered exploiters
	};

	// https://github.com/AechPro/rlgym-ppo/blob/main/rlgym_ppo/learner.py
	struct LearnerConfig {
		int numGames = 300;

		int tickSkip = 8;
		int actionDelay = 7;

		bool renderMode = false;
		// If renderMode, this is the scaling of time for the game
		// 1.0 = Run the game at real time
		// 2.0 = Run the game twice as fast as real time
		float renderTimeScale = 1.0f;

		// If renderMode, poll checkpointFolder every this-many seconds and hot-swap in the newest
		// checkpoint a separate training process has written, so the visualization tracks the model
		// as it learns. Set <= 0 to pin to whatever checkpoint was loaded at startup.
		float renderReloadSecs = 5.0f;

		// RENDER MODE ONLY. Supplies raw controller state for the opponent's cars, so an
		// external agent (an RLBot bot) can play them with continuous controls instead
		// of our discrete action table.
		//
		// Injected as a callback rather than an interface the Learner knows about, on
		// purpose: the RLBot side needs flatbuffers and the RLBot schema, and none of
		// that belongs in GigaLearnCPP. The implementation lives with the executable
		// (src/), which already links RLBotCPP.
		//
		// Called once per decision step with the arena's current state and the set of
		// player rows the opponent owns. Fill `outControls` for those rows and return
		// true; return false to fall back to the action table for this step (bot not
		// connected yet, no fresh input, etc).
		// What the source wants the panel to display about itself.
		struct ExternalControlStatus {
			bool running = false;   // the agent's process is up
			bool connected = false; // ...and it has completed its handshake
			bool controlling = false; // ...and is actually sending inputs we applied
			std::string error;
			std::string name;
		};

		// Called once per decision step with the opponent spec the panel selected (the
		// source owns interpreting it), the arena state, and the player rows the opponent
		// holds. Fill `outControls` for those rows and return true; return false to fall
		// back to the action table for this step (agent not connected, no fresh input,
		// or this spec isn't one the source handles).
		//
		// `outValid` is PER ROW, not all-or-nothing: in a 2v2 one agent can be connected
		// while the other is still starting, and the car without an agent must keep
		// playing on the policy rather than freeze on a zeroed control.
		//
		// Servicing the agent's lifecycle here — rather than through a separate hook —
		// keeps it to one call site: render mode never leaves the collection loop, so
		// this callback is the only place that reliably runs every step.
		std::function<bool(
			const std::string& opponentSpec,
			RocketSim::Team opponentTeam,
			const RLGC::GameState& state,
			const std::vector<int>& opponentPlayerIndices,
			std::vector<RLGC::Action>& outControls,
			std::vector<uint8_t>& outValid,
			ExternalControlStatus& outStatus)> externalControlSource = nullptr;

		// Render only: lists the external agents the panel may offer as opponents. Kept
		// alongside externalControlSource because whoever can serve an agent is also the
		// only thing that knows how to find one.
		std::function<std::vector<std::string>()> vizBotFinder = nullptr;

		// If renderMode, the UDP port the viewer's control panel sends commands to
		// (bound on loopback only). See Util/VizControl.h.
		int vizControlPort = 9276;
		// Rewind history depth, in decision steps. 900 at 15 Hz is a 60-second scrubback;
		// a snapshot is ~1KB at 3v3, so the whole ring is under a megabyte.
		int vizHistoryFrames = 900;

		PPOLearnerConfig ppo = {};

		// Checkpoints are saved here as timestep-numbered subfolders
		//	e.g. a checkpoint at 20,000 steps will save to a subfolder called "20000"
		// Set empty to disable saving
		std::filesystem::path checkpointFolder = "checkpoints"; 

		// Save every timestep
		// Set to zero to just use timestepsPerIteration
		int64_t tsPerSave = 1'000'000;

		int64_t randomSeed = -1; // Set to -1 to use the current time
		int checkpointsToKeep = 8; // Checkpoint storage limit before old checkpoints are deleted, set to -1 to disable

		// Checkpoint robustness (2026-07-13 GPU-lockup incident: the card corrupted
		// device-to-host reads for ~a minute before the driver killed it, poisoning the
		// ENTIRE rotation window - including one checkpoint with finite weights and a
		// behaviorally destroyed policy that structural checks cannot catch).
		// Golden archive: keep the top-N rated checkpoints permanently, outside rotation
		// (dir names "best_r<rating>_<ts>" are non-numeric, so FindNumberedDirs ignores
		// them). Bounds any future loss to "since the last new best". Rate-limited by
		// bestArchiveMinTsSpacing + a small rating margin so a steady climb doesn't copy
		// a checkpoint every save. One archive per mode, N each: best_r (Rating/1v1),
		// best2v2_r and best3v3_r; tools/package_bot.sh --best / --best-2v2 / --best-3v3 package them.
		int bestCheckpointsToKeep = 2;
		float bestArchiveRatingMargin = 5.0f;
		int64_t bestArchiveMinTsSpacing = 25'000'000;
		// Boot sanity probe: after loading a checkpoint that CLAIMS competence (rating >=
		// bootSanityMinRating), run a few kickoff episodes on a throwaway arena and require
		// ball touches. A policy that can't touch kickoff balls is treated as corrupt and
		// the loader falls back (healthy bots touch ~100% within ~4s; the incident's
		// scrambled checkpoint managed 1/10). The rating floor keeps young/fresh runs
		// (which legitimately can't kick off yet) exempt.
		bool bootSanityCheckEnabled = true;
		float bootSanityMinRating = 400.0f;
		LearnerDeviceType deviceType = LearnerDeviceType::AUTO; // Auto will use your CUDA GPU if available
		// Max share of total GPU memory libtorch's caching allocator may hold (0 = no cap). Going over raises a CUDA OOM
		// instead of silently growing - what you want when several trainers share one GPU.
		float cudaMemoryFraction = 0;

		// Allow TF32 tensor-core matmuls on CUDA (Ampere+/Blackwell). libtorch defaults cuBLAS
		// TF32 to OFF, so without this the dense MLPs run strict fp32 and never touch the
		// tensor-core path. Set false for strict-fp32 reproducibility.
		bool allowTF32 = true;

		// Overlap NEXT-iteration experience collection (worker thread, frozen policy snapshot) with
		// THIS iteration's processing + PPO learn. The worker never touches live training weights and
		// its logProbs come from the same snapshot that sampled the actions, so PPO's importance
		// ratio stays exact; the one-update policy lag is standard async-PPO staleness the clip
		// objective absorbs. REVERT to exact sequential behavior by setting this false.
		// Auto-disabled in render mode and with the proposer/practice machinery (unaudited overlap).
		bool pipelinedCollection = false;

		// Standardize the obs values (doesn't seem to help much from my testing)
		bool standardizeObs = false;
		float minObsSTD = 1 / 10.f;
		float maxObsMeanRange = 3;
		int maxObsSamples = 100;

		// Standardize the returns to help the critic (don't disable this unless you know what you're doing)
		bool standardizeReturns = true;
		int maxReturnSamples = 150;

		// Will automatically add the rewards to metrics
		bool addRewardsToMetrics = true;
		int maxRewardSamples = 50; // Maximum reward samples per step for reward metrics
		int rewardSampleRandInterval = 8; // Randomized interval range between sampling rewards (per step)

		// Send metrics to the python metrics receiver
		// The receiver can then log them to wandb or whatever
		bool sendMetrics = true;
		std::string metricsProjectName = "gigalearncpp"; // Project name for the python metrics receiver
		std::string metricsGroupName = "unnamed-runs"; // Group name for the python metrics receiver
		std::string metricsRunName = "gigalearncpp-run"; // Run name for the python metrics receiver

		bool savePolicyVersions = false;
		int64_t tsPerVersion = 25'000'000;
		int maxOldVersions = 32;

		// Train against archived past selves drawn uniformly from the version ring. Since the
		// QD league was removed (2026-07-25) this is the ONLY self-play opponent source, so the
		// realized share matters: the opponent cascade is sequential, and Nexto rolls first, so
		// P(old version) = (1 - externalOpponent.serveFrac) * trainAgainstOldChance.
		// KNOWN PROPERTY, not a bug: the ring is a ROTATING window (maxOldVersions * tsPerVersion
		// = ~800M steps), so as the run matures the pool becomes a narrower slice of recent
		// history - the pool myopia CLAUDE.md documents. Accepted deliberately (user, 2026-07-25)
		// in exchange for having no archive to maintain; the permanent reference set in
		// SkillTrackerConfig is what carries genuinely old styles, and it is never trained against.
		bool trainAgainstOldVersions = false;
		float trainAgainstOldChance = 0.15f; // Chance (from 0 - 1) that an iteration will train against an old version

		// V_exp expectile twin (measurement only - see struct)
		GapSensorConfig gapSensor = {};

		SkillTrackerConfig skillTracker = {};





		// External fixed opponent (Nexto); additive and default-OFF
		ExternalOpponentConfig externalOpponent = {};

		// Exploiter stack (docs/EXPLOITERS.md); both default-OFF
		ExploiterConfig exploiter = {};
		ExploiterPoolConfig exploiterPool = {};

		// Goal-concede-only main run (docs/GCO.md): main's opponents with an exploiter's training recipe. The Learner
		// only needs it for the policy-LR ramp, which reads exploiter.policyLRStartScale / explainedVarThreshold /
		// policyLRRampSteps (the exploiter block is otherwise off in a GCO run)
		bool gcoRun = false;
	};
}