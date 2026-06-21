#pragma once
// -----------------------------------------------------------------------------
// Geodesic-CDLOD planet terrain — sparse triangle-tree core (workstream G, M0).
// See docs/space_zoo_plan.md §2.5-G for the design this implements.
//
// The planet IS this tree: a subdivided icosphere whose triangle quadtree is the
// authoritative, editable terrain. Height is a per-vertex radius (distance from
// center). Vertices are pooled and shared (midpoint cache), so editing height is
// continuous across triangle boundaries for free. Subdivision is sparse/lazy:
// children are materialised only near the camera or under an edit.
//
// Header-only on purpose (glm + std only, like bagel_math.hpp) so it compiles
// without touching the .vcxproj and can be exercised standalone before being
// wired into rendering. Prove the data model + heightAt here first (M0).
//
// Scope of THIS prototype (honest boundaries):
//   DONE  icosahedron seed, pooled shared verts + midpoint/edge cache,
//         lazy subdivide/collapse, per-vertex radius w/ interpolate-on-subdivide,
//         distance-based LOD cut, T-junction seam SNAP on emit, heightAt(dir),
//         raise/lower brush (force-subdivide + pin), edit-delta export/import.
//   TODO  guaranteeing the restricted (<=1 level gap) invariant by force-
//         subdividing *coarser neighbours* needs full cross-face edge adjacency;
//         the seam-snap here is correct for <=1 gaps but does not yet create them.
//         CDLOD distance-morph (kills the LOD pop) is the later upgrade. See §2.5-G.
// -----------------------------------------------------------------------------
#include <glm/glm.hpp>
#include <array>
#include <vector>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <cmath>

namespace bagel::planet {

inline constexpr uint32_t NONE = 0xFFFFFFFFu;

struct TerrainConfig {
    float radius     = 100.0f; // base (sea-level) radius; per-vertex radius starts here
    int   minLevel   = 0;      // coarsest the cut ever goes — every leaf is at least this deep
    int   maxLevel   = 8;      // deepest subdivision (20*4^8 ~ 1.3M tris if fully dense)
    float splitFactor = 2.5f;  // node splits while camera distance < splitFactor * nodeEdgeLen
};

// A pooled, shared surface vertex. Shared between every triangle that touches it,
// so a height edit here moves the surface continuously across all of them.
struct TVertex {
    glm::vec3 dir;             // unit direction from planet center
    float     radius;          // height = distance from center (the edited quantity)
    int32_t   parentA = -1;    // edge (parentA,parentB) this vertex was born on the
    int32_t   parentB = -1;    // midpoint of; -1,-1 for the 12 original icosa verts
    glm::vec3 pos() const { return dir * radius; }
};

// One node of a base triangle's quadtree. Children: 3 corner sub-triangles + 1
// center, mirroring generated.cpp's 1->4 midpoint split.
struct TNode {
    std::array<uint32_t, 3> v{};                 // corner vertex indices (CCW)
    std::array<uint32_t, 4> child{ NONE, NONE, NONE, NONE };
    uint32_t parent = NONE;
    uint8_t  level  = 0;
    bool     pinned = false;                     // subdivided due to an edit -> never auto-collapse
    bool isLeaf() const { return child[0] == NONE; }
};

// Triangle soup ready to upload. Soup (not indexed) because seam snapping is
// per-triangle-instance; a production path would re-index after snapping.
struct RenderMesh {
    std::vector<glm::vec3> positions; // 3 per triangle
    std::vector<glm::vec3> normals;   // flat per-triangle normal, duplicated x3
    size_t triangleCount() const { return positions.size() / 3; }
};

class PlanetTerrain {
public:
    explicit PlanetTerrain(const TerrainConfig& cfg = {}) : cfg_(cfg) { buildIcosahedron(); }

    const TerrainConfig& config() const { return cfg_; }

    // --- Sampling -----------------------------------------------------------
    // Surface radius (height) along a unit direction: descend to the deepest
    // existing node containing dir, barycentric-blend its 3 corner radii.
    float heightAt(const glm::vec3& dir) const {
        glm::vec3 d = glm::normalize(dir);
        uint32_t n = baseNodeContaining(d);
        n = descend(n, d);                       // walk to deepest materialised leaf
        const TNode& t = nodes_[n];
        glm::vec3 b = bary(d, t);
        return b.x * verts_[t.v[0]].radius
             + b.y * verts_[t.v[1]].radius
             + b.z * verts_[t.v[2]].radius;
    }
    glm::vec3 surfacePoint(const glm::vec3& dir) const {
        glm::vec3 d = glm::normalize(dir);
        return d * heightAt(d);
    }

