#include "ecs/components/data_buffer.hpp"
namespace bagel {
    DataBufferComponent::DataBufferComponent(BGLDevice& device, BGLBindlessDescriptorManager& descriptorManager, uint32_t bufferUnitsize, const char* bufferName = NULL)
    {
        objDataBuffer = std::make_unique<BGLBuffer>(
            device,
            bufferUnitsize,
            MAX_TRANSFORM_PER_ENT,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
        objDataBuffer->map();
        bufferHandle = descriptorManager.storeBuffer(objDataBuffer->descriptorInfo(), bufferName);
    }
    
    void DataBufferComponent::writeToBuffer(void* data, size_t size, size_t offset)
    {
        objDataBuffer->writeToBuffer(data, size, offset);
        objDataBuffer->flush();
    }
}