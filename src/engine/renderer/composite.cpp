#include "engine/renderer/bagel_renderer.hpp"
namespace bagel
{

    void BGLRenderer::prepareCompositeBuffer()
    {
        // LDR composite target (gamma-encoded -> UNORM). Same single-color render-pass shape as the
        // other post buffers; finalLayout SHADER_READ so SMAA edge/neighborhood passes sample it.
        auto sc = bglSwapChain->getSwapChainExtent();
        compositeBuffer.width = sc.width;
        compositeBuffer.height = sc.height;

        createAttachment(VK_FORMAT_R8G8B8A8_UNORM,
                         VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                         &compositeBuffer.color,
                         compositeBuffer.width, compositeBuffer.height);

        VkAttachmentDescription desc{};
        desc.format = VK_FORMAT_R8G8B8A8_UNORM;
        desc.samples = VK_SAMPLE_COUNT_1_BIT;
        desc.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        desc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        desc.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        desc.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        desc.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        desc.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorRef;

        std::array<VkSubpassDependency, 2> deps{};
        deps[0] = {VK_SUBPASS_EXTERNAL, 0,
                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                   VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT,
                   VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                   VK_DEPENDENCY_BY_REGION_BIT};
        deps[1] = {0, VK_SUBPASS_EXTERNAL,
                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                   VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                   VK_ACCESS_SHADER_READ_BIT,
                   VK_DEPENDENCY_BY_REGION_BIT};

        VkRenderPassCreateInfo rpInfo{};
        rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpInfo.attachmentCount = 1;
        rpInfo.pAttachments = &desc;
        rpInfo.subpassCount = 1;
        rpInfo.pSubpasses = &subpass;
        rpInfo.dependencyCount = static_cast<uint32_t>(deps.size());
        rpInfo.pDependencies = deps.data();
        VK_CHECK(vkCreateRenderPass(BGLDevice::device(), &rpInfo, nullptr, &compositeBuffer.renderPass));

        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass = compositeBuffer.renderPass;
        fbInfo.attachmentCount = 1;
        fbInfo.pAttachments = &compositeBuffer.color.view;
        fbInfo.width = compositeBuffer.width;
        fbInfo.height = compositeBuffer.height;
        fbInfo.layers = 1;
        VK_CHECK(vkCreateFramebuffer(BGLDevice::device(), &fbInfo, nullptr, &compositeBuffer.frameBuffer));

        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = samplerInfo.minFilter = VK_FILTER_LINEAR; // neighborhood blend needs bilinear
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.addressModeU = samplerInfo.addressModeV = samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.maxAnisotropy = 1.0f;
        samplerInfo.maxLod = 1.0f;
        samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        VK_CHECK(vkCreateSampler(BGLDevice::device(), &samplerInfo, nullptr, &compositeBuffer.sampler));
    }

    void BGLRenderer::destroyCompositeBuffer()
    {
        vkDestroySampler(BGLDevice::device(), compositeBuffer.sampler, nullptr);
        vkDestroyRenderPass(BGLDevice::device(), compositeBuffer.renderPass, nullptr);
        vkDestroyFramebuffer(BGLDevice::device(), compositeBuffer.frameBuffer, nullptr);
        vkDestroyImageView(BGLDevice::device(), compositeBuffer.color.view, nullptr);
        vkDestroyImage(BGLDevice::device(), compositeBuffer.color.image, nullptr);
        vkFreeMemory(BGLDevice::device(), compositeBuffer.color.mem, nullptr);
        compositeBuffer.sampler = VK_NULL_HANDLE;
        compositeBuffer.renderPass = VK_NULL_HANDLE;
        compositeBuffer.frameBuffer = VK_NULL_HANDLE;
        compositeBuffer.color.view = VK_NULL_HANDLE;
        compositeBuffer.color.image = VK_NULL_HANDLE;
        compositeBuffer.color.mem = VK_NULL_HANDLE;
    }

    void BGLRenderer::beginCompositePass(VkCommandBuffer commandBuffer)
    {
        assert(isFrameStarted);
        VkRenderPassBeginInfo rpInfo{};
        rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpInfo.renderPass = compositeBuffer.renderPass;
        rpInfo.framebuffer = compositeBuffer.frameBuffer;
        rpInfo.renderArea = {{0, 0}, {compositeBuffer.width, compositeBuffer.height}};
        VkClearValue cv{};
        cv.color = {0, 0, 0, 1};
        rpInfo.clearValueCount = 1;
        rpInfo.pClearValues = &cv;
        vkCmdBeginRenderPass(commandBuffer, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);
        VkViewport vp{0, 0, (float)compositeBuffer.width, (float)compositeBuffer.height, 0, 1};
        VkRect2D sc{{0, 0}, {compositeBuffer.width, compositeBuffer.height}};
        vkCmdSetViewport(commandBuffer, 0, 1, &vp);
        vkCmdSetScissor(commandBuffer, 0, 1, &sc);
    }
}