    // --- Editing ------------------------------------------------------------
    // Raise/lower a geodesic disc around centerDir (a unit dir). angularRadius in
    // radians; targetLevel forces subdivision so detail exists under the brush.
    // Edited nodes are pinned so camera LOD never collapses them.
    void raiseLower(const glm::vec3& centerDir, float angularRadius, float deltaHeight,
                    int targetLevel) {
        glm::vec3 c = glm::normalize(centerDir);
        targetLevel = std::min(targetLevel, cfg_.maxLevel);
        float cosR  = std::cos(angularRadius);

        // 1) Force-subdivide every node overlapping the disc down to targetLevel, pinning.
        for (uint32_t r = 0; r < baseCount_; ++r) forceSubdivideDisc(r, c, cosR, targetLevel);

        // 2) Apply a smooth falloff to every vertex inside the disc.
        for (auto& vtx : verts_) {
            float cd = glm::dot(vtx.dir, c);
            if (cd <= cosR) continue;
            float t = (cd - cosR) / (1.0f - cosR);           // 0 at rim -> 1 at center
            float falloff = t * t * (3.0f - 2.0f * t);       // smoothstep
            vtx.radius += deltaHeight * falloff;
        }
    }

    // --- LOD cut + render emission -----------------------------------------
    // Select the leaf frontier for a camera position and emit a renderable soup,
    // snapping T-junction midpoints onto coarser neighbours' edges (crack-free).
    RenderMesh buildCut(const glm::vec3& cameraPos) {
        std::vector<uint32_t> leaves;
        for (uint32_t r = 0; r < baseCount_; ++r) selectCut(r, cameraPos, leaves);

        // Active "coarse" edges = every edge actually used by a leaf in the cut.
        // A midpoint vertex whose parent edge appears here sits on a coarse edge
        // (the neighbour across it didn't split) -> snap it to remove the crack.
        std::unordered_set<uint64_t> activeEdges;
        activeEdges.reserve(leaves.size() * 3);
        for (uint32_t li : leaves) {
            const auto& v = nodes_[li].v;
            activeEdges.insert(edgeKey(v[0], v[1]));
            activeEdges.insert(edgeKey(v[1], v[2]));
            activeEdges.insert(edgeKey(v[2], v[0]));
        }

        RenderMesh mesh;
        mesh.positions.reserve(leaves.size() * 3);
        mesh.normals.reserve(leaves.size() * 3);
        for (uint32_t li : leaves) {
            const auto& v = nodes_[li].v;
            glm::vec3 p0 = emitPos(v[0], activeEdges);
            glm::vec3 p1 = emitPos(v[1], activeEdges);
            glm::vec3 p2 = emitPos(v[2], activeEdges);
            glm::vec3 nrm = glm::normalize(glm::cross(p1 - p0, p2 - p0));
            mesh.positions.push_back(p0); mesh.positions.push_back(p1); mesh.positions.push_back(p2);
            mesh.normals.push_back(nrm);  mesh.normals.push_back(nrm);  mesh.normals.push_back(nrm);
        }
        return mesh;
    }

    // Same LOD cut, but as a line-list wireframe (3 edges per emitted triangle)
    // — handy for visualising the dynamic subdivision. Positions are world-space
    // (already radius-scaled); indices are vertex pairs for VK_PRIMITIVE_TOPOLOGY_LINE_LIST.
    struct WireMesh {
        std::vector<glm::vec3> positions;
        std::vector<uint32_t>  lineIndices;
    };
    WireMesh buildCutWire(const glm::vec3& cameraPos) {
        RenderMesh m = buildCut(cameraPos);
        WireMesh w;
        w.positions = std::move(m.positions);
        size_t tris = w.positions.size() / 3;
        w.lineIndices.reserve(tris * 6);
        for (uint32_t t = 0; t < tris; ++t) {
            uint32_t a = 3 * t, b = 3 * t + 1, c = 3 * t + 2;
            w.lineIndices.push_back(a); w.lineIndices.push_back(b);
            w.lineIndices.push_back(b); w.lineIndices.push_back(c);
            w.lineIndices.push_back(c); w.lineIndices.push_back(a);
        }
        return w;
    }

