#include "bagel_ecs_components.hpp"

// vulkan headers
#include <vulkan/vulkan.h>

#include "bagel_buffer.hpp"
#include <memory>

// Rotation Order Definition. Jolt Quaternion returns XYZ rotation therefore we use Z1Y2X3 rotation matrix to match
#define X1Y2Z3

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
        const float c3 = glm::cos(rotation.z + localRotation.z);
        const float s3 = glm::sin(rotation.z + localRotation.z);
        const float c2 = glm::cos(rotation.y + localRotation.y);
        const float s2 = glm::sin(rotation.y + localRotation.y);
        const float c1 = glm::cos(rotation.x + localRotation.x);
        const float s1 = glm::sin(rotation.x + localRotation.x);

        return glm::mat4{
            {
                (scale.x * localScale.x) * (c2 * c3),
                (scale.x * localScale.x)* (c1 * s3 + c3 * s1 * s2),
                (scale.x * localScale.x)* (s1 * s3 - c1 * c3 * s2),
                0.0f,
            },
            {
                (scale.y * localScale.y) * (-c2 * s3),
                (scale.y * localScale.y) * (c1 * c3 - s1 * s2 * s3),
                (scale.y * localScale.y) * (c3 * s1 + c1 * s2 * s3),
                0.0f,
            },
            {
                (scale.z * localScale.z) * (s2),
                (scale.z * localScale.z) * (-c2 * s1),
                (scale.z * localScale.z) * (c1 * c2),
                0.0f,
            },
            {translation.x + localTranslation.x, translation.y + localTranslation.y, translation.z + localTranslation.z, 1.0f} };
    }

    glm::mat4 TransformComponent::mat4Scaled(glm::vec3 scale)
    {
        //X1Y2Z3 
        //Jolt Quaternion returns XYZ rotation
        const float c3 = glm::cos(rotation.z + localRotation.z);
        const float s3 = glm::sin(rotation.z + localRotation.z);
        const float c2 = glm::cos(rotation.y + localRotation.y);
        const float s2 = glm::sin(rotation.y + localRotation.y);
        const float c1 = glm::cos(rotation.x + localRotation.x);
        const float s1 = glm::sin(rotation.x + localRotation.x);

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
            {translation.x + localTranslation.x, translation.y + localTranslation.y, translation.z + localTranslation.z, 1.0f} };
    }

    glm::mat3 TransformComponent::normalMatrix()
    {
        const float c3 = glm::cos(rotation.z + localRotation.z);
        const float s3 = glm::sin(rotation.z + localRotation.z);
        const float c2 = glm::cos(rotation.y + localRotation.y);
        const float s2 = glm::sin(rotation.y + localRotation.y);
        const float c1 = glm::cos(rotation.x + localRotation.x);
        const float s1 = glm::sin(rotation.x + localRotation.x);
        const glm::vec3 invScale = 1.0f / glm::vec3(scale.x * localScale.x, scale.y * localScale.y, scale.z * localScale.z);
        return glm::mat3{
            {
                invScale.x * (c2 * c3),
                invScale.x * (c1 * s3 + c3 * s1 * s2),
                invScale.x * (s1 * s3 - c1 * c3 * s2),
            },
            {
                invScale.y * (-c2 * s3),
                invScale.y * (c1 * c3 - s1 * s2 * s3),
                invScale.y * (c3 * s1 + c1 * s2 * s3),
            },
            {
                invScale.z * (s2),
                invScale.z * (-c2 * s1),
                invScale.z * (c1 * c2),
            }};
    }

    glm::vec3 TransformComponent::getTranslation() const {
        return { translation.x,translation.y,translation.z };
    }

    void TransformComponent::setTranslation(const glm::vec3& _translation)
    {
        translation = _translation;
    }

    void TransformComponent::setScale(const glm::vec3& _scale)
    {
        scale = _scale;
    }

    void TransformComponent::setRotation(const glm::vec3& _rotation)
    {
        rotation = _rotation;
    }

    glm::vec3 TransformComponent::getLocalTranslation() const {
        return { localTranslation.x,localTranslation.y,localTranslation.z };
    }

    void TransformComponent::setLocalTranslation(const glm::vec3& _translation)
    {
        localTranslation = _translation;
    }

    void TransformComponent::setLocalScale(const glm::vec3& _scale)
    {
        localScale = _scale;
    }

    void TransformComponent::setLocalRotation(const glm::vec3& _rotation)
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
