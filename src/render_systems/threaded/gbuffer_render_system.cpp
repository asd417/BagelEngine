#include "render_systems/threaded/gbuffer_render_system.hpp"
#include "math/bagel_math.hpp"

#include <iostream>
#include <vulkan/vulkan_core.h>
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

#include "ecs/components/model.hpp"
#include "ecs/components/planet.hpp"
#include "ecs/components/transform.hpp"

namespace bagel::threaded
{

// The render pass must exist before init(), which builds the framebuffer and the pipelines
// against it.
GBufferRenderSystem::GBufferRenderSystem(
    BGLDevice &device,
    BGLSwapChain &swapchain,
    std::unique_ptr<BGLBindlessDescriptorManager> const &_descriptorManager,
    entt::registry &_registry)
    : BGLThreadedRenderSystem{device}, registry{_registry}, descriptorManager{_descriptorManager}
{
    depthsFormat = swapchain.findDepthFormat();
    std::cout << "Creating GBuffer Render System\n";
    createSampler();
    createRenderPass();
}
GBufferRenderSystem::~GBufferRenderSystem()
{
    shutdown(); // the child class destructor must call shutdown at the start to stop thread and wait for the gpu
    vkDestroySampler(BGLDevice::device(), sampler, nullptr);
}
void GBufferRenderSystem::createRenderPass()
{
    // Set up separate renderpass with references to the color and depth attachments
    // Attachment order: 0=normal, 1=albedo, 2=emission, 3=depth
    std::array<VkAttachmentDescription, 4> attachmentDescs = {};

    for (uint32_t i = 0; i < 4; ++i)
    {
        attachmentDescs[i].samples = VK_SAMPLE_COUNT_1_BIT;
        attachmentDescs[i].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachmentDescs[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachmentDescs[i].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachmentDescs[i].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        if (i == 3)
        {
            attachmentDescs[i].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            attachmentDescs[i].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        }
        else
        {
            attachmentDescs[i].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            attachmentDescs[i].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        }
    }

    // Formats
    attachmentDescs[0].format = normalsFormat;
    attachmentDescs[1].format = albedoFormat;
    attachmentDescs[2].format = emissionFormat;
    attachmentDescs[3].format = depthsFormat;

    std::vector<VkAttachmentReference> colorReferences;
    colorReferences.push_back({0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});
    colorReferences.push_back({1, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});
    colorReferences.push_back({2, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL});

    VkAttachmentReference depthReference = {};
    depthReference.attachment = 3;
    depthReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.pColorAttachments = colorReferences.data();
    subpass.colorAttachmentCount = static_cast<uint32_t>(colorReferences.size());
    subpass.pDepthStencilAttachment = &depthReference;

    // Use subpass dependencies for attachment layout transitions
    std::array<VkSubpassDependency, 2> dependencies;

    dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[0].dstSubpass = 0;
    dependencies[0].srcStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[0].srcAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[0].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    dependencies[1].srcSubpass = 0;
    dependencies[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependencies[1].dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    dependencies[1].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
    dependencies[1].dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;

    VkRenderPassCreateInfo renderPassInfo = {};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.pAttachments = attachmentDescs.data();
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachmentDescs.size());
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 2;
    renderPassInfo.pDependencies = dependencies.data();
    VK_CHECK(vkCreateRenderPass(BGLDevice::device(), &renderPassInfo, nullptr, &renderPass));
}

void GBufferRenderSystem::createFrameBuffer(VkExtent2D extent)
{
    frameBufferExtent = extent;
    const uint32_t dw = extent.width;
    const uint32_t dh = extent.height;
    // (World space) Normals — oct-encoded xy + roughness + metallic, 16 bits each
    createAttachment(normalsFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, &FBAnormal, dw, dh);
    // Albedo (color)
    createAttachment(albedoFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, &FBAalbedo, dw, dh);
    // Emission (RGB, sRGB-encoded)
    createAttachment(emissionFormat, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT, &FBAemission, dw, dh);
    // Depth — also sampled in the lighting pass for position reconstruction
    createAttachment(depthsFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, &FBAdepth, dw, dh);

    std::array<VkImageView, 4> attachments;
    attachments[0] = FBAnormal.view;
    attachments[1] = FBAalbedo.view;
    attachments[2] = FBAemission.view;
    attachments[3] = FBAdepth.view;

    VkFramebufferCreateInfo fbufCreateInfo = {};
    fbufCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    fbufCreateInfo.pNext = NULL;
    fbufCreateInfo.renderPass = renderPass;
    fbufCreateInfo.pAttachments = attachments.data();
    fbufCreateInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    fbufCreateInfo.width = dw;
    fbufCreateInfo.height = dh;
    fbufCreateInfo.layers = 1;
    VK_CHECK(vkCreateFramebuffer(BGLDevice::device(), &fbufCreateInfo, nullptr, &frameBuffer));
}
void GBufferRenderSystem::createSampler()
{
    // Create sampler to sample from the color attachments
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.maxAnisotropy = 1.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 1.0f;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    VK_CHECK(vkCreateSampler(BGLDevice::device(), &samplerInfo, nullptr, &sampler));
}
void GBufferRenderSystem::destroyFrameBufferAttachments()
{
    FBAnormal.destroy();
    FBAalbedo.destroy();
    FBAemission.destroy();
    FBAdepth.destroy();
}
void GBufferRenderSystem::beginRenderPass()
{
    VkRenderPassBeginInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassInfo.renderPass = renderPass;
    renderPassInfo.framebuffer = frameBuffer;
    renderPassInfo.renderArea.offset = {0, 0};
    renderPassInfo.renderArea.extent = frameBufferExtent;

    // 4 clear values: normal, albedo, emission, depth
    std::array<VkClearValue, 4> clearValues{};
    clearValues[0].color = {{0.0f, 0.0f, 0.0f, 0.0f}}; // normal + roughness
    clearValues[1].color = {{0.0f, 0.0f, 0.0f, 0.0f}}; // albedo (w=0 = background)
    clearValues[2].color = {{0.0f, 0.0f, 0.0f, 0.0f}}; // emission
    clearValues[3].depthStencil = {1.0f, 0};           // depth
    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(frameBufferExtent.width);
    viewport.height = static_cast<float>(frameBufferExtent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    VkRect2D scissor{
        {0, 0},
        frameBufferExtent};
    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
}

// Runs on the worker thread. worker() has begun the command buffer and entered the render
// pass, but binds no pipeline and no descriptor set — both are this function's job.
// TODO: bglPipelines[i]->bind(commandBuffer) + vkCmdBindDescriptorSets(set 0) are still
// missing here, so the draws below currently record against no bound pipeline.
void GBufferRenderSystem::render(const FrameInfo *frameInfo)
{
    Frustum frustum;
    frustum.extractFromVP(frameInfo->camera.getProjection() * frameInfo->camera.getView());

    VkDeviceSize offsets[] = {0};

    auto singleGroup = registry.view<TransformComponent, ModelComponent>();
    for (auto [entity, transform, model] : singleGroup.each())
    {
        if (model.mesh().isSkinned)
            continue; // skinned models are drawn by AnimatedGBufferRenderSystem
        if (registry.all_of<PlanetComponent>(entity))
            continue; // drawn by PlanetGBufferRenderSystem
        glm::mat4 modelMatrix = transform.getMat4();
        if (model.frustumCull && !frustum.testAABB(model.mesh().aabbMin, model.mesh().aabbMax, modelMatrix))
            continue;

        vkCmdBindVertexBuffers(commandBuffer, 0, 1, &model.mesh().vertexBuffer, offsets);
        if (model.mesh().indexCount > 0)
            vkCmdBindIndexBuffer(commandBuffer, model.mesh().indexBuffer, 0, VK_INDEX_TYPE_UINT32);

        GBufferPushConstantData push{};
        push.UsesBufferedTransform = 0;
        push.modelMatrix = modelMatrix;
        push.scale = glm::vec4{transform.getWorldScale(), 1.0f};
        push.fallbackAlbedoMap = frameInfo->fallbackAlbedoMap;
        push.materialRowBase = model.mesh().skinBase + model.skinIndex * model.mesh().numSlots;
        vkCmdPushConstants(commandBuffer, pipelineLayout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(GBufferPushConstantData), &push);
        // Solid submeshes only — transparent ones are drawn later in the forward pass.
        for (const ModelComponent::Submesh &sm : model.solidSubmeshes())
        {
            if (model.frustumCull && !frustum.testAABB(sm.aabbMin, sm.aabbMax, modelMatrix))
                continue;

            if (model.mesh().indexCount > 0)
                vkCmdDrawIndexed(commandBuffer, sm.indexCount, 1, sm.firstIndex, 0, 0);
            else
                vkCmdDraw(commandBuffer, sm.vertexCount, 1, sm.firstVertex, 0);
        }
    }

    auto instancedGroup = registry.view<TransformArrayComponent, ModelComponent>();
    for (auto [entity, transform, model] : instancedGroup.each())
    {
        if (model.mesh().isSkinned)
            continue; // skinned models are not instanced/buffered
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, &model.mesh().vertexBuffer, offsets);
        if (model.mesh().indexCount > 0)
            vkCmdBindIndexBuffer(commandBuffer, model.mesh().indexBuffer, 0, VK_INDEX_TYPE_UINT32);

        GBufferPushConstantData push{};
        push.UsesBufferedTransform = transform.useBuffer() ? 1 : 0;
        push.BufferedTransformHandle = transform.bufferHandle;
        if (!transform.useBuffer())
        {
            push.modelMatrix = transform.mat4(0);
            push.scale = glm::vec4{transform.getWorldScale(0), 1.0f};
        }
        push.materialRowBase = model.mesh().skinBase + model.skinIndex * model.mesh().numSlots;
        vkCmdPushConstants(commandBuffer, pipelineLayout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(GBufferPushConstantData), &push);
        // Solid submeshes only — transparent ones are drawn later in the forward pass.
        for (const ModelComponent::Submesh &sm : model.solidSubmeshes())
        {
            if (model.mesh().indexCount > 0)
                vkCmdDrawIndexed(commandBuffer, sm.indexCount, transform.count(), sm.firstIndex, 0, 0);
            else
                vkCmdDraw(commandBuffer, sm.vertexCount, transform.count(), sm.firstVertex, 0);
        }
    }
}

} // namespace bagel::threaded
