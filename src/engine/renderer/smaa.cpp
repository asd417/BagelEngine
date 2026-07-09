#include "engine/renderer/bagel_renderer.hpp"
namespace bagel
{

    void BGLRenderer::prepareSmaaEdgeBuffer()
    {
        // Single RG8 color target, screen-sized. Same render-pass shape as the radiosity buffer:
        // CLEAR on load (the edge shader discards non-edge pixels, so cleared 0 = "no edge"),
        // finalLayout SHADER_READ so later passes / the debug view can sample it.
        auto sc = bglSwapChain->getSwapChainExtent();
        smaaEdgeBuffer.width = sc.width;
        smaaEdgeBuffer.height = sc.height;

        createAttachment(VK_FORMAT_R8G8_UNORM,
                         VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                         &smaaEdgeBuffer.color,
                         smaaEdgeBuffer.width, smaaEdgeBuffer.height);

        VkAttachmentDescription desc{};
        desc.format = VK_FORMAT_R8G8_UNORM;
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
        VK_CHECK(vkCreateRenderPass(BGLDevice::device(), &rpInfo, nullptr, &smaaEdgeBuffer.renderPass));

        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass = smaaEdgeBuffer.renderPass;
        fbInfo.attachmentCount = 1;
        fbInfo.pAttachments = &smaaEdgeBuffer.color.view;
        fbInfo.width = smaaEdgeBuffer.width;
        fbInfo.height = smaaEdgeBuffer.height;
        fbInfo.layers = 1;
        VK_CHECK(vkCreateFramebuffer(BGLDevice::device(), &fbInfo, nullptr, &smaaEdgeBuffer.frameBuffer));

        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.addressModeU = samplerInfo.addressModeV = samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.maxAnisotropy = 1.0f;
        samplerInfo.maxLod = 1.0f;
        samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        VK_CHECK(vkCreateSampler(BGLDevice::device(), &samplerInfo, nullptr, &smaaEdgeBuffer.sampler));
    }

    void BGLRenderer::destroySmaaEdgeBuffer()
    {
        vkDestroySampler(BGLDevice::device(), smaaEdgeBuffer.sampler, nullptr);
        vkDestroyRenderPass(BGLDevice::device(), smaaEdgeBuffer.renderPass, nullptr);
        vkDestroyFramebuffer(BGLDevice::device(), smaaEdgeBuffer.frameBuffer, nullptr);
        vkDestroyImageView(BGLDevice::device(), smaaEdgeBuffer.color.view, nullptr);
        vkDestroyImage(BGLDevice::device(), smaaEdgeBuffer.color.image, nullptr);
        vkFreeMemory(BGLDevice::device(), smaaEdgeBuffer.color.mem, nullptr);
        smaaEdgeBuffer.sampler = VK_NULL_HANDLE;
        smaaEdgeBuffer.renderPass = VK_NULL_HANDLE;
        smaaEdgeBuffer.frameBuffer = VK_NULL_HANDLE;
        smaaEdgeBuffer.color.view = VK_NULL_HANDLE;
        smaaEdgeBuffer.color.image = VK_NULL_HANDLE;
        smaaEdgeBuffer.color.mem = VK_NULL_HANDLE;
    }

    void BGLRenderer::beginSmaaEdgePass(VkCommandBuffer commandBuffer)
    {
        assert(isFrameStarted);
        VkRenderPassBeginInfo rpInfo{};
        rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpInfo.renderPass = smaaEdgeBuffer.renderPass;
        rpInfo.framebuffer = smaaEdgeBuffer.frameBuffer;
        rpInfo.renderArea = {{0, 0}, {smaaEdgeBuffer.width, smaaEdgeBuffer.height}};
        VkClearValue cv{};
        cv.color = {0, 0, 0, 0};
        rpInfo.clearValueCount = 1;
        rpInfo.pClearValues = &cv;
        vkCmdBeginRenderPass(commandBuffer, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);
        VkViewport vp{0, 0, (float)smaaEdgeBuffer.width, (float)smaaEdgeBuffer.height, 0, 1};
        VkRect2D sc{{0, 0}, {smaaEdgeBuffer.width, smaaEdgeBuffer.height}};
        vkCmdSetViewport(commandBuffer, 0, 1, &vp);
        vkCmdSetScissor(commandBuffer, 0, 1, &sc);
    }

