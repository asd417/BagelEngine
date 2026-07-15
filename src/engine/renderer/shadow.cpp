#include "engine/renderer/bagel_renderer.hpp"
#include <iostream>
namespace bagel
{

    void BGLRenderer::prepareShadowMapBuffer()
    {
        VkFormat depthFormat = bglSwapChain->findDepthFormat();
        VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        if (depthFormat >= VK_FORMAT_D16_UNORM_S8_UINT)
            aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;

        // One depth image per cascade, each at its own resolution
        VkImageCreateInfo image{};
        image.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        image.imageType = VK_IMAGE_TYPE_2D;
        image.format = depthFormat;
        image.mipLevels = 1;
        image.arrayLayers = 1;
        image.samples = VK_SAMPLE_COUNT_1_BIT;
        image.tiling = VK_IMAGE_TILING_OPTIMAL;
        image.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = depthFormat;

        for (uint32_t i = 0; i < ShadowMapBuffer::CASCADE_COUNT; i++)
        {
            shadowMapBuffer.depth[i].format = depthFormat;

            image.extent = {ShadowMapBuffer::RESOLUTIONS[i], ShadowMapBuffer::RESOLUTIONS[i], 1};
            VK_CHECK(vkCreateImage(BGLDevice::device(), &image, nullptr, &shadowMapBuffer.depth[i].image));

            VkMemoryRequirements memReqs;
            vkGetImageMemoryRequirements(BGLDevice::device(), shadowMapBuffer.depth[i].image, &memReqs);

            VkMemoryAllocateInfo memAlloc{};
            memAlloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            memAlloc.allocationSize = memReqs.size;
            memAlloc.memoryTypeIndex = bglDevice.findMemoryType(memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            VK_CHECK(vkAllocateMemory(BGLDevice::device(), &memAlloc, nullptr, &shadowMapBuffer.depth[i].mem));
            VK_CHECK(vkBindImageMemory(BGLDevice::device(), shadowMapBuffer.depth[i].image, shadowMapBuffer.depth[i].mem, 0));

            viewInfo.image = shadowMapBuffer.depth[i].image;
            viewInfo.subresourceRange = {aspectMask, 0, 1, 0, 1};
            VK_CHECK(vkCreateImageView(BGLDevice::device(), &viewInfo, nullptr, &shadowMapBuffer.depth[i].view));
        }

        VkAttachmentDescription depthDesc{};
        depthDesc.format = depthFormat;
        depthDesc.samples = VK_SAMPLE_COUNT_1_BIT;
        depthDesc.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthDesc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depthDesc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depthDesc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthDesc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depthDesc.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

        VkAttachmentReference depthRef{0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 0;
        subpass.pDepthStencilAttachment = &depthRef;

        // The lighting pass samples the shadow map at arbitrary UVs, so these
        // dependencies must be framebuffer-global (no BY_REGION).
        std::array<VkSubpassDependency, 2> deps{};
        deps[0] = {VK_SUBPASS_EXTERNAL, 0,
                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                   VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                   VK_ACCESS_SHADER_READ_BIT,
                   VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                   0};
        deps[1] = {0, VK_SUBPASS_EXTERNAL,
                   VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                   VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                   VK_ACCESS_SHADER_READ_BIT,
                   0};

        VkRenderPassCreateInfo rpInfo{};
        rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpInfo.attachmentCount = 1;
        rpInfo.pAttachments = &depthDesc;
        rpInfo.subpassCount = 1;
        rpInfo.pSubpasses = &subpass;
        rpInfo.dependencyCount = static_cast<uint32_t>(deps.size());
        rpInfo.pDependencies = deps.data();
        VK_CHECK(vkCreateRenderPass(BGLDevice::device(), &rpInfo, nullptr, &shadowMapBuffer.renderPass));

        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass = shadowMapBuffer.renderPass;
        fbInfo.attachmentCount = 1;
        fbInfo.layers = 1;
        for (uint32_t i = 0; i < ShadowMapBuffer::CASCADE_COUNT; i++)
        {
            fbInfo.width = ShadowMapBuffer::RESOLUTIONS[i];
            fbInfo.height = ShadowMapBuffer::RESOLUTIONS[i];
            fbInfo.pAttachments = &shadowMapBuffer.depth[i].view;
            VK_CHECK(vkCreateFramebuffer(BGLDevice::device(), &fbInfo, nullptr, &shadowMapBuffer.frameBuffers[i]));
        }

        // Compare sampler for sampler2DShadow in the lighting pass
        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        samplerInfo.compareEnable = VK_TRUE;
        samplerInfo.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        samplerInfo.maxAnisotropy = 1.0f;
        samplerInfo.maxLod = 1.0f;
        VK_CHECK(vkCreateSampler(BGLDevice::device(), &samplerInfo, nullptr, &shadowMapBuffer.sampler));
    }

    void BGLRenderer::beginShadowMapPass(VkCommandBuffer commandBuffer, uint32_t cascade)
    {
        assert(isFrameStarted);
        assert(cascade < ShadowMapBuffer::CASCADE_COUNT);
        VkRenderPassBeginInfo rpInfo{};
        rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpInfo.renderPass = shadowMapBuffer.renderPass;
        rpInfo.framebuffer = shadowMapBuffer.frameBuffers[cascade];
        uint32_t res = ShadowMapBuffer::RESOLUTIONS[cascade];
        rpInfo.renderArea = {{0, 0}, {res, res}};
        VkClearValue cv{};
        cv.depthStencil = {1.0f, 0};
        rpInfo.clearValueCount = 1;
        rpInfo.pClearValues = &cv;
        vkCmdBeginRenderPass(commandBuffer, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);
        VkViewport vp{0, 0, (float)res, (float)res, 0.0f, 1.0f};
        VkRect2D sc{{0, 0}, {res, res}};
        vkCmdSetViewport(commandBuffer, 0, 1, &vp);
        vkCmdSetScissor(commandBuffer, 0, 1, &sc);
    }
}