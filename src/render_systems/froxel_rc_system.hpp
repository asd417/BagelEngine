#pragma once
#include <vulkan/vulkan.h>
#include <glm/glm.hpp>
#include <array>

#include "../bagel_engine_device.hpp"

namespace bagel {

struct FroxelPushConstant {
    glm::ivec3 gridSize;
    int        cascadeIndex;
    float      rangeMax;
    float      blendAlpha;
    uint32_t   radiosityHandle;
    int        isCoarsest;
};

class FroxelRCSystem {
public:
    static constexpr int CASCADE_COUNT = 3;

    // Grid dimensions per cascade — local_size (8,4,4) divides each evenly.
    static constexpr int GRID_X[CASCADE_COUNT] = { 32, 16,  8 };
    static constexpr int GRID_Y[CASCADE_COUNT] = { 16,  8,  4 };
    static constexpr int GRID_Z[CASCADE_COUNT] = { 16,  8,  4 };

    // World-space ray range for cascade 0; each coarser level × 4.
    static constexpr float BASE_RANGE = 0.5f;

    FroxelRCSystem(BGLDevice& device, VkDescriptorSetLayout globalSetLayout);
    ~FroxelRCSystem();

    FroxelRCSystem(const FroxelRCSystem&) = delete;
    FroxelRCSystem& operator=(const FroxelRCSystem&) = delete;

    // Layout for set 1 of the radiosity graphics pipeline (sampler3D read).
    VkDescriptorSetLayout getSamplerSetLayout() const { return samplerSetLayout; }

    // Descriptor set to bind at set 1 during the radiosity pass.
    VkDescriptorSet getSamplerDescriptorSet() const { return samplerDescSet; }

    // Dispatch compute updates for all three cascades (coarsest first).
    // Must be called outside any render pass.
    void update(VkCommandBuffer cmd,
                VkDescriptorSet globalDescriptorSet,
                uint32_t        radiosityHandle,
                float           blendAlpha = 0.05f);

private:
    void createCascadeImages();
    void createSampler();
    void createDescriptorLayouts();
    void createDescriptorPool();
    void allocateAndWriteDescriptorSets();
    void createComputePipeline(VkDescriptorSetLayout globalSetLayout);
    void transitionImagesToGeneral();

    BGLDevice& bglDevice;

    struct CascadeImage {
        VkImage        image  = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView    view   = VK_NULL_HANDLE;
    };
    CascadeImage cascades[CASCADE_COUNT];
    VkSampler    trilinearSampler = VK_NULL_HANDLE;

    // Raw Vulkan descriptor objects (no bindless/variable-count flags)
    VkDescriptorPool      descPool           = VK_NULL_HANDLE;
    VkDescriptorSetLayout computeSetLayout   = VK_NULL_HANDLE;
    VkDescriptorSetLayout samplerSetLayout   = VK_NULL_HANDLE;
    VkDescriptorSet       computeDescSets[CASCADE_COUNT];
    VkDescriptorSet       samplerDescSet     = VK_NULL_HANDLE;

    // Compute pipeline
    VkShaderModule   compShaderModule   = VK_NULL_HANDLE;
    VkPipelineLayout compPipelineLayout = VK_NULL_HANDLE;
    VkPipeline       compPipeline       = VK_NULL_HANDLE;
};

} // namespace bagel
