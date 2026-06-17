// vulkan headers
#include <vulkan/vulkan.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <synchapi.h>
#endif

// STL includes
#include <iostream>
#include <stdexcept>
#include <cmath>
#include <array>
#include <chrono>
#include <thread>
#include <vector>

#include "entt.hpp"
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"

#include "bagel_frame_info.hpp"
#include "bagel_buffer.hpp"
#include "bagel_camera.hpp"
#include "bagel_ecs_components.hpp"
#include "keyboard_movement_controller.hpp"
#include "bagel_console_commands.hpp"
#include "bagel_hierachy.hpp"
#include "bagel_util.hpp"
#include "bagel_imgui.hpp"

#include "physics/bagel_jolt.hpp"
#include "math/bagel_math.hpp"
#include "smaa/smaa_textures.hpp"

#include "Jolt/Jolt.h"

#define GLOBAL_DESCRIPTOR_COUNT 1000

namespace bagel
{

	Application::Application()
	{
		globalPool = BGLDescriptorPool::Builder(bglDevice)
						 .setMaxSets(BGLSwapChain::MAX_FRAMES_IN_FLIGHT * GLOBAL_DESCRIPTOR_COUNT)
						 .addPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, BGLSwapChain::MAX_FRAMES_IN_FLIGHT * GLOBAL_DESCRIPTOR_COUNT)
						 .addPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, BGLSwapChain::MAX_FRAMES_IN_FLIGHT * GLOBAL_DESCRIPTOR_COUNT + BGLSwapChain::MAX_FRAMES_IN_FLIGHT)
						 .addPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, BGLSwapChain::MAX_FRAMES_IN_FLIGHT * GLOBAL_DESCRIPTOR_COUNT + 10)
						 .build();

		descriptorManager = std::make_unique<BGLBindlessDescriptorManager>(bglDevice, *globalPool);
		descriptorManager->createBindlessDescriptorSet(GLOBAL_DESCRIPTOR_COUNT);
		descriptorManager->writeDeferredRenderTargetToDescriptor(bglRenderer.getDRSampler(), bglRenderer.getDRDepthView(), bglRenderer.getDRNormalView(), bglRenderer.getDRAlbedoView(), bglRenderer.getDREmissionView());

		CONSOLE->Log("FirstApp", "Finished Creating Global Pool");

		CONSOLE->Log("FirstApp", "Initializing ENTT Registry");
		registry = entt::registry{};

		CONSOLE->Log("FirstApp", "Initializing IMGUI");
		initImgui();

		CONSOLE->Log("FirstApp", "Initializing Jolt Physics Engine");
		initJolt();
		materialManager = std::make_unique<BGLMaterialManager>(bglDevice, *globalPool, *descriptorManager);
		fallbackAlbedoMap = materialManager->loadTexture("/materials/grid.png");
		skinManager = std::make_unique<BGLSkinManager>(bglDevice, *descriptorManager);
	}

	Application::~Application()
	{
		ImGui_ImplVulkan_Shutdown();
		vkDestroyDescriptorPool(BGLDevice::device(), imguiPool, nullptr);
	}

	void Application::updateDirectionalUBO(entt::registry &registry, GlobalUBO &ubo, glm::vec3 camPos, glm::vec3 camFwd, float aspect)
	{
		auto dirView = registry.view<DirectionalLightComponent>();
		if (!dirView.empty())
		{
			// only works with 1 directional light.
			auto &dlc = dirView.get<DirectionalLightComponent>(*dirView.begin());

			float pitch = glm::radians(dlc.rotation.x);
			float yaw = glm::radians(dlc.rotation.y);
			float c2 = cosf(pitch), s2 = sinf(pitch);
			float c1 = cosf(yaw), s1 = sinf(yaw);
			glm::vec3 u{c1, 0.f, -s1};
			glm::vec3 v{-s1 * s2, -c2, -c1 * s2};
			glm::vec3 w{-c2 * s1, s2, -c1 * c2};

			float tanY = tanf(glm::radians(100.0f) * 0.5f); // must match setPerspectiveProjection above
			float tanX = tanY * aspect;
			float k = tanX * tanX + tanY * tanY;

			float sliceNear = 0.1f; // camera near plane
			for (uint32_t ci = 0; ci < SHADOW_CASCADE_COUNT; ci++)
			{
				float n = sliceNear;
				float f = dlc.cascadeEnds[ci];

				// Bounding sphere of the frustum slice: centre on the view axis at depth cz,
				// radius from whichever corner ring (near or far) is farther from it
				float cz = 0.5f * (n + f) * (1.f + k);
				if (cz > f)
					cz = f;
				float rNear = sqrtf(n * n * k + (n - cz) * (n - cz));
				float rFar = sqrtf(f * f * k + (f - cz) * (f - cz));
				float r = fmaxf(rNear, rFar);
				glm::vec3 center = camPos + camFwd * cz;

				// Snap the centre to the texel grid in the light's uv plane
				float texel = (2.f * r) / float(ShadowMapBuffer::RESOLUTIONS[ci]);
				float cu = floorf(glm::dot(u, center) / texel) * texel;
				float cv = floorf(glm::dot(v, center) / texel) * texel;
				center = u * cu + v * cv + w * glm::dot(w, center);

				// Pull the light back so casters up to casterRange behind the slice still render
				glm::vec3 lightPos = center - w * (r + dlc.casterRange);
				float zRange = 2.f * r + dlc.casterRange;

				glm::mat4 lightView{1.f};
				lightView[0][0] = u.x;
				lightView[1][0] = u.y;
				lightView[2][0] = u.z;
				lightView[0][1] = v.x;
				lightView[1][1] = v.y;
				lightView[2][1] = v.z;
				lightView[0][2] = w.x;
				lightView[1][2] = w.y;
				lightView[2][2] = w.z;
				lightView[3][0] = -glm::dot(u, lightPos);
				lightView[3][1] = -glm::dot(v, lightPos);
				lightView[3][2] = -glm::dot(w, lightPos);

				glm::mat4 lightProj{0.f};
				lightProj[0][0] = 1.f / r;
				lightProj[1][1] = 1.f / r;
				lightProj[2][2] = 1.f / zRange;
				lightProj[3][3] = 1.f;
				// No coordinate flip: shadow depth maps linearly [light->0, far->1]

				ubo.directionalLight.lightSpaceMatrix[ci] = lightProj * lightView;
				sliceNear = f;
			}

			ubo.directionalLight.cascadeSplits = dlc.cascadeEnds;
			ubo.directionalLight.direction = glm::vec4(w, 0.0f);
			ubo.directionalLight.color = glm::vec4(dlc.color.x, dlc.color.y, dlc.color.z, dlc.lux);
			ubo.shadowBiasMin = dlc.shadowBiasMin;
			ubo.shadowBiasSlope = dlc.shadowBiasSlope;
			ubo.hasDirLight = 1;
		}
		else
		{
			ubo.hasDirLight = 0;
		}
	}
	

	void Application::run()
	{
		VkDeviceSize uboAlignment = bglDevice.properties.limits.minUniformBufferOffsetAlignment;
		std::unique_ptr<BGLBuffer> uboBuffers = std::make_unique<BGLBuffer>(
			bglDevice,
			sizeof(GlobalUBO),
			BGLSwapChain::MAX_FRAMES_IN_FLIGHT,
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
			uboAlignment);
		uboBuffers->map();

		std::unique_ptr<BGLBuffer> uboComposition = std::make_unique<BGLBuffer>(
			bglDevice,
			sizeof(CompositionUBO),
			1,
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
		uboComposition->map();

		std::array<VkDescriptorBufferInfo, BGLSwapChain::MAX_FRAMES_IN_FLIGHT> uboInfos;
		for (int i = 0; i < BGLSwapChain::MAX_FRAMES_IN_FLIGHT; i++)
			uboInfos[i] = uboBuffers->descriptorInfoForIndex(i);
		descriptorManager->storeUBOPerFrame(uboInfos, 0);

		uint32_t bloomMipHandles[BGLRenderer::BLOOM_MIPS];
		for (uint32_t i = 0; i < BGLRenderer::BLOOM_MIPS; i++)
		{
			bloomMipHandles[i] = descriptorManager->storeTexture(
				bglRenderer.getBloomMipImageInfo(i),
				bglRenderer.getBloomMipMemory(i),
				bglRenderer.getBloomMipImage(i),
				"BloomMip", false, 0, false);
		}

		uint32_t radiosityHandle = descriptorManager->storeTexture(
			bglRenderer.getRadiosityImageInfo(),
			bglRenderer.getRadiosityMemory(),
			bglRenderer.getRadiosityImage(),
			"RadiosityBuffer", false, 0, false);

		uint32_t smaaEdgeHandle = descriptorManager->storeTexture(
			bglRenderer.getSmaaEdgeImageInfo(),
			bglRenderer.getSmaaEdgeMemory(),
			bglRenderer.getSmaaEdgeImage(),
			"SmaaEdges", false, 0, false);

		// SMAA precomputed LUTs (AreaTex/SearchTex) — consumed by the blending-weight pass.
		SmaaLutHandles smaaLuts = loadSmaaLuts(materialManager->getTextureLoader());

		uint32_t smaaWeightHandle = descriptorManager->storeTexture(
			bglRenderer.getSmaaWeightImageInfo(),
			bglRenderer.getSmaaWeightMemory(),
			bglRenderer.getSmaaWeightImage(),
			"SmaaWeights", false, 0, false);

		// Offscreen composite (LDR) — SMAA edge input + neighborhood-blend color source.
		uint32_t compositeHandle = descriptorManager->storeTexture(
			bglRenderer.getCompositeImageInfo(),
			bglRenderer.getCompositeMemory(),
			bglRenderer.getCompositeImage(),
			"CompositeLDR", false, 0, false);

		std::vector<VkDescriptorSetLayout> pipelineDescriptorSetLayouts = {descriptorManager->getDescriptorSetLayout()};

		GBufferRenderSystem gBufferRenderSystem{
			bglRenderer.getDeferredRenderPass(),
			pipelineDescriptorSetLayouts,
			descriptorManager,
			registry};

		SkinnedGBufferRenderSystem skinnedGBufferRenderSystem{
			bglRenderer.getDeferredRenderPass(),
			pipelineDescriptorSetLayouts,
			descriptorManager,
			registry};

		// Composite renders to the offscreen LDR buffer (so SMAA can sample it), not the swapchain.
		CompositRenderSystem compositRenderSystem{
			bglRenderer.getCompositeRenderPass(),
			pipelineDescriptorSetLayouts,
			descriptorManager,
			registry};

		// SMAA pass 3 / present: blends composite + weights to the swapchain (overlays draw after).
		SmaaNeighborhoodRenderSystem smaaNeighborhoodRenderSystem{
			bglRenderer.getSwapChainRenderPass(),
			pipelineDescriptorSetLayouts,
			descriptorManager};

		WireframeRenderSystem wireframeRenderSystem{
			bglRenderer.getSwapChainRenderPass(),
			pipelineDescriptorSetLayouts,
			descriptorManager,
			registry,
			bglDevice};

		// Bone-posing gizmo: interaction state + overlay renderer (drawn in the swapchain pass).
		GizmoRenderSystem gizmoRenderSystem{
			bglRenderer.getSwapChainRenderPass(),
			pipelineDescriptorSetLayouts,
			bglDevice};
		// poseGizmo is an Application member (so the editmode console command can reach it).

		PointLightSystem pointLightSystem{
			bglRenderer.getSwapChainRenderPass(),
			pipelineDescriptorSetLayouts,
			descriptorManager,
			registry,
			bglDevice};

		BloomRenderSystem bloomRenderSystem{
			bglRenderer.getBloomMipRenderPassClear(0),
			pipelineDescriptorSetLayouts,
			descriptorManager};

		RadiosityRenderSystem radiosityRenderSystem{
			bglRenderer.getRadiosityRenderPass(),
			pipelineDescriptorSetLayouts,
			descriptorManager};
		
		SmaaEdgeRenderSystem smaaEdgeRenderSystem{
			bglRenderer.getSmaaEdgeRenderPass(),
			pipelineDescriptorSetLayouts,
			descriptorManager};

		SmaaWeightRenderSystem smaaWeightRenderSystem{
			bglRenderer.getSmaaWeightRenderPass(),
			pipelineDescriptorSetLayouts,
			descriptorManager};

		SkinnedShadowRenderSystem skinnedShadowRenderSystem{
			bglRenderer.getShadowMapRenderPass(),
			pipelineDescriptorSetLayouts,
			descriptorManager,
			registry};

		ShadowRenderSystem shadowRenderSystem{
			bglRenderer.getShadowMapRenderPass(),
			pipelineDescriptorSetLayouts,
			descriptorManager,
			registry};

		TransparentRenderSystem transparentRenderSystem{
			bglRenderer.getTransparentRenderPass(),
			pipelineDescriptorSetLayouts,
			descriptorManager,
			registry};

		{
			VkImageView shadowViews[BGLBindlessDescriptorManager::SHADOW_MAP_CASCADE_COUNT];
			for (uint32_t i = 0; i < BGLBindlessDescriptorManager::SHADOW_MAP_CASCADE_COUNT; i++)
				shadowViews[i] = bglRenderer.getShadowMapDepthView(i);
			descriptorManager->writeShadowMapToDescriptor(bglRenderer.getShadowMapSampler(), shadowViews);
		}

		BGLCamera camera{};

		auto viewerObject = BGLGameObject::createGameObject();
		viewerObject.transform.setTranslation({0.0f, -3.0f, 0.0f});
		KeyboardMovementController cameraController{};

		auto currentTime = std::chrono::high_resolution_clock::now();

		initCommand();

		BGLJolt::GetInstance()->SetGravity({0, -0.0f, 0});
		BGLJolt::GetInstance()->SetSimulationTimescale(0.5f);
		BGLJolt::GetInstance()->SetComponentActivityAll(true);

		OnSceneLoad();

		// Game loop
		float fpsAccum = 0.0f;
		int fpsFrames = 0;
		float totalTime = 0.0f;

		// Per-section profiling accumulators (ms totals + sample counts)
		using Clock = std::chrono::high_resolution_clock;
		struct PerfSection
		{
			double total = 0.0;
			int n = 0;
		};
		enum Sect
		{
			S_POLL,
			S_CAMERA,
			S_UPDATE,
			S_HIERARCHY,
			S_PHYSICS,
			S_UBO,
			S_IMGUI,
			S_BEGINCMD,
			S_GBUFFER,
			S_BLOOM,
			S_COMPOSITE,
			S_ENDCMD,
			S_COUNT
		};
		static const char *sectName[S_COUNT] = {
			"poll_events", "camera     ", "on_update  ", "hierarchy  ", "physics    ",
			"ubo+lights ", "imgui      ", "begin_cmd  ", "gbuffer    ", "bloom      ",
			"composite  ", "end_cmd    "};
		PerfSection perf[S_COUNT]{};
		double profAccum = 0.0;
		int profFrames = 0;
		double sectMs[S_COUNT]{};
		auto recordSection = [&](Sect s, double ms)
		{
			sectMs[s] = ms;
			perf[s].total += ms;
			perf[s].n++;
		};

		auto tMs = [](Clock::time_point a, Clock::time_point b)
		{
			return std::chrono::duration<double, std::milli>(b - a).count();
		};

		camera.setViewDirection(glm::vec3(-1.0f, -2.0f, -2.0f), glm::vec3(0.0f, 0.0f, 2.5f));

#ifdef _WIN32
		HANDLE hFpsTimer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
		if (!hFpsTimer)
			hFpsTimer = CreateWaitableTimerW(nullptr, FALSE, nullptr); // fallback for older Windows 10
#endif

		bool gravePrev = false; // edge-detect the ` key for the ImGui toggle
		while (!bglWindow.shouldClose())
		{
			auto t0 = Clock::now();
			glfwPollEvents();
			auto t1 = Clock::now();
			recordSection(S_POLL, tMs(t0, t1));

			// ` (grave) toggles all ImGui panels. Edge-detected so one tap flips it, and
			// ignored while an ImGui text field is focused so it doesn't eat the keystroke.
			{
				bool graveDown = glfwGetKey(bglWindow.getGLFWWindow(), GLFW_KEY_GRAVE_ACCENT) == GLFW_PRESS;
				if (graveDown && !gravePrev && !ImGui::GetIO().WantTextInput)
					showImgui = !showImgui;
				gravePrev = graveDown;
			}

			auto newTime = t1;
			float frameTime = std::chrono::duration<float, std::chrono::seconds::period>(newTime - currentTime).count();
			currentTime = newTime;
			totalTime += frameTime;

			t0 = Clock::now();
			if (freeFly)
				cameraController.moveInPlaneXZ(bglWindow.getGLFWWindow(), frameTime, viewerObject, 0);
			camera.setViewYXZ(viewerObject.transform.getTranslation(), viewerObject.transform.getRotation());
			glm::vec3 camPos = viewerObject.transform.getTranslation();
			glm::vec3 camFwd = -glm::vec3(camera.getInverseView()[2]); // camera images along -w of its basis
			float aspect = bglRenderer.getAspectRatio(); //aspect ratio might change due to window resize
			camera.setPerspectiveProjection(glm::radians(100.0f), aspect, 0.1f, 300.0f);
			recordSection(S_CAMERA, tMs(t0, Clock::now()));

			// Bone-posing gizmo input/logic (G toggles edit mode; W/E switch translate/rotate).
			{
				VkExtent2D ext = bglRenderer.getExtent();
				poseGizmo.update(bglWindow.getGLFWWindow(), camera,
					static_cast<float>(ext.width), static_cast<float>(ext.height));
			}
				
			t0 = Clock::now();
			OnUpdate(frameTime);
			recordSection(S_UPDATE, tMs(t0, Clock::now()));

			t0 = Clock::now();
			HierachySystem hs(registry);
			hs.ApplyHiarchialChange();
			recordSection(S_HIERARCHY, tMs(t0, Clock::now()));

			t0 = Clock::now();
			if (runPhys)
			{
				BGLJolt::GetInstance()->ApplyTransformToKinematic(frameTime);
				BGLJolt::GetInstance()->Step(frameTime, 3);
				BGLJolt::GetInstance()->ApplyPhysicsTransform();
			}
			recordSection(S_PHYSICS, tMs(t0, Clock::now()));

			t0 = Clock::now();
			GlobalUBO ubo{};
			ubo.updateCameraInfo(camera.getProjection(), camera.getView(), camera.getInverseView(),glm::inverse(camera.getProjection() * camera.getView()), exposure);
			pointLightSystem.update(ubo, 0);
			updateDirectionalUBO(registry, ubo, camPos, camFwd, aspect);
			recordSection(S_UBO, tMs(t0, Clock::now()));

			// Panels are built only when visible (toggled by `). NewFrame/Render still run
			// every frame so the ImGui backend stays balanced — it just emits empty draw data.
			t0 = Clock::now();
			if (showImgui)
			{
				drawImgui(camera, cameraController, smaaEdgeRenderSystem);
			} // showImgui
			recordSection(S_IMGUI, tMs(t0, Clock::now()));

			if (vsyncDirty)
			{
				vsyncDirty = false;
				bglRenderer.applyVsync(vsync);
			}

			// Re-register G-buffer, radiosity, and bloom mip descriptor entries after a window resize
			if (bglRenderer.consumeGBufferRecreated())
			{
				descriptorManager->writeDeferredRenderTargetToDescriptor(
					bglRenderer.getDRSampler(),
					bglRenderer.getDRDepthView(),
					bglRenderer.getDRNormalView(),
					bglRenderer.getDRAlbedoView(),
					bglRenderer.getDREmissionView());
				descriptorManager->storeTexture(
					bglRenderer.getRadiosityImageInfo(),
					bglRenderer.getRadiosityMemory(),
					bglRenderer.getRadiosityImage(),
					"RadiosityBuffer", true, radiosityHandle, false);
				descriptorManager->storeTexture(
					bglRenderer.getSmaaEdgeImageInfo(),
					bglRenderer.getSmaaEdgeMemory(),
					bglRenderer.getSmaaEdgeImage(),
					"SmaaEdges", true, smaaEdgeHandle, false);
				descriptorManager->storeTexture(
					bglRenderer.getSmaaWeightImageInfo(),
					bglRenderer.getSmaaWeightMemory(),
					bglRenderer.getSmaaWeightImage(),
					"SmaaWeights", true, smaaWeightHandle, false);
				descriptorManager->storeTexture(
					bglRenderer.getCompositeImageInfo(),
					bglRenderer.getCompositeMemory(),
					bglRenderer.getCompositeImage(),
					"CompositeLDR", true, compositeHandle, false);
				for (uint32_t i = 0; i < BGLRenderer::BLOOM_MIPS; i++)
				{
					descriptorManager->storeTexture(
						bglRenderer.getBloomMipImageInfo(i),
						bglRenderer.getBloomMipMemory(i),
						bglRenderer.getBloomMipImage(i),
						"BloomMip", true, bloomMipHandles[i], false);
				}
			}

			t0 = Clock::now();
			auto primaryCommandBuffer = bglRenderer.beginPrimaryCMD();
			recordSection(S_BEGINCMD, tMs(t0, Clock::now()));

			if (primaryCommandBuffer)
			{
				FrameInfo frameInfo{
					frameTime,
					totalTime,
					primaryCommandBuffer,
					camera,
					descriptorManager->getDescriptorSet(bglRenderer.getFrameIndex()),
					registry,
					fallbackAlbedoMap};

				// Advance skeletal animation once per frame, BEFORE the shadow and g-buffer
				// passes sample the pose — otherwise the shadow silhouette would lag the
				// deformed mesh by a frame (or render the bind pose entirely).
				for (auto [animEnt, anim] : registry.view<AnimationComponent>().each())
				{
					// Manual posing: resolve the authored pose into the dynamic palette region
					// (only when it changed) and skip clip playback entirely for this entity.
					if (anim.manualPose)
					{
						if (anim.poseDirty && anim.jointCount > 0)
						{
							std::vector<glm::mat4> palette(anim.jointCount);
							resolvePalette(anim.skeleton, anim.editPose, palette.data());
							skinManager->writePalette(anim.dynamicPaletteBase, palette.data(), anim.jointCount);
							anim.poseDirty = false;
						}
						continue;
					}

					if (!anim.playing)
						continue;
					anim.time += frameTime;
					const float dur = anim.clipDuration(anim.clip);
					if (dur > 0.0f && anim.time > dur)
						anim.time = anim.loop ? std::fmod(anim.time, dur) : dur;
				}

				int frameIdx = bglRenderer.getFrameIndex();
				uboBuffers->writeToIndex(&ubo, frameIdx);
				uboBuffers->flushIndex(frameIdx);

				compositRenderSystem.pushParams.debugMode = (uint32_t)gbufferDebugMode;
				compositRenderSystem.pushParams.bloomHandle = bloomEnabled ? bloomMipHandles[0] : 0u;
				compositRenderSystem.pushParams.bloomIntensity = bloomIntensity;
				compositRenderSystem.pushParams.radiosityHandle = radiosityHandle;
				compositRenderSystem.pushParams.smaaEdgeHandle = smaaEdgeHandle;
				compositRenderSystem.pushParams.smaaWeightHandle = smaaWeightHandle;

				t0 = Clock::now();
				if (ubo.hasDirLight)
				{
					bglDevice.BeginDebugUtilsLabel(primaryCommandBuffer, "Shadow");
					for (uint32_t ci = 0; ci < SHADOW_CASCADE_COUNT; ci++)
					{
						bglRenderer.beginShadowMapPass(primaryCommandBuffer, ci);
						shadowRenderSystem.renderShadowCasters(frameInfo, ci);
						skinnedShadowRenderSystem.renderShadowCasters(frameInfo, ci);
						bglRenderer.endCurrentRenderPass(primaryCommandBuffer);
					}
					bglDevice.EndDebugUtilsLabel(primaryCommandBuffer);
				}
				// gbuffer_fill
				bglDevice.BeginDebugUtilsLabel(primaryCommandBuffer, "gbuffer_fill");
				bglRenderer.beginDeferredRenderPass(primaryCommandBuffer);
				gBufferRenderSystem.renderEntities(frameInfo);
				skinnedGBufferRenderSystem.renderEntities(frameInfo);
				bglRenderer.endCurrentRenderPass(primaryCommandBuffer);
				bglDevice.EndDebugUtilsLabel(primaryCommandBuffer);
				// radiosity
				bglDevice.BeginDebugUtilsLabel(primaryCommandBuffer, "radiosity");
				bglRenderer.beginRadiosityPass(primaryCommandBuffer);
				radiosityRenderSystem.render(frameInfo);
				bglRenderer.endCurrentRenderPass(primaryCommandBuffer);
				bglDevice.EndDebugUtilsLabel(primaryCommandBuffer);
				recordSection(S_GBUFFER, tMs(t0, Clock::now()));
				// Forward transparent: blend HDR transparent lighting into the radiosity buffer
				// (no tonemap — composite does that), depth-tested read-only against the opaque
				// G-buffer depth. Bloom and composite then consume the combined radiosity buffer.
				bglDevice.BeginDebugUtilsLabel(primaryCommandBuffer, "transparent");
				bglRenderer.beginTransparentPass(primaryCommandBuffer);
				transparentRenderSystem.renderEntities(frameInfo);
				bglRenderer.endCurrentRenderPass(primaryCommandBuffer);
				bglDevice.EndDebugUtilsLabel(primaryCommandBuffer);

				// bloom (downsamples the radiosity buffer, now including transparent)
				t0 = Clock::now();
				if (bloomEnabled)
				{
					for (uint32_t i = 0; i < BGLRenderer::BLOOM_MIPS; i++)
					{
						bglDevice.BeginDebugUtilsLabel(primaryCommandBuffer, "bloom_down");
						bglRenderer.beginBloomDownsamplePass(primaryCommandBuffer, i);
						BloomDownPush dp{};
						dp.inputHandle = (i == 0) ? radiosityHandle : bloomMipHandles[i - 1];
						dp.threshold = (i == 0) ? bloomThreshold : 0.0f;
						dp.intensity = 1.0f;
						bloomRenderSystem.renderDownsample(frameInfo, dp);
						bglRenderer.endCurrentRenderPass(primaryCommandBuffer);
						bglDevice.EndDebugUtilsLabel(primaryCommandBuffer);
					}

					for (int i = (int)BGLRenderer::BLOOM_MIPS - 2; i >= 0; i--)
					{
						bglDevice.BeginDebugUtilsLabel(primaryCommandBuffer, "bloom_up");
						bglRenderer.beginBloomUpsamplePass(primaryCommandBuffer, i);
						BloomUpPush up{};
						up.inputHandle = bloomMipHandles[i + 1];
						up.filterRadius = 1.0f;
						up.weight = powf(bloomMipDecay, float(i));
						bloomRenderSystem.renderUpsample(frameInfo, up);
						bglRenderer.endCurrentRenderPass(primaryCommandBuffer);
						bglDevice.EndDebugUtilsLabel(primaryCommandBuffer);
					}
				}
				recordSection(S_BLOOM, tMs(t0, Clock::now()));

				// composite (radiosity + bloom -> tonemap+gamma) into the offscreen LDR buffer
				t0 = Clock::now();
				bglDevice.BeginDebugUtilsLabel(primaryCommandBuffer, "composite");
				bglRenderer.beginCompositePass(primaryCommandBuffer);
				compositRenderSystem.render(frameInfo);
				bglRenderer.endCurrentRenderPass(primaryCommandBuffer);
				bglDevice.EndDebugUtilsLabel(primaryCommandBuffer);

				// SMAA 1x on the composite (LDR, perceptual) — edge detect, blend weights.
				bglDevice.BeginDebugUtilsLabel(primaryCommandBuffer, "smaa_edge");
				bglRenderer.beginSmaaEdgePass(primaryCommandBuffer);
				smaaEdgeRenderSystem.render(frameInfo, compositeHandle);
				bglRenderer.endCurrentRenderPass(primaryCommandBuffer);

				bglRenderer.beginSmaaWeightPass(primaryCommandBuffer);
				smaaWeightRenderSystem.render(frameInfo, smaaEdgeHandle, smaaLuts.areaTex, smaaLuts.searchTex);
				bglRenderer.endCurrentRenderPass(primaryCommandBuffer);
				bglDevice.EndDebugUtilsLabel(primaryCommandBuffer);

				// SMAA pass 3 / present: neighborhood-blend composite + weights -> swapchain, then
				// overlays + ImGui. Blend is disabled (passthrough) when SMAA is off or a debug
				// view is active, so those show the raw composite.
				t0 = Clock::now();
				bglRenderer.blitGBufferDepthToSwapchain(primaryCommandBuffer);
				bglDevice.BeginDebugUtilsLabel(primaryCommandBuffer, "smaa_neighborhood/present");
				bglRenderer.beginSwapChainRenderPass(primaryCommandBuffer);
				smaaNeighborhoodRenderSystem.render(frameInfo, compositeHandle, smaaWeightHandle,
					smaaEnabled && gbufferDebugMode == 0);
				if (showWireframe)
					wireframeRenderSystem.renderEntities(frameInfo);
				if (drawBBox)
					wireframeRenderSystem.renderBBoxes(frameInfo);
				gizmoRenderSystem.render(frameInfo, poseGizmo);
				ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), primaryCommandBuffer);
				bglRenderer.endCurrentRenderPass(primaryCommandBuffer);
				bglDevice.EndDebugUtilsLabel(primaryCommandBuffer);
				recordSection(S_COMPOSITE, tMs(t0, Clock::now()));

				t0 = Clock::now();
				bglRenderer.endPrimaryCMD();
				recordSection(S_ENDCMD, tMs(t0, Clock::now()));
			}

			if (stutterDetect && frameTime * 1000.0f > stutterThresholdMs)
			{
				// find the section that took the most time this frame
				int worst = 0;
				for (int s = 1; s < S_COUNT; s++)
					if (sectMs[s] > sectMs[worst])
						worst = s;
				printf("[STUTTER] %.1f ms (%.0f fps) | worst: %s %.3f ms\n",
					   frameTime * 1000.0f, 1.0f / frameTime,
					   sectName[worst], sectMs[worst]);
			}

			if (showProfile)
			{
				profAccum += frameTime;
				profFrames++;
				if (profAccum >= 1.0)
				{
					double cpuSum = 0.0;
					for (int s = 0; s < S_COUNT; s++)
						cpuSum += perf[s].total;
					printf("=== Profile (%d frames, cpu_sum=%.3f ms) ===\n", profFrames, cpuSum / profFrames);
					for (int s = 0; s < S_COUNT; s++)
					{
						double avg = perf[s].n > 0 ? perf[s].total / perf[s].n : 0.0;
						double pct = cpuSum > 0.0 ? avg / (cpuSum / profFrames) * 100.0 : 0.0;
						char note = ' ';
						if (s == S_BEGINCMD)
							note = '*'; // fence wait / image acquire
						if (s == S_ENDCMD)
							note = '*'; // queue submit + present
						printf("  %c %s : %7.3f ms  (%5.1f%%)\n", note, sectName[s], avg, pct);
					}
					printf("  * = includes GPU sync point\n\n");
					for (int s = 0; s < S_COUNT; s++)
					{
						perf[s].total = 0.0;
						perf[s].n = 0;
					}
					profAccum = 0.0;
					profFrames = 0;
				}
			}
			if (maxFps > 0)
			{
				double targetMs = 1000.0 / maxFps;
				double elapsedMs = tMs(newTime, Clock::now());
				double sleepMs = targetMs - elapsedMs;
				if (sleepMs > 0.5)
				{
#ifdef _WIN32
					LARGE_INTEGER ft;
					ft.QuadPart = -(LONGLONG)(sleepMs * 10000.0); // 100-ns units, negative = relative
					SetWaitableTimer(hFpsTimer, &ft, 0, nullptr, nullptr, FALSE);
					WaitForSingleObject(hFpsTimer, INFINITE);
#else
					std::this_thread::sleep_for(std::chrono::microseconds((long long)(sleepMs * 1000.0)));
#endif
				}
			}
		}
