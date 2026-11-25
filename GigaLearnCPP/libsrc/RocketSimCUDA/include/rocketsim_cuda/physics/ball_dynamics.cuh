#pragma once
#include "rocketsim_cuda/config.h"
#include "rocketsim_cuda/math/vec3.cuh"
#include "rocketsim_cuda/math/quat.cuh"
#include "rocketsim_cuda/physics/integrator.cuh"
#include "rocketsim_cuda/physics/arena_sdf.cuh"
#include "rocketsim_cuda/physics/contact_solver.cuh"
#include "rocketsim_cuda/physics/mesh_ball.cuh"
#include "rocketsim_cuda/types/arena_config.cuh"
#include "rocketsim_cuda/types/ball_state.cuh"

namespace rocketsim_cuda {

/**
 * @brief Step a single ball state forward by dt with analytical arena SDF collisions.
 * Matches 1:1 with CPU RocketSim Ball simulation.
 */
__device__ __forceinline__ void step_ball_state(
    Vec3& pos,
    Vec3& vel,
    Vec3& ang_vel,
    Quat& quat,
    float dt,
    const MutatorConfig& mut_cfg = MutatorConfig(),
    const ArenaMesh* arena_mesh = nullptr) // P2-2: v3's mesh arena when loaded, else the analytic SDF
{
    // Check if sleeping (zero velocity on ground)
    if (vel.length_sq() == 0.0f && ang_vel.length_sq() == 0.0f && pos.z <= BALL_REST_Z + 0.1f) {
        return;
    }

    // 1. Damping
    apply_rigid_body_damping(vel, ang_vel, mut_cfg.ball_drag, 0.0f, dt);

    // 2. Gravity
    const Vec3 vel_pre_gravity = vel;
    vel = vel + mut_cfg.gravity * dt;

    // 3. Arena / Ground collision resolution (updates velocities and pushes out penetration)
    if (arena_mesh && arena_mesh->num_meshes > 0)
        mesh_ball_resolve(*arena_mesh, pos, vel, ang_vel, vel_pre_gravity, mut_cfg.ball_radius, mut_cfg.ball_world_restitution, mut_cfg.ball_world_friction, dt);
    else
        resolve_ball_arena_collision(pos, vel, ang_vel, mut_cfg.ball_radius, mut_cfg.ball_world_restitution, mut_cfg.ball_world_friction, mut_cfg.gravity.z, dt, &vel_pre_gravity);

    // 4. Linear pos integration with post-collision velocity (in Bullet units for exact rounding parity)
    pos = (pos * 0.02f + vel * (0.02f * dt)) * 50.0f;

    // 5. Rotation integration with post-collision angular velocity
    quat = bullet_integrate_quaternion(quat, ang_vel, dt);

    // Velocity Clamping
    float b_speed = vel.length();
    if (b_speed > mut_cfg.ball_max_speed) {
        vel = vel * (mut_cfg.ball_max_speed / b_speed);
    }

    // Pulsar patch P1-1: RocketSim's Ball::_FinishPhysicsTick caps the ball's angular speed (BALL_MAX_ANG_SPEED = 6 rad/s) every tick.
    // Upstream only capped it in the car-ball contact path, so a rolling ball could spin up to its rolling speed and stop losing speed
    // to ground friction (996 vs 716 uu/s after 3 s of rolling at 1500).
    float w_speed_sq = ang_vel.length_sq();
    if (w_speed_sq > BALL_MAX_ANG_SPEED * BALL_MAX_ANG_SPEED) {
        ang_vel = ang_vel * (BALL_MAX_ANG_SPEED / sqrtf(w_speed_sq));
    }
}

} // namespace rocketsim_cuda
