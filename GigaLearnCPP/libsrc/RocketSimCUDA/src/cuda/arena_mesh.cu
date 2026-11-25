// P2-1: host-side loader for the v3 arena meshes (see arena_mesh.cuh). Ports, from RocketSim v3 (0.3.5):
// sim/collision_mesh_file.rs, shared/bvh.rs (calc_bullet_split order), shapes/optimized_bvh.rs (leaf bounds),
// dispatch/internal_edge_utility.rs (generate_internal_edge_info).
#include "rocketsim_cuda/physics/arena_mesh.cuh"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

namespace rocketsim_cuda {
namespace {

struct HV { float x, y, z; };
inline HV operator+(HV a, HV b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
inline HV operator-(HV a, HV b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
inline HV operator*(HV a, float s) { return { a.x * s, a.y * s, a.z * s }; }
inline float dot(HV a, HV b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline HV cross(HV a, HV b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
inline HV norm(HV a) { float l = std::sqrt(dot(a, a)); return a * (1.0f / l); }
inline HV hmin(HV a, HV b) { return { std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z) }; }
inline HV hmax(HV a, HV b) { return { std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z) }; }
inline float comp(HV a, int i) { return i == 0 ? a.x : (i == 1 ? a.y : a.z); }

struct Tri { HV p[3]; HV n; float angle[3]; uint32_t flags; };
struct Leaf { HV mn, mx; int tri; };

inline HV rotate_axis(HV v, HV axis, float ang) { // Rodrigues, axis normalised
    float c = std::cos(ang), s = std::sin(ang);
    return v * c + cross(axis, v) * s + axis * (dot(axis, v) * (1.0f - c));
}

// order leaves as Bullet's builder partitions them; also emit the preorder node list
void build(std::vector<Leaf>& leaves, int start, int end, std::vector<MeshNode>& nodes, std::vector<int>& order) {
    int count = end - start;
    int me = (int)nodes.size();
    nodes.push_back(MeshNode{});
    HV mn = leaves[start].mn, mx = leaves[start].mx;
    for (int i = start + 1; i < end; i++) { mn = hmin(mn, leaves[i].mn); mx = hmax(mx, leaves[i].mx); }
    nodes[me].mn = Vec3(mn.x, mn.y, mn.z); nodes[me].mx = Vec3(mx.x, mx.y, mx.z);
    if (count == 1) {
        nodes[me].tri = (int)order.size(); order.push_back(leaves[start].tri);
        nodes[me].skip = (int)nodes.size();
        return;
    }
    auto center = [](const Leaf& l) { return (l.mn + l.mx) * 0.5f; };
    HV mean{ 0, 0, 0 };
    for (int i = start; i < end; i++) mean = mean + center(leaves[i]);
    mean = mean * (1.0f / (float)count);
    HV var{ 0, 0, 0 };
    for (int i = start; i < end; i++) { HV d = center(leaves[i]) - mean; var = var + HV{ d.x * d.x, d.y * d.y, d.z * d.z }; }
    var = var * (1.0f / ((float)count - 1.0f));
    int axis = (var.x < var.y) ? ((var.y < var.z) ? 2 : 1) : ((var.x < var.z) ? 2 : 0);
    float split_value = comp(mean, axis);
    int split = start;
    for (int i = start; i < end; i++) if (comp(center(leaves[i]), axis) > split_value) { if (i != split) std::swap(leaves[i], leaves[split]); split++; }
    int range = count / 3;
    if (split <= start + range || split >= end - 1 - range) split = start + (count >> 1);
    nodes[me].tri = -1;
    build(leaves, start, split, nodes, order);
    build(leaves, split, end, nodes, order);
    nodes[me].skip = (int)nodes.size();
}

void edge_info(std::vector<Tri>& tris) {
    constexpr float TAU_F = 6.28318530717958647692f, PI_F = 3.14159265358979323846f, EQUAL_SQ = 0.0001f * 0.0001f, PLANAR = 0.0001f;
    for (auto& t : tris) { t.angle[0] = t.angle[1] = t.angle[2] = TAU_F; t.flags = 0; }
    for (size_t ia = 0; ia < tris.size(); ia++) {
        Tri& a = tris[ia];
        for (size_t ib = 0; ib < tris.size(); ib++) {
            if (ia == ib) continue;
            const Tri& b = tris[ib];
            int n = 0, sa[3] = { 0, 0, 0 }, sb[3] = { 0, 0, 0 };
            for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) { HV d = a.p[i] - b.p[j]; if (dot(d, d) < EQUAL_SQ && n < 2) { sa[n] = i; sb[n] = j; n++; } }
            if (n != 2) continue;
            if (sa[0] == 0 && sa[1] == 2) { sa[0] = 2; sa[1] = 0; std::swap(sb[0], sb[1]); }
            int sum_a = sa[0] + sa[1], other_a = 3 - sum_a, other_b = 3 - (sb[0] + sb[1]);
            HV edge = norm(a.p[sa[1]] - a.p[sa[0]]);
            HV q0 = b.p[sb[1]], q1 = b.p[sb[0]], q2 = b.p[other_b];
            HV normal_b = norm(cross(q1 - q0, (q0 - q2) * -1.0f));
            HV ca = norm(cross(edge, a.n)); if (dot(ca, a.p[other_a] - a.p[sa[0]]) < 0) ca = ca * -1.0f;
            HV cb = norm(cross(edge, normal_b)); if (dot(cb, b.p[other_b] - b.p[sb[0]]) < 0) cb = cb * -1.0f;
            HV ce = cross(ca, cb);
            float corrected = 0; bool convex = false;
            if (dot(ce, ce) >= PLANAR) {
                ce = norm(ce);
                HV na = norm(cross(ce, ca));
                float angle2 = std::atan2(dot(cb, na), dot(cb, ca)); // get_angle(na, ca, cb)
                float ang4 = PI_F - angle2;
                convex = dot(a.n, cb) < 0.0f;
                corrected = convex ? ang4 : -ang4;
            }
            int slot = (sum_a == 1) ? 0 : (sum_a == 3 ? 1 : 2);                     // edge 01, 12, 20
            int e = (slot == 0) ? 0 : (slot == 1 ? 1 : 2);
            HV ev = (e == 0) ? a.p[1] - a.p[0] : (e == 1 ? a.p[2] - a.p[1] : a.p[0] - a.p[2]);
            HV axis = norm(ev * -1.0f);
            HV computed = rotate_axis(a.n, axis, -corrected);
            if (dot(computed, normal_b) < 0.0f) a.flags |= (8u << slot);
            a.angle[slot] = -corrected;
            if (convex) a.flags |= (1u << slot);
        }
    }
    for (auto& t : tris) for (float& g : t.angle) if (g == TAU_F) g = 0.0f;
}

template <typename T> const T* upload(const std::vector<T>& v) {
    T* d = nullptr;
    if (cudaMalloc(&d, v.size() * sizeof(T)) != cudaSuccess) return nullptr;
    cudaMemcpy(d, v.data(), v.size() * sizeof(T), cudaMemcpyHostToDevice);
    return d;
}

} // namespace

ArenaMesh LoadArenaMesh(const char* dir) {
    ArenaMesh out;
    namespace fs = std::filesystem;
    fs::path soccar = fs::path(dir) / "soccar";
    std::error_code ec;
    if (!fs::is_directory(soccar, ec)) return out;
    std::vector<fs::path> files;
    for (auto& f : fs::directory_iterator(soccar)) if (f.path().extension() == ".cmf") files.push_back(f.path());
    std::sort(files.begin(), files.end(), [](const fs::path& a, const fs::path& b) {
        auto num = [](const fs::path& p) { std::string s = p.stem().string(); size_t u = s.find_last_of('_'); return std::atoi(s.c_str() + (u == std::string::npos ? 0 : u + 1)); };
        return num(a) < num(b);
    });
    if (files.empty() || (int)files.size() > ARENA_MAX_MESHES) return out;

    std::vector<MeshTriangle> all_tris; std::vector<MeshNode> all_nodes; std::vector<MeshInfo> infos;
    for (const fs::path& path : files) {
        std::ifstream in(path, std::ios::binary);
        uint32_t nt = 0, nv = 0; in.read((char*)&nt, 4); in.read((char*)&nv, 4);
        std::vector<uint32_t> idx(nt * 3); std::vector<HV> verts(nv);
        in.read((char*)idx.data(), idx.size() * 4); in.read((char*)verts.data(), nv * sizeof(HV));
        if (!in || nt == 0) { std::fprintf(stderr, "arena mesh: bad file %s\n", path.c_str()); return ArenaMesh{}; }

        HV vmn = verts[0], vmx = verts[0];
        for (HV v : verts) { vmn = hmin(vmn, v); vmx = hmax(vmx, v); }
        float cy = (vmn.y + vmx.y) * 0.5f;
        HV tr{ 0, cy > 102.4f ? 102.4f : (cy < -102.4f ? -102.4f : 0.0f), 0 };
        if (tr.y != 0) for (HV& v : verts) v = v - tr;

        std::vector<Tri> tris(nt);
        std::vector<Leaf> leaves(nt);
        for (uint32_t i = 0; i < nt; i++) {
            Tri& t = tris[i];
            for (int k = 0; k < 3; k++) t.p[k] = verts[idx[i * 3 + k]];
            t.n = norm(cross(t.p[1] - t.p[0], (t.p[0] - t.p[2]) * -1.0f));
            HV mn = hmin(t.p[0], hmin(t.p[1], t.p[2])), mx = hmax(t.p[0], hmax(t.p[1], t.p[2]));
            // v3 pads flat boxes to 0.002; the extra margin below covers its 16-bit quantization of the node bounds
            constexpr float PAD = 0.02f;
            leaves[i] = Leaf{ mn - HV{ PAD, PAD, PAD }, mx + HV{ PAD, PAD, PAD }, (int)i };
        }
        edge_info(tris);

        std::vector<MeshNode> nodes; std::vector<int> order;
        build(leaves, 0, (int)nt, nodes, order);
        int tri_base = (int)all_tris.size(), node_base = (int)all_nodes.size();
        for (MeshNode& n : nodes) { if (n.tri >= 0) n.tri += tri_base; n.skip += node_base; }
        for (int src : order) {
            const Tri& t = tris[src];
            auto V = [](HV v) { return Vec3(v.x, v.y, v.z); };
            all_tris.push_back(MeshTriangle{ V(t.p[0]), V(t.p[1]), V(t.p[2]), V(t.n), t.angle[0], t.angle[1], t.angle[2], t.flags });
        }
        MeshInfo mi; mi.node_begin = node_base; all_nodes.insert(all_nodes.end(), nodes.begin(), nodes.end()); mi.node_end = (int)all_nodes.size();
        mi.translation = Vec3(tr.x, tr.y, tr.z);
        mi.mn = nodes[0].mn + mi.translation; mi.mx = nodes[0].mx + mi.translation;
        infos.push_back(mi);
    }
    out.tris = upload(all_tris); out.nodes = upload(all_nodes); out.meshes = upload(infos);
    if (!out.tris || !out.nodes || !out.meshes) return ArenaMesh{};
    out.num_meshes = (int32_t)infos.size();
    return out;
}

const ArenaMesh& GetArenaMesh() {
    static std::once_flag once;
    static ArenaMesh mesh;
    std::call_once(once, [] {
        std::vector<std::string> tries;
        if (const char* env = std::getenv("RSC_COLLISION_MESHES")) tries.push_back(env);
        for (const char* p : { "collision_meshes", "../collision_meshes", "build/collision_meshes", "../build/collision_meshes", "../../build/collision_meshes" }) tries.push_back(p);
        for (auto& d : tries) { mesh = LoadArenaMesh(d.c_str()); if (mesh.num_meshes) break; }
        if (!mesh.num_meshes) std::fprintf(stderr, "arena mesh: collision_meshes/soccar not found, using the analytic arena\n");
    });
    return mesh;
}

} // namespace rocketsim_cuda
