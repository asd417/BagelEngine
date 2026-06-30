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
#include "../math/bagel_math.hpp"   // perlin()
#include "../math/simplex_noise.hpp"
#include "../math/planet_cubemap.hpp" // paint cube-map addressing (shared with cubemap.glsl)

namespace bagel::planet
{

    inline constexpr uint32_t NONE = 0xFFFFFFFFu;

    struct TerrainConfig
    {
        float radius = 100.0f;    // base (sea-level) radius; per-vertex radius starts here
        int minLevel = 0;         // coarsest the cut ever goes — every leaf is at least this deep
        int maxLevel = 5;         // deepest subdivision (20*4^8 ~ 1.3M tris if fully dense)
        float splitFactor = 5.0f; // node splits while camera distance < splitFactor * nodeEdgeLen
        // --- procedural height (fBm over perlin, sampled from vertex dir) ---
        float noiseAmplitude  = 8.0f; // peak height added above/below sea-level radius
        float noiseFrequency  = 2.0f; // base feature frequency (cells per unit sphere)
        int   noiseOctaves    = 5;    // fBm octaves; more = finer detail
        float noiseLacunarity = 2.0f;    // frequency multiplier per octave
        float noiseGain       = 0.5f;    // amplitude multiplier per octave
        float sealevel        = 1000.0f; // surface radius cap -> terrain above is flattened to flat seas (high = no cap)
        uint32_t seed         = 1337;    // per-planet seed
        // --- paint height cube-map (user-editable, added on top of the noise) ---
        int   paintRes        = 512;     // texels per cube face (6 faces total)
        float paintHeightScale = 32.0f;  // max |delta| a fully-painted texel encodes (world units)
    };

    // A pooled, shared surface vertex. Shared between every triangle that touches it,
    // so a height edit here moves the surface continuously across all of them.
    struct TVertex
    {
        glm::vec3 dir;        // unit direction from planet center
        float radius;         // height = distance from center (the edited quantity)
        int32_t parentA = -1; // edge (parentA,parentB) this vertex was born on the
        int32_t parentB = -1; // midpoint of; -1,-1 for the 12 original icosa verts
        glm::vec3 pos() const { return dir * radius; }
    };

    // One node of a base triangle's quadtree. Children: 3 corner sub-triangles + 1
    // center, mirroring generated.cpp's 1->4 midpoint split.
    struct TNode
    {
        std::array<uint32_t, 3> v{}; // corner vertex indices (CCW)
        std::array<uint32_t, 4> child{NONE, NONE, NONE, NONE};
        uint32_t parent = NONE;
        uint8_t level = 0;
        bool pinned = false; // subdivided due to an edit -> never auto-collapse
        // Cached: does this subtree contain a pinned non-leaf node? Lets selectLODLeaves skip
        // dormant subtrees in O(1) instead of re-scanning them every rebuild (see
        // markSubtreePinnedUp / hasPinnedDescendant). Only ever set true (pins are permanent).
        bool subtreePinned = false;
        bool isLeaf() const { return child[0] == NONE; }
    };

    // Triangle soup ready to upload. Soup (not indexed) because seam snapping is
    // per-triangle-instance; a production path would re-index after snapping.
    struct RenderMesh
    {
        std::vector<glm::vec3> positions; // 3 per triangle
        std::vector<glm::vec3> normals;   // flat per-triangle normal, duplicated x3
        size_t triangleCount() const { return positions.size() / 3; }
    };

    class PlanetTerrain
    {
    public:
        explicit PlanetTerrain(const TerrainConfig &cfg = {}) : cfg_(cfg) { buildIcosahedron(); }

        const TerrainConfig &config() const { return cfg_; }

        // Live LOD tuning. splitFactor only affects which leaves the next buildLODMesh
        // selects, so no re-tessellation/rebuild of the tree is needed — the caller
        // just needs to force a fresh buildLODMesh (see PlanetComponentSystem).
        void setSplitFactor(float f) { cfg_.splitFactor = f; }

