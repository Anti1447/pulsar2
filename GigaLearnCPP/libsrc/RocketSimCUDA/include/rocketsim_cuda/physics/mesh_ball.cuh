// P2-2: ball vs the v3 arena. Ports sphere_concave / convex_plane contact generation, the internal edge normal adjustment
// (dispatch/internal_edge_utility.rs) and the "special" ball-world solve (all contact normals of a tick averaged into one
// row; overlap corrected on position only). Contacts are not carried between ticks (v3's persistent manifold is not ported).
#pragma once
#include <cmath>
#include "rocketsim_cuda/physics/arena_mesh.cuh"

namespace rocketsim_cuda {

constexpr float BALL_MESH_THRESHOLD_BT = 0.0381f;   // (ball radius + 4 UU) * 0.02, in Bullet units
constexpr int BALL_MAX_CONTACTS = 16;

struct BallContact { Vec3 n; float dist; }; // dist: signed separation in Bullet units (negative = overlap)

__device__ __forceinline__ float mesh_segment_sqr_distance(const Vec3& from, const Vec3& to, const Vec3& p, Vec3& nearest) {
    Vec3 diff = p - from, v = to - from;
    float t = v.dot(diff);
    if (t > 0.0f) {
        float vv = v.dot(v);
        if (t < vv) { t /= vv; diff = diff - v * t; } else { t = 1.0f; diff = diff - v; }
    } else t = 0.0f;
    nearest = from + v * t;
    return diff.dot(diff);
}

// front-face sphere test; returns normal (triangle -> centre), contact point on the triangle and signed depth
__device__ __forceinline__ bool mesh_sphere_triangle(const MeshTriangle& tri, const Vec3& center, float radius, float reach,
                                                     Vec3& out_n, Vec3& out_point, float& out_depth) {
    Vec3 to_center = center - tri.p0;
    float plane = to_center.dot(tri.n);
    if (plane < 0.0f || plane >= reach) return false;
    Vec3 o1 = center - tri.p1, o2 = center - tri.p2;
    bool inside = (tri.p1 - tri.p0).cross(to_center).dot(tri.n) >= 0.0f
               && (tri.p2 - tri.p1).cross(o1).dot(tri.n) >= 0.0f
               && (tri.p0 - tri.p2).cross(o2).dot(tri.n) >= 0.0f;
    Vec3 cp;
    const float reach_sq = reach * reach;
    if (inside) {
        cp = center - tri.n * plane;
    } else {
        float best = reach_sq;
        Vec3 near_pt;
        float d = mesh_segment_sqr_distance(tri.p0, tri.p1, center, near_pt); if (d < best) { best = d; cp = near_pt; }
        d = mesh_segment_sqr_distance(tri.p1, tri.p2, center, near_pt); if (d < best) { best = d; cp = near_pt; }
        d = mesh_segment_sqr_distance(tri.p2, tri.p0, center, near_pt); if (d < best) { best = d; cp = near_pt; }
        if (!(best < reach_sq)) return false;
    }
    Vec3 delta = center - cp;
    float dsq = delta.length_sq();
    if (dsq >= reach_sq) return false;
    if (dsq > 1.1920929e-7f) { float d = sqrtf(dsq); out_n = delta * (1.0f / d); out_depth = -(radius - d); }
    else { out_n = tri.n; out_depth = -radius; }
    out_point = cp;
    return true;
}

__device__ __forceinline__ Vec3 mesh_rotate(const Vec3& v, const Vec3& axis, float ang) {
    float c = cosf(ang), s = sinf(ang);
    return v * c + axis.cross(v) * s + axis * (axis.dot(v) * (1.0f - c));
}

/** Bullet's internal edge adjustment of one contact normal; `local_b` is the contact point on the triangle. */
__device__ __forceinline__ Vec3 mesh_adjust_internal_edge(const MeshTriangle& tri, const Vec3& local_b, const Vec3& normal) {
    constexpr float EDGE_DIST = 0.1f, MAX_ANGLE = 6.28318530717958647692f, T_MIN = 1e-05f, T_MAX = 0.99999f;
    const Vec3 pts[3] = { tri.p0, tri.p1, tri.p2 };
    const float angles[3] = { tri.angle01, tri.angle12, tri.angle20 };
    float dists[3] = { 3.4e38f, 3.4e38f, 3.4e38f };
    int best = -1; float best_dist = 3.4e38f;
    for (int e = 0; e < 3; ++e) {
        if (!(fabsf(angles[e]) < MAX_ANGLE)) continue;
        Vec3 a = pts[e], b = pts[(e + 1) % 3], d = b - a;
        float t = (local_b - a).dot(d) / d.dot(d);
        t = fminf(fmaxf(t, T_MIN), T_MAX);
        dists[e] = (local_b - (a + d * t)).length();
        bool take = dists[e] < best_dist;
        if (e == 1) take = take || (dists[e] == best_dist && angles[e] != 0.0f && !(tri.flags & 2u));
        if (e == 2) take = take || (dists[e] == best_dist && angles[e] != 0.0f);
        if (take) { best = e; if (e != 2) best_dist = dists[e]; }
    }
    Vec3 out = normal;
    const Vec3 first_normal = normal;
    bool processed = false, concave = false;
    for (int e = 0; e < 3; ++e) {
        if (!(fabsf(angles[e]) < MAX_ANGLE) || !(dists[e] < EDGE_DIST)) continue;
        if (!(best == e || (dists[e] < 1.1920929e-7f && angles[e] != 0.0f))) continue;
        processed = true;
        if (angles[e] == 0.0f) { concave = true; continue; }
        bool convex = (tri.flags & (1u << e)) != 0;
        float swap = convex ? 1.0f : -1.0f;
        Vec3 edge = (pts[e] - pts[(e + 1) % 3]);      // -edge(e)
        Vec3 axis = edge.normalized();
        Vec3 na = tri.n * swap;
        Vec3 nb = mesh_rotate(tri.n, axis, angles[e]);
        if (tri.flags & (8u << e)) nb = nb * -1.0f;
        nb = nb * swap;
        Vec3 cn = (e == 0) ? first_normal : out;
        if (first_normal.dot(na) < 0.0f && first_normal.dot(nb) < 0.0f) { concave = true; continue; }
        Vec3 edge_cross = edge.cross(na).normalized();
        float cur = atan2f(cn.dot(edge_cross), cn.dot(na));
        float want = angles[e];
        if ((want < 0.0f && cur < want) || (want >= 0.0f && cur > want)) {
            Vec3 clamped = mesh_rotate(cn, axis, want - cur);
            if (clamped.dot(tri.n) > 0.0f) out = clamped;
        }
    }
    if (processed && concave && tri.n.dot(first_normal) >= 0.0f) out = tri.n;
    return out;
}

/** Collects this tick's ball contacts. `center_bt` = ball centre in Bullet units. Returns the count. */
__device__ inline int mesh_ball_contacts(const ArenaMesh& arena, const Vec3& center_bt, float radius_bt, BallContact* out) {
    int count = 0;
    const float reach = radius_bt + BALL_MESH_THRESHOLD_BT;
    for (int m = 0; m < arena.num_meshes; ++m) {
        const MeshInfo& mi = arena.meshes[m];
        if (center_bt.x + reach < mi.mn.x || center_bt.x - reach > mi.mx.x || center_bt.y + reach < mi.mn.y || center_bt.y - reach > mi.mx.y ||
            center_bt.z + reach < mi.mn.z || center_bt.z - reach > mi.mx.z) continue;
        Vec3 c = center_bt - mi.translation;
        int have = 0; Vec3 cached[4]; int slot[4];
        for (int i = mi.node_begin; i < mi.node_end;) {
            const MeshNode& node = arena.nodes[i];
            bool overlap = !(c.x + reach < node.mn.x || c.x - reach > node.mx.x || c.y + reach < node.mn.y || c.y - reach > node.mx.y ||
                             c.z + reach < node.mn.z || c.z - reach > node.mx.z);
            if (!overlap) { i = node.skip; continue; }
            if (node.tri >= 0) {
                const MeshTriangle& tri = arena.tris[node.tri];
                Vec3 n, pt; float depth;
                if (mesh_sphere_triangle(tri, c, radius_bt, reach, n, pt, depth) && depth <= BALL_MESH_THRESHOLD_BT) {
                    Vec3 adj = mesh_adjust_internal_edge(tri, pt, n);
                    Vec3 on_ball = pt + n * depth - c;          // manifold identity is the point on the ball
                    int use = -1;
                    for (int k = 0; k < have; ++k) if ((cached[k] - on_ball).length_sq() < BALL_MESH_THRESHOLD_BT * BALL_MESH_THRESHOLD_BT) use = k;
                    if (use < 0 && have < 4 && count < BALL_MAX_CONTACTS) { use = have++; slot[use] = count++; }
                    else if (use < 0 && have > 0) { // manifold full: replace the shallowest point
                        use = 0; for (int k = 1; k < have; ++k) if (out[slot[k]].dist > out[slot[use]].dist) use = k;
                    }
                    if (use >= 0) { cached[use] = on_ball; out[slot[use]].n = adj; out[slot[use]].dist = depth; }
                }
            }
            ++i;
        }
    }
    // static planes: floor, ceiling, side walls
    const float planes[4] = { center_bt.z, 2048.0f * 0.02f - center_bt.z, center_bt.x + 4096.0f * 0.02f, 4096.0f * 0.02f - center_bt.x };
    const Vec3 normals[4] = { Vec3(0, 0, 1), Vec3(0, 0, -1), Vec3(1, 0, 0), Vec3(-1, 0, 0) };
    for (int p = 0; p < 4; ++p) {
        float d = planes[p] - radius_bt;
        if (d < BALL_MESH_THRESHOLD_BT && count < BALL_MAX_CONTACTS) { out[count].n = normals[p]; out[count].dist = d; ++count; }
    }
    return count;
}

/** The special ball-world solve. pos in UU, velocities in UU; `vel_pre` = velocity before this tick's gravity. */
__device__ inline void mesh_ball_resolve(const ArenaMesh& arena, Vec3& pos, Vec3& vel, Vec3& ang_vel, const Vec3& vel_pre,
                                         float radius, float restitution, float friction, float dt) {
    BallContact contacts[BALL_MAX_CONTACTS];
    int count = mesh_ball_contacts(arena, pos * 0.02f, radius * 0.02f, contacts);
    if (count == 0) return;
    Vec3 sum(0.0f, 0.0f, 0.0f);
    for (int k = 0; k < count; ++k) sum = sum + contacts[k].n;

    // split impulse: push the position out of every overlapping contact (ERP 0.8), sequentially
    Vec3 push(0.0f, 0.0f, 0.0f);       // pseudo velocity * dt, Bullet units
    float acc[BALL_MAX_CONTACTS];
    for (int k = 0; k < count; ++k) acc[k] = 0.0f;
    for (int it = 0; it < 10; ++it) {
        for (int k = 0; k < count; ++k) {
            if (contacts[k].dist >= 0.0f) continue;
            float want = -0.8f * contacts[k].dist;
            float next = fmaxf(0.0f, acc[k] + (want - push.dot(contacts[k].n)));
            push = push + contacts[k].n * (next - acc[k]);
            acc[k] = next;
        }
    }
    pos = pos + push * 50.0f;

    float len = sum.length();
    if (len < 1e-6f) return;
    Vec3 n = sum * (1.0f / len);
    // rolling-contact solve on the averaged normal (lever = ball radius)
    Vec3 pre_pt = vel_pre - ang_vel.cross(n) * radius;
    float delta_vn = fmaxf(0.0f, -restitution * n.dot(pre_pt)) - n.dot(vel);
    if (delta_vn <= 0.0f) return;
    Vec3 contact_v = vel - ang_vel.cross(n) * radius;
    Vec3 slip = contact_v - n * n.dot(contact_v);
    float speed = slip.length();
    Vec3 tangent(0.0f, 0.0f, 0.0f);
    if (speed > 1e-4f) {
        float j = fminf((2.0f / 7.0f) * speed, friction * delta_vn);
        tangent = slip * (-j / speed);
        ang_vel = ang_vel + tangent.cross(n) * (2.5f / radius);
    }
    vel = vel + n * delta_vn + tangent;
}

} // namespace rocketsim_cuda
