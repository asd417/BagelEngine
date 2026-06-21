#include "planet.hpp"
#include "bagel_model.hpp"

namespace bagel
{
    PlanetComponentSystem::PlanetComponentSystem(BGLDevice &_bglDevice, entt::registry &re) : bglDevice(_bglDevice), registry(re)
    {
    }
    entt::entity PlanetComponentSystem::createPlanet(const glm::vec3 position, planet::TerrainConfig cfg)
    {
        entt::entity planetEntity = registry.create();
        auto& tc = registry.emplace<TransformComponent>(planetEntity);
		tc.setTranslation(position); // mesh is already world-space (radius baked in)
		tc.setScale({ 1.0f, 1.0f, 1.0f });
        auto& mc = registry.emplace<ModelComponent>(planetEntity);
		mc.loadSettings.source = "planet"; // identity only; geometry is rebuilt from the LOD cut
		mc.skinBase = 0; mc.numSlots = 1; mc.numSkins = 1; mc.skinIndex = 0;
		mc.frustumCull = false;
        auto& pc = registry.emplace<PlanetComponent>(planetEntity);
        pc.cfg = cfg;
        pc.terrain = std::make_unique<planet::PlanetTerrain>(cfg);
        return planetEntity;
    }
    void PlanetComponentSystem::update(const glm::vec3 &camWorldPos)
    {
        auto group = registry.group<>(entt::get<ModelComponent, PlanetComponent>);
        for (auto [entity, modelComp, planetComp] : group.each())
        {
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
        planet::RenderMesh mesh = pc.terrain->buildCut(camWorldPos);
        if (mesh.positions.empty())
            return;

        std::vector<BGLModel::Vertex> verts(mesh.positions.size());
        for (size_t i = 0; i < mesh.positions.size(); ++i)
        {
            verts[i].position = mesh.positions[i];
            verts[i].normal = mesh.normals[i];
            verts[i].color = {1.0f, 1.0f, 1.0f};
            verts[i].materialIndex = 0; // -> the all-unused material -> fallback albedo
        }

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
        mc.submeshCount = 1;
        mc.solidSubmeshCount = 1;
        mc.submeshes[0] = {};
        mc.submeshes[0].firstVertex = 0;
        mc.submeshes[0].vertexCount = mc.vertexCount;
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