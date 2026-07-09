#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

#include "../render_systems/bagel_render_system.hpp"
#include "../bagel_frame_info.hpp"

namespace bagel {

	class BGLDevice;

	// Overlay renderer for LEGO connection markers: a ring at every detected stud / hole plus a
	// short axis line, colored by connector type. Reuses the wireframe LINE_LIST pipeline (no
	// depth), so it draws on top of the scene.
	//
	// This used to live in GizmoRenderSystem::renderConnections; it was pulled into lego/ so the
	// engine's render layer no longer depends on LegoConnectionComponent. Owns its own small
	// ring/seg line meshes (the gizmo's copies stayed with the bone gizmo). The app constructs it
	// in Application::OnRenderInit and draws it from OnSwapchainOverlay.
	class LegoConnectionRenderSystem : BGLRenderSystem {
	public:
		LegoConnectionRenderSystem(
			VkRenderPass renderPass,
			std::vector<VkDescriptorSetLayout> setLayouts,
			BGLDevice& device);
		~LegoConnectionRenderSystem();

		void render(FrameInfo& frameInfo);

	private:
		BGLDevice& device;

		VkBuffer ringVB = VK_NULL_HANDLE; VkDeviceMemory ringMem = VK_NULL_HANDLE; uint32_t ringCount = 0; // unit ring in XY (normal +Z)
		VkBuffer segVB  = VK_NULL_HANDLE; VkDeviceMemory segMem  = VK_NULL_HANDLE; uint32_t segCount  = 0; // unit X segment

		void drawMesh(VkCommandBuffer cmd, VkBuffer vb, uint32_t vertCount,
		              const glm::mat4& model, float scale, const glm::vec4& color);
	};

} // namespace bagel
