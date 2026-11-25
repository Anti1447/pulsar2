#pragma once
#include <cuda_runtime.h>
#include <cmath>
#include "rocketsim_cuda/config.h"
#include "rocketsim_cuda/math/vec3.cuh"
#include "rocketsim_cuda/math/quat.cuh"
#include "rocketsim_cuda/math/mat3.cuh"
#include "rocketsim_cuda/types/car_state.cuh"
#include "rocketsim_cuda/types/car_controls.cuh"
#include "rocketsim_cuda/types/car_config.cuh"
#include "rocketsim_cuda/physics/suspension.cuh"

namespace rocketsim_cuda {

// Mechanics constants from RLConst
constexpr float JUMP_ACCEL                  = 4375.0f / 3.0f;
constexpr float JUMP_IMMEDIATE_FORCE        = 875.0f / 3.0f;
constexpr int   JUMP_MIN_TICKS              = 3;
constexpr int   JUMP_MAX_TICKS              = 24;
constexpr int   JUMP_REARM_TICKS            = 15;
constexpr float JUMP_LEAVING_GROUND_SPEED   = 50.0f; // v3 suspension_relative_vel > 1 (bullet units)
constexpr float DODGE_DEADZONE              = 0.5f;
constexpr float DOUBLEJUMP_MAX_DELAY        = 1.25f;

constexpr float FLIP_Z_DAMP_120             = 0.35f;
constexpr float FLIP_Z_DAMP_START           = 0.15f;
constexpr float FLIP_Z_DAMP_END             = 0.21f;
constexpr float FLIP_TORQUE_TIME            = 0.65f;
constexpr float FLIP_PITCHLOCK_EXTRA_TIME   = 0.3f;
constexpr float FLIP_PITCH_CANCEL_MIN_TIME  = 0.041f;
constexpr float FLIP_INITIAL_VEL_SCALE      = 500.0f;
constexpr float FLIP_TORQUE_X               = 260.0f; // Left/Right
constexpr float FLIP_TORQUE_Y               = 224.0f; // Forward/backward
constexpr float FLIP_FORWARD_IMPULSE_MAX_SPEED_SCALE  = 1.0f;
constexpr float FLIP_SIDE_IMPULSE_MAX_SPEED_SCALE     = 1.9f;
constexpr float FLIP_BACKWARD_IMPULSE_MAX_SPEED_SCALE = 2.5f;
constexpr float FLIP_BACKWARD_IMPULSE_SCALE_X         = 16.0f / 15.0f;

constexpr float CAR_AIR_CONTROL_TORQUE_X    = 130.0f;
constexpr float CAR_AIR_CONTROL_TORQUE_Y    = 95.0f;
constexpr float CAR_AIR_CONTROL_TORQUE_Z    = 400.0f;

constexpr float CAR_AIR_CONTROL_DAMPING_X   = 30.0f;
constexpr float CAR_AIR_CONTROL_DAMPING_Y   = 20.0f;
constexpr float CAR_AIR_CONTROL_DAMPING_Z   = 50.0f;

constexpr float CAR_TORQUE_SCALE            = 2.0f * 3.14159265358979323846f / 65536.0f * 1000.0f;
constexpr float THROTTLE_AIR_ACCEL          = 200.0f / 3.0f;

constexpr float BRAKING_NO_THROTTLE_SPEED_THRESH = 0.01f;

constexpr float CAR_AUTOROLL_FORCE          = 100.0f;
constexpr float CAR_AUTOROLL_TORQUE         = 80.0f;

constexpr float CAR_AUTOFLIP_IMPULSE        = 200.0f;
constexpr float CAR_AUTOFLIP_ANG_VEL_DELTA  = 1000.0f;
constexpr float CAR_AUTOFLIP_UP_DOT_THRESH  = -0.966f;
constexpr float CAR_AUTOFLIP_TIME           = 0.4f;
constexpr float CAR_AUTOFLIP_NORMZ_THRESH   = 0.707f;

// Piecewise curve functions
__device__ __forceinline__ float get_drive_torque_factor(float abs_fwd_speed) {
    if (abs_fwd_speed <= 0.0f) return 1.0f;
    if (abs_fwd_speed < 1400.0f) return 1.0f - (abs_fwd_speed / 1400.0f) * 0.9f;
    if (abs_fwd_speed < 1410.0f) return 0.1f - ((abs_fwd_speed - 1400.0f) / 10.0f) * 0.1f;
    return 0.0f;
}

__device__ __forceinline__ float get_steer_angle(float speed) {
    if (speed <= 0.0f) return 0.53356f;
    if (speed < 500.0f) return 0.53356f + (speed / 500.0f) * (0.31930f - 0.53356f);
    if (speed < 1000.0f) return 0.31930f + ((speed - 500.0f) / 500.0f) * (0.18203f - 0.31930f);
    if (speed < 1500.0f) return 0.18203f + ((speed - 1000.0f) / 500.0f) * (0.10570f - 0.18203f);
    if (speed < 1750.0f) return 0.10570f + ((speed - 1500.0f) / 250.0f) * (0.08507f - 0.10570f);
    if (speed < 3000.0f) return 0.08507f + ((speed - 1750.0f) / 1250.0f) * (0.03454f - 0.08507f);
    return 0.03454f;
}

__device__ __forceinline__ float get_powerslide_steer_angle(float speed) {
    if (speed <= 0.0f) return 0.39235f;
    if (speed < 2500.0f) return 0.39235f + (speed / 2500.0f) * (0.12610f - 0.39235f);
    return 0.12610f;
}

__device__ __forceinline__ float get_steer_angle_threewheel(float speed) {
    if (speed <= 0.0f) return 0.342473f;
    if (speed >= 2300.0f) return 0.034837f;
    return 0.342473f + (speed / 2300.0f) * (0.034837f - 0.342473f);
}

__device__ __forceinline__ float get_non_sticky_friction_scale(float normal_z) {
    if (normal_z <= 0.0f) return 0.1f;
    if (normal_z < 0.7075f) return 0.1f + (normal_z / 0.7075f) * (0.5f - 0.1f);
    if (normal_z < 1.0f) return 0.5f + ((normal_z - 0.7075f) / (1.0f - 0.7075f)) * (1.0f - 0.5f);
    return 1.0f;
}

__device__ __forceinline__ float get_lat_friction(float slip) {
    if (slip <= 0.0f) return 1.0f;
    if (slip < 1.0f) return 1.0f - 0.8f * slip;
    return 0.2f;
}

__device__ __forceinline__ float get_lat_friction_threewheel(float slip) {
    if (slip <= 0.0f) return 0.30f;
    if (slip >= 1.0f) return 0.25f;
    return 0.30f + slip * (0.25f - 0.30f);
}

__device__ __forceinline__ float get_handbrake_long_friction(float slip) {
    if (slip <= 0.0f) return 0.5f;
    if (slip < 1.0f) return 0.5f + 0.4f * slip;
    return 0.9f;
}

/**
 * @brief Faithful boost persistence and consumption matching Car::_UpdateBoost.
 */
__device__ __forceinline__ void update_car_boost(
    uint32_t car_idx,
    CarStateSoA& car_state,
    const CarControls& ctrl,
    bool is_on_ground,
    const Mat3& basis,
    float dt,
    Vec3& total_force)
{
    float boost = car_state.boost[car_idx];
    bool is_boosting = (car_state.is_boosting[car_idx] != 0);
    float boosting_time = car_state.boosting_time[car_idx];
    bool thrust = false;

    // v3 update_boost
    if (is_boosting) {
        boost = fmaxf(0.0f, boost - 33.3f * dt); // v3 USED_PER_SECOND
        bool latch_expired = !ctrl.boost && boosting_time >= BOOST_MIN_TIME;
        if (boost == 0.0f || latch_expired) {
            is_boosting = false;
        } else {
            thrust = true;
        }
    } else if (ctrl.boost && boost > 0.0f) {
        is_boosting = true;
        thrust = true;
    }

    if (thrust) {
        boosting_time += dt;
        float boost_accel = is_on_ground ? BOOST_ACCEL_GROUND : BOOST_ACCEL_AIR;
        total_force = total_force + basis.forward * (boost_accel * CAR_MASS);
        car_state.time_since_boosted[car_idx] = 0.0f;
    } else {
        boosting_time = 0.0f;
        car_state.time_since_boosted[car_idx] += dt;
    }

    car_state.boost[car_idx] = boost;
    car_state.is_boosting[car_idx] = is_boosting ? 1 : 0;
    car_state.boosting_time[car_idx] = boosting_time;
}

/**
 * @brief Updates handbrake, wheel engine/brake forces, steer angles, friction curves, and sticky downforce.
 */
__device__ __forceinline__ void update_car_wheel_dynamics(
    uint32_t car_idx,
    CarStateSoA& car_state,
    const CarControls& ctrl,
    int num_wheels_in_contact,
    const uint8_t* wheels_contact,
    const Vec3* contact_normals,
    const Mat3& basis,
    const Vec3& vel,
    const Vec3& omega,
    float dt,
    Vec3& total_force,
    const CarConfig& car_cfg = CAR_CONFIG_OCTANE,
    int prev_wheels_in_contact = -1) // sticky force is gated on LAST tick's contact (v3); -1 = this tick's
{
    float fwd_speed = vel.dot(basis.forward);
    float abs_fwd_speed = fabsf(fwd_speed);

    // 1. Handbrake value update
    float handbrake_val = car_state.handbrake_val[car_idx];
    if (ctrl.handbrake) {
        handbrake_val += 5.0f * dt;
    } else {
        handbrake_val -= 2.0f * dt;
    }
    handbrake_val = fminf(fmaxf(handbrake_val, 0.0f), 1.0f);
    car_state.handbrake_val[car_idx] = handbrake_val;

    // 2. Throttle & Brake forces
    float real_throttle = ctrl.throttle;
    float real_brake = 0.0f;
    if (ctrl.boost && car_state.boost[car_idx] > 0.0f) {
        real_throttle = 1.0f;
    }

    float drive_speed_scale = get_drive_torque_factor(abs_fwd_speed);
    float engine_throttle = real_throttle;

    if (!ctrl.handbrake) {
        float abs_throttle = fabsf(real_throttle);
        if (abs_throttle >= 0.001f) {
            if (abs_fwd_speed > 25.0f && ((real_throttle > 0.0f) != (fwd_speed > 0.0f))) {
                real_brake = 1.0f;
                if (abs_fwd_speed > BRAKING_NO_THROTTLE_SPEED_THRESH) {
                    engine_throttle = 0.0f;
                }
            }
        } else {
            engine_throttle = 0.0f;
            real_brake = (abs_fwd_speed < 25.0f) ? 1.0f : 0.15f;
        }
    }

    if (num_wheels_in_contact < 3) {
        drive_speed_scale /= 4.0f;
    }

    // CPU Reference Car.cpp:409-410:
    // THROTTLE_TORQUE_AMOUNT = CAR_MASS_BT * 400.0f = 72000.0f
    // driveEngineForce = engineThrottle * (THROTTLE_TORQUE_AMOUNT * UU_TO_BT) * driveSpeedScale = 72000.0f * 0.02f = 1440.0f
    // BRAKE_TORQUE_AMOUNT = CAR_MASS_BT * (14.25f + 1/3f) = 2625.0f
    // driveBrakeForce = realBrake * (BRAKE_TORQUE_AMOUNT * UU_TO_BT) = 2625.0f * 0.02f = 52.5f
    float drive_engine_force = engine_throttle * 1440.0f * drive_speed_scale;
    float drive_brake_force = real_brake * 52.5f;

    car_state.wheel_engine_force[car_idx] = drive_engine_force;
    car_state.wheel_brake[car_idx] = drive_brake_force;

    // 3. Steer angle (Car.cpp:418)
    float steer_angle = car_cfg.three_wheels ? get_steer_angle_threewheel(abs_fwd_speed) : get_steer_angle(abs_fwd_speed);
    if (handbrake_val > 0.0f) {
        steer_angle += (get_powerslide_steer_angle(abs_fwd_speed) - steer_angle) * handbrake_val;
    }
    steer_angle *= ctrl.steer;
    car_state.wheel_steer_angle[car_idx] = steer_angle;

    // 4. Per-wheel friction coefficients (Car.cpp:432-475)
    #pragma unroll
    for (int w = 0; w < 4; ++w) {
        if (!wheels_contact[w]) continue;

        Vec3 hit_normal = contact_normals[w];
        float steer = (w < 2) ? steer_angle : 0.0f;
        Vec3 lat_dir = basis.right * cosf(steer) - basis.forward * sinf(steer);
        Vec3 long_dir = lat_dir.cross(hit_normal);

        Vec3 wheel_offset = car_cfg.get_wheel_connection_offset(w);
        Vec3 wheel_delta = basis * wheel_offset;
        Vec3 cross_vec = omega.cross(wheel_delta) + vel;

        float base_friction = fabsf(cross_vec.dot(lat_dir));
        float friction_curve_input = 0.0f;
        if (base_friction > 5.0f) {
            friction_curve_input = base_friction / (fabsf(cross_vec.dot(long_dir)) + base_friction);
        }

        float lat_fric = car_cfg.three_wheels ? get_lat_friction_threewheel(friction_curve_input) : get_lat_friction(friction_curve_input);
        float long_fric = 1.0f;

        if (handbrake_val > 0.0f) {
            lat_fric *= (0.1f - 1.0f) * handbrake_val + 1.0f;
            long_fric *= (get_handbrake_long_friction(friction_curve_input) - 1.0f) * handbrake_val + 1.0f;
        }

        bool is_contact_sticky = (real_throttle != 0.0f) || (abs_fwd_speed > 25.0f);
        if (!is_contact_sticky) {
            float non_sticky = get_non_sticky_friction_scale(hit_normal.z);
            lat_fric *= non_sticky;
            long_fric *= non_sticky;
        }

        if (w == 0) car_state.wheel_lat_friction_0[car_idx] = lat_fric;
        else if (w == 1) car_state.wheel_lat_friction_1[car_idx] = lat_fric;
        else if (w == 2) car_state.wheel_lat_friction_2[car_idx] = lat_fric;
        else car_state.wheel_lat_friction_3[car_idx] = lat_fric;

        if (w == 0) car_state.wheel_long_friction_0[car_idx] = long_fric;
        else if (w == 1) car_state.wheel_long_friction_1[car_idx] = long_fric;
        else if (w == 2) car_state.wheel_long_friction_2[car_idx] = long_fric;
        else car_state.wheel_long_friction_3[car_idx] = long_fric;
    }

    // 5. Sticky Downforce (Car.cpp:485-497)
    if ((prev_wheels_in_contact < 0 ? num_wheels_in_contact : prev_wheels_in_contact) > 0) {
        Vec3 sum_normals(0.0f, 0.0f, 0.0f);
        for (int w = 0; w < 4; ++w) {
            if (wheels_contact[w]) {
                sum_normals = sum_normals + contact_normals[w];
            }
        }
        Vec3 upwards_dir = (sum_normals.length_sq() > 1e-6f) ? sum_normals.normalized() : basis.up;
        bool full_stick = (ctrl.throttle != 0.0f) || (abs_fwd_speed > 25.0f); // v3: sticky keeps the raw throttle
        float sticky_scale = (car_cfg.three_wheels ? 0.0f : 0.5f) + (full_stick ? (1.0f - fabsf(upwards_dir.z)) : 0.0f);
        total_force = total_force + upwards_dir * (sticky_scale * GRAVITY_Z * CAR_MASS);
    }
}

// ---------------------------------------------------------------------------
// Jump, flip and air mechanics follow RocketSim v3 (crate 0.3.5, sim/car/base.rs
// and auto_flip.rs), the engine the CPU trainer runs. See PATCHES.md P1-6.
// "on_ground" here is the PREVIOUS tick's wheel contact, as in v3.
// ---------------------------------------------------------------------------

// v3 quantize_axis_input: the game's 8-bit control quantization.
__device__ __forceinline__ float quantize_axis(float x) {
    float c = fminf(fmaxf(x, -1.0f), 1.0f);
    float y = (c < 0.0f) ? fmaxf(c * 128.0f, -128.0f) : fminf(c * 127.0f, 127.0f);
    float w = ((y + 128.0f) + (y + 128.0f)) + 0.5f;
    int byte = ((static_cast<int>(rintf(w))) >> 1) & 0xFF;
    float q = static_cast<float>(byte) - 128.0f;
    return (byte < 0x80) ? q * (1.0f / 128.0f) : q / 127.0f;
}

// One entry of UE3's 16384-entry sine table, indexed in 65536 units per turn.
__device__ __forceinline__ float ue3_sin_angle(int angle) {
    float j = static_cast<float>((angle >> 2) & 0x3FFF);
    float a = ((j + j) * 3.14159265358979323846f) * (1.0f / 16384.0f);
    return static_cast<float>(sin(static_cast<double>(a)));
}

__device__ __forceinline__ void clamp_car_speed(Vec3& vel) {
    float sq = vel.length_sq();
    if (sq > CAR_MAX_SPEED * CAR_MAX_SPEED) vel = vel * (CAR_MAX_SPEED / sqrtf(sq));
}

/**
 * @brief Flip vertical damping, applied before everything else in the tick (v3 apply_flip_zdamp_prepass).
 */
__device__ __forceinline__ void apply_car_flip_zdamp(
    uint32_t car_idx, const CarStateSoA& car_state, bool was_on_ground, Vec3& vel)
{
    if (was_on_ground || !car_state.is_flipping[car_idx]) return;
    float t = car_state.flip_time[car_idx];
    if (t < FLIP_Z_DAMP_START || t > FLIP_TORQUE_TIME) return;
    if (vel.z < 0.0f || t < FLIP_Z_DAMP_END) vel.z *= (1.0f - FLIP_Z_DAMP_120);
}

/**
 * @brief First jump (v3 update_jump). Tick based: jump_time holds ticks * dt.
 * is_jumping: 0 = no, 1 = jumping with the button held so far, 2 = jumping, hold already broken.
 */
__device__ __forceinline__ void update_car_jump(
    uint32_t car_idx,
    CarStateSoA& car_state,
    const CarControls& controls,
    bool on_ground,
    const Mat3& basis,
    float dt,
    Vec3& vel,
    Vec3& total_force)
{
    bool jump_pressed = controls.jump && !(car_state.last_controls_jump[car_idx]);
    uint8_t has_jumped = car_state.has_jumped[car_idx];
    uint8_t is_jumping = car_state.is_jumping[car_idx];
    int ticks = static_cast<int>(lroundf(car_state.jump_time[car_idx] / dt));

    // v3 re-arms in the post-tick of the previous step; applying it first here is the same point in the sequence.
    if (has_jumped && !is_jumping && on_ground) {
        if (car_state.air_time[car_idx] > 0.0f) {
            // just landed: keep the jump if the car is still moving off the surface (all wheels extending)
            if (vel.dot(basis.up) <= JUMP_LEAVING_GROUND_SPEED) has_jumped = 0;
        } else if (ticks > JUMP_REARM_TICKS) {
            has_jumped = 0;
        }
    }

    if (!has_jumped && on_ground && jump_pressed) {
        is_jumping = 1;
        has_jumped = 1;
        ticks = 0;
    }

    ticks += 1;

    if (is_jumping) {
        if (!controls.jump) is_jumping = 2;
        bool sustain = ticks <= JUMP_MIN_TICKS || (is_jumping == 1 && controls.jump && ticks <= JUMP_MAX_TICKS);
        if (!sustain) {
            is_jumping = 0;
            ticks = 1;
        }
    }

    if (is_jumping) {
        if (ticks == 1) {
            vel = vel + basis.up * JUMP_IMMEDIATE_FORCE;
            clamp_car_speed(vel);
        }
        total_force = total_force + basis.up * (JUMP_ACCEL * CAR_MASS);
    }

    car_state.is_jumping[car_idx] = is_jumping;
    car_state.has_jumped[car_idx] = has_jumped;
    car_state.jump_time[car_idx] = static_cast<float>(ticks) * dt;
}

/**
 * @brief Double jump and dodge (v3 update_double_jump_or_flip).
 * @return true on the tick a flip ends.
 */
__device__ __forceinline__ bool update_car_double_jump_or_flip(
    uint32_t car_idx,
    CarStateSoA& car_state,
    const CarControls& controls,
    bool on_ground,
    bool was_jumping,
    const Mat3& basis,
    float forward_speed,
    float dt,
    Vec3& vel)
{
    bool jump_pressed = controls.jump && !(car_state.last_controls_jump[car_idx]);
    uint8_t is_jumping = car_state.is_jumping[car_idx];
    uint8_t has_jumped = car_state.has_jumped[car_idx];
    uint8_t has_double_jumped = car_state.has_double_jumped[car_idx];
    uint8_t has_flipped = car_state.has_flipped[car_idx];
    uint8_t is_flipping = car_state.is_flipping[car_idx];
    float air_time = car_state.air_time[car_idx];
    float air_time_since_jump = car_state.air_time_since_jump[car_idx];
    float flip_time = car_state.flip_time[car_idx];
    bool flip_ended = false;

    if (on_ground) {
        has_double_jumped = 0;
        has_flipped = 0;
        air_time = 0.0f;
        air_time_since_jump = 0.0f;
        flip_time += dt;
    } else {
        air_time += dt;
        if (has_jumped && !was_jumping && !is_jumping) air_time_since_jump += dt;
        else air_time_since_jump = 0.0f;

        if (jump_pressed && air_time_since_jump < DOUBLEJUMP_MAX_DELAY) {
            float in_x = quantize_axis(-controls.pitch);
            float in_y = quantize_axis(controls.yaw);
            float in_z = quantize_axis(controls.roll);
            bool is_flip_input = (fabsf(in_x) + fabsf(in_y) + fabsf(in_z)) >= DODGE_DEADZONE;
            bool can_use = !car_state.is_auto_flipping[car_idx] && !has_double_jumped && !has_flipped;

            if (can_use) {
                has_jumped = 1;
                if (is_flip_input) {
                    flip_time = 0.0f;
                    has_flipped = 1;
                    is_flipping = 1;

                    Vec3 dir(in_x, in_y + in_z, 0.0f);
                    if (fabsf(dir.x) < 0.1f && fabsf(dir.y) < 0.1f) dir = Vec3(0.0f, 0.0f, 0.0f);
                    else dir = dir.normalized();

                    Vec3 dz(fabsf(dir.x) < 0.1f ? 0.0f : dir.x, fabsf(dir.y) < 0.1f ? 0.0f : dir.y, 0.0f);

                    if (dz.length_sq() > 0.0f) {
                        car_state.flip_rel_torque_x[car_idx] = -dir.y;
                        car_state.flip_rel_torque_y[car_idx] = dir.x;
                        car_state.flip_rel_torque_z[car_idx] = 0.0f;

                        bool backward = (fabsf(forward_speed) < 100.0f)
                            ? signbit(dz.x)
                            : (signbit(dz.x) != signbit(forward_speed));
                        float ratio = fabsf(forward_speed) / CAR_MAX_SPEED;
                        float scale_x = backward ? FLIP_BACKWARD_IMPULSE_MAX_SPEED_SCALE : FLIP_FORWARD_IMPULSE_MAX_SPEED_SCALE;

                        Vec3 dv = dz * FLIP_INITIAL_VEL_SCALE;
                        dv.x *= ((scale_x - 1.0f) * ratio) + 1.0f;
                        dv.y *= ((FLIP_SIDE_IMPULSE_MAX_SPEED_SCALE - 1.0f) * ratio) + 1.0f;
                        if (backward) dv.x *= FLIP_BACKWARD_IMPULSE_SCALE_X;

                        int yaw = static_cast<int>(rintf(atan2f(basis.forward.y, basis.forward.x) * (32768.0f / 3.14159265358979323846f)));
                        Vec3 fwd2d(ue3_sin_angle(yaw + 0x4000), ue3_sin_angle(yaw), 0.0f);
                        Vec3 rgt2d(-fwd2d.y, fwd2d.x, 0.0f);

                        vel = vel + fwd2d * dv.x + rgt2d * dv.y;
                        clamp_car_speed(vel);
                    }
                } else {
                    vel = vel + basis.up * JUMP_IMMEDIATE_FORCE;
                    clamp_car_speed(vel);
                    has_double_jumped = 1;
                }
            }
        }

        if (is_flipping) {
            bool still = has_flipped && flip_time < FLIP_TORQUE_TIME;
            is_flipping = still ? 1 : 0;
            flip_time = still ? flip_time + dt : dt;
            flip_ended = !still;
        } else {
            flip_time += dt;
        }
    }

    car_state.has_jumped[car_idx] = has_jumped;
    car_state.has_double_jumped[car_idx] = has_double_jumped;
    car_state.has_flipped[car_idx] = has_flipped;
    car_state.is_flipping[car_idx] = is_flipping;
    car_state.air_time[car_idx] = air_time;
    car_state.air_time_since_jump[car_idx] = air_time_since_jump;
    car_state.flip_time[car_idx] = flip_time;
    return flip_ended;
}

/**
 * @brief Dodge torque, air control with damping and air throttle (v3 update_air_torque). Airborne cars only.
 */
__device__ __forceinline__ void update_car_air_control(
    uint32_t car_idx,
    CarStateSoA& car_state,
    const CarControls& controls,
    const Mat3& basis,
    int num_wheels_contact,
    bool prev_is_flipping,
    float prev_flip_time,
    const Vec3& omega,
    Vec3& total_torque_omega,
    Vec3& total_force)
{
    bool is_flipping = (car_state.is_flipping[car_idx] != 0);

    if (is_flipping && num_wheels_contact < 3) {
        float rel_x = car_state.flip_rel_torque_x[car_idx];
        float rel_y = car_state.flip_rel_torque_y[car_idx];
        if (rel_x != 0.0f || rel_y != 0.0f) {
            float pitch_scale = 1.0f;
            if (rel_y != 0.0f && controls.pitch != 0.0f &&
                signbit(rel_y) == signbit(controls.pitch) &&
                prev_flip_time >= FLIP_PITCH_CANCEL_MIN_TIME) {
                pitch_scale = 1.0f - fminf(fabsf(quantize_axis(-controls.pitch)), 1.0f);
            }
            // components are rounded to whole units before the pitch-cancel scale
            Vec3 dodge_torque(rintf(rel_x * FLIP_TORQUE_X), rintf(rel_y * FLIP_TORQUE_Y), 0.0f);
            dodge_torque.y *= pitch_scale;
            total_torque_omega = total_torque_omega + (basis * dodge_torque);
        }
    }

    if (num_wheels_contact != 0) return;

    float pitch = quantize_axis(controls.pitch);
    float yaw = quantize_axis(controls.yaw);
    float roll = quantize_axis(controls.roll);
    bool any_input = (pitch != 0.0f || yaw != 0.0f || roll != 0.0f);

    float pitch_scale = 1.0f;
    if (any_input && (prev_is_flipping || is_flipping || prev_flip_time < FLIP_PITCHLOCK_EXTRA_TIME)) {
        pitch_scale = 0.0f;
    }

    Vec3 dir_pitch = basis.right * -1.0f;
    Vec3 dir_yaw = basis.up;
    Vec3 dir_roll = basis.forward * -1.0f;

    float in_pitch = pitch * pitch_scale;
    Vec3 air_torque = dir_pitch * (in_pitch * CAR_AIR_CONTROL_TORQUE_X)
                    + dir_yaw * (yaw * CAR_AIR_CONTROL_TORQUE_Y)
                    + dir_roll * (roll * CAR_AIR_CONTROL_TORQUE_Z);

    float damp_pitch = dir_pitch.dot(omega) * CAR_AIR_CONTROL_DAMPING_X * (1.0f - fabsf(in_pitch));
    float damp_yaw = dir_yaw.dot(omega) * CAR_AIR_CONTROL_DAMPING_Y * (1.0f - fabsf(yaw));
    float damp_roll = dir_roll.dot(omega) * CAR_AIR_CONTROL_DAMPING_Z;
    Vec3 damping = dir_yaw * damp_yaw + dir_pitch * damp_pitch + dir_roll * damp_roll;

    total_torque_omega = total_torque_omega + (air_torque - damping) * CAR_TORQUE_SCALE;

    // boosting forces full air throttle
    float throttle = car_state.is_boosting[car_idx] ? 1.0f : quantize_axis(controls.throttle);
    if (throttle != 0.0f) {
        total_force = total_force + basis.forward * (throttle * THROTTLE_AIR_ACCEL * CAR_MASS);
    }
}

/**
 * @brief Turtle recovery (v3 auto_flip.rs).
 */
__device__ __forceinline__ void update_car_auto_flip(
    uint32_t car_idx,
    CarStateSoA& car_state,
    const CarControls& ctrl,
    const Mat3& basis,
    float dt,
    Vec3& vel,
    Vec3& omega)
{
    bool jump_pressed = ctrl.jump && !(car_state.last_controls_jump[car_idx]);

    if (jump_pressed && car_state.world_contact_has_contact[car_idx] &&
        car_state.world_contact_normal_z[car_idx] > CAR_AUTOFLIP_NORMZ_THRESH)
    {
        float y_z = basis.right.z;
        float z_z = basis.up.z;
        float cy = sqrtf(z_z * z_z + y_z * y_z);
        float roll = (cy > 16.0f * 1.1920929e-7f) ? atan2f(y_z, z_z) : 0.0f;

        Vec3 normal(car_state.world_contact_normal_x[car_idx],
                    car_state.world_contact_normal_y[car_idx],
                    car_state.world_contact_normal_z[car_idx]);
        if (normal.dot(basis.up) < CAR_AUTOFLIP_UP_DOT_THRESH) {
            car_state.auto_flip_timer[car_idx] = CAR_AUTOFLIP_TIME * (fabsf(roll) / 3.14159265358979323846f);
            // v3 takes the sign of its rotator-unit roll, which is this angle up to quantization
            car_state.auto_flip_torque_scale[car_idx] = (roll > 0.0f) ? 1.0f : -1.0f;
            car_state.is_auto_flipping[car_idx] = 1;
            vel = vel - basis.up * CAR_AUTOFLIP_IMPULSE;
        }
    }

    if (car_state.is_auto_flipping[car_idx]) {
        float timer = car_state.auto_flip_timer[car_idx];
        if (timer <= 0.0f) {
            car_state.is_auto_flipping[car_idx] = 0;
            car_state.auto_flip_timer[car_idx] = 0.0f;
        } else {
            omega = omega + basis.forward * (CAR_AUTOFLIP_ANG_VEL_DELTA * car_state.auto_flip_torque_scale[car_idx]);
            float sq = omega.length_sq();
            if (sq > CAR_MAX_ANG_SPEED * CAR_MAX_ANG_SPEED) omega = omega * (CAR_MAX_ANG_SPEED / sqrtf(sq));
            car_state.auto_flip_timer[car_idx] = timer - dt;
        }
    }
}

/**
 * @brief Surface alignment (auto-roll) matching Car::_UpdateAutoRoll.
 */
__device__ __forceinline__ void update_car_auto_roll(
    uint32_t car_idx,
    const CarStateSoA& car_state,
    int num_wheels_in_contact,
    const uint8_t* wheels_contact,
    const Vec3* contact_normals,
    const Mat3& basis,
    float dt,
    Vec3& total_force,
    Vec3& total_torque_omega)
{
    Vec3 ground_up_dir;
    if (num_wheels_in_contact > 0) {
        Vec3 sum_contact_dir(0.0f, 0.0f, 0.0f);
        #pragma unroll
        for (int w = 0; w < 4; ++w) {
            if (wheels_contact[w]) {
                sum_contact_dir = sum_contact_dir + contact_normals[w];
            }
        }
        ground_up_dir = (sum_contact_dir.length_sq() > 1e-6f) ? sum_contact_dir.normalized() : basis.up;
    } else {
        ground_up_dir = Vec3(
            car_state.world_contact_normal_x[car_idx],
            car_state.world_contact_normal_y[car_idx],
            car_state.world_contact_normal_z[car_idx]
        );
        if (ground_up_dir.length_sq() > 1e-6f) {
            ground_up_dir = ground_up_dir.normalized();
        } else {
            ground_up_dir = basis.up;
        }
    }

    Vec3 ground_down_dir = ground_up_dir * -1.0f;
    Vec3 forward_dir = basis.forward;
    Vec3 right_dir   = basis.right;

    Vec3 cross_right_dir   = ground_up_dir.cross(forward_dir);
    Vec3 cross_forward_dir = ground_down_dir.cross(cross_right_dir);

    float right_torque_factor   = 1.0f - fminf(fmaxf(right_dir.dot(cross_right_dir), 0.0f), 1.0f);
    float forward_torque_factor = 1.0f - fminf(fmaxf(forward_dir.dot(cross_forward_dir), 0.0f), 1.0f);

    Vec3 torque_dir_right   = forward_dir * (right_dir.dot(ground_up_dir) >= 0.0f ? -1.0f : 1.0f);
    Vec3 torque_dir_forward = right_dir * (forward_dir.dot(ground_up_dir) >= 0.0f ? 1.0f : -1.0f);

    Vec3 torque_right   = torque_dir_right * right_torque_factor;
    Vec3 torque_forward = torque_dir_forward * forward_torque_factor;

    total_force = total_force + ground_down_dir * (CAR_AUTOROLL_FORCE * CAR_MASS);
    total_torque_omega = total_torque_omega + (torque_forward + torque_right) * CAR_AUTOROLL_TORQUE;
}

/**
 * @brief Updates supersonic status matching Car::_PostTickUpdate.
 */
__device__ __forceinline__ void update_car_supersonic(
    uint32_t car_idx,
    CarStateSoA& car_state,
    const Vec3& vel,
    float dt)
{
    if (car_state.is_demoed && car_state.is_demoed[car_idx]) {
        if (car_state.is_supersonic) car_state.is_supersonic[car_idx] = 0;
        if (car_state.supersonic_time) car_state.supersonic_time[car_idx] = 0.0f;
        return;
    }

    float speed_sq = vel.length_sq();
    bool is_super = (car_state.is_supersonic[car_idx] != 0);
    float super_time = car_state.supersonic_time[car_idx];

    if (is_super && super_time < SUPERSONIC_MAINTAIN_MAX_TIME) {
        is_super = (speed_sq >= SUPERSONIC_MAINTAIN_MIN_SPEED * SUPERSONIC_MAINTAIN_MIN_SPEED);
    } else {
        is_super = (speed_sq >= SUPERSONIC_START_SPEED * SUPERSONIC_START_SPEED);
    }

    if (is_super) {
        super_time += dt;
    } else {
        super_time = 0.0f;
    }

    car_state.is_supersonic[car_idx] = is_super ? 1 : 0;
    car_state.supersonic_time[car_idx] = super_time;
}

// ============================================================================
// Canonical Car Respawn Dynamics (src/Sim/Car/Car.cpp:43-69, RLConst.h:393-398)
// ============================================================================

struct RespawnLocationDef {
    float x;
    float y;
    float z;
    float yaw;
};

// 4 Canonical Soccar Respawn Locations (RLConst.h:393-398)
#if defined(__CUDA_ARCH__)
__device__ static constexpr RespawnLocationDef SOCCAR_RESPAWN_LOCATIONS[4] = {
#else
static constexpr RespawnLocationDef SOCCAR_RESPAWN_LOCATIONS[4] = {
#endif
    { -2304.0f, -4608.0f, 36.0f, 1.57079632679f }, // Yaw = pi/2 (90 deg, facing +Y)
    { -2688.0f, -4608.0f, 36.0f, 1.57079632679f },
    {  2304.0f, -4608.0f, 36.0f, 1.57079632679f },
    {  2688.0f, -4608.0f, 36.0f, 1.57079632679f }
};

/**
 * @brief Respawns a car after its demo respawn timer expires.
 * Mirrors Car::Respawn (src/Sim/Car/Car.cpp:43-69) and Arena kickoff mirroring (src/Sim/Arena/Arena.cpp:187-190).
 */
__device__ inline void respawn_car_device(
    uint32_t car_idx,
    uint32_t car_in_env_idx,
    uint32_t cars_per_env,
    uint32_t env_idx,
    CarStateSoA& car_state,
    float spawn_boost = BOOST_SPAWN)
{
    uint8_t team = car_state.team ? car_state.team[car_idx] : (car_in_env_idx % 2);
    uint32_t team_car_idx = (cars_per_env > 1) ? (car_in_env_idx / 2) : car_in_env_idx;
    uint32_t spawn_slot = (env_idx + team_car_idx) % 4;

    const RespawnLocationDef& loc = SOCCAR_RESPAWN_LOCATIONS[spawn_slot];

    float x = loc.x;
    float y = loc.y;
    float z = loc.z;
    Quat q = Quat(0.7071068f, 0.0f, 0.0f, 0.7071068f); // Yaw = pi/2

    if (team != 0) { // Orange team: mirror X -> -X, Y -> -Y, yaw -> yaw + pi (Arena.cpp:187-190)
        x = -loc.x;
        y = -loc.y;
        q = Quat(0.7071068f, 0.0f, 0.0f, -0.7071068f); // Yaw = -pi/2
    }

    // Set position and orientation
    car_state.pos_x[car_idx] = x;
    car_state.pos_y[car_idx] = y;
    car_state.pos_z[car_idx] = z;

    car_state.pos_bt_x[car_idx] = x * 0.02f;
    car_state.pos_bt_y[car_idx] = y * 0.02f;
    car_state.pos_bt_z[car_idx] = z * 0.02f;

    car_state.q_w[car_idx] = q.w;
    car_state.q_x[car_idx] = q.x;
    car_state.q_y[car_idx] = q.y;
    car_state.q_z[car_idx] = q.z;

    // Reset velocities to zero
    car_state.vel_x[car_idx] = 0.0f;
    car_state.vel_y[car_idx] = 0.0f;
    car_state.vel_z[car_idx] = 0.0f;

    car_state.vel_bt_x[car_idx] = 0.0f;
    car_state.vel_bt_y[car_idx] = 0.0f;
    car_state.vel_bt_z[car_idx] = 0.0f;

    car_state.ang_vel_x[car_idx] = 0.0f;
    car_state.ang_vel_y[car_idx] = 0.0f;
    car_state.ang_vel_z[car_idx] = 0.0f;

    // Reset demo flags
    if (car_state.is_demoed) car_state.is_demoed[car_idx] = 0;
    if (car_state.demo_respawn_timer) car_state.demo_respawn_timer[car_idx] = 0.0f;

    // Reset supersonic flags
    if (car_state.is_supersonic) car_state.is_supersonic[car_idx] = 0;
    if (car_state.supersonic_time) car_state.supersonic_time[car_idx] = 0.0f;

    // Set spawn boost
    car_state.boost[car_idx] = spawn_boost;
    car_state.boosting_time[car_idx] = 0.0f;
    car_state.time_since_boosted[car_idx] = 0.0f;
    car_state.is_boosting[car_idx] = 0;

    // Reset support & dynamics flags
    car_state.is_on_ground[car_idx] = 1;
    car_state.has_jumped[car_idx] = 0;
    car_state.is_jumping[car_idx] = 0;
    car_state.jump_time[car_idx] = 0.0f;
    car_state.has_double_jumped[car_idx] = 0;
    car_state.has_flipped[car_idx] = 0;
    car_state.is_flipping[car_idx] = 0;
    car_state.flip_time[car_idx] = 0.0f;
    car_state.air_time[car_idx] = 0.0f;
    car_state.air_time_since_jump[car_idx] = 0.0f;

    car_state.is_auto_flipping[car_idx] = 0;
    car_state.auto_flip_timer[car_idx] = 0.0f;
    car_state.auto_flip_torque_scale[car_idx] = 0.0f;
    car_state.handbrake_val[car_idx] = 0.0f;

    car_state.wheel_contact_0[car_idx] = 1;
    car_state.wheel_contact_1[car_idx] = 1;
    car_state.wheel_contact_2[car_idx] = 1;
    car_state.wheel_contact_3[car_idx] = 1;

    car_state.suspension_length_0[car_idx] = 0.0f;
    car_state.suspension_length_1[car_idx] = 0.0f;
    car_state.suspension_length_2[car_idx] = 0.0f;
    car_state.suspension_length_3[car_idx] = 0.0f;

    if (car_state.ball_touched) car_state.ball_touched[car_idx] = 0;
    if (car_state.ball_hit_is_valid) car_state.ball_hit_is_valid[car_idx] = 0;
    if (car_state.car_contact_cooldown_timer) car_state.car_contact_cooldown_timer[car_idx] = 0.0f;
    if (car_state.car_contact_other_car_id) car_state.car_contact_other_car_id[car_idx] = -1;
}

} // namespace rocketsim_cuda
