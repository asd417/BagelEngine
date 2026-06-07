#include "froxel_rc_system.hpp"
#include "../bagel_util.hpp"

#include <fstream>
#include <stdexcept>
#include <iostream>
#include <array>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

namespace bagel {

static std::vector<char> readSPV(const std::string& path)
{
    std::ifstream f(path, std::ios::ate | std::ios::binary);
    if (!f.is_open())
        throw std::runtime_error("FroxelRCSystem: cannot open shader: " + path);
    size_t sz = static_cast<size_t>(f.tellg());
    std::vector<char> buf(sz);
    f.seekg(0);
    f.read(buf.data(), sz);
    return buf;
}

// ── constructor / destructor ─────────────────────────────────────────────────

FroxelRCSystem::FroxelRCSystem(BGLDevice& device, VkDescriptorSetLayout globalSetLayout)
    : bglDevice{ device }
{
    std::cout << "Creating FroxelRCSystem\n";
    createCascadeImages();
    createSampler();
    createDescriptorLayouts();
    createDescriptorPool();
    allocateAndWriteDescriptorSets();
    createComputePipeline(globalSetLayout);
    transitionImagesToGeneral();
}

FroxelRCSystem::~FroxelRCSystem()
{
    VkDevice dev = BGLDevice::device();
    vkDestroyPipeline(dev, compPipeline, nullptr);
    vkDestroyPipelineLayout(dev, compPipelineLayout, nullptr);
    vkDestroyShaderModule(dev, compShaderModule, nullptr);
    vkDestroyDescriptorPool(dev, descPool, nullptr);
    vkDestroyDescriptorSetLayout(dev, computeSetLayout, nullptr);
    vkDestroyDescriptorSetLayout(dev, samplerSetLayout, nullptr);
    for (int i = 0; i < CASCADE_COUNT; i++) {
        vkDestroyImageView(dev, cascades[i].view, nullptr);
        vkDestroyImage(dev, cascades[i].image, nullptr);
        vkFreeMemory(dev, cascades[i].memory, nullptr);
    }
    vkDestroySampler(dev, trilinearSampler, nullptr);
}

// ── cascade 3D image creation ─────────────────────────────────────────────────

void FroxelRCSystem::createCascadeImages()
{
    VkDevice dev = BGLDevice::device();
    for (int c = 0; c < CASCADE_COUNT; c++) {
        VkImageCreateInfo imgInfo{};
        imgInfo.sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imgInfo.imageType     = VK_IMAGE_TYPE_3D;
        imgInfo.format        = VK_FORMAT_R16G16B16A16_SFLOAT;
        imgInfo.extent        = { (uint32_t)GRID_X[c], (uint32_t)GRID_Y[c], (uint32_t)GRID_Z[c] };
        imgInfo.mipLevels     = 1;
        imgInfo.arrayLayers   = 1;
        imgInfo.samples       = VK_SAMPLE_COUNT_1_BIT;
        imgInfo.tiling        = VK_IMAGE_TILING_OPTIMAL;
        imgInfo.usage         = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imgInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imgInfo.sharingMode   = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateImage(dev, &imgInfo, nullptr, &cascades[c].image) != VK_SUCCESS)
            throw std::runtime_error("FroxelRCSystem: failed to create cascade image");

        VkMemoryRequirements req;
        vkGetImageMemoryRequirements(dev, cascades[c].image, &req);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize  = req.size;
        allocInfo.memoryTypeIndex = bglDevice.findMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (vkAllocateMemory(dev, &allocInfo, nullptr, &cascades[c].memory) != VK_SUCCESS)
            throw std::runtime_error("FroxelRCSystem: failed to allocate cascade memory");
        vkBindImageMemory(dev, cascades[c].image, cascades[c].memory, 0);

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType                           = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image                           = cascades[c].image;
        viewInfo.viewType                        = VK_IMAGE_VIEW_TYPE_3D;
        viewInfo.format                          = VK_FORMAT_R16G16B16A16_SFLOAT;
        viewInfo.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel   = 0;
        viewInfo.subresourceRange.levelCount     = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount     = 1;
        if (vkCreateImageView(dev, &viewInfo, nullptr, &cascades[c].view) != VK_SUCCESS)
            throw std::runtime_error("FroxelRCSystem: failed to create cascade image view");
    }
}

void FroxelRCSystem::createSampler()
{
    VkSamplerCreateInfo info{};
    info.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    info.magFilter    = VK_FILTER_LINEAR;
    info.minFilter    = VK_FILTER_LINEAR;
    info.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    info.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    info.maxAnisotropy = 1.0f;
    info.borderColor  = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    info.minLod = 0.0f;
    info.maxLod = 0.0f;
    if (vkCreateSampler(BGLDevice::device(), &info, nullptr, &trilinearSampler) != VK_SUCCESS)
        throw std::runtime_error("FroxelRCSystem: failed to create sampler");
}

