#include "planet.hpp"
#include "bagel_model.hpp"
#include "../bagel_textures.hpp"
#include "../bagel_material.hpp"

#include <string>
#include <cassert>
#include <cstring>
#include <array>
#include <vector>
#include <unordered_map>
#include <iostream>

namespace bagel
{
    // Static low-poly icosphere as a flat triangle SOUP (3 verts/triangle, smooth radial
    // normals), origin-centered at `radius`. Used for the ocean sphere — a separate
    // transparent submesh appended to the planet's vertex buffer. No LOD: a fixed
    // subdivision `level` (2-3 is plenty for a normal-perturbed water surface).
    static void buildIcosphereSoup(float radius, int level, uint16_t materialIndex,
                                   std::vector<BGLModel::Vertex> &out)
    {
        const float t = (1.0f + std::sqrt(5.0f)) * 0.5f;
        std::vector<glm::vec3> v = {
            {-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0},
            {0, -1, t}, {0, 1, t}, {0, -1, -t}, {0, 1, -t},
            {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1} };
        for (auto &p : v) p = glm::normalize(p);
        std::vector<glm::ivec3> faces = {
            {0,11,5},{0,5,1},{0,1,7},{0,7,10},{0,10,11},{1,5,9},{5,11,4},{11,10,2},
            {10,7,6},{7,1,8},{3,9,4},{3,4,2},{3,2,6},{3,6,8},{3,8,9},{4,9,5},
            {2,4,11},{6,2,10},{8,6,7},{9,8,1} };

        std::unordered_map<uint64_t, uint32_t> midCache;
        auto midpoint = [&](uint32_t a, uint32_t b) {
            uint64_t key = (static_cast<uint64_t>(std::min(a, b)) << 32) | std::max(a, b);
            auto it = midCache.find(key);
            if (it != midCache.end()) return it->second;
            uint32_t idx = static_cast<uint32_t>(v.size());
            v.push_back(glm::normalize(v[a] + v[b]));
            midCache.emplace(key, idx);
            return idx;
        };
        for (int l = 0; l < level; ++l) {
            std::vector<glm::ivec3> next;
            next.reserve(faces.size() * 4);
            for (auto &f : faces) {
                uint32_t a = midpoint(f.x, f.y), b = midpoint(f.y, f.z), c = midpoint(f.z, f.x);
                next.push_back({f.x, (int)a, (int)c});
                next.push_back({f.y, (int)b, (int)a});
                next.push_back({f.z, (int)c, (int)b});
                next.push_back({(int)a, (int)b, (int)c});
            }
            faces.swap(next);
        }
        out.reserve(out.size() + faces.size() * 3);
        for (auto &f : faces) {
            for (int k = 0; k < 3; ++k) {
                const glm::vec3 dir = v[(&f.x)[k]];
                BGLModel::Vertex vert{};
                vert.position = dir * radius;
                vert.normal = dir; // outward radial; waves perturb it in the shader
                vert.color = {1.0f, 1.0f, 1.0f};
                vert.materialIndex = materialIndex;
                out.push_back(vert);
            }
        }
    }

    // memcpy one paint face from the packed CPU buffer (pc.paint) into its mapped host-visible
    // image, honoring the image's row pitch. No GPU work — the shader samples it in place.
    static void uploadFaceToMapped(PlanetComponent &pc, int face)
    {
        if (!pc.paintMapped[face]) return;
        const int res = pc.cfg.paintRes;
        const uint16_t *src = pc.paint.data() + static_cast<size_t>(face) * res * res;
        const size_t rowBytes = static_cast<size_t>(res) * sizeof(uint16_t);
        if (pc.paintRowPitch == rowBytes)
        {
            std::memcpy(pc.paintMapped[face], src, rowBytes * res);
        }
        else
        {
            uint8_t *dst = static_cast<uint8_t *>(pc.paintMapped[face]);
            for (int y = 0; y < res; ++y)
                std::memcpy(dst + static_cast<size_t>(y) * pc.paintRowPitch, src + static_cast<size_t>(y) * res, rowBytes);
        }
    }

