#include "model/model_component_builder.hpp"
namespace bagel
{

// usually, model_loaders handle vertex buffer and index buffer.
// this function is for loading a model directly from a known vertex and index buffer
void ModelComponentBuilder::buildComponent(ModelComponent &mc, const char *modelFileName, const std::vector<BGLModel::Vertex> &verts, const std::vector<uint32_t> &indices, const std::vector<SubmeshInfo> &submeshes, bool mapped)
{
    ModelCacheManager &cache = ModelCacheManager::get();

    // Generated meshes are cached by name like any other source. If one was already built
    // under this key, share it (flyweight) — create() asserts on a duplicate key.
    if (Model *cached = cache.find(modelFileName))
    {
        mc.model = cached;
        return;
    }

    Model &model = cache.create(std::string(modelFileName));
    mc.model = &model;
    model.loadSettings.source = modelFileName; // source path = the model's identity / cache key
    mc.loadSettings.source = modelFileName;

    // Model-space AABB (used for frustum culling).
    computeModelBounds(model, verts);

    // `mapped` chooses the upload path: false = staged (immutable), true = persistently mapped
    // (CPU-writable, so editComponent can update the geometry in place later).
    model.mappedVB = createVertexBuffer(sizeof(BGLModel::Vertex) * verts.size(), (void *)verts.data(),
                                        model.vertexBuffer, model.vertexMemory, mapped);
    if (!indices.empty())
    {
        model.mappedIB = createIndexBuffer(sizeof(uint32_t) * indices.size(), (void *)indices.data(), model.indexBuffer, model.indexMemory, mapped);
    }

    // Generated submeshes are all solid (transparentMaterial == false), so solidSubmeshCount
    // ends up equal to submeshCount and the solid-first ordering holds.
    populateSubmeshes(model, submeshes, verts);
    model.vertexCount = static_cast<uint32_t>(verts.size());
    model.indexCount = static_cast<uint32_t>(indices.size());
}
void ModelComponentBuilder::editComponent(ModelComponent &mc, const char *modelFileName, const std::vector<BGLModel::Vertex> &verts, const std::vector<uint32_t> &indices, const std::vector<SubmeshInfo> &submeshes)
{
    ModelCacheManager &cache = ModelCacheManager::get();
    Model *cached = cache.find(modelFileName);
    if (!cached)
    {
        throw std::runtime_error(std::string("Model Cache named ") + modelFileName + " not found");
    }
    if (indices.empty() || cached->indexCount == 0)
    {
        throw std::runtime_error("Mesh editing requires index buffer for now");
    }
    if (!cached->mappedIB || !cached->mappedVB)
    {
        throw std::runtime_error("Mesh editing requires a mapped (CPU-writable) mesh; build it with mapped=true so both the vertex and index buffers stay mapped");
    }
    // GPU may still be reading the buffers we're about to overwrite or free.
    vkDeviceWaitIdle(BGLDevice::device());

    // The current allocation holds exactly `count * elementSize` bytes, so the live counts tell us
    // the capacity. If the new data still fits, memcpy in place; if it's larger, free and reallocate
    // a fresh mapped buffer at the new size (createVertexBuffer/IndexBufferMapped also copies the data).
    const VkDevice device = BGLDevice::device();
    if (verts.size() > cached->vertexCount)
    {
        vkUnmapMemory(device, cached->vertexMemory);
        vkDestroyBuffer(device, cached->vertexBuffer, nullptr);
        vkFreeMemory(device, cached->vertexMemory, nullptr);
        cached->vertexBuffer = VK_NULL_HANDLE;
        cached->vertexMemory = VK_NULL_HANDLE;
        cached->mappedVB = createVertexBufferMapped(sizeof(BGLModel::Vertex) * verts.size(), (void *)verts.data(),
                                                    cached->vertexBuffer, cached->vertexMemory);
    }
    else
    {
        memcpy(cached->mappedVB, verts.data(), sizeof(BGLModel::Vertex) * verts.size());
    }

    if (indices.size() > cached->indexCount)
    {
        vkUnmapMemory(device, cached->indexMemory);
        vkDestroyBuffer(device, cached->indexBuffer, nullptr);
        vkFreeMemory(device, cached->indexMemory, nullptr);
        cached->indexBuffer = VK_NULL_HANDLE;
        cached->indexMemory = VK_NULL_HANDLE;
        cached->mappedIB = createIndexBufferMapped(sizeof(uint32_t) * indices.size(), (void *)indices.data(),
                                                   cached->indexBuffer, cached->indexMemory);
    }
    else
    {
        memcpy(cached->mappedIB, indices.data(), sizeof(uint32_t) * indices.size());
    }

    // Refresh CPU-side metadata to match the new geometry. populateSubmeshes rebuilds the
    // submesh table from scratch, so shrinking counts are handled correctly.
    computeModelBounds(*cached, verts);
    populateSubmeshes(*cached, submeshes, verts);
    cached->vertexCount = static_cast<uint32_t>(verts.size());
    cached->indexCount = static_cast<uint32_t>(indices.size());
}
} // namespace bagel