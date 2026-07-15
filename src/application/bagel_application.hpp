#pragma once

#include <glm/gtc/constants.hpp>

#include "engine/bagel_descriptors.hpp"
#include "engine/bagel_window.hpp"
#include "engine/bagel_engine_device.hpp"
#include "engine/renderer/bagel_renderer.hpp"
#include "bagel_keybinds.hpp" // Source-style key -> console-command bindings

#include "render_systems/point_light_render_system.hpp"
#include "render_systems/wireframe_render_system.hpp"
#include "render_systems/gbuffer_render_system.hpp"
#include "render_systems/planet_gbuffer_render_system.hpp"
#include "render_systems/composit_render_system.hpp"
#include "render_systems/bloom_render_system.hpp"
#include "render_systems/radiosity_render_system.hpp"
#include "render_systems/shadow_render_system.hpp"
#include "render_systems/transparent_render_system.hpp"
#include "render_systems/water_render_system.hpp"
#include "render_systems/animated_gbuffer_render_system.hpp"
#include "render_systems/animated_shadow_render_system.hpp"
#include "render_systems/smaa_edge_render_system.hpp"
#include "render_systems/smaa_weight_render_system.hpp"
#include "render_systems/smaa_neighborhood_render_system.hpp"
#include "render_systems/gizmo_render_system.hpp"

//#include "texture/bagel_textures.hpp"
//#include "model/bagel_model.hpp"
#include "bagel_camera.hpp"
#include "bagel_material.hpp"
#include "animation/bagel_skin_manager.hpp"

#include <memory>
#include <string>
#include <vector>

#define CONSOLE ConsoleApp::Instance()

namespace bagel
{
	// Used by drawImgui() below by reference only; full definition is included in the .cpp.
	class KeyboardMovementController;
	// Per-section profiling accumulators (ms totals + sample counts)
	using Clock = std::chrono::high_resolution_clock;
	class Application
	{
	public:
		static constexpr int WIDTH = 800;
		static constexpr int HEIGHT = 800;

		Application();
		~Application();

		Application(const Application &) = delete;
		Application &operator=(const Application &) = delete;

		void updateDirectionalUBO(entt::registry &registry, GlobalUBO &ubo, glm::vec3 camPos, glm::vec3 camFwd, float aspect);
		void drawImgui(BGLCamera &camera, KeyboardMovementController &cameraController, SmaaEdgeRenderSystem &edgeRender);
		void run();
		entt::registry &getRegistry() { return registry; }

		// Console Command variables
		bool freeFly = true;
		bool runPhys = false;
		bool showInfo = false;
		bool showImgui = true; // ` (grave) key toggles all ImGui panels
		bool showWireframe = false;
		bool drawBBox = false;
		// Entity selected by left-click physics raycast (BGLJolt::PickEntity); entt::null if none.
		entt::entity selectedEntity = entt::null;
		bool bloomEnabled = cfg::kBloomEnabled;
		bool smaaEnabled = true; // SMAA neighborhood blend (R_SMAA); off = passthrough
		float bloomIntensity = cfg::kBloomIntensity;
		float bloomThreshold = cfg::kBloomThreshold;
		float bloomMipDecay = cfg::kBloomMipDecay;
		int gbufferDebugMode = 0; // 0=composite 1=albedo 2=normals 3=position 4=roughness 5=metallic 6=bloom 7=raw emission

		float smaaEdgeThreshold = 0.05f;
		float smaaLocalContrastAdapt = 2.0f;
		// Retune the shared texture sampler's mip LOD bias live (console R_MIPBIAS <value>).
		// Negative = sharper/more shimmer; positive = blurrier. Forwards to BGLTextureLoader.
		void setTextureMipBias(float bias);
		bool showProfile = false;
		bool stutterDetect = true;
		float stutterThresholdMs = 33.3f; // flag frames slower than this (~30fps)
		int maxFps = 0;					  // 0 = unlimited; minimum enforced value is 15
		bool vsync = false;
		bool vsyncDirty = false;
		float exposure = cfg::kExposure;
		// Procedural water/ocean opacity controls (forwarded to water.frag as push constants, live
		// from the Settings panel). waterOpaqueDepth = water column (world units) that reads opaque at
		// waterCamRefDist; the threshold scales with camera distance so near water shows more depth.
		float waterOpaqueDepth = 2.0f;
		float waterCamRefDist = 100.0f;
		// Camera perspective controls (the "close"/"far" view distances + vertical FOV),
		// live-tunable from the Settings panel. Far must reach past the planet (radius 64,
		// spawn ~160 out). cameraFovDegrees feeds both the projection and the shadow-cascade
		// frustum math in updateDirectionalUBO (keep them in sync).
		float cameraNear = cfg::kCameraNear;
		float cameraFar = cfg::kCameraFar;
		// HORIZONTAL FOV in degrees. Vertical FOV is derived from this + the aspect ratio
		// (fovY = 2*atan(tan(fovX/2)/aspect)) so horizontal framing is stable across window
		// sizes. ~60 keeps rectilinear edge distortion mild; the engine previously hardcoded
		// a very wide 100 (as vertical, which got even wider horizontally on non-square views).
		float cameraFovDegrees = cfg::kCameraFovDegrees;

		// Bone-posing gizmo edit mode (console: editmode 0/1; also the G hotkey).
		void setGizmoEditMode(bool on) { poseGizmo.setEditMode(on); }
		bool gizmoEditModeOn() const { return poseGizmo.editModeOn(); }


		// Source-style keybinds (key -> console command). Driven by the bind/unbind console
		// commands and polled each frame in run().
		KeyBindManager &getKeybinds() { return keybinds; }