    // Stable per-planet+face dedup key for the bindless paint textures (keyed on the
    // entity id, which the snapshot loader preserves across save/load).
    static std::string planetFaceTexName(entt::entity e, int face)
    {
        return "__planet_" + std::to_string(static_cast<uint32_t>(entt::to_integral(e))) +
               "_f" + std::to_string(face);
    }

    PlanetComponentSystem::PlanetComponentSystem(BGLDevice &_bglDevice, entt::registry &re) : bglDevice(_bglDevice), registry(re)
    {
    }

    void PlanetComponentSystem::allocatePaintTextures(entt::entity e, PlanetComponent &pc, BGLTextureLoader &texLoader)
    {
        const int res = pc.cfg.paintRes;
        const size_t faceLen = static_cast<size_t>(res) * res;
        uint32_t base = 0;
        for (int f = 0; f < planet::PAINT_FACES; ++f)
        {
            const std::string name = planetFaceTexName(e, f);
            // Preferred: a persistently-mapped host-visible image — paint strokes just memcpy
            // into it (see update()), no recreate. Falls back to a staged upload if the GPU
            // can't sample a linear-tiled R16 image.
            auto hi = texLoader.createHostVisibleTexture(name.c_str(), res, res, VK_FORMAT_R16_UNORM);
            uint32_t h;
            if (hi.ok)
            {
                h = hi.handle;
                pc.paintMapped[f] = hi.mapped;
                pc.paintRowPitch = hi.rowPitch;
                uploadFaceToMapped(pc, f); // initial content
            }
            else
            {
                pc.paintMapped[f] = nullptr;
                const uint8_t *px = reinterpret_cast<const uint8_t *>(pc.paint.data() + f * faceLen);
                h = texLoader.updateTextureFromMemory(name.c_str(), px, res, res, VK_FORMAT_R16_UNORM);
            }
            if (f == 0) base = h;
            else assert(h == base + static_cast<uint32_t>(f) &&
                        "planet paint faces must occupy contiguous bindless handles");
        }
        pc.faceBaseHandle = base;
    }

    void PlanetComponentSystem::setupOceanMaterial(ModelComponent &mc, BGLMaterialManager &materialManager)
    {
        // Two material slots: 0 = terrain (procedural; never sampled via the skin table),
        // 1 = ocean (sentinel the transparent shader recognizes — no real texture).
        uint32_t base = materialManager.allocateSkinBlock(2);
        materialManager.writeSkinEntry(base + 0, 0, 0, 0, 0);
        materialManager.writeSkinEntry(base + 1, OCEAN_MATERIAL_SENTINEL, 0, 0, 0);
        mc.skinBase  = static_cast<uint16_t>(base);
        mc.numSlots  = 2;
        mc.numSkins  = 1;
        mc.skinIndex = 0;
    }