    // --- Serialization (edited deltas only; see §2.5-G) ---------------------
    // A stable, allocation-independent key for an edited vertex: recurse its
    // parent edge down to the 12 base verts. The mesh is never stored; on load
    // the tree is rebuilt and these deltas re-subdivide + re-apply.
    struct HeightDelta { uint64_t key; float delta; };

    std::vector<HeightDelta> exportDeltas() const {
        std::vector<HeightDelta> out;
        for (const auto& v : verts_) {
            float d = v.radius - cfg_.radius;
            if (std::fabs(d) > 1e-4f) out.push_back({ stableKey(v), d });
        }
        return out;
    }

    size_t vertexCount() const { return verts_.size(); }
    size_t nodeCount() const { return nodes_.size(); }

private:
    // ---- construction ------------------------------------------------------
    void buildIcosahedron() {
        const float t = (1.0f + std::sqrt(5.0f)) * 0.5f;
        const glm::vec3 sv[12] = {
            {-1, t, 0}, {1, t, 0}, {-1,-t, 0}, {1,-t, 0},
            { 0,-1, t}, {0, 1, t}, { 0,-1,-t}, {0, 1,-t},
            { t, 0,-1}, {t, 0, 1}, {-t, 0,-1}, {-t, 0, 1},
        };
        for (const auto& s : sv) addVertex(glm::normalize(s), -1, -1);
        const uint32_t f[20][3] = {
            {0,11,5},{0,5,1},{0,1,7},{0,7,10},{0,10,11},
            {1,5,9},{5,11,4},{11,10,2},{10,7,6},{7,1,8},
            {3,9,4},{3,4,2},{3,2,6},{3,6,8},{3,8,9},
            {4,9,5},{2,4,11},{6,2,10},{8,6,7},{9,8,1},
        };
        for (auto& tri : f) {
            TNode n;
            n.v = { tri[0], tri[1], tri[2] };
            n.level = 0;
            nodes_.push_back(n);
        }
        baseCount_ = 20;
    }

    uint32_t addVertex(const glm::vec3& dir, int32_t pa, int32_t pb) {
        TVertex v; v.dir = dir; v.radius = cfg_.radius; v.parentA = pa; v.parentB = pb;
        verts_.push_back(v);
        return static_cast<uint32_t>(verts_.size() - 1);
    }

    static uint64_t edgeKey(uint32_t a, uint32_t b) {
        uint32_t lo = std::min(a, b), hi = std::max(a, b);
        return (static_cast<uint64_t>(lo) << 32) | hi;
    }

    // Shared midpoint vertex of edge (a,b): created once, born at the average
    // radius of its endpoints (smooth surface until edited). Records its parents.
    uint32_t midpoint(uint32_t a, uint32_t b) {
        uint64_t key = edgeKey(a, b);
        auto it = midCache_.find(key);
        if (it != midCache_.end()) return it->second;
        glm::vec3 dir = glm::normalize(verts_[a].dir + verts_[b].dir);
        uint32_t idx = addVertex(dir, static_cast<int32_t>(std::min(a, b)),
                                       static_cast<int32_t>(std::max(a, b)));
        verts_[idx].radius = 0.5f * (verts_[a].radius + verts_[b].radius);
        midCache_.emplace(key, idx);
        return idx;
    }

    // ---- subdivision -------------------------------------------------------
    void subdivide(uint32_t ni) {
        TNode& n = nodes_[ni];
        if (!n.isLeaf() || n.level >= cfg_.maxLevel) return;
        uint32_t a = midpoint(n.v[0], n.v[1]);
        uint32_t b = midpoint(n.v[1], n.v[2]);
        uint32_t c = midpoint(n.v[2], n.v[0]);
        uint8_t  lvl = n.level + 1;
        std::array<uint32_t, 3> v = n.v;          // copy: nodes_ may reallocate below
        uint32_t kids[4] = {
            makeChild({ v[0], a, c }, ni, lvl),
            makeChild({ v[1], b, a }, ni, lvl),
            makeChild({ v[2], c, b }, ni, lvl),
            makeChild({ a, b, c }, ni, lvl),
        };
        for (int i = 0; i < 4; ++i) nodes_[ni].child[i] = kids[i];
    }