        // Live procedural-height tuning. baseHeight() is a pure function of direction,
        // so re-sampling every existing vertex regenerates the terrain in place — the
        // quadtree/LOD frontier is kept, only radii change. Caller forces a buildLODMesh.
        // (Note: this overwrites radii, so any future raise/lower edits are reset.)
        void setNoise(float amplitude, float frequency, int octaves,
                      float lacunarity, float gain, float sealevel, uint32_t seed)
        {
            cfg_.noiseAmplitude  = amplitude;
            cfg_.noiseFrequency  = frequency;
            cfg_.noiseOctaves    = octaves;
            cfg_.noiseLacunarity = lacunarity;
            cfg_.noiseGain       = gain;
            cfg_.sealevel        = sealevel;
            cfg_.seed            = seed;
            for (auto &v : verts_)
                v.radius = surfaceRadius(v.dir);
        }

        // --- Paint binding ------------------------------------------------------
        // The 6-face R16 paint buffer is owned by PlanetComponent (so it serializes
        // as the authored recipe); the terrain samples it through this non-owning
        // pointer. Length must be 6 * paintRes * paintRes. Call recomputeRadii()
        // after binding a non-empty buffer (e.g. on map load) to fold it into radii.
        void bindPaint(uint16_t *paintBuffer) { paint_ = paintBuffer; }
        void recomputeRadii()
        {
            for (auto &v : verts_)
                v.radius = surfaceRadius(v.dir);
        }
        // Bitmask of cube faces touched since the last call (clears it). The render
        // system uploads exactly these faces to the GPU.
        uint32_t takeDirtyFaces()
        {
            uint32_t d = dirtyFaces_;
            dirtyFaces_ = 0;
            return d;
        }

        // --- Sampling -----------------------------------------------------------
        // Surface radius (height) along a unit direction: descend to the deepest
        // existing node containing dir, barycentric-blend its 3 corner radii.
        float heightAt(const glm::vec3 &dir) const
        {
            glm::vec3 d = glm::normalize(dir);
            uint32_t n = baseNodeContaining(d);
            n = descend(n, d); // walk to deepest materialised leaf
            const TNode &t = nodes_[n];
            glm::vec3 b = bary(d, t);
            return b.x * verts_[t.v[0]].radius + b.y * verts_[t.v[1]].radius + b.z * verts_[t.v[2]].radius;
        }
        glm::vec3 surfacePoint(const glm::vec3 &dir) const;
        // --- Editing (paint into the height cube-map) ---------------------------
        // Raise/lower a geodesic disc around centerDir (a unit dir) by writing into
        // the paint cube-map, so the edit is captured texture-side and shows up in
        // BOTH the mesh (resampled below) and the per-fragment shading. angularRadius
        // in radians; targetLevel forces subdivision so the mesh is fine enough to
        // carry the painted relief. Edited nodes are pinned so LOD never collapses them.
        void paintBrush(const glm::vec3 &centerDir, float angularRadius, float deltaHeight,
                        int targetLevel)
        {
            glm::vec3 c = glm::normalize(centerDir);
            targetLevel = std::min(targetLevel, cfg_.maxLevel);
            float cosR = std::cos(angularRadius);

            // 1) Force-subdivide every node overlapping the disc down to targetLevel, pinning.
            for (uint32_t r = 0; r < baseCount_; ++r)
                forceSubdivideDisc(r, c, cosR, targetLevel);

            // 2) Splat the smoothstep falloff into the overlapping paint texels.
            if (paint_)
                splatBrush(c, cosR, deltaHeight);

            // 3) Resample every vertex inside the disc so the mesh picks up the paint.
            for (auto &vtx : verts_)
            {
                if (glm::dot(vtx.dir, c) <= cosR)
                    continue;
                vtx.radius = surfaceRadius(vtx.dir);
            }
        }