    entt::entity PlanetComponentSystem::createPlanet(const glm::vec3 position, planet::TerrainConfig cfg, BGLMaterialManager &materialManager)
    {
        entt::entity planetEntity = registry.create();
        auto& tc = registry.emplace<TransformComponent>(planetEntity);
		tc.setTranslation(position); // mesh is already world-space (radius baked in)
		tc.setScale({ 1.0f, 1.0f, 1.0f });

		ModelLoadSettings settings{};
		settings.scaleVec = { 1.0f, 1.0f, 1.0f };
		settings.source = "planet";
		// Planet geometry is procedural: rebuildPlanetMesh regenerates the LOD cut into a
		// device-local vertex buffer (reuploaded whenever the cut/paint changes). It is NOT a
		// loadable model asset, so the planet must NOT go through ModelComponentBuilder/loadModel
		// — that path looks for a "planet" file (hits the unknown-file branch, leaving a stale/null
		// loader) and, via buffer-sharing dedup on source=="planet", could alias multiple planets
		// onto one buffer. Emplace the ModelComponent directly; rebuildPlanetMesh fills its buffer
		// + submeshes on the first pcs.update() (render systems skip a planet whose buffer is null).
		settings.isDynamic = false; // device-local reupload-on-dirty (see rebuildPlanetMesh)
		auto& mc = registry.emplace<ModelComponent>(planetEntity);
		mc.loadSettings = settings;
		mc.frustumCull = false;

		setupOceanMaterial(mc, materialManager); // 2-slot skin block (terrain + ocean sentinel)
        auto& pc = registry.emplace<PlanetComponent>(planetEntity);
        pc.cfg = cfg;
        // Authored paint buffer (zero delta everywhere); terrain samples it through bindPaint.
        pc.paint.assign(static_cast<size_t>(planet::PAINT_FACES) * cfg.paintRes * cfg.paintRes, planet::PAINT_ZERO);
        pc.terrain = std::make_unique<planet::PlanetTerrain>(cfg);
        pc.terrain->bindPaint(pc.paint.data());
        pc.terrain->recomputeRadii(); // fold paint (zero here) into vertex radii
        allocatePaintTextures(planetEntity, pc, materialManager.getTextureLoader());
        return planetEntity;
    }

    void PlanetComponentSystem::update(const glm::vec3 &camWorldPos, BGLTextureLoader &texLoader)
    {
        auto group = registry.group<>(entt::get<ModelComponent, PlanetComponent>);
        for (auto [entity, modelComp, planetComp] : group.each())
        {
            // Push any live paint edits to the GPU and force a rebuild so the mesh follows.
            if (planetComp.terrain)
            {
                uint32_t dirty = planetComp.terrain->takeDirtyFaces();
                if (dirty)
                {
                    const int res = planetComp.cfg.paintRes;
                    const size_t faceLen = static_cast<size_t>(res) * res;
                    for (int f = 0; f < planet::PAINT_FACES; ++f)
                    {
                        if (!(dirty & (1u << f))) continue;
                        if (planetComp.paintMapped[f])
                        {
                            uploadFaceToMapped(planetComp, f); // host-visible: just memcpy, no recreate
                        }
                        else
                        {
                            const std::string name = planetFaceTexName(entity, f);
                            const uint8_t *px = reinterpret_cast<const uint8_t *>(planetComp.paint.data() + f * faceLen);
                            texLoader.updateTextureFromMemory(name.c_str(), px, res, res, VK_FORMAT_R16_UNORM);
                        }
                    }
                    planetComp.planetLastRebuildCam = glm::vec3(1e9f); // force the mesh rebuild below
                }
            }
            // Only re-tessellate when the camera has moved enough to change the cut.
            if (glm::distance(camWorldPos, planetComp.planetLastRebuildCam) < 0.05f * planetComp.terrain->config().radius)
                continue;
            rebuildPlanetMesh(modelComp, planetComp, camWorldPos);
        }
    }