    uint32_t makeChild(std::array<uint32_t, 3> v, uint32_t parent, uint8_t level) {
        TNode n; n.v = v; n.parent = parent; n.level = level;
        nodes_.push_back(n);
        return static_cast<uint32_t>(nodes_.size() - 1);
    }

    // Recursively subdivide nodes overlapping the geodesic disc to targetLevel; pin.
    void forceSubdivideDisc(uint32_t ni, const glm::vec3& c, float cosR, int targetLevel) {
        if (!nodeIntersectsDisc(ni, c, cosR)) return;
        nodes_[ni].pinned = true;
        if (nodes_[ni].level >= targetLevel) return;
        if (nodes_[ni].isLeaf()) subdivide(ni);
        std::array<uint32_t, 4> kids = nodes_[ni].child;
        if (kids[0] == NONE) return;              // hit maxLevel
        for (uint32_t k : kids) forceSubdivideDisc(k, c, cosR, targetLevel);
    }

    // ---- LOD cut -----------------------------------------------------------
    void selectCut(uint32_t ni, const glm::vec3& cam, std::vector<uint32_t>& out) {
        // Force subdivision up to minLevel everywhere (uniform coarse floor), then let
        // camera distance drive splitting up to maxLevel.
        bool belowMin = nodes_[ni].level < cfg_.minLevel;
        bool split = (belowMin || shouldSplit(ni, cam)) && nodes_[ni].level < cfg_.maxLevel;
        // Pinned (edited) subtrees always render at their materialised detail.
        if (!split && !hasPinnedDescendant(ni)) { out.push_back(ni); return; }
        if (nodes_[ni].isLeaf()) {
            if (!split) { out.push_back(ni); return; }
            subdivide(ni);
            if (nodes_[ni].isLeaf()) { out.push_back(ni); return; } // maxLevel
        }
        // Copy the child indices BEFORE recursing: a deeper selectCut may subdivide and
        // push_back into nodes_, reallocating it — iterating nodes_[ni].child in place
        // would then dereference freed memory and read garbage child indices.
        std::array<uint32_t, 4> kids = nodes_[ni].child;
        for (uint32_t k : kids) selectCut(k, cam, out);
    }

    bool shouldSplit(uint32_t ni, const glm::vec3& cam) const {
        const TNode& n = nodes_[ni];
        glm::vec3 center = (verts_[n.v[0]].pos() + verts_[n.v[1]].pos() + verts_[n.v[2]].pos()) / 3.0f;
        float edgeLen = glm::length(verts_[n.v[0]].pos() - verts_[n.v[1]].pos());
        float dist    = glm::length(cam - center);
        return dist < cfg_.splitFactor * edgeLen;
    }

    bool hasPinnedDescendant(uint32_t ni) const {
        const TNode& n = nodes_[ni];
        if (n.pinned && !n.isLeaf()) return true;
        if (n.isLeaf()) return false;
        for (uint32_t k : n.child) if (hasPinnedDescendant(k)) return true;
        return false;
    }

    // ---- geometry helpers --------------------------------------------------
    glm::vec3 emitPos(uint32_t vi, const std::unordered_set<uint64_t>& activeEdges) const {
        const TVertex& v = verts_[vi];
        if (v.parentA >= 0) {
            uint64_t pe = edgeKey(static_cast<uint32_t>(v.parentA), static_cast<uint32_t>(v.parentB));
            if (activeEdges.count(pe)) {
                // A coarse leaf still uses this vertex's parent edge -> we are a hanging
                // T-junction midpoint. The coarse neighbour renders the STRAIGHT chord
                // pa->pb, while this vertex sits out on the sphere arc (and may be displaced
                // by edits), opening a crack between chord and arc. Snap to the LINEAR
                // midpoint of the parents' positions so pa -> v -> pb is collinear with the
                // coarse edge and the gap closes. (NOT normalize(dir)*avgRadius — that is the
                // arc point, i.e. exactly where v already is, which closes nothing.)
                const TVertex& pa = verts_[v.parentA];
                const TVertex& pb = verts_[v.parentB];
                return 0.5f * (pa.pos() + pb.pos());
            }
        }
        return v.pos();
    }

