#pragma once
#include "Reward.h"
#include "../Math.h"
#include <atomic>
#include <map>

// Ported from RLGymBot_v3 (BoostManagementRewards.h). Logic unchanged unless noted.

namespace RLGC {

    // ============================================================================
    // SUPERSONIC REWARD
    // Rewards maintaining supersonic speed (2300+ uu/s).
    // Base reward + duration bonus for sustained supersonic.
    // ============================================================================
    class SupersonicReward : public Reward {
    private:
        mutable std::map<uint32_t, float> supersonicDuration;

        inline static std::atomic<long long> s_supersonicTimeMilli{ 0 };
        inline static std::atomic<long long> s_rewardMilli{ 0 };

        static constexpr float SUPERSONIC_THRESHOLD = 2200.0f;
        static constexpr float BASE_REWARD_PER_SECOND = 0.5f;
        static constexpr float MAX_DURATION_BONUS = 2.0f;
        static constexpr float DURATION_SCALE = 5.0f;

    public:
        struct SupersonicMetrics {
            double supersonicTime = 0.0;
            double rewardSum = 0.0;
        };

        static SupersonicMetrics GetAndResetMetrics() {
            SupersonicMetrics m;
            long long t = s_supersonicTimeMilli.exchange(0);
            m.supersonicTime = double(t) / 1000.0;
            long long r = s_rewardMilli.exchange(0);
            m.rewardSum = double(r) / 1000.0;
            return m;
        }

        virtual void Reset(const GameState& initialState) override {
            supersonicDuration.clear();
        }

        virtual float GetReward(const Player& player, const GameState& state, bool isFinal) override {
            float speed = player.vel.Length();
            float& duration = supersonicDuration[player.carId];

            if (speed >= SUPERSONIC_THRESHOLD) {
                duration += state.deltaTime;

                float baseReward = BASE_REWARD_PER_SECOND * state.deltaTime;
                float durationBonus = RS_MIN(duration / DURATION_SCALE, 1.0f) * MAX_DURATION_BONUS;
                float sustainBonus = durationBonus * state.deltaTime;

                float totalReward = baseReward + sustainBonus;

                s_supersonicTimeMilli.fetch_add(static_cast<long long>(state.deltaTime * 1000.0f));
                s_rewardMilli.fetch_add(static_cast<long long>(totalReward * 1000.0f));

                return totalReward;
            }
            else {
                duration = 0.0f;
                return 0.0f;
            }
        }
    };

} // namespace RLGC
