// P1-11: RocketSim v3 sim/quantize.rs, the UE3 network-style quantization applied to every car and the ball at the START of a tick.
#pragma once
#include <cmath>
#include "rocketsim_cuda/math/vec3.cuh"
#include "rocketsim_cuda/math/quat.cuh"

namespace rocketsim_cuda {

__device__ __forceinline__ float quantize_pos_component(float v) { return floorf(v * 100.0f + 0.5f) * (1.0f / 100.0f); }
__device__ __forceinline__ float quantize_vel_component(float v, float scale) {
    float inv = 1.0f / scale;
    float r = truncf(v * scale) * inv;
    float sign = (r > 0.0f) ? 1.0f : ((r < 0.0f) ? -1.0f : 0.0f);
    return r + sign * (0.1f * inv);
}
__device__ __forceinline__ Vec3 quantize_position(const Vec3& p) { return Vec3(quantize_pos_component(p.x), quantize_pos_component(p.y), quantize_pos_component(p.z)); }
__device__ __forceinline__ Vec3 quantize_velocity(const Vec3& v) { return Vec3(quantize_vel_component(v.x, 100.0f), quantize_vel_component(v.y, 100.0f), quantize_vel_component(v.z, 100.0f)); }
__device__ __forceinline__ Vec3 quantize_ang_velocity(const Vec3& v) { return Vec3(quantize_vel_component(v.x, 10000.0f), quantize_vel_component(v.y, 10000.0f), quantize_vel_component(v.z, 10000.0f)); }

__device__ __forceinline__ void quantize_quat_normalize(float q[4]) { // x, y, z, w
    float len = ((q[0] * q[0] + q[1] * q[1]) + q[2] * q[2]) + q[3] * q[3];
    if (!(len > 1e-8f)) { q[0] = q[1] = q[2] = 0.0f; q[3] = 1.0f; return; }
    float s = 1.0f / sqrtf(len);
    for (int i = 0; i < 4; ++i) q[i] *= s;
}
__device__ __forceinline__ float quantize_quat_component(float c) {
    constexpr int MAX = (1 << 18) - 1;
    const float max_f = static_cast<float>(MAX);
    float v = ((c * 1.41421356237309504880f) * 0.5f + 0.5f) * max_f;
    v = (v + v) + 0.5f;
    int k = static_cast<int>(rintf(v)) >> 1;
    k = k < 0 ? 0 : (k > MAX ? MAX : k);
    float r = static_cast<float>(k) / max_f - 0.5f;
    return (r + r) * 0.70710677f;
}
__device__ __forceinline__ Quat quantize_rotation(const Quat& in) {
    float q[4] = { in.x, in.y, in.z, in.w };
    quantize_quat_normalize(q);
    int i01 = (fabsf(q[0]) < fabsf(q[1])) ? 1 : 0;
    int i23 = (fabsf(q[2]) < fabsf(q[3])) ? 3 : 2;
    int big = (fabsf(q[i01]) >= fabsf(q[i23])) ? i01 : i23;
    float sign = (q[big] < 0.0f) ? -1.0f : 1.0f;
    float d[3]; int n = 0;
    for (int i = 0; i < 4; ++i) if (i != big) d[n++] = quantize_quat_component(q[i] * sign);
    float rest = ((1.0f - d[0] * d[0]) - d[1] * d[1]) - d[2] * d[2];
    float o[4]; n = 0;
    for (int i = 0; i < 4; ++i) o[i] = (i == big) ? sqrtf(fmaxf(rest, 0.0f)) : d[n++];
    quantize_quat_normalize(o);
    return Quat(o[3], o[0], o[1], o[2]);
}

} // namespace rocketsim_cuda
