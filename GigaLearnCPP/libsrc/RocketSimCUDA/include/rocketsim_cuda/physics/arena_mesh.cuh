// P2-1: RocketSim v3's arena on the GPU: the 16 soccar collision meshes (.cmf, Bullet units) plus the static planes
// (floor, ceiling, side walls). Triangles are stored in the order v3's BVH visits them, with a stackless preorder tree per
// mesh, so queries report triangles in the same order as the CPU engine. Edge data reproduces Bullet's internal-edge info.
#pragma once
#include <cstdint>
#include <cuda_runtime.h>
#include "rocketsim_cuda/math/vec3.cuh"

namespace rocketsim_cuda {

struct MeshTriangle {
    Vec3 p0, p1, p2;   // mesh-local, Bullet units
    Vec3 n;            // unit face normal
    float angle01, angle12, angle20;
    uint32_t flags;    // bits: 0-2 edge convex (01, 12, 20), 3-5 swap neighbour normal
};
struct MeshNode {
    Vec3 mn, mx;       // mesh-local, Bullet units
    int32_t tri;       // >= 0: leaf holding this triangle; -1: branch
    int32_t skip;      // index of the next node when this subtree is not entered
};
struct MeshInfo {
    int32_t node_begin, node_end;
    Vec3 translation;  // body translation (the goal components sit at y = +-102.4)
    Vec3 mn, mx;       // world bounds, Bullet units
};
constexpr int ARENA_MAX_MESHES = 16;
// coarse occupancy grid: a cell holds 1 if any triangle's bounds overlap it, so queries far from every surface skip the trees
constexpr float ARENA_CELL_BT = 2.56f;                          // 128 UU
constexpr int ARENA_GRID_X = 68, ARENA_GRID_Y = 96, ARENA_GRID_Z = 20;
constexpr float ARENA_GRID_MIN_X = -87.04f, ARENA_GRID_MIN_Y = -122.88f, ARENA_GRID_MIN_Z = -5.12f;

struct ArenaMesh {
    const MeshTriangle* tris = nullptr;
    const MeshNode* nodes = nullptr;
    const MeshInfo* meshes = nullptr;
    const uint8_t* grid = nullptr;
    int32_t num_meshes = 0;   // 0 = not loaded: the analytic SDF is used instead
};

// Host: loads <dir>/soccar/*.cmf once (device memory, kept for the process lifetime). Returns an empty ArenaMesh when the
// directory is missing. GetArenaMesh() loads on first use from $RSC_COLLISION_MESHES, else ./collision_meshes and parents.
/** False only if no mesh triangle can overlap the box [lo, hi] (world, Bullet units). */
__device__ __forceinline__ bool arena_mesh_near(const ArenaMesh& arena, const Vec3& lo, const Vec3& hi) {
    auto idx = [](float v, float mn, int n) { int i = static_cast<int>(floorf((v - mn) * (1.0f / ARENA_CELL_BT))); return i < 0 ? 0 : (i >= n ? n - 1 : i); };
    const int x0 = idx(lo.x, ARENA_GRID_MIN_X, ARENA_GRID_X), x1 = idx(hi.x, ARENA_GRID_MIN_X, ARENA_GRID_X);
    const int y0 = idx(lo.y, ARENA_GRID_MIN_Y, ARENA_GRID_Y), y1 = idx(hi.y, ARENA_GRID_MIN_Y, ARENA_GRID_Y);
    const int z0 = idx(lo.z, ARENA_GRID_MIN_Z, ARENA_GRID_Z), z1 = idx(hi.z, ARENA_GRID_MIN_Z, ARENA_GRID_Z);
    for (int z = z0; z <= z1; ++z) for (int y = y0; y <= y1; ++y) for (int x = x0; x <= x1; ++x)
        if (arena.grid[(z * ARENA_GRID_Y + y) * ARENA_GRID_X + x]) return true;
    return false;
}

ArenaMesh LoadArenaMesh(const char* collision_meshes_dir);
const ArenaMesh& GetArenaMesh();

} // namespace rocketsim_cuda