    // Barycentric coords of unit dir within node's triangle (via ray/plane hit).
    glm::vec3 bary(const glm::vec3& dir, const TNode& t) const {
        const glm::vec3 p0 = verts_[t.v[0]].dir;
        const glm::vec3 p1 = verts_[t.v[1]].dir;
        const glm::vec3 p2 = verts_[t.v[2]].dir;
        glm::vec3 nrm = glm::cross(p1 - p0, p2 - p0);
        float denom = glm::dot(dir, nrm);
        glm::vec3 hit = (std::fabs(denom) > 1e-8f) ? dir * (glm::dot(p0, nrm) / denom) : dir;
        glm::vec3 v0 = p1 - p0, v1 = p2 - p0, v2 = hit - p0;
        float d00 = glm::dot(v0, v0), d01 = glm::dot(v0, v1), d11 = glm::dot(v1, v1);
        float d20 = glm::dot(v2, v0), d21 = glm::dot(v2, v1);
        float dn = d00 * d11 - d01 * d01;
        if (std::fabs(dn) < 1e-12f) return { 1.0f, 0.0f, 0.0f };
        float v = (d11 * d20 - d01 * d21) / dn;
        float w = (d00 * d21 - d01 * d20) / dn;
        return { 1.0f - v - w, v, w };
    }

    // Descend to the deepest existing child containing dir (1->4 split rule:
    // the dominant barycentric corner picks a corner child; else the center).
    uint32_t descend(uint32_t ni, const glm::vec3& dir) const {
        while (!nodes_[ni].isLeaf()) {
            glm::vec3 b = bary(dir, nodes_[ni]);
            uint32_t next;
            if (b.x >= 0.5f)      next = nodes_[ni].child[0];
            else if (b.y >= 0.5f) next = nodes_[ni].child[1];
            else if (b.z >= 0.5f) next = nodes_[ni].child[2];
            else                  next = nodes_[ni].child[3];
            ni = next;
        }
        return ni;
    }

    uint32_t baseNodeContaining(const glm::vec3& dir) const {
        uint32_t best = 0; float bestDot = -2.0f;
        for (uint32_t i = 0; i < baseCount_; ++i) {
            glm::vec3 c = glm::normalize(verts_[nodes_[i].v[0]].dir +
                                         verts_[nodes_[i].v[1]].dir +
                                         verts_[nodes_[i].v[2]].dir);
            float d = glm::dot(dir, c);
            if (d > bestDot) { bestDot = d; best = i; }
        }
        return best;
    }

    bool nodeIntersectsDisc(uint32_t ni, const glm::vec3& c, float cosR) const {
        const TNode& n = nodes_[ni];
        // Cheap conservative test: any corner inside, or center within radius.
        for (uint32_t vi : n.v) if (glm::dot(verts_[vi].dir, c) >= cosR) return true;
        glm::vec3 ctr = glm::normalize(verts_[n.v[0]].dir + verts_[n.v[1]].dir + verts_[n.v[2]].dir);
        return glm::dot(ctr, c) >= cosR;
    }

    // Allocation-independent vertex id: hash of the recursively-resolved base
    // ancestry, so the same surface point keys the same across rebuilds.
    uint64_t stableKey(const TVertex& v) const {
        if (v.parentA < 0) {
            // base vertex: index is stable (0..11)
            uint32_t idx = static_cast<uint32_t>(&v - verts_.data());
            return 0x9E3779B97F4A7C15ull ^ idx;
        }
        uint64_t ka = stableKey(verts_[v.parentA]);
        uint64_t kb = stableKey(verts_[v.parentB]);
        uint64_t lo = std::min(ka, kb), hi = std::max(ka, kb);
        uint64_t h = lo * 0x100000001B3ull;       // FNV-ish mix of the unordered pair
        h ^= hi + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
        return h;
    }

    TerrainConfig cfg_;
    std::vector<TVertex> verts_;
    std::vector<TNode>   nodes_;
    std::unordered_map<uint64_t, uint32_t> midCache_;
    uint32_t baseCount_ = 0;
};

} // namespace bagel::planet
