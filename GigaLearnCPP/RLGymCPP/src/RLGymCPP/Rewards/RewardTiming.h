#pragma once
#include "../Gamestates/GameState.h"
#include <unordered_map>
#include <limits>

// ============================================================================
// RewardTiming.h
// 2026-09-12 | Rev 1.0 | NJH+Claude
//
// Shared building blocks for the timing patterns documented in CLAUDE.md
// section 7C (cooldowns, duration gating, duration windows). Before this
// file, every reward re-implemented its own `std::unordered_map<uint32_t,
// float>` + decrement loop by hand (GroundedAtContactReward, FlipDuring-
// ContestPenalty, and ~25 others were verbatim copies of the same ~10 lines).
//
// These are plain composable members, not a base class — rewards keep
// inheriting from Reward/RewardWrapper/PlayerReward as before and just hold
// one of these as a field, calling Reset()/Advance() from their own
// Reset()/PreStep() overrides. See CLAUDE.md 7C for the semantics each type
// encodes; this file only centralizes the bookkeeping, not reward logic.
// ============================================================================

namespace RLGC {

    // Per-step delta time with the standard fallback used everywhere in this
    // codebase (the assumed tick rate for the first step of an episode, when
    // deltaTime may be zero/unset).
    static inline float StepDt(const GameState& state) {
        return (state.deltaTime > 0.0f) ? state.deltaTime : (1.0f / 120.0f);
    }

    // ------------------------------------------------------------------------
    // Cooldown / minimum re-trigger interval (CLAUDE.md 7C).
    // Ticks down every step regardless of whether the trigger condition is
    // met, so the cooldown always expires on schedule even under continuous
    // contact (e.g. an opponent dribble that touches the ball every frame).
    // ------------------------------------------------------------------------
    class PerPlayerCooldown {
        mutable std::unordered_map<uint32_t, float> _remaining;

    public:
        void Reset() { _remaining.clear(); }

        // Ticks this player's cooldown by dt and reports whether it's still
        // active. Call once per GetReward(), before evaluating the trigger
        // condition; if this returns true, return the reward's no-op value
        // (usually 0) immediately without evaluating the trigger.
        bool TickAndCheck(uint32_t carId, float dt) const {
            float& cd = _remaining[carId];
            if (cd > 0.0f) {
                cd -= dt;
                if (cd < 0.0f) cd = 0.0f;
                return true;
            }
            return false;
        }

        // Call when the reward actually fires, to start the cooldown.
        void Trigger(uint32_t carId, float cooldownSeconds) const {
            _remaining[carId] = cooldownSeconds;
        }

        // Alternate release timing: ticks down (if active) and reports whether
        // the cooldown has now expired AFTER this frame's decrement — i.e. a
        // cooldown that lands exactly on 0 this frame releases THIS frame.
        // (TickAndCheck instead gates on the PRE-decrement value, so the same
        // frame stays blocked and release happens next frame.) Some reward
        // classes in this codebase use one convention, some the other — match
        // whichever the file you're migrating already does; don't switch it.
        bool TickAndIsExpired(uint32_t carId, float dt) const {
            float& cd = _remaining[carId];
            if (cd > 0.0f) {
                cd -= dt;
                if (cd < 0.0f) cd = 0.0f;
            }
            return cd <= 0.0f;
        }

        // Peek without ticking — for call sites that decrement all tracked
        // players in bulk (via TickAll, from PreStep) and only need a
        // per-player check inside GetReward.
        bool IsOnCooldown(uint32_t carId) const {
            auto it = _remaining.find(carId);
            return it != _remaining.end() && it->second > 0.0f;
        }

        // Ticks down every currently-tracked player's cooldown by dt, without
        // requiring a carId up front. For call sites that decrement all
        // entries once per step from PreStep rather than per-player from
        // GetReward.
        void TickAll(float dt) {
            for (auto& kv : _remaining) {
                kv.second -= dt;
                if (kv.second < 0.0f) kv.second = 0.0f;
            }
        }

        // Unconditionally decrements this single player's value by dt
        // (floored at 0) and returns the result — for a "retrigger to full
        // value on condition, else decay" grace timer read as a raw value
        // (e.g. gated via `> 0`) rather than a plain on/off check.
        float TickDown(uint32_t carId, float dt) const {
            float& cd = _remaining[carId];
            cd -= dt;
            if (cd < 0.0f) cd = 0.0f;
            return cd;
        }
    };

    // ------------------------------------------------------------------------
    // Duration gating (CLAUDE.md 7C): a per-player counter that accumulates
    // while a condition holds and resets to 0 the instant it breaks.
    // ------------------------------------------------------------------------
    class PerPlayerDuration {
        mutable std::unordered_map<uint32_t, float> _duration;

    public:
        void Reset() { _duration.clear(); }

        // Advances this player's counter (+= dt if conditionMet, else reset
        // to 0) and returns the resulting duration.
        float Update(uint32_t carId, float dt, bool conditionMet) const {
            float& dur = _duration[carId];
            if (conditionMet) dur += dt;
            else dur = 0.0f;
            return dur;
        }

        float Get(uint32_t carId) const {
            auto it = _duration.find(carId);
            return it != _duration.end() ? it->second : 0.0f;
        }
    };

    // ------------------------------------------------------------------------
    // Duration windows (CLAUDE.md 7C): "how long since an event" tracking.
    // Advance() must be called exactly once per step from PreStep() — NOT
    // from GetReward(), which runs once per player and would inflate the
    // clock 2-6x in 2v2/3v3.
    // ------------------------------------------------------------------------
    class PerPlayerEventWindow {
        float _currentTime = 0.0f;
        mutable std::unordered_map<uint32_t, float> _lastEventTime;

    public:
        void Reset() { _lastEventTime.clear(); _currentTime = 0.0f; }

        // Call once per step, from PreStep().
        void Advance(float dt) { _currentTime += dt; }

        void MarkEvent(uint32_t carId) const { _lastEventTime[carId] = _currentTime; }

        // Infinity if the event has never fired for this player.
        float TimeSinceEvent(uint32_t carId) const {
            auto it = _lastEventTime.find(carId);
            return it != _lastEventTime.end()
                ? (_currentTime - it->second)
                : std::numeric_limits<float>::infinity();
        }

        bool WithinWindow(uint32_t carId, float windowSeconds) const {
            return TimeSinceEvent(carId) < windowSeconds;
        }
    };

} // namespace RLGC