        // --- LOD cut + render emission -----------------------------------------
        // Select the leaf frontier for a camera position and emit a renderable soup,
        // snapping T-junction midpoints onto coarser neighbours' edges (crack-free).
        RenderMesh buildLODMesh(const glm::vec3 &cameraPos)
        {
            std::vector<uint32_t> leaves;
            for (uint32_t r = 0; r < baseCount_; ++r)
                selectLODLeaves(r, cameraPos, leaves);

            // Active "coarse" edges = every edge actually used by a leaf in the cut.
            // A midpoint vertex whose parent edge appears here sits on a coarse edge
            // (the neighbour across it didn't split) -> snap it to remove the crack.
            std::unordered_set<uint64_t> activeEdges;
            activeEdges.reserve(leaves.size() * 3);
            for (uint32_t li : leaves)
            {
                const auto &v = nodes_[li].v;
                activeEdges.insert(edgeKey(v[0], v[1]));
                activeEdges.insert(edgeKey(v[1], v[2]));
                activeEdges.insert(edgeKey(v[2], v[0]));
            }

            // Per-vertex normals from the analytic height gradient (surfaceNormal) — the CPU
            // twin of planet_gbuffer.frag. Smooth and seam-free by construction (a continuous
            // function of dir), so no face-normal accumulation / seam averaging is needed.
            RenderMesh mesh;
            mesh.positions.reserve(leaves.size() * 3);
            mesh.normals.reserve(leaves.size() * 3);
            for (uint32_t li : leaves)
            {
                const auto &v = nodes_[li].v;
                for (int k = 0; k < 3; ++k)
                {
                    uint32_t vi = v[k];
                    mesh.positions.push_back(seamSafePos(vi, activeEdges));
                    mesh.normals.push_back(surfaceNormal(verts_[vi].dir, verts_[vi].radius));
                }
            }
            return mesh;
        }

        // Same LOD cut, but as a line-list wireframe (3 edges per emitted triangle)
        // — handy for visualising the dynamic subdivision. Positions are world-space
        // (already radius-scaled); indices are vertex pairs for VK_PRIMITIVE_TOPOLOGY_LINE_LIST.
        struct WireMesh
        {
            std::vector<glm::vec3> positions;
            std::vector<uint32_t> lineIndices;
        };
        WireMesh buildLODWireMesh(const glm::vec3 &cameraPos)
        {
            RenderMesh m = buildLODMesh(cameraPos);
            WireMesh w;
            w.positions = std::move(m.positions);
            size_t tris = w.positions.size() / 3;
            w.lineIndices.reserve(tris * 6);
            for (uint32_t t = 0; t < tris; ++t)
            {
                uint32_t a = 3 * t, b = 3 * t + 1, c = 3 * t + 2;
                w.lineIndices.push_back(a);
                w.lineIndices.push_back(b);
                w.lineIndices.push_back(b);
                w.lineIndices.push_back(c);
                w.lineIndices.push_back(c);
                w.lineIndices.push_back(a);
            }
            return w;
        }

        // Serialization note: edits no longer live as per-vertex deltas. They are
        // captured in the paint cube-map (owned by PlanetComponent), which is what the
        // map file stores alongside TerrainConfig; the tree + radii are rebuilt from
        // those on load (bindPaint + recomputeRadii). See bagel_ecs_serialize.hpp.

        size_t vertexCount() const { return verts_.size(); }
        size_t nodeCount() const { return nodes_.size(); }

    private:
        // ---- construction ------------------------------------------------------
        void buildIcosahedron()
        {
            const float t = (1.0f + std::sqrt(5.0f)) * 0.5f;
            const glm::vec3 sv[12] = {
                {-1, t, 0},
                {1, t, 0},
                {-1, -t, 0},
                {1, -t, 0},
                {0, -1, t},
                {0, 1, t},
                {0, -1, -t},
                {0, 1, -t},
                {t, 0, -1},
                {t, 0, 1},
                {-t, 0, -1},
                {-t, 0, 1},
            };
            for (const auto &s : sv)
                addVertex(glm::normalize(s), -1, -1);
            const uint32_t f[20][3] = {
                {0, 11, 5},
                {0, 5, 1},
                {0, 1, 7},
                {0, 7, 10},
                {0, 10, 11},
                {1, 5, 9},
                {5, 11, 4},
                {11, 10, 2},
                {10, 7, 6},
                {7, 1, 8},
                {3, 9, 4},
                {3, 4, 2},
                {3, 2, 6},
                {3, 6, 8},
                {3, 8, 9},
                {4, 9, 5},
                {2, 4, 11},
                {6, 2, 10},
                {8, 6, 7},
                {9, 8, 1},
            };
            for (auto &tri : f)
            {
                TNode n;
                n.v = {tri[0], tri[1], tri[2]};
                n.level = 0;
                nodes_.push_back(n);
            }
            baseCount_ = 20;
        }