    void BGLRenderer::prepareSmaaWeightBuffer()
    {
        // RGBA8 blending-weight target, screen-sized. CLEAR on load (non-edge pixels = 0 weight),
        // finalLayout SHADER_READ so the neighborhood-blend pass / debug view can sample it.
        auto sc = bglSwapChain->getSwapChainExtent();
        smaaWeightBuffer.width = sc.width;
        smaaWeightBuffer.height = sc.height;

        createAttachment(VK_FORMAT_R8G8B8A8_UNORM,
                         VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                         &smaaWeightBuffer.color,
                         smaaWeightBuffer.width, smaaWeightBuffer.height);

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
        VK_CHECK(vkCreateRenderPass(BGLDevice::device(), &rpInfo, nullptr, &smaaWeightBuffer.renderPass));

        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass = smaaWeightBuffer.renderPass;
        fbInfo.attachmentCount = 1;
        fbInfo.pAttachments = &smaaWeightBuffer.color.view;
        fbInfo.width = smaaWeightBuffer.width;
        fbInfo.height = smaaWeightBuffer.height;
        fbInfo.layers = 1;
        VK_CHECK(vkCreateFramebuffer(BGLDevice::device(), &fbInfo, nullptr, &smaaWeightBuffer.frameBuffer));

        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.addressModeU = samplerInfo.addressModeV = samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.maxAnisotropy = 1.0f;
        samplerInfo.maxLod = 1.0f;
        samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        VK_CHECK(vkCreateSampler(BGLDevice::device(), &samplerInfo, nullptr, &smaaWeightBuffer.sampler));
    }

    void BGLRenderer::destroySmaaWeightBuffer()
    {
        vkDestroySampler(BGLDevice::device(), smaaWeightBuffer.sampler, nullptr);
        vkDestroyRenderPass(BGLDevice::device(), smaaWeightBuffer.renderPass, nullptr);
        vkDestroyFramebuffer(BGLDevice::device(), smaaWeightBuffer.frameBuffer, nullptr);
        vkDestroyImageView(BGLDevice::device(), smaaWeightBuffer.color.view, nullptr);
        vkDestroyImage(BGLDevice::device(), smaaWeightBuffer.color.image, nullptr);
        vkFreeMemory(BGLDevice::device(), smaaWeightBuffer.color.mem, nullptr);
        smaaWeightBuffer.sampler = VK_NULL_HANDLE;
        smaaWeightBuffer.renderPass = VK_NULL_HANDLE;
        smaaWeightBuffer.frameBuffer = VK_NULL_HANDLE;
        smaaWeightBuffer.color.view = VK_NULL_HANDLE;
        smaaWeightBuffer.color.image = VK_NULL_HANDLE;
        smaaWeightBuffer.color.mem = VK_NULL_HANDLE;
    }

    void BGLRenderer::beginSmaaWeightPass(VkCommandBuffer commandBuffer)
    {
        assert(isFrameStarted);
        VkRenderPassBeginInfo rpInfo{};
        rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpInfo.renderPass = smaaWeightBuffer.renderPass;
        rpInfo.framebuffer = smaaWeightBuffer.frameBuffer;
        rpInfo.renderArea = {{0, 0}, {smaaWeightBuffer.width, smaaWeightBuffer.height}};
        VkClearValue cv{};
        cv.color = {0, 0, 0, 0};
        rpInfo.clearValueCount = 1;
        rpInfo.pClearValues = &cv;
        vkCmdBeginRenderPass(commandBuffer, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);
        VkViewport vp{0, 0, (float)smaaWeightBuffer.width, (float)smaaWeightBuffer.height, 0, 1};
        VkRect2D sc{{0, 0}, {smaaWeightBuffer.width, smaaWeightBuffer.height}};
        vkCmdSetViewport(commandBuffer, 0, 1, &vp);
        vkCmdSetScissor(commandBuffer, 0, 1, &sc);
    }
}