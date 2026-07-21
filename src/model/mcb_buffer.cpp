#include "imgui/bagel_imgui.hpp"
#include "model/model_component_builder.hpp"

namespace bagel
{
#define CONSOLE ConsoleApp::Instance()

void ModelComponentBuilder::createVertexBufferStaged(size_t bufferSize, void *bufferSrc, VkBuffer &bufferDst, VkDeviceMemory &memoryDst)
{
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;
    void *mapped;
    bglDevice.createBuffer(
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        stagingBuffer,
        stagingMemory);
    vkMapMemory(BGLDevice::device(), stagingMemory, 0, VK_WHOLE_SIZE, 0, &mapped);
    // Write Vertex data to stagingBuffer
    assert(mapped && "Cannot copy to unmapped buffer");
    memcpy(mapped, bufferSrc, bufferSize);

    bglDevice.createBuffer(
        bufferSize,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        bufferDst,
        memoryDst);

    // Finished Mapping vertex buffer to staging buffer inside device
    bglDevice.copyBuffer(stagingBuffer, bufferDst, bufferSize);

    // Finished Mapping device staging buffer to device vertex buffer. Destroy staging buffer.
    vkUnmapMemory(BGLDevice::device(), stagingMemory);
    vkDestroyBuffer(BGLDevice::device(), stagingBuffer, nullptr);
    vkFreeMemory(BGLDevice::device(), stagingMemory, nullptr);
    mapped = nullptr;
}

// Allocate a host-mappable buffer, preferring the BAR window (DEVICE_LOCAL | HOST_VISIBLE):
// CPU-writable VRAM the GPU reads at full speed. If that's unavailable — no such memory type
// (no ReBAR) or the BAR window is too small for bufferSize — fall back to plain HOST_VISIBLE
// system RAM (slower GPU fetches over PCIe) and log it. createBuffer() throws on either
// failure, leaving a created-but-unbound VkBuffer; destroy it before retrying so it doesn't leak.
void ModelComponentBuilder::createMappableBuffer(size_t bufferSize, VkBufferUsageFlags usage, const char *tag, VkBuffer &bufferDst, VkDeviceMemory &memoryDst)
{
    // Reject anything past the device's per-allocation cap up front, with a clear message —
    // otherwise it surfaces as an opaque VK_ERROR_OUT_OF_DEVICE_MEMORY deeper in createBuffer.
    // (NVIDIA reports UINT64_MAX here, i.e. no cap beyond the heap, so this never false-positives.)
    if (bufferSize > bglDevice.maxMemoryAllocationSize)
    {
        throw std::runtime_error("Mapped " + std::string(tag) + " buffer of " + std::to_string(bufferSize) +
                                 " bytes exceeds the device maxMemoryAllocationSize of " +
                                 std::to_string(bglDevice.maxMemoryAllocationSize) + " bytes");
    }
    try
    {
        bglDevice.createBuffer(
            bufferSize, usage,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            bufferDst, memoryDst);
    }
    catch (const std::exception &)
    {
        vkDestroyBuffer(BGLDevice::device(), bufferDst, nullptr);
        bufferDst = VK_NULL_HANDLE;
        CONSOLE->Log("ModelComponentBuilder",
                     "BAR (DEVICE_LOCAL|HOST_VISIBLE) allocation of " + std::to_string(bufferSize) + "-byte " + tag +
                         " buffer failed; allocating in HOST_VISIBLE system memory instead");
        bglDevice.createBuffer(
            bufferSize, usage,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            bufferDst, memoryDst);
    }
}

void *ModelComponentBuilder::createVertexBufferMapped(size_t bufferSize, void *bufferSrc, VkBuffer &bufferDst, VkDeviceMemory &memoryDst)
{
    void *mapped;
    createMappableBuffer(bufferSize, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, "vertex", bufferDst, memoryDst);
    vkMapMemory(BGLDevice::device(), memoryDst, 0, VK_WHOLE_SIZE, 0, &mapped);
    // Write vertex data straight into the mapped buffer
    assert(mapped && "Cannot copy to unmapped buffer");
    memcpy(mapped, bufferSrc, bufferSize);
    return mapped;
}

void ModelComponentBuilder::createIndexBufferStaged(size_t bufferSize, void *bufferSrc, VkBuffer &bufferDst, VkDeviceMemory &memoryDst)
{
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingMemory;
    void *mapped;
    bglDevice.createBuffer(
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        stagingBuffer,
        stagingMemory);
    vkMapMemory(BGLDevice::device(), stagingMemory, 0, VK_WHOLE_SIZE, 0, &mapped);
    // Write Index data to stagingBuffer
    assert(mapped && "Cannot copy to unmapped buffer");
    memcpy(mapped, bufferSrc, bufferSize);

    bglDevice.createBuffer(
        bufferSize,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        bufferDst,
        memoryDst);

    // Copy staging buffer into the device-local index buffer
    bglDevice.copyBuffer(stagingBuffer, bufferDst, bufferSize);

    // Finished Mapping device staging buffer to device index buffer. Destroy staging buffer.
    vkUnmapMemory(BGLDevice::device(), stagingMemory);
    vkDestroyBuffer(BGLDevice::device(), stagingBuffer, nullptr);
    vkFreeMemory(BGLDevice::device(), stagingMemory, nullptr);
    mapped = nullptr;
}

void *ModelComponentBuilder::createIndexBufferMapped(size_t bufferSize, void *bufferSrc, VkBuffer &bufferDst, VkDeviceMemory &memoryDst)
{
    void *mapped;
    createMappableBuffer(bufferSize, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, "index", bufferDst, memoryDst);
    vkMapMemory(BGLDevice::device(), memoryDst, 0, VK_WHOLE_SIZE, 0, &mapped);
    // Write index data straight into the mapped buffer
    assert(mapped && "Cannot copy to unmapped buffer");
    memcpy(mapped, bufferSrc, bufferSize);
    return mapped;
}

void *ModelComponentBuilder::createVertexBuffer(size_t bufferSize, VkBuffer &bufferDst, VkDeviceMemory &memoryDst, bool mapped)
{
    if (mapped)
    {
        return createVertexBufferMapped(bufferSize, (void *)activeLoader->getVertices().data(), bufferDst, memoryDst);
    }
    else
        createVertexBufferStaged(bufferSize, (void *)activeLoader->getVertices().data(), bufferDst, memoryDst);
    return nullptr;
}

void *ModelComponentBuilder::createVertexBuffer(size_t bufferSize, void *bufferSrc, VkBuffer &bufferDst, VkDeviceMemory &memoryDst, bool mapped)
{
    if (mapped)
    {
        return createVertexBufferMapped(bufferSize, bufferSrc, bufferDst, memoryDst);
    }
    else
        createVertexBufferStaged(bufferSize, bufferSrc, bufferDst, memoryDst);
    return nullptr;
}

void *ModelComponentBuilder::createIndexBuffer(size_t bufferSize, VkBuffer &bufferDst, VkDeviceMemory &memoryDst, bool mapped)
{
    if (mapped)
    {
        return createIndexBufferMapped(bufferSize, (void *)activeLoader->getIndices().data(), bufferDst, memoryDst);
    }
    else
        createIndexBufferStaged(bufferSize, (void *)activeLoader->getIndices().data(), bufferDst, memoryDst);
    return nullptr;
}

void *ModelComponentBuilder::createIndexBuffer(size_t bufferSize, void *bufferSrc, VkBuffer &bufferDst, VkDeviceMemory &memoryDst, bool mapped)
{
    if (mapped)
    {
        return createIndexBufferMapped(bufferSize, bufferSrc, bufferDst, memoryDst);
    }
    else
        createIndexBufferStaged(bufferSize, bufferSrc, bufferDst, memoryDst);
    return nullptr;
}
} // namespace bagel