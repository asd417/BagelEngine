#include "bagel_ecs_components.hpp"

#include "bagel_buffer.hpp"
#include <memory>
#define MAX_TRANSFORM_ARRAYSIZE 100000

namespace bagel {
    struct OBJDataBufferUnit {
        glm::mat4 modelMatrix{ 1.0f };
        glm::mat4 normalMatrix{ 1.0f };
    };

    DataBufferComponent::DataBufferComponent(BGLDevice& device, BGLBindlessDescriptorManager& descriptorManager) : bglDevice{ device }
    {
        objDataBuffer = std::make_unique<BGLBuffer>(
            bglDevice,
            sizeof(OBJDataBufferUnit),
            MAX_TRANSFORM_ARRAYSIZE,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
        objDataBuffer->map();
        bufferHandle = descriptorManager.storeBuffer(objDataBuffer->descriptorInfo());
    }

    DataBufferComponent::~DataBufferComponent()
    {
        objDataBuffer->unmap();
    }

    void DataBufferComponent::writeToBuffer(void* data, size_t size, size_t offset)
    {
        objDataBuffer->writeToBuffer(data, size, offset);
        objDataBuffer->flush();
    }

    glm::mat4 TransformComponent::mat4(uint32_t index)
    {
        const float c3 = glm::cos(rotation[index].z);
        const float s3 = glm::sin(rotation[index].z);
        const float c2 = glm::cos(rotation[index].x);
        const float s2 = glm::sin(rotation[index].x);
        const float c1 = glm::cos(rotation[index].y);
        const float s1 = glm::sin(rotation[index].y);
        return glm::mat4{
            {
                scale[index].x * (c1 * c3 + s1 * s2 * s3),
                scale[index].x * (c2 * s3),
                scale[index].x * (c1 * s2 * s3 - c3 * s1),
                0.0f,
            },
            {
                scale[index].y * (c3 * s1 * s2 - c1 * s3),
                scale[index].y * (c2 * c3),
                scale[index].y * (c1 * c3 * s2 + s1 * s3),
                0.0f,
            },
            {
                scale[index].z * (c2 * s1),
                scale[index].z * (-s2),
                scale[index].z * (c1 * c2),
                0.0f,
            },
            {translation[index].x, translation[index].y, translation[index].z, 1.0f} };

    }
    //glm will convert mat3 to mat4 automatically
    glm::mat3 TransformComponent::normalMatrix(uint32_t index)
    {
        const float c3 = glm::cos(rotation[index].z);
        const float s3 = glm::sin(rotation[index].z);
        const float c2 = glm::cos(rotation[index].x);
        const float s2 = glm::sin(rotation[index].x);
        const float c1 = glm::cos(rotation[index].y);
        const float s1 = glm::sin(rotation[index].y);
        const glm::vec3 invScale = 1.0f / scale[index];
        return glm::mat3{
            {
                invScale.x * (c1 * c3 + s1 * s2 * s3),
                invScale.x * (c2 * s3),
                invScale.x * (c1 * s2 * s3 - c3 * s1),
            },
            {
                invScale.y * (c3 * s1 * s2 - c1 * s3),
                invScale.y * (c2 * c3),
                invScale.y * (c1 * c3 * s2 + s1 * s3),
            },
            {
                invScale.z * (c2 * s1),
                invScale.z * (-s2),
                invScale.z * (c1 * c2),
            } };
    }
    void TransformComponent::ToBufferComponent(DataBufferComponent& bufferComponent)
    {
        for (int i = 0; i < translation.size(); i++) {
            OBJDataBufferUnit objData{};
            objData.modelMatrix = mat4(i);
            objData.normalMatrix = normalMatrix(i);

            bufferComponent.writeToBuffer(&objData, sizeof(objData), i * sizeof(OBJDataBufferUnit));
        }
        bufferHandle = bufferComponent.getBufferHandle();
        usingBuffer = true;
    }
    TextureComponent::TextureComponent(BGLDevice& device) : bglDevice{device}
    {

    }
    TextureComponent::~TextureComponent()
    {
        vkDestroyImageView(bglDevice.device(), view, nullptr);
        vkDestroyImage(bglDevice.device(), image, nullptr);
        vkDestroySampler(bglDevice.device(), sampler, nullptr);
        vkFreeMemory(bglDevice.device(), device_memory, nullptr);
    }

}