        // Procedural sea-level-relative height as a pure function of direction, so
        // every vertex — base or lazily-born midpoint — agrees at any subdivision
        // level (keeps the surface continuous and crack-free).
        // Edit this function to change the shape of the base planet
        float baseHeight(const glm::vec3 &dir) const
        {
            float sum = 0.0f, amp = 1.0f, norm = 0.0f, freq = cfg_.noiseFrequency;
            for (int o = 0; o < cfg_.noiseOctaves; ++o)
            {
                sum  += amp * perlin(dir * freq, cfg_.seed + o);
                norm += amp;
                freq *= cfg_.noiseLacunarity;
                amp  *= cfg_.noiseGain;
            }
            float n = (norm > 0.0f) ? sum / norm : 0.0f; // ~[-1,1]
            // No sea-level clamp: sealevel is now a SEPARATE ocean sphere (transparent submesh),
            // not a floor on the land. Terrain keeps its full noise band; low ground simply ends
            // up below the ocean radius and reads as submerged.
            return cfg_.radius + cfg_.noiseAmplitude * n;
        }

        // Final surface radius = procedural base + user-painted delta. The single
        // shared definition of "height"; mirrored on the GPU in planet_gbuffer.frag.
        // Falls back to baseHeight when no paint buffer is bound (e.g. at construction).
        float surfaceRadius(const glm::vec3 &dir) const
        {
            float h = baseHeight(dir);
            if (paint_)
                h += samplePaint(paint_, cfg_.paintRes, cfg_.paintHeightScale, dir);
            return h;
        }

        // Analytic gradient of baseHeight's noise term w.r.t. dir (paint excluded — low-freq
        // authored detail, negligible for the shading normal; matches planet_gbuffer.frag).
        // Built from one perlinD per octave, mirroring baseHeight's loop exactly.
        glm::vec3 baseHeightGrad(const glm::vec3 &dir) const
        {
            float amp = 1.0f, norm = 0.0f, freq = cfg_.noiseFrequency;
            glm::vec3 grad(0.0f);
            for (int o = 0; o < cfg_.noiseOctaves; ++o)
            {
                glm::vec4 pd = perlinD(dir * freq, cfg_.seed + o);
                grad += amp * freq * glm::vec3(pd.y, pd.z, pd.w);
                norm += amp;
                freq *= cfg_.noiseLacunarity;
                amp  *= cfg_.noiseGain;
            }
            grad = (norm > 0.0f) ? grad / norm : glm::vec3(0.0f);
            return cfg_.noiseAmplitude * grad;
        }

        // Outward surface normal at dir from the analytic height gradient — the CPU twin of
        // planet_gbuffer.frag's per-fragment normal, so base-mesh and shader normals agree.
        // `radius` is surfaceRadius(dir) (= H0, paint included). n = normalize(H0*d - gTang),
        // gTang = tangent-plane component of the gradient; basis-free so it's continuous at
        // the poles (an arbitrary tangent frame would flip there and leave a seam ring).
        glm::vec3 surfaceNormal(const glm::vec3 &dir, float radius) const
        {
            glm::vec3 d = glm::normalize(dir);
            glm::vec3 g = baseHeightGrad(d);
            glm::vec3 gTang = g - glm::dot(g, d) * d;
            return glm::normalize(radius * d - gTang);
        }

        uint32_t addVertex(const glm::vec3 &dir, int32_t pa, int32_t pb)
        {
            TVertex v;
            v.dir = dir;
            v.radius = surfaceRadius(dir);
            v.parentA = pa;
            v.parentB = pb;
            verts_.push_back(v);
            return static_cast<uint32_t>(verts_.size() - 1);
        }

