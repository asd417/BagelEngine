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
#include "render_systems/bloom_render_system.hpp"
#include "render_systems/radiosity_render_system.hpp"
#include "render_systems/shadow_render_system.hpp"

#ifdef PHYSTEST
#include "physics/bagel_physics.h"
#endif

#include "bagel_gameobject.hpp"
#include "bagel_model.hpp"
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
		bool drawBBox = false;
		bool  bloomEnabled   = true;
		float bloomIntensity = 0.054f;
		float bloomThreshold = 0.16f;
		float bloomMipDecay  = 0.5f;
		int gbufferDebugMode = 0; // 0=composite 1=albedo 2=normals 3=position 4=roughness 5=metallic 6=bloom 7=raw emission
		bool showProfile = false;
		bool stutterDetect = true;
		float stutterThresholdMs = 33.3f; // flag frames slower than this (~30fps)
		int maxFps = 0; // 0 = unlimited; minimum enforced value is 15
		bool vsync = false;
		bool vsyncDirty = false;
		float exposure = 0.0075f;
	
		// Override in derived classes
		virtual void OnSceneLoad() {}
		virtual void OnUpdate(float dt) {}

	protected:
		uint32_t fallbackAlbedoMap = 0;

		// Engine subsystems — accessible to derived application classes
		// IMPORTANT: bglDevice must outlive registry (ModelComponent destructors call vkDestroyBuffer).
		// Members are destroyed in reverse declaration order, so registry is declared last.
		BGLWindow bglWindow{WIDTH, HEIGHT, "Bagel Engine"};
		BGLDevice bglDevice{bglWindow};
		BGLRenderer bglRenderer{bglWindow, bglDevice};
		std::unique_ptr<BGLDescriptorPool> globalPool;
		std::unique_ptr<BGLBindlessDescriptorManager> descriptorManager;
		std::unique_ptr<BGLMaterialManager> materialManager;
		entt::registry registry;

	private:
		std::unique_ptr<BGLDescriptorSetLayout> modelSetLayout;
		void initCommand();
		void initJolt();
		void initImgui();
		VkDescriptorPool imguiPool;
	};
}
