#pragma once
#include <entt.hpp>
#include "game/planet_terrain.hpp"
#include "components/model.hpp"

namespace bagel
{
    // This component directly edits the modelcomponent
    // turns the model into an icosphere planet with dynamic retopology
    struct PlanetComponent
    {
        planet::TerrainConfig cfg;
        glm::vec3 planetLastRebuildCam = glm::vec3(1e9f);
        std::unique_ptr<bagel::planet::PlanetTerrain> terrain = nullptr;
    };

    class PlanetComponentSystem
    {
    public:
        PlanetComponentSystem(BGLDevice &bglDevice, entt::registry &re);
        ~PlanetComponentSystem() = default;

        entt::entity createPlanet(const glm::vec3 position, planet::TerrainConfig cfg);
        void update(const glm::vec3 &camWorldPos);
        void rebuildPlanetMesh(ModelComponent &mc, PlanetComponent &pc, const glm::vec3 &camWorldPos);

    private:
        BGLDevice &bglDevice;
        entt::registry &registry;
        void uploadPlanetBuffer(VkBufferUsageFlags usage,
                                const void *src, VkDeviceSize size,
                                VkBuffer &dstBuf, VkDeviceMemory &dstMem);
    };
}