		// Console "map <name>" hook: load /maps/<name>.bmap by name and rehydrate. Base is a no-op
		// error; the derived app implements the actual load. Returns a status message for the console.
		virtual std::string consoleLoadMap(const std::string &name) { return "[error] map: not supported in this app"; }

		// Override in derived classes
		virtual void OnSceneLoad() {}
		virtual void OnUpdate(BGLCamera& camera, float dt) {}
		// Called once per frame inside the ImGui frame (after NewFrame, before any
		// registry-reading UI), so a derived class may draw its own panels and safely
		// mutate the scene/registry in response to widgets.
		virtual void OnDrawGui() {}

		// Called ONCE inside run() after the swapchain render pass + descriptor set layouts exist,
		// but before the frame loop. Lets a derived app build its OWN render systems (which need the
		// render pass at construction) without the engine knowing their types. Draw them from
		// OnSwapchainOverlay(). `setLayouts` are the same layouts the engine render systems use.
		virtual void OnRenderInit(VkRenderPass swapchainPass,
		                          const std::vector<VkDescriptorSetLayout>& setLayouts) { (void)swapchainPass; (void)setLayouts; }

		// Called every frame inside the swapchain render pass, at the overlay stage (after the scene
		// and the engine gizmo, before ImGui). Lets a derived app draw app-specific overlays through
		// FrameInfo — e.g. LEGO connection markers, via a render system created in OnRenderInit().
		virtual void OnSwapchainOverlay(FrameInfo& frameInfo) { (void)frameInfo; }

	protected:
		uint32_t fallbackAlbedoMap = 0;

		// World-space camera position for the current frame, refreshed in run() right
		// before OnUpdate(dt). Lets a derived app drive camera-relative work (e.g. the
		// planet LOD cut) without threading the camera through the OnUpdate signature.
		glm::vec3 cameraWorldPos{0.0f};

		// Request the free-fly camera be teleported to a position (consumed once by run()).
		// A scene (OnSceneLoad / a build button) calls this to frame itself — e.g. the
		// radius-64 planet needs the camera outside the body, not at the default spawn.
		void setSpawnCameraPos(const glm::vec3 &p)
		{
			spawnCameraPos = p;
			spawnCameraPosDirty = true;
		}
		glm::vec3 spawnCameraPos{0.0f};
		bool spawnCameraPosDirty = false;

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
		PoseGizmo poseGizmo{registry};
		// Key -> console-command table; polled each frame in run() (see bagel_keybinds.hpp).
		KeyBindManager keybinds;

	private:
		void initCommand();
		void initJolt();
		void initImgui();

		void updateAnimation(float frameTime);
		// Reused scratch for updateAnimation's manual-pose palette resolve; resized in place each
		// frame so the per-frame path doesn't heap-allocate after the first grow.
		std::vector<glm::mat4> paletteScratch;
		// Cache Mat4 transform of all entities. No updates to transformcomponents are allowed after this point.
		void cacheTransforms();

		void profile(double frameTime);
		// When a frame blows past stutterThresholdMs, print the slowest section that frame.
		void detectStutter(double frameTime);
		double tMs(Clock::time_point a, Clock::time_point b);
		void syncFrameRate(void *timerHandle, std::chrono::steady_clock::time_point frameLastTime);

		// ---- per-frame CPU profiling ------------------------------------------------------
		// run() times each phase into perf[]/sectMs[] (via the recordSection lambda); profile()
		// prints a 1-second rolling average and clears the accumulators. These are MEMBERS (not
		// run() locals) so profile() — a separate method — can reach them. sectName is C++17
		// inline-constexpr, so no out-of-line definition is needed.
		struct PerfSection { double total = 0.0; int n = 0; };
		enum Sect {
			S_POLL, S_CAMERA, S_GIZMO, S_UPDATE, S_HIERARCHY, S_PHYSICS,
			S_UBO, S_IMGUI, S_BEGINCMD, S_GBUFFER, S_BLOOM, S_COMPOSITE, S_ENDCMD,
			S_COUNT
		};
		static constexpr const char *sectName[S_COUNT] = {
			"poll_events", "camera     ", "gizmo      ", "on_update  ", "hierarchy  ", "physics    ",
			"ubo+lights ", "imgui      ", "begin_cmd  ", "gbuffer    ", "bloom      ",
			"composite  ", "end_cmd    "};
		PerfSection perf[S_COUNT]{};
		double sectMs[S_COUNT]{};
		double profAccum = 0.0;
		int    profFrames = 0;

		void registerDescriptorEntries();
		void reregisterDescriptorEntries();
		VkDescriptorPool imguiPool;
		std::unique_ptr<BGLDescriptorSetLayout> modelSetLayout;

		// Render Handles
		// Bindless handles for the engine's internal render targets. Assigned on the first
		// registerDescriptorEntries() call (append) and reused in place on resize. Initialized
		// so the first call never passes an indeterminate value as a designated handle.
		uint16_t radiosityHandle = 0;
		// Opaque G-buffer depth, exposed to the water pass so it can read scene depth behind the ocean
		// surface (thickness -> opacity; far plane behind the limb -> opaque, fixing water-over-space).
		uint16_t gDepthHandle = 0;
		uint16_t smaaEdgeHandle = 0;
		uint16_t smaaWeightHandle = 0;
		uint16_t compositeHandle = 0;
		uint16_t bloomMipHandles[BGLRenderer::BLOOM_MIPS]{};
		bool renderTargetsRegistered = false; // false until the handles above are appended once
	};
}
