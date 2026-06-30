#pragma once
#include <entt.hpp>
#include <vector>
#include <array>
#include <cstdint>
#include "game/planet_terrain.hpp"
#include "components/model.hpp"

namespace bagel
{
    class BGLTextureLoader;   // bagel_textures.hpp (kept out of this header to stay light)
    class BGLMaterialManager; // bagel_material.hpp

    // Skin-table albedo sentinel marking a submesh as ocean: the transparent shader
    // sees this in place of a real albedo handle and switches to the procedural water
    // path (waves/fresnel/fog) instead of sampling a texture. Mirrored in transparent.vert.
    inline constexpr uint32_t OCEAN_MATERIAL_SENTINEL = 0xFFFFFFFFu;

    // This component directly edits the modelcomponent
    // turns the model into an icosphere planet with dynamic retopology
    struct PlanetComponent
    {
        planet::TerrainConfig cfg;
        glm::vec3 planetLastRebuildCam = glm::vec3(1e9f);
        std::unique_ptr<bagel::planet::PlanetTerrain> terrain = nullptr;
        // Authored paint cube-map: flat R16, length 6*cfg.paintRes*cfg.paintRes, init 32768
        // (zero delta). This is the serialized source of truth; `terrain` samples it through a
        // bound pointer. paintRes/paintHeightScale live in cfg (also serialized).
        std::vector<uint16_t> paint;
        // Transient: bindless handle of paint face 0. The 6 faces are allocated contiguously,
        // so face f is faceBaseHandle + f. Rebuilt on createPlanet / rehydrate.
        uint32_t faceBaseHandle = 0;
        // Transient: persistently-mapped host-visible memory for each face (null if that face
        // fell back to the staged/recreate upload path). A paint stroke just memcpys pc.paint
        // into here — no image recreate. paintRowPitch is the mapped row stride in bytes.
        std::array<void*, planet::PAINT_FACES> paintMapped{};
        size_t paintRowPitch = 0;
    };

    class PlanetComponentSystem
    {
    public:
        PlanetComponentSystem(BGLDevice &bglDevice, entt::registry &re);
        ~PlanetComponentSystem() = default;

        // materialManager owns the texture loader (paint faces) and the skin table (ocean
        // material). Passed per-call so the system doesn't depend on subsystem construction order.
        entt::entity createPlanet(const glm::vec3 position, planet::TerrainConfig cfg, BGLMaterialManager &materialManager);
        void update(const glm::vec3 &camWorldPos, BGLTextureLoader &texLoader);
        void rebuildPlanetMesh(ModelComponent &mc, PlanetComponent &pc, const glm::vec3 &camWorldPos);
        // Allocate (or re-upload) the 6 paint faces for one planet; sets pc.faceBaseHandle.
        // Used by createPlanet and by map rehydrate. entity feeds the dedup texture names.
        void allocatePaintTextures(entt::entity e, PlanetComponent &pc, BGLTextureLoader &texLoader);
        // Reserve a 2-slot skin block on the ModelComponent: slot 0 = terrain (procedural, drawn
        // by PlanetGBufferRenderSystem; the entry is unused), slot 1 = ocean (sentinel marker the
        // transparent shader recognizes). Called by createPlanet and by map rehydrate.
        void setupOceanMaterial(ModelComponent &mc, BGLMaterialManager &materialManager);

    private:
        BGLDevice &bglDevice;
        entt::registry &registry;
        void uploadPlanetBuffer(VkBufferUsageFlags usage,
                                const void *src, VkDeviceSize size,
                                VkBuffer &dstBuf, VkDeviceMemory &dstMem);
    };
}