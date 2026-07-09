#include "engine/renderer/bagel_renderer.hpp"
namespace bagel
{

    void BGLRenderer::prepareRadiosityBuffer()
    {
        auto sc = bglSwapChain->getSwapChainExtent();
        radiosityBuffer.width = sc.width;
        radiosityBuffer.height = sc.height;

        createAttachment(VK_FORMAT_R16G16B16A16_SFLOAT,
                         VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                         &radiosityBuffer.color,
                         radiosityBuffer.width, radiosityBuffer.height);

        VkAttachmentDescription desc{};
        desc.format = VK_FORMAT_R16G16B16A16_SFLOAT;
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
        VK_CHECK(vkCreateRenderPass(BGLDevice::device(), &rpInfo, nullptr, &radiosityBuffer.renderPass));

        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass = radiosityBuffer.renderPass;
        fbInfo.attachmentCount = 1;
        fbInfo.pAttachments = &radiosityBuffer.color.view;
        fbInfo.width = radiosityBuffer.width;
        fbInfo.height = radiosityBuffer.height;
        fbInfo.layers = 1;
        VK_CHECK(vkCreateFramebuffer(BGLDevice::device(), &fbInfo, nullptr, &radiosityBuffer.frameBuffer));

        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
        samplerInfo.addressModeU = samplerInfo.addressModeV = samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.maxAnisotropy = 1.0f;
        samplerInfo.maxLod = 1.0f;
        samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        VK_CHECK(vkCreateSampler(BGLDevice::device(), &samplerInfo, nullptr, &radiosityBuffer.sampler));
    }

    void BGLRenderer::destroyRadiosityBuffer()
    {
        vkDestroySampler(BGLDevice::device(), radiosityBuffer.sampler, nullptr);
        vkDestroyRenderPass(BGLDevice::device(), radiosityBuffer.renderPass, nullptr);
        vkDestroyFramebuffer(BGLDevice::device(), radiosityBuffer.frameBuffer, nullptr);
        vkDestroyImageView(BGLDevice::device(), radiosityBuffer.color.view, nullptr);
        vkDestroyImage(BGLDevice::device(), radiosityBuffer.color.image, nullptr);
        vkFreeMemory(BGLDevice::device(), radiosityBuffer.color.mem, nullptr);
        radiosityBuffer.sampler = VK_NULL_HANDLE;
        radiosityBuffer.renderPass = VK_NULL_HANDLE;
        radiosityBuffer.frameBuffer = VK_NULL_HANDLE;
        radiosityBuffer.color.view = VK_NULL_HANDLE;
        radiosityBuffer.color.image = VK_NULL_HANDLE;
        radiosityBuffer.color.mem = VK_NULL_HANDLE;
    }

    void BGLRenderer::beginRadiosityPass(VkCommandBuffer commandBuffer)
    {
        assert(isFrameStarted);
        VkRenderPassBeginInfo rpInfo{};
        rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpInfo.renderPass = radiosityBuffer.renderPass;
        rpInfo.framebuffer = radiosityBuffer.frameBuffer;
        rpInfo.renderArea = {{0, 0}, {radiosityBuffer.width, radiosityBuffer.height}};
        VkClearValue cv{};
        cv.color = {0, 0, 0, 0};
        rpInfo.clearValueCount = 1;
        rpInfo.pClearValues = &cv;
        vkCmdBeginRenderPass(commandBuffer, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);
        VkViewport vp{0, 0, (float)radiosityBuffer.width, (float)radiosityBuffer.height, 0, 1};
        VkRect2D sc{{0, 0}, {radiosityBuffer.width, radiosityBuffer.height}};
        vkCmdSetViewport(commandBuffer, 0, 1, &vp);
        vkCmdSetScissor(commandBuffer, 0, 1, &sc);
    }
}