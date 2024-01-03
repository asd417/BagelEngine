#pragma once
#include <memory>
#include <vector>

#include "entt.hpp"
#include <glm/gtc/constants.hpp>

#include "bagel_render_system.hpp"
#include "../bagel_pipeline.hpp"
#include "../bagel_frame_info.hpp"
#include "../bgl_camera.hpp"
#include "../bgl_gameobject.hpp"
#include "../bgl_model.hpp"
#include "../bagel_imgui.hpp"


//#define MODELRENDER_ORIGINAL
namespace bagel {
	struct ECSPushConstantData {
		glm::mat4 modelMatrix{ 1.0f };
		glm::mat4 normalMatrix{ 1.0f };

		uint32_t diffuseTextureHandle;
		uint32_t emissionTextureHandle;
		uint32_t normalTextureHandle;
		uint32_t roughmetalTextureHandle;
		// Stores flags to determine which textures are present
		uint32_t textureMapFlag;

		uint32_t BufferedTransformHandle = 0;
		uint32_t UsesBufferedTransform = 0;
	};

	class ModelRenderSystem : BGLRenderSystem{
	public:
		ModelRenderSystem(
			VkRenderPass renderPass,
			std::vector<VkDescriptorSetLayout> setLayouts,
			std::unique_ptr<BGLBindlessDescriptorManager> const& _descriptorManager,
			std::unique_ptr<BGLModelBufferManager> const& _modelBufferManager,
			entt::registry& _registry,
			ConsoleApp& consoleApp);

		void renderEntities(FrameInfo& frameInfo);

		bool stopBinding = false;
	private:
		entt::registry& registry;
		std::unique_ptr<BGLBindlessDescriptorManager> const& descriptorManager;
		std::unique_ptr<BGLModelBufferManager> const& modelBufferManager;
	};

}