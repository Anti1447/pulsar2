// Host-side adapter over the vendored RocketSim-CUDA (plan: docs/GPU_ENV_PLAN.md, phase P1). Pure C++ interface (no CUDA types) so the
// trainer-side code can include it from ordinary translation units. One SimContext per game mode; arena indices are global.
#pragma once
#include <cstdint>
#include <memory>
#include <vector>

namespace pulsar_gpu {
	constexpr int NUM_PADS = 34;
	constexpr int PRED_TICKS = 241; // ball path length: slice k (0.25 s apart) is row 30*k

	struct CarData {
		float pos[3] = {}, vel[3] = {}, angVel[3] = {};
		float quat[4] = { 1, 0, 0, 0 }; // w, x, y, z
		float boost = 33.33333f;
		uint8_t team = 0, onGround = 1, wheels[4] = { 1, 1, 1, 1 };
		uint8_t hasJumped = 0, hasDoubleJumped = 0, hasFlipped = 0, isJumping = 0, isFlipping = 0, isDemoed = 0, isSupersonic = 0, isAutoFlipping = 0, isBoosting = 0;
		float jumpTime = 0, flipTime = 0, airTime = 0, airTimeSinceJump = 0, handbrake = 0, demoRespawn = 0, supersonicTime = 0;
		float autoFlipTimer = 0, autoFlipTorqueScale = 0, timeSinceBoosted = 0, boostingTime = 0, flipRelTorque[3] = {};
		uint8_t worldContact = 0; float worldNormal[3] = {};
		int32_t carContactOther = -1; float carContactCooldown = 0;
		uint8_t hitValid = 0; float hitRelPos[3] = {}, hitExtra[3] = {}; uint64_t hitTick = 0;
		uint8_t bump = 0, bumped = 0, demo = 0, demoed = 0; // step events (cross-team, accumulated over the last Step)
	};
	struct BallData { float pos[3] = { 0, 0, 93.15f }, vel[3] = {}, angVel[3] = {}, quat[4] = { 1, 0, 0, 0 }; };
	struct ControlData { float throttle = 0, steer = 0, pitch = 0, yaw = 0, roll = 0; uint8_t jump = 0, boost = 0, handbrake = 0; };
	struct ModeSpec { int blue, orange, arenas; };

	class ArenaBatch {
	public:
		explicit ArenaBatch(const std::vector<ModeSpec>& modes);
		~ArenaBatch();
		ArenaBatch(const ArenaBatch&) = delete;
		ArenaBatch& operator=(const ArenaBatch&) = delete;

		int NumArenas() const;
		int NumCars(int arena) const;
		// Blue cars are the first `blue` slots? No: slots alternate blue/orange while both teams have cars, then the larger team fills the rest
		// (1v1: B O; 2v2: B O B O; 2v1: B O B; 3v1: B O B B). TeamOfSlot gives the layout.
		int TeamOfSlot(int arena, int slot) const;

		// Resets the arena (pads active, tick 0, no events) and writes the given state. cars.size() must equal NumCars(arena).
		void SetState(int arena, const std::vector<CarData>& cars, const BallData& ball);
		void SetControls(int arena, const std::vector<ControlData>& ctl);
		// Clears the step event flags, then steps `ticks` ticks with the controls currently set.
		void Step(int ticks);
		// Downloads state, events, pads, ticks and ball prediction into the host mirrors. Call after Step / SetState.
		void Sync();

		const CarData& Car(int arena, int slot) const;
		const BallData& Ball(int arena) const;
		bool GoalScored(int arena) const;
		int ScoringTeam(int arena) const; // valid when GoalScored
		uint64_t Tick(int arena) const;
		const uint8_t* PadActive(int arena) const; // NUM_PADS, upstream pad order
		const float* PadCooldown(int arena) const;
		// 8 slices x 6 floats (pos, vel) of the ball 30, 60 ... 240 ticks ahead (physics of the GPU engine itself); valid after Sync.
		const float* BallPredSlices(int arena) const;
		static const float* PadPositions(); // NUM_PADS x 3, upstream pad order

	private:
		struct Impl;
		std::unique_ptr<Impl> impl;
	};
}
