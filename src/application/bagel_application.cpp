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
#include "bagel_engine_config.hpp"

#include "Jolt/Jolt.h"

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

			// cameraFovDegrees is HORIZONTAL; derive vertical from aspect (must match the
			// projection in run()).
			float tanX = tanf(glm::radians(cameraFovDegrees) * 0.5f);
			float tanY = tanX / aspect;
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

		registerDescriptorEntries();
		// SMAA precomputed LUTs (AreaTex/SearchTex) — consumed by the blending-weight pass.
		SmaaLutHandles smaaLuts = loadSmaaLuts(materialManager->getTextureLoader());

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

		PlanetGBufferRenderSystem planetGBufferRenderSystem{
			bglRenderer.getDeferredRenderPass(),
			pipelineDescriptorSetLayouts,
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

		// Procedural ocean. Same HDR pass as the transparents, drawn after them.
		WaterRenderSystem waterRenderSystem{
			bglRenderer.getTransparentRenderPass(),
			pipelineDescriptorSetLayouts,
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

		auto frameCurrentTime = Clock::now(); // same clock as frameLastTime (they're subtracted each frame)

		initCommand();

		BGLJolt::GetInstance()->SetGravity({0, -0.0f, 0});
		BGLJolt::GetInstance()->SetSimulationTimescale(0.5f);
		BGLJolt::GetInstance()->SetComponentActivityAll(true);

		OnSceneLoad();

		// Game loop
		float fpsAccum = 0.0f;
		int fpsFrames = 0;
		float totalTime = 0.0f;

		// Profiling state (perf[]/sectMs[]/profAccum/profFrames + the Sect enum and sectName[])
		// now lives on the Application as members so the extracted profile() can reach it.
		auto recordSection = [&](Sect s, double ms)
		{
			sectMs[s] = ms;
			perf[s].total += ms;
			perf[s].n++;
		};

#ifdef _WIN32
		HANDLE hFpsTimer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
		if (!hFpsTimer)
			hFpsTimer = CreateWaitableTimerW(nullptr, FALSE, nullptr); // fallback for older Windows 10
#endif

		bool gravePrev = false; // edge-detect the hard-coded ` UI-toggle key
		HierachySystem hierachy(registry);
		auto frameLastTime = Clock::now();
		while (!bglWindow.shouldClose())
		{
			// Timestamp the start of this frame. frameTime = (this start - previous start), and
			// syncFrameRate() sleeps until this start + the target frame duration. Without this
			// per-frame update frameTime stays 0, so dt-scaled movement (WASD) never advances.
			frameLastTime = Clock::now();
			// ` (grave) toggles all ImGui panels — kept HARD-CODED on purpose: it must always
			// work (never rebindable or clearable via `unbindall`) so you can't lock yourself out
			// of the UI/console. Edge-detected; ignored while an ImGui text field is focused.
			{
				auto t0 = Clock::now();
				glfwPollEvents();

				bool graveDown = glfwGetKey(bglWindow.getGLFWWindow(), GLFW_KEY_GRAVE_ACCENT) == GLFW_PRESS;
				if (graveDown && !gravePrev && !ImGui::GetIO().WantTextInput)
					showImgui = !showImgui;
				gravePrev = graveDown;

				// Keybinds: fire each bound console command on its key's press edge (Source-style
				// `bind`). Suppressed while an ImGui text field is focused so typing doesn't fire binds.
				keybinds.poll(bglWindow.getGLFWWindow(), ImGui::GetIO().WantTextInput,
							  [](const char *cmd)
							  { ConsoleApp::Instance()->Run(cmd); });
				recordSection(S_POLL, tMs(t0, Clock::now()));
			}

			// calculate frame time
			float frameTime = std::chrono::duration<float, std::chrono::seconds::period>(frameLastTime - frameCurrentTime).count();
			frameCurrentTime = frameLastTime;
			totalTime += frameTime;

			auto t0 = Clock::now();
			// A scene can request a one-shot camera teleport (e.g. to frame the planet).
			if (spawnCameraPosDirty)
			{
				viewerObject.transform.setTranslation(spawnCameraPos);
				spawnCameraPosDirty = false;
			}
			if (freeFly)
				cameraController.moveInPlaneXZ(bglWindow.getGLFWWindow(), frameTime, viewerObject, 0);
			camera.setViewYXZ(viewerObject.transform.getTranslation(), viewerObject.transform.getRotation());
			cameraWorldPos = viewerObject.transform.getTranslation();  // expose to OnUpdate (planet LOD, etc.)
			glm::vec3 camFwd = -glm::vec3(camera.getInverseView()[2]); // camera images along -w of its basis
			float aspect = bglRenderer.getAspectRatio();			   // aspect ratio might change due to window resize
			// cameraFovDegrees is HORIZONTAL; derive vertical from the aspect ratio so the
			// horizontal framing stays fixed as the window/aspect changes.
			float fovX = glm::radians(cameraFovDegrees);
			float fovY = 2.0f * atanf(tanf(fovX * 0.5f) / aspect);
			camera.setPerspectiveProjection(fovY, aspect, cameraNear, cameraFar);
			recordSection(S_CAMERA, tMs(t0, Clock::now()));

			// Bone-posing gizmo input/logic (G toggles edit mode; W/E switch translate/rotate).
			{
				t0 = Clock::now();
				VkExtent2D ext = bglRenderer.getExtent();
				poseGizmo.update(bglWindow.getGLFWWindow(), camera,
								 static_cast<float>(ext.width), static_cast<float>(ext.height));
				// Planet height painter (B toggles paint mode; LMB drags paint the surface).
				planetPaint.update(bglWindow.getGLFWWindow(), camera,
								   static_cast<float>(ext.width), static_cast<float>(ext.height), registry);
				recordSection(S_GIZMO, tMs(t0, Clock::now()));
			}

			{
				t0 = Clock::now();
				OnUpdate(frameTime);
				recordSection(S_UPDATE, tMs(t0, Clock::now()));
			}

			{
				t0 = Clock::now();
				hierachy.ResolveSkeletonGlobals(); // resolve bones BEFORE parents so attachments are current
				hierachy.ApplyHiarchialChange();
				recordSection(S_HIERARCHY, tMs(t0, Clock::now()));
			}

			{
				t0 = Clock::now();
				if (runPhys)
				{
					BGLJolt::GetInstance()->ApplyTransformToKinematic(frameTime);
					BGLJolt::GetInstance()->Step(frameTime, 3);
					BGLJolt::GetInstance()->ApplyPhysicsTransform();
				}
				recordSection(S_PHYSICS, tMs(t0, Clock::now()));
			}

			t0 = Clock::now();
			GlobalUBO ubo{};
			ubo.updateCameraInfo(camera.getProjection(), camera.getView(), camera.getInverseView(), glm::inverse(camera.getProjection() * camera.getView()), exposure);
			pointLightSystem.update(ubo, 0);
			updateDirectionalUBO(registry, ubo, cameraWorldPos, camFwd, aspect);
			recordSection(S_UBO, tMs(t0, Clock::now()));

			{
				// Panels are built only when visible (toggled by `). NewFrame/Render still run
				// every frame so the ImGui backend stays balanced — it just emits empty draw data.
				// (drawImgui must be called every frame; it gates the PANELS on showImgui internally.
				// Gating the whole call would skip Render(), freezing the last frame's panels on screen.)
				t0 = Clock::now();
				drawImgui(camera, cameraController, smaaEdgeRenderSystem);
				recordSection(S_IMGUI, tMs(t0, Clock::now()));
			}

			if (vsyncDirty)
			{
				vsyncDirty = false;
				bglRenderer.applyVsync(vsync);
			}

			updateAnimation(frameTime);

			// cache transform
			// all edits to the transform components should be finished by now
			// this part caches the mat4 calculation on all transform components so recalculation is unnecessary.
			// this means render systems should never edit the transforms
			cacheTransforms();

			reregisterDescriptorEntries();

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

				int frameIdx = bglRenderer.getFrameIndex();
				uboBuffers->writeToIndex(&ubo, frameIdx);
				uboBuffers->flushIndex(frameIdx);

				compositRenderSystem.pushParams.debugMode = (uint32_t)gbufferDebugMode;
				compositRenderSystem.pushParams.bloomHandle = bloomEnabled ? bloomMipHandles[0] : 0u;
				compositRenderSystem.pushParams.bloomIntensity = bloomIntensity;
				compositRenderSystem.pushParams.radiosityHandle = radiosityHandle;
				compositRenderSystem.pushParams.smaaEdgeHandle = smaaEdgeHandle;
				compositRenderSystem.pushParams.smaaWeightHandle = smaaWeightHandle;

				if (ubo.hasDirLight)
				{
					bglDevice.BeginDebugUtilsLabel(primaryCommandBuffer, "Shadow");
					for (uint32_t ci = 0; ci < SHADOW_CASCADE_COUNT; ci++)
					{
						bglRenderer.beginShadowMapPass(primaryCommandBuffer, ci);
						shadowRenderSystem.renderShadowCasters(frameInfo, ci, ubo.directionalLight.lightSpaceMatrix[ci]);
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
				planetGBufferRenderSystem.renderEntities(frameInfo);
				bglRenderer.endCurrentRenderPass(primaryCommandBuffer);
				bglDevice.EndDebugUtilsLabel(primaryCommandBuffer);
				// radiosity
				bglDevice.BeginDebugUtilsLabel(primaryCommandBuffer, "radiosity");
				bglRenderer.beginRadiosityPass(primaryCommandBuffer);
				radiosityRenderSystem.render(frameInfo);
				bglRenderer.endCurrentRenderPass(primaryCommandBuffer);
				bglDevice.EndDebugUtilsLabel(primaryCommandBuffer);
				// Forward transparent: blend HDR transparent lighting into the radiosity buffer
				// (no tonemap — composite does that), depth-tested read-only against the opaque
				// G-buffer depth. Bloom and composite then consume the combined radiosity buffer.
				bglDevice.BeginDebugUtilsLabel(primaryCommandBuffer, "transparent");
				bglRenderer.beginTransparentPass(primaryCommandBuffer);
				transparentRenderSystem.renderEntities(frameInfo);
				// Water is drawn AFTER the transparent objects, in this same HDR pass.
				// TODO(pre/post-water transparents): currently ALL transparents draw before the
				// water. For correct submerged-vs-surface transparency, split the transparent queue
				// into a pre-water group (here, before the water) and a post-water group (after the
				// water — atmosphere / glass over the surface). See WaterRenderSystem.
				waterRenderSystem.renderEntities(frameInfo);
				bglRenderer.endCurrentRenderPass(primaryCommandBuffer);
				bglDevice.EndDebugUtilsLabel(primaryCommandBuffer);

				// bloom (downsamples the radiosity buffer, now including transparent)
				if (bloomEnabled)
				{
					bglDevice.BeginDebugUtilsLabel(primaryCommandBuffer, "bloom_down");
					for (uint32_t i = 0; i < BGLRenderer::BLOOM_MIPS; i++)
					{
						bglRenderer.beginBloomDownsamplePass(primaryCommandBuffer, i);
						BloomDownPush dp{};
						dp.inputHandle = (i == 0) ? radiosityHandle : bloomMipHandles[i - 1];
						dp.threshold = (i == 0) ? bloomThreshold : 0.0f;
						dp.intensity = 1.0f;
						bloomRenderSystem.renderDownsample(frameInfo, dp);
						bglRenderer.endCurrentRenderPass(primaryCommandBuffer);
					}
					bglDevice.EndDebugUtilsLabel(primaryCommandBuffer);
					bglDevice.BeginDebugUtilsLabel(primaryCommandBuffer, "bloom_up");
					for (int i = (int)BGLRenderer::BLOOM_MIPS - 2; i >= 0; i--)
					{
						bglRenderer.beginBloomUpsamplePass(primaryCommandBuffer, i);
						BloomUpPush up{};
						up.inputHandle = bloomMipHandles[i + 1];
						up.filterRadius = 1.0f;
						up.weight = powf(bloomMipDecay, float(i));
						bloomRenderSystem.renderUpsample(frameInfo, up);
						bglRenderer.endCurrentRenderPass(primaryCommandBuffer);
					}
					bglDevice.EndDebugUtilsLabel(primaryCommandBuffer);
				}

				// composite (radiosity + bloom -> tonemap+gamma) into the offscreen LDR buffer
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

				bglRenderer.blitGBufferDepthToSwapchain(primaryCommandBuffer);
				bglDevice.BeginDebugUtilsLabel(primaryCommandBuffer, "smaa_neighborhood/present");
				bglRenderer.beginSwapChainRenderPass(primaryCommandBuffer);
				smaaNeighborhoodRenderSystem.render(frameInfo, compositeHandle, smaaWeightHandle, smaaEnabled && gbufferDebugMode == 0);

				if (showWireframe)
				{
					wireframeRenderSystem.renderModelsWireframe(frameInfo); // planet + static models as wireframe
					wireframeRenderSystem.renderEntities(frameInfo);		// any WireframeComponent / collision overlay
				}
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
			if (stutterDetect)
				detectStutter(frameTime);
			if (showProfile)
				profile(frameTime);
			syncFrameRate(hFpsTimer, frameLastTime);
		}
#ifdef _WIN32
		if (hFpsTimer)
			CloseHandle(hFpsTimer);
#endif
		vkDeviceWaitIdle(BGLDevice::device());
	}
	inline double Application::tMs(Clock::time_point a, Clock::time_point b)
	{
		return std::chrono::duration<double, std::milli>(b - a).count();
	};
	void Application::syncFrameRate(HANDLE timerHandle, std::chrono::steady_clock::time_point frameLastTime)
	{
		if (maxFps > 0)
		{
			double targetMs = 1000.0 / maxFps;
			double elapsedMs = tMs(frameLastTime, Clock::now());
			double sleepMs = targetMs - elapsedMs;
			if (sleepMs > 0.5)
			{
#ifdef _WIN32
				LARGE_INTEGER ft;
				ft.QuadPart = -(LONGLONG)(sleepMs * 10000.0); // 100-ns units, negative = relative
				SetWaitableTimer(timerHandle, &ft, 0, nullptr, nullptr, FALSE);
				WaitForSingleObject(timerHandle, INFINITE);
#else
				std::this_thread::sleep_for(std::chrono::microseconds((long long)(sleepMs * 1000.0)));
#endif
			}
		}
	}
	void Application::cacheTransforms()
	{
		for (const auto &[_, tc] : registry.view<TransformComponent>().each())
		{
			tc.cacheMat4();
		}
	}
	void Application::profile(double frameTime)
	{
		profAccum += frameTime;
		profFrames++;
		if (profAccum < 1.0)
			return;

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
	// Caller gates on the stutterDetect toggle; the threshold test + report live here (mirrors
	// profile()). Reads the same per-frame sectMs[]/sectName[] the recordSection lambda fills.
	void Application::detectStutter(double frameTime)
	{
		if (frameTime * 1000.0 <= stutterThresholdMs)
			return;
		// find the section that took the most time this frame
		int worst = 0;
		for (int s = 1; s < S_COUNT; s++)
			if (sectMs[s] > sectMs[worst])
				worst = s;
		printf("[STUTTER] %.1f ms (%.0f fps) | worst: %s %.3f ms\n",
			   frameTime * 1000.0, 1.0 / frameTime,
			   sectName[worst], sectMs[worst]);
	}
	void Application::setTextureMipBias(float bias)
	{
		if (materialManager)
			materialManager->getTextureLoader().setMipLodBias(bias);
	}
	void Application::updateAnimation(double frameTime)
	{

		// Advance skeletal animation once per frame, BEFORE the shadow and g-buffer
		// passes sample the pose — otherwise the shadow silhouette would lag the
		// deformed mesh by a frame (or render the bind pose entirely).
		for (auto [animEnt, anim] : registry.view<AnimationComponent>().each())
		{
			// Manual posing: resolve the authored pose (plus any IK setups) into the dynamic
			// palette region, and skip clip playback entirely for this entity. Re-runs when
			// the pose changed or any IK is active (IK goal/pole joints may move each frame).
			if (anim.manualPose)
			{
				bool hasIK = false;
				for (const IKSetup &s : anim.ikSetups)
					if (s.valid())
					{
						hasIK = true;
						break;
					}
				if ((anim.poseDirty || hasIK) && anim.jointCount > 0)
				{
					// editPose + IK -> final pose (same helper the gizmo uses to place markers).
					Pose finalPose;
					applyManualPose(anim.skeleton, anim.editPose, anim.ikSetups, finalPose);
					std::vector<glm::mat4> palette(anim.jointCount);
					resolvePalette(anim.skeleton, finalPose, palette.data());
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
	}
	void Application::registerDescriptorEntries()
	{
		descriptorManager->writeDeferredRenderTargetToDescriptor(
			bglRenderer.getDRSampler(),
			bglRenderer.getDRDepthView(),
			bglRenderer.getDRNormalView(),
			bglRenderer.getDRAlbedoView(),
			bglRenderer.getDREmissionView());
		// First call appends each render target and captures the bindless handle it was
		// assigned; later calls (after a gbuffer/window-resize recreate) overwrite those same
		// slots in place via the designated-handle path. The handles aren't valid until the
		// first append, so the initial registration MUST use designated=false — otherwise
		// storeTexture indexes a slot that doesn't exist yet (vector subscript out of range).
		const bool reuse = renderTargetsRegistered;
		radiosityHandle = descriptorManager->storeTexture(
			bglRenderer.getRadiosityImageInfo(),
			bglRenderer.getRadiosityMemory(),
			bglRenderer.getRadiosityImage(),
			"RadiosityBuffer", reuse, radiosityHandle, false);
		smaaEdgeHandle = descriptorManager->storeTexture(
			bglRenderer.getSmaaEdgeImageInfo(),
			bglRenderer.getSmaaEdgeMemory(),
			bglRenderer.getSmaaEdgeImage(),
			"SmaaEdges", reuse, smaaEdgeHandle, false);
		smaaWeightHandle = descriptorManager->storeTexture(
			bglRenderer.getSmaaWeightImageInfo(),
			bglRenderer.getSmaaWeightMemory(),
			bglRenderer.getSmaaWeightImage(),
			"SmaaWeights", reuse, smaaWeightHandle, false);
		compositeHandle = descriptorManager->storeTexture(
			bglRenderer.getCompositeImageInfo(),
			bglRenderer.getCompositeMemory(),
			bglRenderer.getCompositeImage(),
			"CompositeLDR", reuse, compositeHandle, false);
		for (uint32_t i = 0; i < BGLRenderer::BLOOM_MIPS; i++)
		{
			bloomMipHandles[i] = descriptorManager->storeTexture(
				bglRenderer.getBloomMipImageInfo(i),
				bglRenderer.getBloomMipMemory(i),
				bglRenderer.getBloomMipImage(i),
				"BloomMip", reuse, bloomMipHandles[i], false);
		}
		renderTargetsRegistered = true;
	}
	void Application::reregisterDescriptorEntries()
	{
		// Re-register G-buffer, radiosity, and bloom mip descriptor entries after a window resize
		if (bglRenderer.consumeGBufferRecreated())
		{
			registerDescriptorEntries();
		}
	}
	void Application::initJolt()
	{
		BGLJolt::Initialize(bglDevice, registry);
	}
} // namespace bagel
