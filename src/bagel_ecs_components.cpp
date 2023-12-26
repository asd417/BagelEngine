#include "bagel_ecs_components.hpp"

// vulkan headers
#include <vulkan/vulkan.h>

#include "bagel_buffer.hpp"
#include <memory>

// Rotation Order Definition. Jolt Quaternion returns XYZ rotation therefore we use Z1Y2X3 rotation matrix to match
#define X1Y2Z3

namespace bagel {
    DataBufferComponent::DataBufferComponent(BGLDevice& device, BGLBindlessDescriptorManager& descriptorManager, uint32_t bufferUnitsize)
    {
        objDataBuffer = std::make_unique<BGLBuffer>(
            device,
            bufferUnitsize,
            MAX_TRANSFORM_PER_ENT,
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

    glm::mat4 TransformComponent::mat4()
    {
        //X1Y2Z3 
        //Jolt Quaternion returns XYZ rotation
        const float c3 = glm::cos(rotation.z);
        const float s3 = glm::sin(rotation.z);
        const float c2 = glm::cos(rotation.y);
        const float s2 = glm::sin(rotation.y);
        const float c1 = glm::cos(rotation.x);
        const float s1 = glm::sin(rotation.x);

        return glm::mat4{
            {
                scale.x * (c2 * c3),
                scale.x * (c1 * s3 + c3 * s1 * s2),
                scale.x * (s1 * s3 - c1 * c3 * s2),
                0.0f,
            },
            {
                scale.y * (-c2 * s3),
                scale.y * (c1 * c3 - s1 * s2 * s3),
                scale.y * (c3 * s1 + c1 * s2 * s3),
                0.0f,
            },
            {
                scale.z * (s2),
                scale.z * (-c2 * s1),
                scale.z * (c1 * c2),
                0.0f,
            },
            {translation.x, translation.y, translation.z, 1.0f} };
    }

    glm::mat3 TransformComponent::normalMatrix()
    {
        const float c3 = glm::cos(rotation.z);
        const float s3 = glm::sin(rotation.z);
        const float c2 = glm::cos(rotation.y);
        const float s2 = glm::sin(rotation.y);
        const float c1 = glm::cos(rotation.x);
        const float s1 = glm::sin(rotation.x);
        const glm::vec3 invScale = 1.0f / scale;
        return glm::mat3{
            {
                scale.x * (c2 * c3),
                scale.x * (c1 * s3 + c3 * s1 * s2),
                scale.x * (s1 * s3 - c1 * c3 * s2),
            },
            {
                scale.y * (-c2 * s3),
                scale.y * (c1 * c3 - s1 * s2 * s3),
                scale.y * (c3 * s1 + c1 * s2 * s3),
            },
            {
                scale.z * (s2),
                scale.z * (-c2 * s1),
                scale.z * (c1 * c2),
            }};
    }

    glm::vec3 TransformComponent::getTranslation() const {
        return { translation.x,translation.y,translation.z };
    }

    void TransformComponent::setTranslation(glm::vec3 _translation)
    {
        //_translation *= -1;
        translation = _translation;
    }

    void TransformComponent::setScale(glm::vec3 _scale)
    {
        scale = _scale;
    }

    void TransformComponent::setRotation(glm::vec3 _rotation)
    {
        rotation = _rotation;
    }

    glm::vec3 TransformComponent::getLocalTranslation() const {
        return { localTranslation.x,localTranslation.y,localTranslation.z };
    }

    void TransformComponent::setLocalTranslation(glm::vec3 _translation)
    {
        //_translation *= -1;
        localTranslation = _translation;
    }

    void TransformComponent::setLocalScale(glm::vec3 _scale)
    {
        localScale = _scale;
    }

    void TransformComponent::setLocalRotation(glm::vec3 _rotation)
    {
        localRotation = _rotation;
    }

    glm::mat4 TransformArrayComponent::mat4(uint32_t index)
    {
        const float c3 = glm::cos(rotation[index].z);
        const float s3 = glm::sin(rotation[index].z);
        const float c2 = glm::cos(rotation[index].y);
        const float s2 = glm::sin(rotation[index].y);
        const float c1 = glm::cos(rotation[index].x);
        const float s1 = glm::sin(rotation[index].x);
        return glm::mat4{
            {
                scale[index].x * (c2 * c3),
                scale[index].x * (c1 * s3 + c3 * s1 * s2),
                scale[index].x * (s1 * s3 - c1 * c3 * s2),
                0.0f,
            },
            {
                scale[index].y * (-c2 * s3),
                scale[index].y * (c1 * c3 - s1 * s2 * s3),
                scale[index].y * (c3 * s1 + c1 * s2 * s3),
                0.0f,
            },
            {
                scale[index].z * (s2),
                scale[index].z * (-c2 * s1),
                scale[index].z * (c1 * c2),
                0.0f,
            },
            {translation[index].x, translation[index].y, translation[index].z, 1.0f} };
    }

    //glm will convert mat3 to mat4 automatically
    glm::mat3 TransformArrayComponent::normalMatrix(uint32_t index)
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
    void TransformArrayComponent::ToBufferComponent(DataBufferComponent& bufferComponent)
    {
        for (int i = 0; i < maxIndex; i++) {
            TransformBufferUnit objData{};
            objData.modelMatrix = mat4(i);
            objData.normalMatrix = normalMatrix(i);

            bufferComponent.writeToBuffer(&objData, sizeof(objData), i * sizeof(TransformBufferUnit));
        }
        bufferHandle = bufferComponent.getBufferHandle();
        usingBuffer = true;
    }

    TextureComponent::~TextureComponent()
    {
        if (!duplicate) 
        {
            vkDestroyImageView(BGLDevice::device(), view, nullptr);
            vkDestroyImage(BGLDevice::device(), image, nullptr);
            vkDestroySampler(BGLDevice::device(), sampler, nullptr);
            vkFreeMemory(BGLDevice::device(), device_memory, nullptr);
        }
    }

}