        static uint64_t edgeKey(uint32_t a, uint32_t b)
        {
            uint32_t lo = std::min(a, b), hi = std::max(a, b);
            return (static_cast<uint64_t>(lo) << 32) | hi;
        }

        // Shared midpoint vertex of edge (a,b): created once. Records its parents.
        // Height comes from baseHeight(dir) in addVertex, so each finer level reveals
        // more noise detail (rather than a flat average of its parents).
        uint32_t midpoint(uint32_t a, uint32_t b)
        {
            uint64_t key = edgeKey(a, b);
            auto it = midCache_.find(key);
            if (it != midCache_.end())
                return it->second;
            glm::vec3 dir = glm::normalize(verts_[a].dir + verts_[b].dir);
            uint32_t idx = addVertex(dir, static_cast<int32_t>(std::min(a, b)),
                                     static_cast<int32_t>(std::max(a, b)));
            midCache_.emplace(key, idx);
            return idx;
        }

        // ---- subdivision -------------------------------------------------------
        void subdivide(uint32_t ni)
        {
            TNode &n = nodes_[ni];
            if (!n.isLeaf() || n.level >= cfg_.maxLevel)
                return;
            uint32_t a = midpoint(n.v[0], n.v[1]);
            uint32_t b = midpoint(n.v[1], n.v[2]);
            uint32_t c = midpoint(n.v[2], n.v[0]);
            uint8_t lvl = n.level + 1;
            std::array<uint32_t, 3> v = n.v; // copy: nodes_ may reallocate below
            uint32_t kids[4] = {
                makeChild({v[0], a, c}, ni, lvl),
                makeChild({v[1], b, a}, ni, lvl),
                makeChild({v[2], c, b}, ni, lvl),
                makeChild({a, b, c}, ni, lvl),
            };
            for (int i = 0; i < 4; ++i)
                nodes_[ni].child[i] = kids[i];
            // This node just became a non-leaf. If it's pinned (edited), it now counts as a
            // pinned non-leaf, so propagate the cached flag up the ancestor chain. (LOD-driven
            // subdivisions create unpinned children and never trip this.)
            if (nodes_[ni].pinned)
                markSubtreePinnedUp(ni);
        }

        uint32_t makeChild(std::array<uint32_t, 3> v, uint32_t parent, uint8_t level)
        {
            TNode n;
            n.v = v;
            n.parent = parent;
            n.level = level;
            nodes_.push_back(n);
            return static_cast<uint32_t>(nodes_.size() - 1);
        }

        // Recursively subdivide nodes overlapping the geodesic disc to targetLevel; pin.
        void forceSubdivideDisc(uint32_t ni, const glm::vec3 &c, float cosR, int targetLevel)
        {
            if (!nodeIntersectsDisc(ni, c, cosR))
                return;
            nodes_[ni].pinned = true;
            // If this node is already subdivided, pinning it makes it a pinned non-leaf right now
            // (subdivide() won't run below to do it). Leaf nodes get marked by subdivide() if/when
            // they split; a pinned leaf at targetLevel correctly stays unmarked.
            if (!nodes_[ni].isLeaf())
                markSubtreePinnedUp(ni);
            if (nodes_[ni].level >= targetLevel)
                return;
            if (nodes_[ni].isLeaf())
                subdivide(ni);
            std::array<uint32_t, 4> kids = nodes_[ni].child;
            if (kids[0] == NONE)
                return; // hit maxLevel
            for (uint32_t k : kids)
                forceSubdivideDisc(k, c, cosR, targetLevel);
        }