// ── descriptor layouts ────────────────────────────────────────────────────────

void FroxelRCSystem::createDescriptorLayouts()
{
    VkDevice dev = BGLDevice::device();

    // Compute layout: binding 0 = write image, binding 1 = coarser image
    {
        std::array<VkDescriptorSetLayoutBinding, 2> bindings{};
        for (int b = 0; b < 2; b++) {
            bindings[b].binding         = b;
            bindings[b].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            bindings[b].descriptorCount = 1;
            bindings[b].stageFlags      = VK_SHADER_STAGE_COMPUTE_BIT;
        }
        VkDescriptorSetLayoutCreateInfo info{};
        info.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        info.bindingCount = static_cast<uint32_t>(bindings.size());
        info.pBindings    = bindings.data();
        if (vkCreateDescriptorSetLayout(dev, &info, nullptr, &computeSetLayout) != VK_SUCCESS)
            throw std::runtime_error("FroxelRCSystem: failed to create compute set layout");
    }

    // Sampler layout: binding 0 = sampler3D for cascade 0
    {
        VkDescriptorSetLayoutBinding binding{};
        binding.binding         = 0;
        binding.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding.descriptorCount = 1;
        binding.stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
        VkDescriptorSetLayoutCreateInfo info{};
        info.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        info.bindingCount = 1;
        info.pBindings    = &binding;
        if (vkCreateDescriptorSetLayout(dev, &info, nullptr, &samplerSetLayout) != VK_SUCCESS)
            throw std::runtime_error("FroxelRCSystem: failed to create sampler set layout");
    }
}

void FroxelRCSystem::createDescriptorPool()
{
    std::array<VkDescriptorPoolSize, 2> sizes{};
    sizes[0] = { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          CASCADE_COUNT * 2 };
    sizes[1] = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1 };

    VkDescriptorPoolCreateInfo info{};
    info.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    info.maxSets       = CASCADE_COUNT + 1;
    info.poolSizeCount = static_cast<uint32_t>(sizes.size());
    info.pPoolSizes    = sizes.data();
    if (vkCreateDescriptorPool(BGLDevice::device(), &info, nullptr, &descPool) != VK_SUCCESS)
        throw std::runtime_error("FroxelRCSystem: failed to create descriptor pool");
}

void FroxelRCSystem::allocateAndWriteDescriptorSets()
{
    VkDevice dev = BGLDevice::device();

    // Compute descriptor sets (one per cascade)
    std::array<VkDescriptorSetLayout, CASCADE_COUNT> computeLayouts;
    computeLayouts.fill(computeSetLayout);
    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool     = descPool;
    allocInfo.descriptorSetCount = CASCADE_COUNT;
    allocInfo.pSetLayouts        = computeLayouts.data();
    if (vkAllocateDescriptorSets(dev, &allocInfo, computeDescSets) != VK_SUCCESS)
        throw std::runtime_error("FroxelRCSystem: failed to allocate compute descriptor sets");

    for (int c = 0; c < CASCADE_COUNT; c++) {
        int coarserIdx = (c + 1 < CASCADE_COUNT) ? (c + 1) : c;
        std::array<VkDescriptorImageInfo, 2> imgInfos{};
        imgInfos[0] = { VK_NULL_HANDLE, cascades[c].view,          VK_IMAGE_LAYOUT_GENERAL };
        imgInfos[1] = { VK_NULL_HANDLE, cascades[coarserIdx].view,  VK_IMAGE_LAYOUT_GENERAL };

        std::array<VkWriteDescriptorSet, 2> writes{};
        for (int b = 0; b < 2; b++) {
            writes[b].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[b].dstSet          = computeDescSets[c];
            writes[b].dstBinding      = b;
            writes[b].descriptorCount = 1;
            writes[b].descriptorType  = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            writes[b].pImageInfo      = &imgInfos[b];
        }
        vkUpdateDescriptorSets(dev, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
    }

    // Sampler descriptor set
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts        = &samplerSetLayout;
    if (vkAllocateDescriptorSets(dev, &allocInfo, &samplerDescSet) != VK_SUCCESS)
        throw std::runtime_error("FroxelRCSystem: failed to allocate sampler descriptor set");

    VkDescriptorImageInfo samplerImgInfo{};
    samplerImgInfo.sampler     = trilinearSampler;
    samplerImgInfo.imageView   = cascades[0].view;
    samplerImgInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkWriteDescriptorSet samplerWrite{};
    samplerWrite.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    samplerWrite.dstSet          = samplerDescSet;
    samplerWrite.dstBinding      = 0;
    samplerWrite.descriptorCount = 1;
    samplerWrite.descriptorType  = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    samplerWrite.pImageInfo      = &samplerImgInfo;
    vkUpdateDescriptorSets(dev, 1, &samplerWrite, 0, nullptr);
}

// ── compute pipeline ─────────────────────────────────────────────────────────

void FroxelRCSystem::createComputePipeline(VkDescriptorSetLayout globalSetLayout)
{
    VkDevice dev = BGLDevice::device();

    auto code = readSPV(util::enginePath("/shaders/froxel_rc_update.comp.spv"));
    VkShaderModuleCreateInfo moduleInfo{};
    moduleInfo.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    moduleInfo.codeSize = code.size();
    moduleInfo.pCode    = reinterpret_cast<const uint32_t*>(code.data());
    if (vkCreateShaderModule(dev, &moduleInfo, nullptr, &compShaderModule) != VK_SUCCESS)
        throw std::runtime_error("FroxelRCSystem: failed to create compute shader module");

    std::array<VkDescriptorSetLayout, 2> setLayouts = { globalSetLayout, computeSetLayout };

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushRange.offset     = 0;
    pushRange.size       = sizeof(FroxelPushConstant);

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount         = static_cast<uint32_t>(setLayouts.size());
    layoutInfo.pSetLayouts            = setLayouts.data();
    layoutInfo.pushConstantRangeCount = 1;
    layoutInfo.pPushConstantRanges    = &pushRange;
    if (vkCreatePipelineLayout(dev, &layoutInfo, nullptr, &compPipelineLayout) != VK_SUCCESS)
        throw std::runtime_error("FroxelRCSystem: failed to create compute pipeline layout");

    VkPipelineShaderStageCreateInfo stageInfo{};
    stageInfo.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stageInfo.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = compShaderModule;
    stageInfo.pName  = "main";

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage  = stageInfo;
    pipelineInfo.layout = compPipelineLayout;
    if (vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &compPipeline) != VK_SUCCESS)
        throw std::runtime_error("FroxelRCSystem: failed to create compute pipeline");
}

