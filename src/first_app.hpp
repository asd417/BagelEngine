#pragma once

#include <glm/gtc/constants.hpp>

#include "bagel_descriptors.hpp"
#include "bagel_window.hpp"
#include "bagel_engine_device.hpp"
#include "bagel_renderer.hpp"

#include "render_systems/simple_render_system.hpp"
#include "render_systems/point_light_render_system.hpp"
#include "render_systems/ecs_model_render_system.hpp"
#include "render_systems/wireframe_render_system.hpp"

#ifdef PHYSTEST
#include "physics/bagel_physics.h"
#endif

#include "bgl_gameobject.hpp"
#include "bgl_model.hpp"
#include "bagel_textures.hpp"
#include "bagel_imgui.hpp"
#include "physics/bagel_physics.hpp"

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
		entt::registry& getRegistry() { return registry; }

		//Command variables
		bool freeFly = true;
		bool runPhys = false;
		bool rotateLight = false;
		bool showFPS = false;
		ConsoleApp console{};

	private:
		entt::entity loadECSObjects();
		entt::entity makeTestEntity(glm::vec3 translation);
		void makeGrid();
		entt::entity makeAxisModel(glm::vec3 pos);
		void createMonitor();
		BGLWindow bglWindow{ WIDTH, HEIGHT, "Bagel Engine" };
		BGLDevice bglDevice{ bglWindow };
		BGLRenderer bglRenderer{ bglWindow,bglDevice };

		std::unique_ptr<BGLDescriptorSetLayout> modelSetLayout;
		std::unique_ptr<BGLDescriptorPool> globalPool;
		//It is critical to define variables that require bglDevice below this line as variables below will get destroyed first

		std::unique_ptr<BGLBindlessDescriptorManager> descriptorManager;
		std::unique_ptr<BGLModelBufferManager> modelBufferManager;
		entt::registry registry;

		void initRenderSystems();
		void initCommand();
		//External System inits
		void initJolt();
		void initImgui();
		VkDescriptorPool imguiPool;
	};
}