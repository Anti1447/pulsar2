#include "rocketsim_cuda/physics/quantize.cuh"
#include "rocketsim_cuda/config.h"
#include "rocketsim_cuda/sim_context.cuh"
#include "rocketsim_cuda/physics/arena_sdf.cuh"
#include "rocketsim_cuda/physics/integrator.cuh"
#include "rocketsim_cuda/physics/suspension.cuh"
#include "rocketsim_cuda/physics/contact_solver.cuh"
#include "rocketsim_cuda/physics/car_dynamics.cuh"
#include "rocketsim_cuda/physics/car_contact.cuh"
#include "rocketsim_cuda/types/car_config.cuh"
#include "rocketsim_cuda/types/arena_config.cuh"
#include "rocketsim_cuda/physics/ball_dynamics.cuh"

namespace rocketsim_cuda {

__device__ void StepBallDevice(
    uint32_t env_idx,
    BallStateSoA& ball_state,
    float dt,
    const MutatorConfig& mut_cfg = MutatorConfig(),
    const ArenaMesh* arena_mesh = nullptr)
{
    Vec3 pos(ball_state.pos_x[env_idx], ball_state.pos_y[env_idx], ball_state.pos_z[env_idx]);
    Vec3 vel(ball_state.vel_x[env_idx], ball_state.vel_y[env_idx], ball_state.vel_z[env_idx]);
    Vec3 ang_vel(ball_state.ang_vel_x[env_idx], ball_state.ang_vel_y[env_idx], ball_state.ang_vel_z[env_idx]);
    Quat quat(ball_state.q_w[env_idx], ball_state.q_x[env_idx], ball_state.q_y[env_idx], ball_state.q_z[env_idx]);

    step_ball_state(pos, vel, ang_vel, quat, dt, mut_cfg, arena_mesh);

    // Write back coalesced
    ball_state.pos_x[env_idx] = pos.x;
    ball_state.pos_y[env_idx] = pos.y;
    ball_state.pos_z[env_idx] = pos.z;

    ball_state.vel_x[env_idx] = vel.x;
    ball_state.vel_y[env_idx] = vel.y;
    ball_state.vel_z[env_idx] = vel.z;

    ball_state.q_w[env_idx] = quat.w;
    ball_state.q_x[env_idx] = quat.x;
    ball_state.q_y[env_idx] = quat.y;
    ball_state.q_z[env_idx] = quat.z;

    ball_state.ang_vel_x[env_idx] = ang_vel.x;
    ball_state.ang_vel_y[env_idx] = ang_vel.y;
    ball_state.ang_vel_z[env_idx] = ang_vel.z;
}

__device__ void StepCarDevice(
    uint32_t car_idx,
    uint32_t car_in_env_idx,
    uint32_t cars_per_env,
    CarStateSoA& car_state,
    const CarControlsSoA& controls,
    const float* __restrict__ actions_tensor,
    float dt,
    const MutatorConfig& mut_cfg,
    bool has_ball,
    const Vec3& ball_pos,
    const Vec3& ball_vel_bt,
    const Vec3& ball_omega,
    const Vec3* other_cars_pos,
    const Mat3* other_cars_basis,
    const Vec3* other_cars_vel_bt,
    const Vec3* other_cars_omega,
    const uint8_t* other_cars_is_demoed,
    const uint8_t* other_cars_hitbox_type,
    BodyReactionImpulse* ball_reaction,
    BodyReactionImpulse* other_cars_reactions,
    const ArenaMesh* arena_mesh = nullptr)
{
    // Reset ball touched for this car this tick (R4)
    if (car_state.ball_touched) {
        car_state.ball_touched[car_idx] = 0;
    }

    // Skip if demolished, decrement respawn timer and respawn when ready (R2, R4)
    if (car_state.is_demoed && car_state.is_demoed[car_idx]) {
        float timer = car_state.demo_respawn_timer ? car_state.demo_respawn_timer[car_idx] : 0.0f;
        if (timer > 0.0f) {
            timer = fmaxf(timer - dt, 0.0f);
            if (car_state.demo_respawn_timer) {
                car_state.demo_respawn_timer[car_idx] = timer;
            }
        }
        if (timer <= 0.0f) {
            uint32_t env_idx = (cars_per_env > 0) ? (car_idx / cars_per_env) : 0;
            respawn_car_device(
                car_idx, car_in_env_idx, cars_per_env, env_idx,
                car_state, mut_cfg.car_spawn_boost_amount
            );
        }
        return;
    }

    // 1. Controls: Direct VRAM tensor consumption or fallback to CarControlsSoA
    CarControls ctrl;
    if (actions_tensor) {
        const float* a = actions_tensor + car_idx * 8;
        ctrl.throttle  = a[0];
        ctrl.steer     = a[1];
        ctrl.pitch     = a[2];
        ctrl.yaw       = a[3];
        ctrl.roll      = a[4];
        ctrl.jump      = (a[5] > 0.5f) ? 1 : 0;
        ctrl.boost     = (a[6] > 0.5f) ? 1 : 0;
        ctrl.handbrake = (a[7] > 0.5f) ? 1 : 0;
        ctrl.padding   = 0;
        ctrl.clamp_fix();
    } else {
        ctrl.throttle  = controls.throttle[car_idx];
        ctrl.steer     = controls.steer[car_idx];
        ctrl.pitch     = controls.pitch[car_idx];
        ctrl.yaw       = controls.yaw[car_idx];
        ctrl.roll      = controls.roll[car_idx];
        ctrl.boost     = controls.boost[car_idx];
        ctrl.jump      = controls.jump[car_idx];
        ctrl.handbrake = controls.handbrake[car_idx];
        ctrl.padding   = 0;
        ctrl.clamp_fix();
    }

    // 2. Load car state
    Vec3 pos(car_state.pos_x[car_idx], car_state.pos_y[car_idx], car_state.pos_z[car_idx]);
    Vec3 pos_bt = pos * 0.02f;
    Vec3 vel(car_state.vel_x[car_idx], car_state.vel_y[car_idx], car_state.vel_z[car_idx]);
    Vec3 omega(car_state.ang_vel_x[car_idx], car_state.ang_vel_y[car_idx], car_state.ang_vel_z[car_idx]);
    Quat quat(car_state.q_w[car_idx], car_state.q_x[car_idx], car_state.q_y[car_idx], car_state.q_z[car_idx]);

    Mat3 basis = Mat3::from_quat(quat);

    // Hitbox preset lookup (R5)
    uint8_t hitbox_type = car_state.hitbox_type ? car_state.hitbox_type[car_idx] : 0;
    const CarConfig& car_cfg = get_car_config(hitbox_type);

    // 3. Multi-body wheel raycast query (R3)
    uint8_t wheels_contact[4] = {0};
    float susp_lengths[4] = {0};
    WheelRaycastResult wheel_results[4];

    Vec3 vel_bt_init = vel * 0.02f;
    evaluate_car_wheels_raycast_multibody(
        pos, basis, car_cfg,
        has_ball, ball_pos, mut_cfg.ball_radius,
        cars_per_env, car_in_env_idx,
        other_cars_pos, other_cars_basis, other_cars_is_demoed, other_cars_hitbox_type,
        wheels_contact, susp_lengths, wheel_results,
        vel_bt_init, omega, arena_mesh
    );

    // Track ball touched from wheel contacts (R4)
    if (has_ball && car_state.ball_touched) {
        for (int w = 0; w < 4; ++w) {
            if (wheel_results[w].hit_object_type == HIT_OBJECT_BALL) {
                car_state.ball_touched[car_idx] = 1;
                break;
            }
        }
    }

    int num_wheels_contact = wheels_contact[0] + wheels_contact[1] + wheels_contact[2] + wheels_contact[3];
    bool is_on_ground = (num_wheels_contact >= 3);

    // Jump, flip, air and boost logic reads LAST tick's wheel contact, as RocketSim v3 does.
    bool was_on_ground = car_state.is_on_ground[car_idx] != 0;
    int prev_wheels_contact = car_state.wheel_contact_0[car_idx] + car_state.wheel_contact_1[car_idx]
                            + car_state.wheel_contact_2[car_idx] + car_state.wheel_contact_3[car_idx];
    apply_car_flip_zdamp(car_idx, car_state, was_on_ground, vel);

    // 4. Load previous tick's cached wheel dynamics
    float cached_engine_force = car_state.wheel_engine_force[car_idx];
    float cached_brake = car_state.wheel_brake[car_idx];
    float cached_steer_angle = car_state.wheel_steer_angle[car_idx];
    float cached_lat_frictions[4] = {
        car_state.wheel_lat_friction_0[car_idx],
        car_state.wheel_lat_friction_1[car_idx],
        car_state.wheel_lat_friction_2[car_idx],
        car_state.wheel_lat_friction_3[car_idx]
    };
    float cached_long_frictions[4] = {
        car_state.wheel_long_friction_0[car_idx],
        car_state.wheel_long_friction_1[car_idx],
        car_state.wheel_long_friction_2[car_idx],
        car_state.wheel_long_friction_3[car_idx]
    };

    // 5. Update wheel dynamics (throttle, brake, steer, friction curves, sticky downforce) for NEXT tick
    Vec3 total_force(0.0f, 0.0f, 0.0f);
    Vec3 total_torque_omega(0.0f, 0.0f, 0.0f);
    Vec3 contact_normals[4] = {
        wheel_results[0].contact_normal,
        wheel_results[1].contact_normal,
        wheel_results[2].contact_normal,
        wheel_results[3].contact_normal
    };

    update_car_wheel_dynamics(
        car_idx, car_state, ctrl,
        num_wheels_contact, wheels_contact,
        contact_normals, basis,
        vel, omega, dt,
        total_force,
        car_cfg,
        car_state.is_on_ground[car_idx] == 2 ? 0 : prev_wheels_contact // P2-4: v3 has no sticky force on the first tick after a state set
    );

    // P1-10: v3 applies this tick's engine force, brake, steer angle and friction in this tick (v2, and upstream, one tick later)
    cached_engine_force = car_state.wheel_engine_force[car_idx];
    cached_brake = car_state.wheel_brake[car_idx];
    cached_steer_angle = car_state.wheel_steer_angle[car_idx];
    cached_lat_frictions[0] = car_state.wheel_lat_friction_0[car_idx]; cached_lat_frictions[1] = car_state.wheel_lat_friction_1[car_idx];
    cached_lat_frictions[2] = car_state.wheel_lat_friction_2[car_idx]; cached_lat_frictions[3] = car_state.wheel_lat_friction_3[car_idx];
    cached_long_frictions[0] = car_state.wheel_long_friction_0[car_idx]; cached_long_frictions[1] = car_state.wheel_long_friction_1[car_idx];
    cached_long_frictions[2] = car_state.wheel_long_friction_2[car_idx]; cached_long_frictions[3] = car_state.wheel_long_friction_3[car_idx];

    // 6. Landing ends a flip (v3 pre_tick_update)
    float fwd_speed = vel.dot(basis.forward);
    if (was_on_ground) {
        if (car_state.is_flipping[car_idx]) car_state.flip_time[car_idx] = 0.0f;
        car_state.is_flipping[car_idx] = 0;
    }

    // 7. First jump, turtle recovery, double jump / dodge, then air torques (v3 order)
    bool was_jumping = car_state.is_jumping[car_idx] != 0;
    update_car_jump(car_idx, car_state, ctrl, was_on_ground, basis, dt, vel, total_force);
    update_car_auto_flip(car_idx, car_state, ctrl, basis, dt, vel, omega);
    bool prev_is_flipping = car_state.is_flipping[car_idx] != 0;
    float prev_flip_time = car_state.flip_time[car_idx];
    bool flip_ended = update_car_double_jump_or_flip(car_idx, car_state, ctrl, was_on_ground, was_jumping, basis, fwd_speed, dt, vel);
    if (!was_on_ground) {
        update_car_air_control(car_idx, car_state, ctrl, basis, prev_wheels_contact, prev_is_flipping, prev_flip_time,
                               omega, total_torque_omega, total_force);
    }

    // 9. Surface alignment (auto-roll) matching Car.cpp:135
    if (ctrl.throttle != 0.0f && !car_state.is_flipping[car_idx] && !flip_ended &&
        ((num_wheels_contact > 0 && num_wheels_contact < 4) || car_state.world_contact_has_contact[car_idx])) {
        update_car_auto_roll(
            car_idx, car_state, num_wheels_contact, wheels_contact,
            contact_normals, basis, dt, total_force, total_torque_omega
        );
    }
    car_state.world_contact_has_contact[car_idx] = 0;

    // 10. Multi-body suspension & bilateral tire friction impulses (btVehicleRL::updateVehicleSecond) (R3)
    Vec3 vel_bt = vel * 0.02f;
    // v3 raycasts after the jump / dodge impulses, so suspension damping sees them.
    refresh_wheel_contact_velocities(pos, vel_bt, omega, basis, wheel_results);
    apply_suspension_and_friction_multibody(
        pos, basis, car_cfg, wheel_results, dt,
        cached_engine_force, cached_brake, cached_steer_angle,
        cached_lat_frictions, cached_long_frictions,
        vel_bt, omega,
        has_ball, ball_pos, ball_vel_bt, ball_omega,
        cars_per_env, other_cars_pos, other_cars_basis, other_cars_vel_bt, other_cars_omega, other_cars_hitbox_type,
        ball_reaction, other_cars_reactions,
        mut_cfg.car_mass
    );
    vel = vel_bt * 50.0f;

    // 11. Boost update matching Car.cpp:143
    update_car_boost(car_idx, car_state, ctrl, was_on_ground, basis, dt, total_force);

    // 12. Gravity (R6)
    total_force = total_force + mut_cfg.gravity * mut_cfg.car_mass;

    // 13-15. Linear and angular integration with the chassis arena contact (P1-7).
    // On its wheels (3+ touching) a car keeps the upstream order and solver, which track the CPU engines
    // better on ramps and walls. Tumbling or flipping, contacts are solved Bullet-style on the start-of-tick
    // pose before positions move, which tracks flip landings far better.
    vel = vel + total_force * ((1.0f / mut_cfg.car_mass) * dt);
    omega = omega + total_torque_omega * dt;
    if (num_wheels_contact >= 3) {
        pos_bt = pos_bt + (vel * 0.02f) * dt;
        pos = pos_bt * 50.0f;
        resolve_chassis_arena_collision_driving(
            car_idx, car_state, pos, vel, omega, basis, dt,
            car_cfg.hitbox_pos_offset, car_cfg.get_hitbox_half(),
            mut_cfg.car_mass, car_cfg.calculate_inv_inertia_bt(mut_cfg.car_mass)
        );
        pos_bt = pos * 0.02f;
    } else {
        resolve_chassis_arena_collision(
            car_idx, car_state, pos, vel, omega, basis, dt,
            car_cfg.hitbox_pos_offset, car_cfg.get_hitbox_half(),
            mut_cfg.car_mass, car_cfg.calculate_inv_inertia_bt(mut_cfg.car_mass)
        );
        pos_bt = pos_bt + (vel * 0.02f) * dt;
        pos = pos_bt * 50.0f;
    }

    // 16. Quaternion integration
    quat = bullet_integrate_quaternion(quat, omega, dt);

    // 18. Supersonic status update
    update_car_supersonic(car_idx, car_state, vel, dt);

    // 19. Contact cooldown timer update (R1)
    update_car_contact_cooldown(car_idx, car_state, dt);

    // 20. Write back SoA
    car_state.pos_bt_x[car_idx] = pos_bt.x;
    car_state.pos_bt_y[car_idx] = pos_bt.y;
    car_state.pos_bt_z[car_idx] = pos_bt.z;

    car_state.pos_x[car_idx] = pos.x;
    car_state.pos_y[car_idx] = pos.y;
    car_state.pos_z[car_idx] = pos.z;

    car_state.vel_x[car_idx] = vel.x;
    car_state.vel_y[car_idx] = vel.y;
    car_state.vel_z[car_idx] = vel.z;

    car_state.q_w[car_idx] = quat.w;
    car_state.q_x[car_idx] = quat.x;
    car_state.q_y[car_idx] = quat.y;
    car_state.q_z[car_idx] = quat.z;

    car_state.ang_vel_x[car_idx] = omega.x;
    car_state.ang_vel_y[car_idx] = omega.y;
    car_state.ang_vel_z[car_idx] = omega.z;

    car_state.is_on_ground[car_idx] = is_on_ground ? 1 : 0;

    car_state.wheel_contact_0[car_idx] = wheels_contact[0];
    car_state.wheel_contact_1[car_idx] = wheels_contact[1];
    car_state.wheel_contact_2[car_idx] = wheels_contact[2];
    car_state.wheel_contact_3[car_idx] = wheels_contact[3];

    car_state.suspension_length_0[car_idx] = susp_lengths[0];
    car_state.suspension_length_1[car_idx] = susp_lengths[1];
    car_state.suspension_length_2[car_idx] = susp_lengths[2];
    car_state.suspension_length_3[car_idx] = susp_lengths[3];

    // Last Controls
    car_state.last_controls_throttle[car_idx]   = ctrl.throttle;
    car_state.last_controls_steer[car_idx]      = ctrl.steer;
    car_state.last_controls_pitch[car_idx]      = ctrl.pitch;
    car_state.last_controls_yaw[car_idx]        = ctrl.yaw;
    car_state.last_controls_roll[car_idx]       = ctrl.roll;
    car_state.last_controls_boost[car_idx]      = ctrl.boost;
    car_state.last_controls_jump[car_idx]       = ctrl.jump;
    car_state.last_controls_handbrake[car_idx]  = ctrl.handbrake;
}

__device__ void StepCarDevice(
    uint32_t car_idx,
    CarStateSoA& car_state,
    const CarControlsSoA& controls,
    const float* __restrict__ actions_tensor,
    float dt)
{
    MutatorConfig mut_cfg;
    StepCarDevice(
        car_idx, 0, 1,
        car_state, controls, actions_tensor, dt,
        mut_cfg,
        false, Vec3(0,0,0), Vec3(0,0,0), Vec3(0,0,0),
        nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
        nullptr, nullptr
    );
}

__global__ void StepSimulationKernel(
    uint32_t num_envs,
    uint32_t cars_per_env,
    BallStateSoA ball_state,
    CarStateSoA car_state,
    ArenaStateSoA arena_state,
    CarControlsSoA controls,
    const float* __restrict__ actions_tensor,
    float dt,
    MutatorConfigSoA mutator_config = {},
    ArenaConfigSoA arena_config = {},
    CarConfigSoA car_config = {},
    ArenaMesh arena_mesh = {})
{
    uint32_t env_idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (env_idx >= num_envs) return;

    MutatorConfig mut_cfg = mutator_config.get(env_idx);
    ArenaConfig arena_cfg = arena_config.get(env_idx);

    // P1-6 / P1-11: speed limits, then quantization, at the start of the tick (RocketSim v3 step_tick)
    for (uint32_t c = 0; c < cars_per_env; ++c) {
        uint32_t car_idx = env_idx * cars_per_env + c;
        if (car_state.is_demoed && car_state.is_demoed[car_idx]) continue;
        Vec3 p(car_state.pos_x[car_idx], car_state.pos_y[car_idx], car_state.pos_z[car_idx]);
        Vec3 v(car_state.vel_x[car_idx], car_state.vel_y[car_idx], car_state.vel_z[car_idx]);
        Vec3 w(car_state.ang_vel_x[car_idx], car_state.ang_vel_y[car_idx], car_state.ang_vel_z[car_idx]);
        Quat q(car_state.q_w[car_idx], car_state.q_x[car_idx], car_state.q_y[car_idx], car_state.q_z[car_idx]);
        float vs = v.length_sq(); if (vs > CAR_MAX_SPEED * CAR_MAX_SPEED) v = v * (CAR_MAX_SPEED / sqrtf(vs));
        float ws = w.length_sq(); if (ws > CAR_MAX_ANG_SPEED * CAR_MAX_ANG_SPEED) w = w * (CAR_MAX_ANG_SPEED / sqrtf(ws));
        p = quantize_position(p); v = quantize_velocity(v); w = quantize_ang_velocity(w); q = quantize_rotation(q);
        car_state.pos_x[car_idx] = p.x; car_state.pos_y[car_idx] = p.y; car_state.pos_z[car_idx] = p.z;
        car_state.pos_bt_x[car_idx] = p.x * 0.02f; car_state.pos_bt_y[car_idx] = p.y * 0.02f; car_state.pos_bt_z[car_idx] = p.z * 0.02f;
        car_state.vel_x[car_idx] = v.x; car_state.vel_y[car_idx] = v.y; car_state.vel_z[car_idx] = v.z;
        car_state.ang_vel_x[car_idx] = w.x; car_state.ang_vel_y[car_idx] = w.y; car_state.ang_vel_z[car_idx] = w.z;
        car_state.q_w[car_idx] = q.w; car_state.q_x[car_idx] = q.x; car_state.q_y[car_idx] = q.y; car_state.q_z[car_idx] = q.z;
    }
    {
        Vec3 p = quantize_position(Vec3(ball_state.pos_x[env_idx], ball_state.pos_y[env_idx], ball_state.pos_z[env_idx]));
        Vec3 v = quantize_velocity(Vec3(ball_state.vel_x[env_idx], ball_state.vel_y[env_idx], ball_state.vel_z[env_idx]));
        Vec3 w = quantize_ang_velocity(Vec3(ball_state.ang_vel_x[env_idx], ball_state.ang_vel_y[env_idx], ball_state.ang_vel_z[env_idx]));
        Quat q = quantize_rotation(Quat(ball_state.q_w[env_idx], ball_state.q_x[env_idx], ball_state.q_y[env_idx], ball_state.q_z[env_idx]));
        ball_state.pos_x[env_idx] = p.x; ball_state.pos_y[env_idx] = p.y; ball_state.pos_z[env_idx] = p.z;
        ball_state.vel_x[env_idx] = v.x; ball_state.vel_y[env_idx] = v.y; ball_state.vel_z[env_idx] = v.z;
        ball_state.ang_vel_x[env_idx] = w.x; ball_state.ang_vel_y[env_idx] = w.y; ball_state.ang_vel_z[env_idx] = w.z;
        ball_state.q_w[env_idx] = q.w; ball_state.q_x[env_idx] = q.x; ball_state.q_y[env_idx] = q.y; ball_state.q_z[env_idx] = q.z;
    }

    // Multi-body kinematics snapshot for this environment (up to 8 cars for 4v4 support)
    constexpr uint32_t MAX_ENV_CARS = 8;
    Vec3 cars_pos[MAX_ENV_CARS];
    Mat3 cars_basis[MAX_ENV_CARS];
    Vec3 cars_vel_bt[MAX_ENV_CARS];
    Vec3 cars_omega[MAX_ENV_CARS];
    uint8_t cars_is_demoed[MAX_ENV_CARS];
    uint8_t cars_hitbox_type[MAX_ENV_CARS];
    Quat cars_quat[MAX_ENV_CARS];

    uint32_t active_cars = (cars_per_env > MAX_ENV_CARS) ? MAX_ENV_CARS : cars_per_env;
    for (uint32_t c = 0; c < active_cars; ++c) {
        uint32_t car_idx = env_idx * cars_per_env + c;
        cars_pos[c] = Vec3(car_state.pos_x[car_idx], car_state.pos_y[car_idx], car_state.pos_z[car_idx]);
        Quat q(car_state.q_w[car_idx], car_state.q_x[car_idx], car_state.q_y[car_idx], car_state.q_z[car_idx]);
        cars_basis[c] = Mat3::from_quat(q);
        cars_quat[c] = q;
        cars_vel_bt[c] = Vec3(car_state.vel_x[car_idx] * 0.02f, car_state.vel_y[car_idx] * 0.02f, car_state.vel_z[car_idx] * 0.02f);
        cars_omega[c] = Vec3(car_state.ang_vel_x[car_idx], car_state.ang_vel_y[car_idx], car_state.ang_vel_z[car_idx]);
        cars_is_demoed[c] = car_state.is_demoed ? car_state.is_demoed[car_idx] : 0;
        cars_hitbox_type[c] = car_state.hitbox_type ? car_state.hitbox_type[car_idx] : 0;
    }

    Vec3 ball_pos(ball_state.pos_x[env_idx], ball_state.pos_y[env_idx], ball_state.pos_z[env_idx]);
    Vec3 ball_vel_bt(ball_state.vel_x[env_idx] * 0.02f, ball_state.vel_y[env_idx] * 0.02f, ball_state.vel_z[env_idx] * 0.02f);
    Vec3 ball_omega(ball_state.ang_vel_x[env_idx], ball_state.ang_vel_y[env_idx], ball_state.ang_vel_z[env_idx]);

    const Vec3 ball_vel_start(ball_state.vel_x[env_idx], ball_state.vel_y[env_idx], ball_state.vel_z[env_idx]);

    BodyReactionImpulse total_ball_reaction;
    BodyReactionImpulse total_cars_reactions[MAX_ENV_CARS];

    // Step Cars
    for (uint32_t c = 0; c < cars_per_env; ++c) {
        uint32_t car_idx = env_idx * cars_per_env + c;
        StepCarDevice(
            car_idx, c, active_cars,
            car_state, controls, actions_tensor, dt,
            mut_cfg,
            true, ball_pos, ball_vel_bt, ball_omega,
            cars_pos, cars_basis, cars_vel_bt, cars_omega,
            cars_is_demoed, cars_hitbox_type,
            &total_ball_reaction, total_cars_reactions, &arena_mesh
        );
    }

    // Apply wheel reactions to ball (Newton's 3rd Law) (R3)
    if (total_ball_reaction.lin_impulse_bt.length_sq() > 0.0f || total_ball_reaction.ang_impulse_bt.length_sq() > 0.0f) {
        apply_wheel_reaction_to_ball(total_ball_reaction, ball_vel_bt, ball_omega);
        ball_state.vel_x[env_idx] = ball_vel_bt.x * 50.0f;
        ball_state.vel_y[env_idx] = ball_vel_bt.y * 50.0f;
        ball_state.vel_z[env_idx] = ball_vel_bt.z * 50.0f;
        ball_state.ang_vel_x[env_idx] = ball_omega.x;
        ball_state.ang_vel_y[env_idx] = ball_omega.y;
        ball_state.ang_vel_z[env_idx] = ball_omega.z;
    }

    // Apply wheel reactions to target cars (Newton's 3rd Law) (R3)
    for (uint32_t c = 0; c < active_cars; ++c) {
        if (total_cars_reactions[c].lin_impulse_bt.length_sq() > 0.0f || total_cars_reactions[c].ang_impulse_bt.length_sq() > 0.0f) {
            uint32_t car_idx = env_idx * cars_per_env + c;
            Vec3 c_vel_bt(car_state.vel_x[car_idx] * 0.02f, car_state.vel_y[car_idx] * 0.02f, car_state.vel_z[car_idx] * 0.02f);
            Vec3 c_omega(car_state.ang_vel_x[car_idx], car_state.ang_vel_y[car_idx], car_state.ang_vel_z[car_idx]);
            apply_wheel_reaction_to_car(total_cars_reactions[c], cars_basis[c], c_vel_bt, c_omega, cars_hitbox_type[c], mut_cfg.car_mass);
            car_state.vel_x[car_idx] = c_vel_bt.x * 50.0f;
            car_state.vel_y[car_idx] = c_vel_bt.y * 50.0f;
            car_state.vel_z[car_idx] = c_vel_bt.z * 50.0f;
            car_state.ang_vel_x[car_idx] = c_omega.x;
            car_state.ang_vel_y[car_idx] = c_omega.y;
            car_state.ang_vel_z[car_idx] = c_omega.z;
        }
    }

    // Step Ball (R6 mutators)
    StepBallDevice(env_idx, ball_state, dt, mut_cfg, &arena_mesh);

    // Increment Arena Tick Count
    if (arena_state.tick_count) {
        arena_state.tick_count[env_idx]++;
    }

    // Resolve Car-Car Collisions & Bumps (all-pairs c1 < c2 per arena)
    if (cars_per_env > 1) {
        resolve_all_car_car_collisions(
            env_idx, cars_per_env, car_state, dt,
            static_cast<int>(mut_cfg.demo_mode),
            mut_cfg.enable_team_demos,
            mut_cfg.bump_force_scale,
            mut_cfg.respawn_delay
        );
    }

    // Resolve Car-Ball Collisions (R4, R5, R6)
    // Pulsar patch P1-4: simultaneous. Upstream resolved cars one after another against the ball the previous car had already pushed,
    // so in a 50/50 the LAST slot always won (symmetric kickoff race: ball ended 1400 uu toward blue's side on the GPU, centred on the CPU).
    // Now every car resolves against the pre-step ball and the ball's deltas are summed.
    // P1-9: Bullet's order. Contacts are found on the START-of-tick pose and solved on this tick's velocities (forces and tyres
    // already applied), and positions move with the solved velocities. Everything above has already integrated, so the contact's
    // velocity change is carried into the positions afterwards.
    const Vec3 ball_end(ball_state.pos_x[env_idx], ball_state.pos_y[env_idx], ball_state.pos_z[env_idx]);
    BallSnapshot snap{ ball_pos,
                       Vec3(ball_state.vel_x[env_idx], ball_state.vel_y[env_idx], ball_state.vel_z[env_idx]),
                       Vec3(ball_state.ang_vel_x[env_idx], ball_state.ang_vel_y[env_idx], ball_state.ang_vel_z[env_idx]),
                       ball_vel_start };
    Vec3 car_end_pos[8], car_end_vel[8]; Quat car_end_quat[8];
    BallDeltas acc; bool any_hit = false; bool hit_car[8] = { false, false, false, false, false, false, false, false };
    CarBallRow rows[8];
    // Phase 0: detect every contact against the pre-step ball; hit bookkeeping, push-out and the RocketSim extra hit impulse (summed, applied after the solve).
    for (uint32_t c = 0; c < cars_per_env && c < 8; ++c) {
        uint32_t car_idx = env_idx * cars_per_env + c;
        if (car_state.is_demoed && car_state.is_demoed[car_idx]) continue;
        uint8_t ht = car_state.hitbox_type ? car_state.hitbox_type[car_idx] : 0;
        const CarConfig& car_cfg = get_car_config(ht);
        car_end_pos[c] = Vec3(car_state.pos_x[car_idx], car_state.pos_y[car_idx], car_state.pos_z[car_idx]);
        car_end_vel[c] = Vec3(car_state.vel_x[car_idx], car_state.vel_y[car_idx], car_state.vel_z[car_idx]);
        car_end_quat[c] = Quat(car_state.q_w[car_idx], car_state.q_x[car_idx], car_state.q_y[car_idx], car_state.q_z[car_idx]);
        const bool rewind = !cars_is_demoed[c]; // a car that respawned this tick has no meaningful earlier pose
        Vec3 hit_pos = rewind ? cars_pos[c] : car_end_pos[c];
        Quat hit_quat = rewind ? cars_quat[c] : car_end_quat[c];
        Vec3 hit_vel = rewind ? cars_vel_bt[c] * 50.0f : car_end_vel[c];
        car_state.pos_x[car_idx] = hit_pos.x; car_state.pos_y[car_idx] = hit_pos.y; car_state.pos_z[car_idx] = hit_pos.z;
        car_state.q_w[car_idx] = hit_quat.w; car_state.q_x[car_idx] = hit_quat.x; car_state.q_y[car_idx] = hit_quat.y; car_state.q_z[car_idx] = hit_quat.z;
        cars_pos[c] = hit_pos; cars_quat[c] = hit_quat;
        if (resolve_car_ball_collision(env_idx, car_idx, ball_state, car_state, arena_state, dt, mut_cfg, car_cfg, &snap, &acc, &rows[c], &hit_vel)) {
            hit_car[c] = true; any_hit = true;
            if (car_state.ball_touched) car_state.ball_touched[car_idx] = 1;
        } else {
            car_state.pos_x[car_idx] = car_end_pos[c].x; car_state.pos_y[car_idx] = car_end_pos[c].y; car_state.pos_z[car_idx] = car_end_pos[c].z;
            car_state.q_w[car_idx] = car_end_quat[c].w; car_state.q_x[car_idx] = car_end_quat[c].x; car_state.q_y[car_idx] = car_end_quat[c].y; car_state.q_z[car_idx] = car_end_quat[c].z;
        }
    }
    // Phase 1 (P1-5): the contact impulses, solved the way Bullet does: Gauss-Seidel passes over all touching cars, alternating direction,
    // on the evolving ball velocity. The result no longer depends on slot order and does not double-count a ball squeezed between two cars.
    Vec3 bvel = snap.vel, bomg = snap.omega;
    if (any_hit) {
        // P1-8: 10 iterations, all normal rows then all friction rows, as Bullet orders them
        for (int pass = 0; pass < 10; ++pass) {
            for (int friction = 0; friction < 2; ++friction) {
                for (uint32_t k = 0; k < cars_per_env && k < 8; ++k) {
                    uint32_t c = (pass & 1) ? (cars_per_env - 1 - k) : k;
                    if (!hit_car[c]) continue;
                    uint32_t car_idx = env_idx * cars_per_env + c;
                    uint8_t ht = car_state.hitbox_type ? car_state.hitbox_type[car_idx] : 0;
                    solve_car_ball_row(car_idx, car_state, rows[c], friction != 0, bvel, bomg, mut_cfg, get_car_config(ht));
                }
            }
        }
        // move the hit cars: this tick's integration, plus the contact's velocity change, plus the push-out
        for (uint32_t c = 0; c < cars_per_env && c < 8; ++c) {
            if (!hit_car[c]) continue;
            uint32_t car_idx = env_idx * cars_per_env + c;
            Vec3 pushed(car_state.pos_x[car_idx], car_state.pos_y[car_idx], car_state.pos_z[car_idx]);
            Vec3 v1(car_state.vel_x[car_idx], car_state.vel_y[car_idx], car_state.vel_z[car_idx]);
            Vec3 w1(car_state.ang_vel_x[car_idx], car_state.ang_vel_y[car_idx], car_state.ang_vel_z[car_idx]);
            Vec3 p1 = car_end_pos[c] + (v1 - car_end_vel[c]) * dt + (pushed - cars_pos[c]);
            Quat q1 = cars_is_demoed[c] ? car_end_quat[c] : bullet_integrate_quaternion(cars_quat[c], w1, dt);
            car_state.pos_x[car_idx] = p1.x; car_state.pos_y[car_idx] = p1.y; car_state.pos_z[car_idx] = p1.z;
            car_state.pos_bt_x[car_idx] = p1.x * 0.02f; car_state.pos_bt_y[car_idx] = p1.y * 0.02f; car_state.pos_bt_z[car_idx] = p1.z * 0.02f;
            car_state.q_w[car_idx] = q1.w; car_state.q_x[car_idx] = q1.x; car_state.q_y[car_idx] = q1.y; car_state.q_z[car_idx] = q1.z;
        }
        Vec3 p = ball_end + acc.dpos + (bvel - snap.vel) * dt, v = bvel + acc.dvel, w = bomg + acc.domega;
        float b_speed = v.length(); if (b_speed > mut_cfg.ball_max_speed) v = v * (mut_cfg.ball_max_speed / b_speed);
        float b_ang = w.length(); if (b_ang > BALL_MAX_ANG_SPEED) w = w * (BALL_MAX_ANG_SPEED / b_ang);
        ball_state.pos_x[env_idx] = p.x; ball_state.pos_y[env_idx] = p.y; ball_state.pos_z[env_idx] = p.z;
        ball_state.vel_x[env_idx] = v.x; ball_state.vel_y[env_idx] = v.y; ball_state.vel_z[env_idx] = v.z;
        ball_state.ang_vel_x[env_idx] = w.x; ball_state.ang_vel_y[env_idx] = w.y; ball_state.ang_vel_z[env_idx] = w.z;
    }

    // Step Arena Termination & Rewards
    if (arena_state.is_goal || arena_state.terminated || arena_state.rewards || arena_state.is_out_of_bounds) {
        float bx = ball_state.pos_x[env_idx];
        float by = ball_state.pos_y[env_idx];
        float bz = ball_state.pos_z[env_idx];
        float b_vy = ball_state.vel_y[env_idx];

        uint8_t goal_flag = 0;
        uint8_t score_team = 0;
        if (fabsf(bx) < GOAL_WIDTH * 0.5f && bz < GOAL_HEIGHT) {
            float goal_threshold = mut_cfg.goal_base_threshold_y + mut_cfg.ball_radius;
            if (by > goal_threshold) {
                goal_flag = 1;
                score_team = 0;
            } else if (by < -goal_threshold) {
                goal_flag = 1;
                score_team = 1;
            }
        }
        if (arena_state.is_goal) {
            arena_state.is_goal[env_idx] = goal_flag;
        }
        if (arena_state.scoring_team) {
            arena_state.scoring_team[env_idx] = score_team;
        }

        uint8_t oob_flag = 0;
        if (bz > ARENA_HEIGHT + 200.0f || fabsf(bx) > ARENA_EXTENT_X + 500.0f || fabsf(by) > ARENA_EXTENT_Y + 1200.0f) {
            oob_flag = 1;
        }
        if (arena_state.is_out_of_bounds) {
            arena_state.is_out_of_bounds[env_idx] = oob_flag;
        }

        if (arena_state.terminated) {
            arena_state.terminated[env_idx] = (goal_flag || oob_flag) ? 1 : 0;
        }
        if (arena_state.truncated) {
            arena_state.truncated[env_idx] = 0;
        }

        if (arena_state.rewards) {
            for (uint32_t c = 0; c < cars_per_env; ++c) {
                uint32_t car_idx = env_idx * cars_per_env + c;
                uint8_t team = (c % 2 == 0) ? 0 : 1;
                float goal_dir = (team == 0) ? 1.0f : -1.0f;
                float rew = (b_vy * goal_dir) * (1.0f / 6000.0f);
                if (goal_flag) {
                    rew += (score_team == team) ? 1.0f : -1.0f;
                }
                arena_state.rewards[car_idx] = rew;
            }
        }
    }

    // Step Boost Pad Cooldowns
    if (arena_state.pad_cooldown) {
        for (uint32_t p = 0; p < MAX_BOOST_PADS; ++p) {
            uint32_t pad_idx = env_idx * MAX_BOOST_PADS + p;
            float cd = arena_state.pad_cooldown[pad_idx];
            if (cd > 0.0f) {
                cd -= dt;
                if (cd <= 0.0f) {
                    cd = 0.0f;
                    if (arena_state.pad_is_active) {
                        arena_state.pad_is_active[pad_idx] = 1;
                    }
                }
                arena_state.pad_cooldown[pad_idx] = cd;
            }
        }
    }

    // Proximity Boost Pickup for cars in this environment
    if (arena_state.pad_is_active) {
        for (uint32_t c = 0; c < cars_per_env; ++c) {
            uint32_t car_idx = env_idx * cars_per_env + c;
            if (car_state.is_demoed && car_state.is_demoed[car_idx]) continue;
            float current_boost = car_state.boost[car_idx];
            if (current_boost >= BOOST_MAX) continue;

            Vec3 car_pos(car_state.pos_x[car_idx], car_state.pos_y[car_idx], car_state.pos_z[car_idx]);
            for (uint32_t p = 0; p < MAX_BOOST_PADS; ++p) {
                uint32_t pad_idx = env_idx * MAX_BOOST_PADS + p;
                if (arena_state.pad_is_active[pad_idx]) {
                    const BoostPadDef& pad = SOCCAR_BOOST_PADS[p];
                    float dz = fabsf(car_pos.z - pad.z);
                    if (dz < 95.0f) {
                        float dx = car_pos.x - pad.x;
                        float dy = car_pos.y - pad.y;
                        if ((dx * dx + dy * dy) < pad.radius_sq) {
                            current_boost = fminf(current_boost + pad.boost_amount, BOOST_MAX);
                            car_state.boost[car_idx] = current_boost;
                            arena_state.pad_is_active[pad_idx] = 0;
                            if (arena_state.pad_cooldown) {
                                arena_state.pad_cooldown[pad_idx] = pad.is_big ? mut_cfg.boost_pad_cooldown_big : mut_cfg.boost_pad_cooldown_small;
                            }
                            if (current_boost >= BOOST_MAX) {
                                break;
                            }
                        }
                    }
                }
            }
        }
    }
}

void sim_step_batch(SimContext* ctx, uint32_t batch_size, const CarControlsSoA* controls, const float* actions_tensor) {
    if (!ctx) return;
    uint32_t num_envs = (batch_size > 0) ? batch_size : ctx->GetNumEnvs();
    uint32_t cars_per_env = ctx->GetCarsPerEnv();

    const CarControlsSoA& ctrl_soa = controls ? *controls : ctx->GetControls();
    cudaStream_t stream = ctx->GetStream();

    uint32_t block_size = 128;
    uint32_t grid_size = (num_envs + block_size - 1) / block_size;

    StepSimulationKernel<<<grid_size, block_size, 0, stream>>>(
        num_envs,
        cars_per_env,
        ctx->GetBallState(),
        ctx->GetCarState(),
        ctx->GetArenaState(),
        ctrl_soa,
        actions_tensor,
        DELTA_TIME,
        ctx->GetMutatorConfig(),
        ctx->GetArenaConfig(),
        ctx->GetCarConfig(),
        GetArenaMesh()
    );

    if (ctx->IsGameEventTrackingEnabled()) {
        ctx->UpdateGameEvents(DELTA_TIME, 1);
    }
}

void SimContext::Step(uint32_t batch_size, const float* actions_tensor) {
    sim_step_batch(this, batch_size, nullptr, actions_tensor);
}

} // namespace rocketsim_cuda