        // Accumulate deltaHeight * smoothstep falloff into every paint texel whose
        // direction lies inside the geodesic disc (dot(dir,c) > cosR). Walks only the
        // faces the disc can touch (cheap angular cull); marks touched faces dirty.
        void splatBrush(const glm::vec3 &c, float cosR, float deltaHeight)
        {
            const int res = cfg_.paintRes;
            const float hs = cfg_.paintHeightScale;
            // A cube face spans up to ~54.7 deg from its center (to a corner). Skip a
            // face when even its farthest point can't reach the brush disc.
            const float angR = std::acos(std::clamp(cosR, -1.0f, 1.0f));
            const float faceCullCos = std::cos(std::min(angR + 0.9554f, 3.14159265f));
            const glm::vec3 faceCenter[6] = {
                { 1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1} };
            for (int face = 0; face < PAINT_FACES; ++face)
            {
                if (glm::dot(faceCenter[face], c) < faceCullCos)
                    continue; // disc cannot reach this face
                for (int j = 0; j < res; ++j)
                {
                    for (int i = 0; i < res; ++i)
                    {
                        glm::vec2 uv{ (i + 0.5f) / res, (j + 0.5f) / res };
                        glm::vec3 dir = faceUVToDir(face, uv);
                        float cd = glm::dot(dir, c);
                        if (cd <= cosR)
                            continue;
                        float t = (cd - cosR) / (1.0f - cosR);     // 0 at rim -> 1 at center
                        float falloff = t * t * (3.0f - 2.0f * t); // smoothstep
                        uint32_t idx = texelIndex(face, i, j, res);
                        float cur = decodePaint(paint_[idx], hs);
                        paint_[idx] = encodePaint(cur + deltaHeight * falloff, hs);
                    }
                }
                dirtyFaces_ |= (1u << face);
            }
        }

        // ---- LOD cut -----------------------------------------------------------
        void selectLODLeaves(uint32_t ni, const glm::vec3 &cam, std::vector<uint32_t> &out)
        {
            // Force subdivision up to minLevel everywhere (uniform coarse floor), then let
            // camera distance drive splitting up to maxLevel.
            bool belowMin = nodes_[ni].level < cfg_.minLevel;
            bool split = (belowMin || shouldSplit(ni, cam)) && nodes_[ni].level < cfg_.maxLevel;
            // Pinned (edited) subtrees always render at their materialised detail.
            if (!split && !hasPinnedDescendant(ni))
            {
                out.push_back(ni);
                return;
            }
            if (nodes_[ni].isLeaf())
            {
                if (!split)
                {
                    out.push_back(ni);
                    return;
                }
                subdivide(ni);
                if (nodes_[ni].isLeaf())
                {
                    out.push_back(ni);
                    return;
                } // maxLevel
            }
            // Copy the child indices BEFORE recursing: a deeper selectLODLeaves may subdivide and
            // push_back into nodes_, reallocating it — iterating nodes_[ni].child in place
            // would then dereference freed memory and read garbage child indices.
            std::array<uint32_t, 4> kids = nodes_[ni].child;
            for (uint32_t k : kids)
                selectLODLeaves(k, cam, out);
        }

        bool shouldSplit(uint32_t ni, const glm::vec3 &cam) const
        {
            const TNode &n = nodes_[ni];
            glm::vec3 center = (verts_[n.v[0]].pos() + verts_[n.v[1]].pos() + verts_[n.v[2]].pos()) / 3.0f;
            float edgeLen = glm::length(verts_[n.v[0]].pos() - verts_[n.v[1]].pos());
            float dist = glm::length(cam - center);
            return dist < cfg_.splitFactor * edgeLen;
        }

        // O(1): the per-node flag is kept current by markSubtreePinnedUp whenever a node becomes
        // a pinned non-leaf. Replaces the old full-subtree recursion that made the LOD cut scale
        // with the (ever-growing) total tree size instead of just the active frontier.
        bool hasPinnedDescendant(uint32_t ni) const { return nodes_[ni].subtreePinned; }

        // Mark ni and its ancestors as containing a pinned non-leaf. Stops at the first node
        // already marked (its ancestors are already marked too), so this is O(depth) at worst.
        void markSubtreePinnedUp(uint32_t ni)
        {
            while (ni != NONE && !nodes_[ni].subtreePinned)
            {
                nodes_[ni].subtreePinned = true;
                ni = nodes_[ni].parent;
            }
        }