#ifdef _WIN32
		if (hFpsTimer)
			CloseHandle(hFpsTimer);
#endif
		vkDeviceWaitIdle(BGLDevice::device());
	}

	void Application::initCommand()
	{
		CONSOLE->AddCommand("FREEFLY", this, ConsoleCommand::ToggleFly);
		CONSOLE->AddCommand("TOGGLEPHYSICS", this, ConsoleCommand::TogglePhys);
		CONSOLE->AddCommand("SHOWINFO", this, ConsoleCommand::ShowInfo);
		CONSOLE->AddCommand("SHOWWIREFRAME", this, ConsoleCommand::ShowWireframe);
		CONSOLE->AddCommand("R_DRAWBBOX", this, ConsoleCommand::DrawBBox);
		CONSOLE->AddCommand("PROFILE", this, ConsoleCommand::ShowProfile);
		CONSOLE->AddCommandWithArg("R_DRAWMODE", this, ConsoleCommand::SetDrawMode);
		CONSOLE->AddCommandWithArg("R_DRAWBLOOM", this, ConsoleCommand::SetBloom);
		CONSOLE->AddCommandWithArg("R_MAXFPS", this, ConsoleCommand::SetMaxFPS);
		CONSOLE->AddCommandWithArg("R_VSYNC", this, ConsoleCommand::SetVSync);
		CONSOLE->AddCommandWithArg("SKIN", this, ConsoleCommand::SetSkin);
		CONSOLE->AddCommandWithArg("R_MIPBIAS", this, ConsoleCommand::SetMipBias);
		CONSOLE->AddCommand("R_SMAA", this, ConsoleCommand::ToggleSmaa);
		CONSOLE->AddCommandWithArg("EDITMODE", this, ConsoleCommand::SetEditMode);
	}

	void Application::setTextureMipBias(float bias)
	{
		if (materialManager)
			materialManager->getTextureLoader().setMipLodBias(bias);
	}

	void Application::initJolt()
	{
		BGLJolt::Initialize(bglDevice, registry);
	}

	void Application::initImgui()
	{
		VkDescriptorPoolSize pool_sizes[] =
			{
				{VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
				{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
				{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
				{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
				{VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
				{VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
				{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
				{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
				{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
				{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
				{VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000}};

		VkDescriptorPoolCreateInfo pool_info = {};
		pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
		pool_info.maxSets = 1000;
		pool_info.poolSizeCount = static_cast<uint32_t>(std::size(pool_sizes));
		pool_info.pPoolSizes = pool_sizes;

		VK_CHECK(vkCreateDescriptorPool(BGLDevice::device(), &pool_info, nullptr, &imguiPool));

		ImGui::CreateContext();
		ImGui_ImplGlfw_InitForVulkan(bglWindow.getGLFWWindow(), true);

		ImGui_ImplVulkan_InitInfo init_info = {};
		init_info.ApiVersion = VK_API_VERSION_1_3;
		init_info.Instance = bglDevice.getInstance();
		init_info.PhysicalDevice = bglDevice.getPhysicalDevice();
		init_info.Device = BGLDevice::device();
		init_info.QueueFamily = bglDevice.findPhysicalQueueFamilies().graphicsFamily;
		init_info.Queue = bglDevice.graphicsQueue();
		init_info.DescriptorPool = imguiPool;
		init_info.MinImageCount = 3;
		init_info.ImageCount = 3;
		init_info.PipelineInfoMain.RenderPass = bglRenderer.getSwapChainRenderPass();
		init_info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

		ImGui_ImplVulkan_Init(&init_info);
	}
	void Application::drawImgui(BGLCamera& camera, KeyboardMovementController& cameraController, SmaaEdgeRenderSystem& edgeRender)
	{
		ImGui_ImplVulkan_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();
		// Pose-gizmo edit-mode overlay, pinned to the top-left.
		if (poseGizmo.editModeOn())
		{
			const ImGuiWindowFlags ovFlags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize
				| ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing
				| ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs;
			ImGui::SetNextWindowPos(ImVec2(10.0f, 10.0f), ImGuiCond_Always);
			ImGui::SetNextWindowBgAlpha(0.35f);
			ImGui::Begin("##editmode_overlay", nullptr, ovFlags);
			ImGui::TextUnformatted("Edit mode active. Press T for transform mode, R for rotation mode");
			ImGui::Text("Press L to toggle local/global space (now: %s)",
				poseGizmo.localSpaceOn() ? "local" : "global");
			ImGui::End();
		}
		// Let the derived app draw its own panels and mutate the scene/registry
		// before anything reads it this frame (e.g. map build/load buttons).
		OnDrawGui();
		VkExtent2D ext = bglRenderer.getExtent();
		if (showInfo)
			DrawInfoPanels(registry, ext.width, ext.height, camera.getProjection(), camera.getView());
		ConsoleApp::Instance()->Draw("Console", nullptr);
		ImGui::Begin("Settings");
		ImGui::SliderFloat("Mouse Sensitivity", &cameraController.mouseSensitivity, 0.05f, 0.3f);
		ImGui::Separator();
		{
			auto dirView = registry.view<DirectionalLightComponent>();
			if (!dirView.empty())
			{
				auto &dlc = dirView.get<DirectionalLightComponent>(*dirView.begin());
				ImGui::Text("Shadow Cascades");
				ImGui::SliderFloat("Cascade 0 End", &dlc.cascadeEnds.x, 0.001f, 20.0f, "%.1f", ImGuiSliderFlags_Logarithmic);
				ImGui::SliderFloat("Cascade 1 End", &dlc.cascadeEnds.y, 0.001f, 30.0f, "%.1f", ImGuiSliderFlags_Logarithmic);
				ImGui::SliderFloat("Cascade 2 End", &dlc.cascadeEnds.z, 0.001f, 70.0f, "%.1f", ImGuiSliderFlags_Logarithmic);
				ImGui::SliderFloat("Cascade 3 End", &dlc.cascadeEnds.w, 0.001f, 100.0f, "%.1f", ImGuiSliderFlags_Logarithmic);
				// keep the splits strictly increasing
				// dlc.cascadeEnds.y = fmaxf(dlc.cascadeEnds.y, dlc.cascadeEnds.x + 0.1f);
				// dlc.cascadeEnds.z = fmaxf(dlc.cascadeEnds.z, dlc.cascadeEnds.y + 0.1f);
				// dlc.cascadeEnds.w = fmaxf(dlc.cascadeEnds.w, dlc.cascadeEnds.z + 0.1f);
				ImGui::SliderFloat("Caster Range", &dlc.casterRange, 10.0f, 2000.0f);
				ImGui::SliderFloat("Shadow Bias", &dlc.shadowBiasMin, 0.0f, 0.02f, "%.4f");
				ImGui::SliderFloat("Shadow Bias Slope", &dlc.shadowBiasSlope, 0.0f, 0.05f, "%.4f");
				ImGui::Separator();
			}
		}
		ImGui::Checkbox("Bloom", &bloomEnabled);
		if (bloomEnabled)
		{
			ImGui::SliderFloat("Bloom Intensity", &bloomIntensity, 0.0f, 2.0f);
			ImGui::SliderFloat("Bloom Threshold", &bloomThreshold, 0.001f, 2.0f, "%.4f", ImGuiSliderFlags_Logarithmic);
			ImGui::SliderFloat("Bloom Mip Decay", &bloomMipDecay, 0.5f, 1.0f);
			ImGui::SliderFloat("Exposure", &exposure, 0.001f, 2.0f, "%.4f", ImGuiSliderFlags_Logarithmic);
		}
		ImGui::Text("SMAA");
		// Edge detection method: luma (default), color (catches chroma edges, ~2x cost), or
		// depth (cheapest, geometry-only; wants a much smaller threshold — depth is non-linear).
		ImGui::Combo("SMAA Edge Method", &edgeRender.edgeMethod, "Predicated Luma\0Luma\0Color\0Depth\0Custom\0");
		// Log scale can't represent 0 — use a small positive min (like Bloom Threshold) or the
		// low end of the slider is unusable. SMAA's useful luma-threshold range is ~0.05-0.2.
		ImGui::SliderFloat("SMAA Threshold", &edgeRender.edgeThreshold, 0.001f, 2.0f, "%.4f", ImGuiSliderFlags_Logarithmic);
		// Unused by the depth method.
		ImGui::SliderFloat("SMAA Local Constrast Adaptation", &edgeRender.localConstrastAdapt, 1.0f, 4.0f, "%.4f");
		ImGui::End();
		ImGui::Render();
	}
} // namespace bagel
