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

struct ArenaMesh {
    const MeshTriangle* tris = nullptr;
    const MeshNode* nodes = nullptr;
    const MeshInfo* meshes = nullptr;
    int32_t num_meshes = 0;   // 0 = not loaded: the analytic SDF is used instead
};

// Host: loads <dir>/soccar/*.cmf once (device memory, kept for the process lifetime). Returns an empty ArenaMesh when the
// directory is missing. GetArenaMesh() loads on first use from $RSC_COLLISION_MESHES, else ./collision_meshes and parents.
ArenaMesh LoadArenaMesh(const char* collision_meshes_dir);
const ArenaMesh& GetArenaMesh();

} // namespace rocketsim_cuda