    void PlanetComponentSystem::rebuildPlanetMesh(ModelComponent &mc, PlanetComponent &pc, const glm::vec3 &camWorldPos)
    {
        // 1) Select the LOD cut for this camera as a triangle soup (3 verts per tri, flat-shaded).
        //    Drawn non-indexed, so no index buffer is needed.
        planet::RenderMesh mesh = pc.terrain->buildLODMesh(camWorldPos);
        if (mesh.positions.empty())
            return;

        std::vector<BGLModel::Vertex> verts(mesh.positions.size());
        for (size_t i = 0; i < mesh.positions.size(); ++i)
        {
            verts[i].position = mesh.positions[i];
            verts[i].normal = mesh.normals[i];
            verts[i].color = {1.0f, 1.0f, 1.0f};
            verts[i].materialIndex = 0; // terrain slot (procedural shader ignores it)
        }
        const uint32_t terrainCount = static_cast<uint32_t>(verts.size());

        // Append the static ocean sphere (transparent submesh). materialIndex 1 -> the ocean
        // sentinel slot, so TransparentRenderSystem draws it via the procedural water shader.
        buildIcosphereSoup(pc.cfg.sealevel, 3, /*materialIndex*/ 1, verts);
        const uint32_t seaCount = static_cast<uint32_t>(verts.size()) - terrainCount;

        // 2) Replace the GPU vertex buffer. Demo-grade rebuild: wait for the GPU to go idle
        //    so nothing in flight references the old buffer, then free + recreate.
        //    (Workstream B will replace this with a ring/double-buffer to avoid the stall.)
        vkDeviceWaitIdle(BGLDevice::device());
        if (mc.ownsBuffers)
        {
            vkDestroyBuffer(BGLDevice::device(), mc.vertexBuffer, nullptr);
            vkDestroyBuffer(BGLDevice::device(), mc.indexBuffer, nullptr);
            vkFreeMemory(BGLDevice::device(), mc.vertexMemory, nullptr);
            vkFreeMemory(BGLDevice::device(), mc.indexMemory, nullptr);
        }
        mc.vertexBuffer = mc.indexBuffer = VK_NULL_HANDLE;
        mc.vertexMemory = mc.indexMemory = VK_NULL_HANDLE;

        uploadPlanetBuffer(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                           verts.data(), sizeof(BGLModel::Vertex) * verts.size(),
                           mc.vertexBuffer, mc.vertexMemory);

        mc.ownsBuffers = true;
        mc.indexCount = 0; // non-indexed: the G-buffer pass falls back to vkCmdDraw
        mc.vertexCount = static_cast<uint32_t>(verts.size());
        // Submesh 0 = solid terrain (drawn by PlanetGBufferRenderSystem); submesh 1 = transparent
        // ocean (drawn by TransparentRenderSystem). solid-first ordering, split at solidSubmeshCount.
        mc.submeshCount = 2;
        mc.solidSubmeshCount = 1;
        mc.submeshes[0] = {};
        mc.submeshes[0].firstVertex = 0;
        mc.submeshes[0].vertexCount = terrainCount;
        mc.submeshes[0].materialIndex = 0;
        mc.submeshes[1] = {};
        mc.submeshes[1].firstVertex = terrainCount;
        mc.submeshes[1].vertexCount = seaCount;
        mc.submeshes[1].materialIndex = 1;
        // A generous AABB (radius + headroom for edits); unused while frustumCull is off.
        float r = pc.terrain->config().radius * 1.5f;
        mc.aabbMin = glm::vec3(-r);
        mc.aabbMax = glm::vec3(r);

        pc.planetLastRebuildCam = camWorldPos;
    }
    // ---- Planet (geodesic-CDLOD, wireframe) --------------------------------

    // Create a device-local buffer and fill it from `src` via a staging copy.
    // Mirrors the helper in wireframe_render_system.cpp (kept local to avoid coupling).
    void PlanetComponentSystem::uploadPlanetBuffer(VkBufferUsageFlags usage,
                                                   const void *src, VkDeviceSize size,
                                                   VkBuffer &dstBuf, VkDeviceMemory &dstMem)
    {
        VkBuffer stagingBuf;
        VkDeviceMemory stagingMem;
        bglDevice.createBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                               stagingBuf, stagingMem);
        void *mapped;
        vkMapMemory(BGLDevice::device(), stagingMem, 0, VK_WHOLE_SIZE, 0, &mapped);
        memcpy(mapped, src, size);
        vkUnmapMemory(BGLDevice::device(), stagingMem);
        bglDevice.createBuffer(size, usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, dstBuf, dstMem);
        bglDevice.copyBuffer(stagingBuf, dstBuf, size);
        vkDestroyBuffer(BGLDevice::device(), stagingBuf, nullptr);
        vkFreeMemory(BGLDevice::device(), stagingMem, nullptr);
    }
}