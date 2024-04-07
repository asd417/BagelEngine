#include "bagel_ecs_components.hpp"

// vulkan headers
#include <vulkan/vulkan.h>

#include "bagel_buffer.hpp"
#include "bagel_imgui.hpp"
#include <memory>

// Rotation Order Definition. Jolt Quaternion returns XYZ rotation therefore we use Z1Y2X3 rotation matrix to match
#define X1Y2Z3
#define CONSOLE ConsoleApp::Instance()

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

    //returns UNSCALED model matrix. Scale will be applied in shader
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
                scale.x * localScale.x * (c2 * c3),
                scale.x * localScale.x * (c1 * s3 + c3 * s1 * s2),
                scale.x * localScale.x * (s1 * s3 - c1 * c3 * s2),
                0.0f,
            },
            {
                scale.y * localScale.y * (-c2 * s3),
                scale.y * localScale.y * (c1 * c3 - s1 * s2 * s3),
                scale.y * localScale.y * (c3 * s1 + c1 * s2 * s3),
                0.0f,
            },
            {
                scale.z * localScale.z * (s2),
                scale.z * localScale.z * (-c2 * s1),
                scale.z * localScale.z * (c1 * c2),
                0.0f,
            },
            {translation.x + localTranslation.x, translation.y + localTranslation.y, translation.z + localTranslation.z, 1.0f} };
    }

    //Returns mat4 with inverse scale. Mostly obsolete since normal matrix will be calculated in shader;
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
    inline void TransformArrayComponent::addTransform(glm::vec3 _translation, glm::vec3 _scale, glm::vec3 _rotation)
    {
        if (maxIndex < MAX_TRANSFORM_PER_ENT) {
            _translation.y *= -1;
            translation[maxIndex] = _translation;
            scale[maxIndex] = _scale;
            rotation[maxIndex] = _rotation;
            maxIndex++;
        }
        else {
            char buff[64];
            sprintf(buff, "Transform Array Full. MAX_TRANSFORM_PER_ENT %d \n", MAX_TRANSFORM_PER_ENT);
            CONSOLE->Log("TransformArrayComponent::addTransform", buff);
        }
    }
    void TransformArrayComponent::ToBufferComponent(DataBufferComponent& bufferComponent)
    {
        for (int i = 0; i < maxIndex; i++) {
            TransformBufferUnit objData{};
            objData.modelMatrix = mat4(i);
            objData.scale = glm::vec4{ getWorldScale(i), 1.0f };

            bufferComponent.writeToBuffer(&objData, sizeof(objData), i * sizeof(TransformBufferUnit));
        }
        bufferHandle = bufferComponent.getBufferHandle();
        usingBuffer = true;
    }
    void ModelComponent::useDiffuseComponent(DiffuseTextureComponent& dfc) {
        for (int i = 0; i < submeshes.size(); i++) {
            setDiffuseTextureToSubmesh(i, dfc.textureHandle[i]);
        }
    }
    void ModelComponent::useEmissionComponent(EmissionTextureComponent& dfc) {
        for (int i = 0; i < submeshes.size(); i++) {
            setEmissionTextureToSubmesh(i, dfc.textureHandle[i]);
        }
    }
    void ModelComponent::useNormalComponent(NormalTextureComponent& dfc) {
        for (int i = 0; i < submeshes.size(); i++) {
            setNormalTextureToSubmesh(i, dfc.textureHandle[i]);
        }
    }
    void ModelComponent::useRoughMetalComponent(RoughnessMetalTextureComponent& dfc) {
        for (int i = 0; i < submeshes.size(); i++) {
            setRoughMetalTextureToSubmesh(i, dfc.textureHandle[i]);
        }
    }
}