        // ---- geometry helpers --------------------------------------------------
        glm::vec3 seamSafePos(uint32_t vi, const std::unordered_set<uint64_t> &activeEdges) const
        {
            const TVertex &v = verts_[vi];
            if (v.parentA >= 0)
            {
                uint64_t pe = edgeKey(static_cast<uint32_t>(v.parentA), static_cast<uint32_t>(v.parentB));
                if (activeEdges.count(pe))
                { // if edge pe is in the set activeEdge, it means there is a T-junction
                    // A coarse leaf still uses this vertex's parent edge -> we are a hanging
                    // T-junction midpoint. The coarse neighbour renders the STRAIGHT chord
                    // pa->pb, while this vertex sits out on the sphere arc (and may be displaced
                    // by edits), opening a crack between chord and arc. Snap to the LINEAR
                    // midpoint of the parents' positions so pa -> v -> pb is collinear with the
                    // coarse edge and the gap closes. (NOT normalize(dir)*avgRadius — that is the
                    // arc point, i.e. exactly where v already is, which closes nothing.)
                    const TVertex &pa = verts_[v.parentA];
                    const TVertex &pb = verts_[v.parentB];
                    return 0.5f * (pa.pos() + pb.pos());
                }
            }
            return v.pos();
        }

        // Barycentric coords of unit dir within node's triangle (via ray/plane hit).
        glm::vec3 bary(const glm::vec3 &dir, const TNode &t) const
        {
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
            if (std::fabs(dn) < 1e-12f)
                return {1.0f, 0.0f, 0.0f};
            float v = (d11 * d20 - d01 * d21) / dn;
            float w = (d00 * d21 - d01 * d20) / dn;
            return {1.0f - v - w, v, w};
        }

        // Descend to the deepest existing child containing dir (1->4 split rule:
        // the dominant barycentric corner picks a corner child; else the center).
        uint32_t descend(uint32_t ni, const glm::vec3 &dir) const
        {
            while (!nodes_[ni].isLeaf())
            {
                glm::vec3 b = bary(dir, nodes_[ni]);
                uint32_t next;
                if (b.x >= 0.5f)
                    next = nodes_[ni].child[0];
                else if (b.y >= 0.5f)
                    next = nodes_[ni].child[1];
                else if (b.z >= 0.5f)
                    next = nodes_[ni].child[2];
                else
                    next = nodes_[ni].child[3];
                ni = next;
            }
            return ni;
        }

        uint32_t baseNodeContaining(const glm::vec3 &dir) const
        {
            uint32_t best = 0;
            float bestDot = -2.0f;
            for (uint32_t i = 0; i < baseCount_; ++i)
            {
                glm::vec3 c = glm::normalize(verts_[nodes_[i].v[0]].dir +
                                             verts_[nodes_[i].v[1]].dir +
                                             verts_[nodes_[i].v[2]].dir);
                float d = glm::dot(dir, c);
                if (d > bestDot)
                {
                    bestDot = d;
                    best = i;
                }
            }
            return best;
        }

        bool nodeIntersectsDisc(uint32_t ni, const glm::vec3 &c, float cosR) const
        {
            const TNode &n = nodes_[ni];
            // Cheap conservative test: any corner inside, or center within radius.
            for (uint32_t vi : n.v)
                if (glm::dot(verts_[vi].dir, c) >= cosR)
                    return true;
            glm::vec3 ctr = glm::normalize(verts_[n.v[0]].dir + verts_[n.v[1]].dir + verts_[n.v[2]].dir);
            return glm::dot(ctr, c) >= cosR;
        }

        TerrainConfig cfg_;
        std::vector<TVertex> verts_;
        std::vector<TNode> nodes_;
        std::unordered_map<uint64_t, uint32_t> midCache_;
        uint32_t baseCount_ = 0;
        // Non-owning view of PlanetComponent's 6-face R16 paint buffer (see bindPaint).
        uint16_t *paint_ = nullptr;
        uint32_t dirtyFaces_ = 0; // faces touched since the last takeDirtyFaces()
    };

} // namespace bagel::planet
