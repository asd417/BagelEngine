#pragma once
#include <memory>
#include <vector>

#include "entt.hpp"
#include <glm/gtc/constants.hpp>

#include "bagel_render_system.hpp"
#include "../bagel_pipeline.hpp"
#include "../bagel_frame_info.hpp"
#include "../bagel_camera.hpp"
#include "../bagel_gameobject.hpp"
#include "../bagel_model.hpp"


//#define MODELRENDER_ORIGINAL
namespace bagel {
	struct WireframePushConstantData {
		glm::mat4 modelMatrix{ 1.0f };
		glm::vec4 scale{};
		uint32_t BufferedTransformHandle = 0;
		uint32_t UsesBufferedTransform = 0;
		glm::vec4 color{ 1.0f };  // per-draw color; w=0 falls back to ubo.lineColor in shader
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
		void renderBBoxes(FrameInfo& frameInfo);
		~WireframeRenderSystem();
	private:
		entt::registry& registry;
		std::unique_ptr<BGLBuffer> uboBuffer;
		std::unique_ptr<BGLBindlessDescriptorManager> const& descriptorManager;
		BGLDevice& device;

		bool drawCollision = true;

		// Unit wire cube shared geometry for bbox drawing
		VkBuffer       bboxVertexBuffer  = VK_NULL_HANDLE;
		VkDeviceMemory bboxVertexMemory  = VK_NULL_HANDLE;
		VkBuffer       bboxIndexBuffer   = VK_NULL_HANDLE;
		VkDeviceMemory bboxIndexMemory   = VK_NULL_HANDLE;
		static constexpr uint32_t BBOX_INDEX_COUNT = 24;
	};

}