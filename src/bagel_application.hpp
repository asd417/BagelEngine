#pragma once

#include <glm/gtc/constants.hpp>

#include "bagel_descriptors.hpp"
#include "bagel_window.hpp"
#include "bagel_engine_device.hpp"
#include "bagel_renderer.hpp"

#include "render_systems/point_light_render_system.hpp"
#include "render_systems/wireframe_render_system.hpp"
#include "render_systems/gbuffer_render_system.hpp"
#include "render_systems/composit_render_system.hpp"
#include "render_systems/transparent_render_system.hpp"
#include "render_systems/bloom_render_system.hpp"

#ifdef PHYSTEST
#include "physics/bagel_physics.h"
#endif

#include "bgl_gameobject.hpp"
#include "bgl_model.hpp"
#include "bagel_textures.hpp"
#include "bagel_material.hpp"

#include "physics/bagel_physics.hpp"

#include <memory>
#include <vector>

#define CONSOLE ConsoleApp::Instance()

namespace bagel
{
	class Application
	{
	public:
		static constexpr int WIDTH = 800;
		static constexpr int HEIGHT = 800;

		Application();
		~Application();

		Application(const Application &) = delete;
		Application &operator=(const Application &) = delete;

		void run();
		entt::registry &getRegistry() { return registry; }

		// Console Command variables
		bool freeFly = true;
		bool runPhys = false;
		bool showFPS = false;
		bool showInfo = false;
		bool showWireframe = false;
		bool bloomEnabled = true;
		int gbufferDebugMode = 0; // 0=composite 1=albedo 2=normals 3=position 4=roughness 5=metallic 6=bloom 7=raw emission

		// Override in derived classes
		virtual void OnSceneLoad() {}
		virtual void OnUpdate(float dt) {}

	protected:
		// Engine subsystems — accessible to derived application classes
		entt::registry registry;
		BGLWindow bglWindow{WIDTH, HEIGHT, "Bagel Engine"};
		BGLDevice bglDevice{bglWindow};
		BGLRenderer bglRenderer{bglWindow, bglDevice};
		std::unique_ptr<BGLDescriptorPool> globalPool;
		std::unique_ptr<BGLBindlessDescriptorManager> descriptorManager;

		std::unique_ptr<BGLMaterialManager> materialManager;

	private:
		std::unique_ptr<BGLDescriptorSetLayout> modelSetLayout;
		void initCommand();
		void initJolt();
		void initImgui();
		VkDescriptorPool imguiPool;
	};
}
