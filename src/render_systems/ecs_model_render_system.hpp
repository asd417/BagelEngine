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
#ifdef MODELRENDER_ORIGINAL
	class ModelRenderSystem {
	public:
		ModelRenderSystem(
			BGLDevice& device, 
			VkRenderPass renderPass, 
			std::vector<VkDescriptorSetLayout> setLayouts, 
			std::unique_ptr<BGLBindlessDescriptorManager> const& _descriptorManager, 
			entt::registry& registry);
		~ModelRenderSystem();

		ModelRenderSystem(const ModelRenderSystem&) = delete;
		ModelRenderSystem& operator=(const ModelRenderSystem&) = delete;
		void renderEntities(FrameInfo& frameInfo);

	private:
		void createPipelineLayout(std::vector<VkDescriptorSetLayout> setLayouts);
		void createPipeline(VkRenderPass renderPass);

		BGLDevice& bglDevice;
		entt::registry& registry;

		std::unique_ptr<BGLPipeline> bglPipeline;
		VkPipelineLayout pipelineLayout;
		std::unique_ptr<BGLBindlessDescriptorManager> const& descriptorManager;
		std::unique_ptr <BGLBuffer> objDataBuffer;
	};
#else
	class ModelRenderSystem : BGLRenderSystem{
	public:
		ModelRenderSystem( 
			VkRenderPass renderPass,
			std::vector<VkDescriptorSetLayout> setLayouts,
			std::unique_ptr<BGLBindlessDescriptorManager> const& _descriptorManager,
			entt::registry& _registry) : 
				BGLRenderSystem{ renderPass, setLayouts, sizeof(ECSPushConstantData)}, 
				descriptorManager{_descriptorManager },
				registry{_registry}
		{
			createPipeline(renderPass, "/shaders/simple_shader.vert.spv", "/shaders/simple_shader.frag.spv", nullptr);
		}
		void renderEntities(FrameInfo& frameInfo);
	private:
		entt::registry& registry;
		std::unique_ptr<BGLBindlessDescriptorManager> const& descriptorManager;
	};
#endif
}