#pragma once

#include <glm/gtc/constants.hpp>

#include "bagel_descriptors.hpp"
#include "bagel_window.hpp"
#include "bagel_engine_device.hpp"
#include "bagel_renderer.hpp"

#include "systems/simple_render_system.hpp"
#include "systems/point_light_render_system.hpp"
#include "systems/ecs_model_render_system.hpp"

#include "bgl_gameobject.hpp"
#include "bgl_model.hpp"
#include "bagel_textures.h"

#include <memory>
#include <vector>



namespace bagel {
	class FirstApp {
	public:
		// constexpr for calculating in compiletime
		static constexpr int WIDTH = 800;
		static constexpr int HEIGHT = 800;
		
		FirstApp();
		~FirstApp();

		FirstApp(const FirstApp&) = delete;
		FirstApp& operator=(const FirstApp&) = delete;

		void run();
		void loadECSObjects();
		entt::registry& getRegistry() { return registry; }
	private:
		void loadGameObjects();
		std::unique_ptr<BGLTexture> createTextureImage(std::string filepath);

		uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);

		BGLWindow bglWindow{ WIDTH, HEIGHT, "Hello" };
		BGLDevice bglDevice{ bglWindow };
		BGLRenderer bglRenderer{ bglWindow,bglDevice };

		std::unique_ptr<BGLDescriptorSetLayout> modelSetLayout;
		std::unique_ptr<BGLDescriptorPool> globalPool{};
		//It is critical to define variables that require bglDevice below this line as variables below will get destroyed first
		entt::registry registry;
		BGLGameObject::Map gameObjects;
	};
	
}