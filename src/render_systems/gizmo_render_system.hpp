#pragma once
#include <memory>
#include <vector>

#include <vulkan/vulkan.h>
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

#include "bagel_render_system.hpp"
#include "../bagel_frame_info.hpp"
#include "../pose_gizmo.hpp"

namespace bagel {

	// Overlay renderer for the bone-posing gizmo. Reuses the wireframe pipeline + shader
	// (LINE_LIST, per-draw color), so no new shader is needed. Reads interaction state from a
	// PoseGizmo: draws a small marker at every joint, plus translate arrows / rotate rings at the
	// selected joint, highlighting the hovered/dragged axis. Drawn in the swapchain pass after the
	// scene so it sits on top.
	class GizmoRenderSystem : BGLRenderSystem {
	public:
		GizmoRenderSystem(
			VkRenderPass renderPass,
			std::vector<VkDescriptorSetLayout> setLayouts,
			BGLDevice& device);
		~GizmoRenderSystem();

		void render(FrameInfo& frameInfo, const PoseGizmo& gizmo);

	private:
		BGLDevice& device;

		// Shared unit meshes (positions only, LINE_LIST).
		VkBuffer       arrowVB  = VK_NULL_HANDLE; VkDeviceMemory arrowMem  = VK_NULL_HANDLE; uint32_t arrowCount  = 0;
		VkBuffer       ringVB   = VK_NULL_HANDLE; VkDeviceMemory ringMem   = VK_NULL_HANDLE; uint32_t ringCount   = 0;
		VkBuffer       markerVB = VK_NULL_HANDLE; VkDeviceMemory markerMem = VK_NULL_HANDLE; uint32_t markerCount = 0;
		VkBuffer       segVB    = VK_NULL_HANDLE; VkDeviceMemory segMem    = VK_NULL_HANDLE; uint32_t segCount    = 0; // unit X segment for bone lines

		void drawMesh(VkCommandBuffer cmd, VkBuffer vb, uint32_t vertCount,
		              const glm::mat4& model, float scale, const glm::vec4& color);
	};

} // namespace bagel
