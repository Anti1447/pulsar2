#pragma once
// Port of v2's Sim/GameEventTracker (shot/goal/save attribution) for the v3
// compat layer. Logic is line-for-line v2 (GameEventTracker.cpp @ vendored
// 2.1.1) with engine-internal reads swapped for compat equivalents:
//   car->_internalState.ballHitInfo  ->  car->_ballHitInfo
//   ball->_internalState.updateCounter -> ball->_updateCounter
//   ball rigid-body pos/vel reads     -> ball->GetState()
#include "RocketSim.h"

RS_NS_START

typedef std::function<void(class Arena* arena, Car* shooter, Car* passer, void* userInfo)> ShotEventFn;
typedef std::function<void(class Arena* arena, Car* scorer, Car* passer, void* userInfo)> GoalEventFn;
typedef std::function<void(class Arena* arena, Car* saver, void* userInfo)> SaveEventFn;

struct GameEventTrackerConfig {
	float shotMinSpeed = 1750;
	float shotTouchMinDelay = 0.3f;
	float predScoreExtraMargin = 0;
	float shotEventCooldown = 1.0f;
	float shotMinScoreTime = 2.0f;
	float goalMaxTouchTime = 4.0f;
	float passMaxTouchTime = 2.0f;
};

struct GameEventTracker {
	GameEventTrackerConfig config = {};

	struct {
		ShotEventFn func = NULL;
		void* userInfo = NULL;
	} _shotCallback;
	void SetShotCallback(ShotEventFn callbackFn, void* userInfo = NULL) {
		_shotCallback.func = callbackFn;
		_shotCallback.userInfo = userInfo;
	}

	// Verified shot (2026-11-05): raised together with the shot callback, but ONLY when the credited shooter is also the LAST car
	// to touch the ball (any team). The plain shot event credits the latest touch by the team OPPOSITE the threatened net, even when
	// another car touched after it - so a car that blasts the ball at its own net after an opponent's touch hands that opponent a
	// phantom shot. ShotReward's replacement (VerifiedShotReward) reads this one; the plain shot event (and saves) are unchanged.
	struct {
		ShotEventFn func = NULL;
		void* userInfo = NULL;
	} _verifiedShotCallback;
	void SetVerifiedShotCallback(ShotEventFn callbackFn, void* userInfo = NULL) {
		_verifiedShotCallback.func = callbackFn;
		_verifiedShotCallback.userInfo = userInfo;
	}

	struct {
		GoalEventFn func = NULL;
		void* userInfo = NULL;
	} _goalCallback;
	void SetGoalCallback(GoalEventFn callbackFn, void* userInfo = NULL) {
		_goalCallback.func = callbackFn;
		_goalCallback.userInfo = userInfo;
	}

	struct {
		SaveEventFn func = NULL;
		void* userInfo = NULL;
	} _saveCallback;
	void SetSaveCallback(SaveEventFn callbackFn, void* userInfo = NULL) {
		_saveCallback.func = callbackFn;
		_saveCallback.userInfo = userInfo;
	}

	float _shotCooldown = 0;
	bool _ballShot = false;
	Team _ballShotGoalTeam = {};
	bool _ballScoredLast = false;
	uint64_t _lastBallUpdateCount = 0;

	void Update(Arena* arena);

	bool autoStateSetDetection = true;

	void ResetPersistentInfo();
};

RS_NS_END
