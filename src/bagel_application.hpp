#pragma once

#include <glm/gtc/constants.hpp>

#include "bagel_descriptors.hpp"
#include "bagel_window.hpp"
#include "bagel_engine_device.hpp"
#include "bagel_renderer.hpp"
#include "bagel_keybinds.hpp"   // Source-style key -> console-command bindings

#include "render_systems/point_light_render_system.hpp"
#include "render_systems/wireframe_render_system.hpp"
#include "render_systems/gbuffer_render_system.hpp"
#include "render_systems/composit_render_system.hpp"
#include "render_systems/bloom_render_system.hpp"
#include "render_systems/radiosity_render_system.hpp"
#include "render_systems/shadow_render_system.hpp"
#include "render_systems/transparent_render_system.hpp"
#include "render_systems/skinned_gbuffer_render_system.hpp"
#include "render_systems/skinned_shadow_render_system.hpp"
#include "render_systems/smaa_edge_render_system.hpp"
#include "render_systems/smaa_weight_render_system.hpp"
#include "render_systems/smaa_neighborhood_render_system.hpp"
#include "render_systems/gizmo_render_system.hpp"

#ifdef PHYSTEST
#include "physics/bagel_physics.h"
#endif

#include "bagel_gameobject.hpp"
#include "bagel_model.hpp"
#include "bagel_textures.hpp"
#include "bagel_material.hpp"
#include "animation/bagel_skin_manager.hpp"

#include "physics/bagel_physics.hpp"

#include <memory>
#include <string>
#include <vector>

#define CONSOLE ConsoleApp::Instance()

namespace bagel
{
	// Used by drawImgui() below by reference only; full definition is included in the .cpp.
	class KeyboardMovementController;

	class Application
	{
	public:
		static constexpr int WIDTH = 800;
		static constexpr int HEIGHT = 800;

		Application();
		~Application();

		Application(const Application &) = delete;
		Application &operator=(const Application &) = delete;

		void updateDirectionalUBO(entt::registry& registry, GlobalUBO& ubo, glm::vec3 camPos, glm::vec3 camFwd, float aspect);
		void drawImgui(BGLCamera& camera, KeyboardMovementController& cameraController, SmaaEdgeRenderSystem& edgeRender);
		void run();
		entt::registry &getRegistry() { return registry; }

		// Console Command variables
		bool freeFly = true;
		bool runPhys = false;
		bool showInfo = false;
		bool showImgui = true;   // ` (grave) key toggles all ImGui panels
		bool showWireframe = false;
		bool drawBBox = false;
		bool  bloomEnabled   = true;
		bool  smaaEnabled    = true;   // SMAA neighborhood blend (R_SMAA); off = passthrough
		float bloomIntensity = 0.054f;
		float bloomThreshold = 0.16f;
		float bloomMipDecay  = 0.5f;
		int gbufferDebugMode = 0; // 0=composite 1=albedo 2=normals 3=position 4=roughness 5=metallic 6=bloom 7=raw emission

		float smaaEdgeThreshold = 0.05f;
		float smaaLocalContrastAdapt = 2.0f;
		// Retune the shared texture sampler's mip LOD bias live (console R_MIPBIAS <value>).
		// Negative = sharper/more shimmer; positive = blurrier. Forwards to BGLTextureLoader.
		void setTextureMipBias(float bias);
		bool showProfile = false;
		bool stutterDetect = true;
		float stutterThresholdMs = 33.3f; // flag frames slower than this (~30fps)
		int maxFps = 0; // 0 = unlimited; minimum enforced value is 15
		bool vsync = false;
		bool vsyncDirty = false;
		float exposure = 0.0075f;

		// Bone-posing gizmo edit mode (console: editmode 0/1; also the G hotkey).
		void setGizmoEditMode(bool on) { poseGizmo.setEditMode(on); }
		bool gizmoEditModeOn() const   { return poseGizmo.editModeOn(); }

		// Source-style keybinds (key -> console command). Driven by the bind/unbind console
		// commands and polled each frame in run().
		KeyBindManager& getKeybinds() { return keybinds; }
	
		// Console "map <name>" hook: load /maps/<name>.bmap by name and rehydrate. Base is a no-op
		// error; the derived app implements the actual load. Returns a status message for the console.
		virtual std::string consoleLoadMap(const std::string& name) { return "[error] map: not supported in this app"; }

		// Override in derived classes
		virtual void OnSceneLoad() {}
		virtual void OnUpdate(float dt) {}
		// Called once per frame inside the ImGui frame (after NewFrame, before any
		// registry-reading UI), so a derived class may draw its own panels and safely
		// mutate the scene/registry in response to widgets.
		virtual void OnDrawGui() {}

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
		std::unique_ptr<BGLSkinManager> skinManager;
		entt::registry registry;
		// Declared after registry (constructed after it; holds only a reference to it).
		PoseGizmo poseGizmo{ registry };
		// Key -> console-command table; polled each frame in run() (see bagel_keybinds.hpp).
		KeyBindManager keybinds;

	private:
		std::unique_ptr<BGLDescriptorSetLayout> modelSetLayout;
		void initCommand();
		void initJolt();
		void initImgui();
		VkDescriptorPool imguiPool;
	};
}
