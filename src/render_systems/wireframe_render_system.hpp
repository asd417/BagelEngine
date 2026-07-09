#pragma once
#include <memory>
#include <vector>

#include "entt.hpp"
#include <glm/gtc/constants.hpp>

#include "bagel_render_system.hpp"
#include "engine/bagel_pipeline.hpp"
#include "bagel_frame_info.hpp"
#include "bagel_camera.hpp"
#include "bagel_gameobject.hpp"
#include "model/bagel_model.hpp"


//#define MODELRENDER_ORIGINAL
namespace bagel {
	struct WireframePushConstantData {
		glm::mat4 modelMatrix{ 1.0f };       // offset 0 (already includes scale via computeMat4)
		uint32_t BufferedTransformHandle = 0; // offset 64
		uint32_t UsesBufferedTransform = 0;   // offset 68
		// std430 aligns the next vec4 to 16 bytes; without this pad `color` would land at 72 in
		// C++ but the shader reads it at 80 (garbage alpha -> the frag's a>0 test fails and
		// everything falls back to white ubo.lineColor).
		uint32_t _pad0 = 0;                   // offset 72
		uint32_t _pad1 = 0;                   // offset 76
		glm::vec4 color{ 1.0f };  // offset 80 — per-draw color; w=0 falls back to ubo.lineColor in shader
	};

	class WireframeRenderSystem : BGLRenderSystem {
	public:
		WireframeRenderSystem(
			VkRenderPass renderPass,
			std::vector<VkDescriptorSetLayout> setLayouts,
			std::unique_ptr<BGLBindlessDescriptorManager> const& _descriptorManager,
			entt::registry& _registry,
			BGLDevice& device);

		void renderEntities(FrameInfo& frameInfo);
		// Overlay the scene's solid geometry (every non-skinned ModelComponent, the planet
		// included) as a wireframe. Reuses the model vertex/index buffers directly — no
		// per-entity WireframeComponent — drawn with a TRIANGLE_LIST polygon-line pipeline.
		void renderModelsWireframe(FrameInfo& frameInfo);
		void renderBBoxes(FrameInfo& frameInfo);
		// Draw a single highlight bbox (distinct color) around one entity — the selection outline.
		// No-op if the entity is invalid or has no drawable ModelComponent bounds.
		void renderSelection(FrameInfo& frameInfo, entt::entity entity);
		~WireframeRenderSystem();
	private:
		entt::registry& registry;
		std::unique_ptr<BGLBuffer> uboBuffer;
		std::unique_ptr<BGLBindlessDescriptorManager> const& descriptorManager;
		BGLDevice& device;

		// Second pipeline (the base's bglPipeline is the LINE_LIST one used by the normal-viz /
		// bbox paths). This one is TRIANGLE_LIST + VK_POLYGON_MODE_LINE so it can draw ordinary
		// triangle meshes as wireframe via the same shader + pipeline layout.
		std::unique_ptr<BGLPipeline> modelWirePipeline;

		bool drawCollision = true;

		// Unit wire cube shared geometry for bbox drawing
		VkBuffer       bboxVertexBuffer  = VK_NULL_HANDLE;
		VkDeviceMemory bboxVertexMemory  = VK_NULL_HANDLE;
		VkBuffer       bboxIndexBuffer   = VK_NULL_HANDLE;
		VkDeviceMemory bboxIndexMemory   = VK_NULL_HANDLE;
		static constexpr uint32_t BBOX_INDEX_COUNT = 24;
	};

}