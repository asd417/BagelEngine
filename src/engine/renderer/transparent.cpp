#include "engine/renderer/bagel_renderer.hpp"
namespace bagel
{

    void BGLRenderer::prepareTransparentPass()
    {
        // Radiosity-only forward transparent pass: blends HDR transparent lighting into the
        // radiosity buffer (which bloom and composite read), depth-testing read-only against the
        // opaque G-buffer depth. No swapchain/tonemap here — composite handles display.
        std::array<VkAttachmentDescription, 2> att{};
        // 0: radiosity HDR — LOAD opaque lighting, blend transparent over it, keep readable for bloom
        att[0].format = VK_FORMAT_R16G16B16A16_SFLOAT;
        att[0].samples = VK_SAMPLE_COUNT_1_BIT;
        att[0].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        att[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        att[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        att[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        att[0].initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        att[0].finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        // 1: opaque G-buffer depth — LOAD, read-only depth test (no write). STORE is required:
        // DONT_CARE lets the driver discard the depth contents after this pass, which breaks every
        // later reader of the G-buffer depth (composite world-pos reconstruction, SMAA depth mode,
        // the depth blit). The pass doesn't write depth, but the contents must survive it.
        att[1].format = deferredRenderFrameBuffer.depth.format;
        att[1].samples = VK_SAMPLE_COUNT_1_BIT;
        att[1].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
        att[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        att[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        att[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        att[1].initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        att[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;

        VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkAttachmentReference depthRef{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL};

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = 1;
        subpass.pColorAttachments = &colorRef;
        subpass.pDepthStencilAttachment = &depthRef;

        std::array<VkSubpassDependency, 2> deps{};
        // Wait for the radiosity pass color write and its G-buffer depth sample (plus the depth write) before blending.
        deps[0] = {VK_SUBPASS_EXTERNAL, 0,
                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                   VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT,
                   VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
                   VK_DEPENDENCY_BY_REGION_BIT};
        // Make the radiosity blend visible to the bloom downsample / composite shader reads.
        deps[1] = {0, VK_SUBPASS_EXTERNAL,
                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                   VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                   VK_ACCESS_SHADER_READ_BIT,
                   VK_DEPENDENCY_BY_REGION_BIT};

        VkRenderPassCreateInfo rpInfo{};
        rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpInfo.attachmentCount = static_cast<uint32_t>(att.size());
        rpInfo.pAttachments = att.data();
        rpInfo.subpassCount = 1;
        rpInfo.pSubpasses = &subpass;
        rpInfo.dependencyCount = static_cast<uint32_t>(deps.size());
        rpInfo.pDependencies = deps.data();
        VK_CHECK(vkCreateRenderPass(BGLDevice::device(), &rpInfo, nullptr, &transparentRenderPass));
    }

    void BGLRenderer::buildTransparentFramebuffers()
    {
        // Single full-screen framebuffer: the radiosity color + the opaque G-buffer depth.
        auto ext = bglSwapChain->getSwapChainExtent();
        std::array<VkImageView, 2> views{
            radiosityBuffer.color.view,
            deferredRenderFrameBuffer.depth.view};
        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass = transparentRenderPass;
        fbInfo.attachmentCount = static_cast<uint32_t>(views.size());
        fbInfo.pAttachments = views.data();
        fbInfo.width = ext.width;
        fbInfo.height = ext.height;
        fbInfo.layers = 1;
        VK_CHECK(vkCreateFramebuffer(BGLDevice::device(), &fbInfo, nullptr, &transparentFramebuffer));
    }

    void BGLRenderer::destroyTransparentFramebuffers()
    {
        if (transparentFramebuffer != VK_NULL_HANDLE)
        {
            vkDestroyFramebuffer(BGLDevice::device(), transparentFramebuffer, nullptr);
            transparentFramebuffer = VK_NULL_HANDLE;
        }
    }

    void BGLRenderer::beginTransparentPass(VkCommandBuffer commandBuffer)
    {
        assert(isFrameStarted);
        auto ext = bglSwapChain->getSwapChainExtent();
        VkRenderPassBeginInfo rpInfo{};
        rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpInfo.renderPass = transparentRenderPass;
        rpInfo.framebuffer = transparentFramebuffer;
        rpInfo.renderArea = {{0, 0}, ext};
        rpInfo.clearValueCount = 0; // all attachments LOAD
        vkCmdBeginRenderPass(commandBuffer, &rpInfo, VK_SUBPASS_CONTENTS_INLINE);
        VkViewport vp{0, 0, (float)ext.width, (float)ext.height, 0, 1};
        VkRect2D sc{{0, 0}, ext};
        vkCmdSetViewport(commandBuffer, 0, 1, &vp);
        vkCmdSetScissor(commandBuffer, 0, 1, &sc);
    }
}