// ── image layout transition ───────────────────────────────────────────────────

void FroxelRCSystem::transitionImagesToGeneral()
{
    bglDevice.immediateUpload([this](VkCommandBuffer cmd) {
        std::array<VkImageMemoryBarrier, CASCADE_COUNT> barriers;
        for (int c = 0; c < CASCADE_COUNT; c++) {
            barriers[c] = {};
            barriers[c].sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barriers[c].oldLayout           = VK_IMAGE_LAYOUT_UNDEFINED;
            barriers[c].newLayout           = VK_IMAGE_LAYOUT_GENERAL;
            barriers[c].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barriers[c].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barriers[c].image               = cascades[c].image;
            barriers[c].subresourceRange    = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            barriers[c].srcAccessMask       = 0;
            barriers[c].dstAccessMask       = VK_ACCESS_SHADER_WRITE_BIT;
        }
        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, 0, nullptr, 0, nullptr,
            CASCADE_COUNT, barriers.data());
    });
}

// ── update dispatch ───────────────────────────────────────────────────────────

void FroxelRCSystem::update(VkCommandBuffer cmd,
                             VkDescriptorSet globalDescriptorSet,
                             uint32_t        radiosityHandle,
                             float           blendAlpha)
{
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, compPipeline);

    // Dispatch coarsest cascade first so the merge chain is ready when finer cascades run.
    for (int c = CASCADE_COUNT - 1; c >= 0; c--) {
        std::array<VkDescriptorSet, 2> sets = { globalDescriptorSet, computeDescSets[c] };
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, compPipelineLayout,
            0, static_cast<uint32_t>(sets.size()), sets.data(), 0, nullptr);

        float rangeForCascade = BASE_RANGE;
        for (int k = 0; k < c; k++) rangeForCascade *= 4.0f;

        FroxelPushConstant push{};
        push.gridSize        = glm::ivec3(GRID_X[c], GRID_Y[c], GRID_Z[c]);
        push.cascadeIndex    = c;
        push.rangeMax        = rangeForCascade;
        push.blendAlpha      = blendAlpha;
        push.radiosityHandle = radiosityHandle;
        push.isCoarsest      = (c == CASCADE_COUNT - 1) ? 1 : 0;
        vkCmdPushConstants(cmd, compPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT,
            0, sizeof(FroxelPushConstant), &push);

        vkCmdDispatch(cmd, GRID_X[c] / 8, GRID_Y[c] / 4, GRID_Z[c] / 4);

        // Barrier between cascade dispatches to flush writes before the next read
        if (c > 0) {
            VkMemoryBarrier memBarrier{};
            memBarrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            memBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            memBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            vkCmdPipelineBarrier(cmd,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                0, 1, &memBarrier, 0, nullptr, 0, nullptr);
        }
    }

    // Final barrier: compute write → fragment shader read (radiosity pass)
    VkMemoryBarrier finalBarrier{};
    finalBarrier.sType         = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    finalBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    finalBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 1, &finalBarrier, 0, nullptr, 0, nullptr);
}

} // namespace bagel
