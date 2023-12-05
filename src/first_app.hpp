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
#include "bagel_imgui.h"

#include <memory>
#include <vector>


#define VK_CHECK(x)                                                     \
	do                                                                  \
	{                                                                   \
		VkResult err = x;                                               \
		if (err)                                                        \
		{                                                               \
			std::cout <<"Detected Vulkan error: " << err << std::endl;  \
			abort();                                                    \
		}                                                               \
	} while (0)


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

		bool freeFly = false;

		ConsoleApp console{};

	private:
		BGLWindow bglWindow{ WIDTH, HEIGHT, "Bagel Engine" };
		BGLDevice bglDevice{ bglWindow };
		BGLRenderer bglRenderer{ bglWindow,bglDevice };

		std::unique_ptr<BGLDescriptorSetLayout> modelSetLayout;
		std::unique_ptr<BGLDescriptorPool> globalPool;
		//It is critical to define variables that require bglDevice below this line as variables below will get destroyed first

		std::unique_ptr<BGLBindlessDescriptorManager> descriptorManager;
		entt::registry registry;

		//ImGUI
		void init_imgui();
		VkDescriptorPool imguiPool;
	};
}