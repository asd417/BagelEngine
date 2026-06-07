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

#include "Jolt/Jolt.h"

#define GLOBAL_DESCRIPTOR_COUNT 1000

namespace bagel {

	Application::Application()
	{
		globalPool = BGLDescriptorPool::Builder(bglDevice)
			.setMaxSets(BGLSwapChain::MAX_FRAMES_IN_FLIGHT * GLOBAL_DESCRIPTOR_COUNT)
			.addPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,          BGLSwapChain::MAX_FRAMES_IN_FLIGHT * GLOBAL_DESCRIPTOR_COUNT)
			.addPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,          BGLSwapChain::MAX_FRAMES_IN_FLIGHT * GLOBAL_DESCRIPTOR_COUNT)
			.addPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,  BGLSwapChain::MAX_FRAMES_IN_FLIGHT * GLOBAL_DESCRIPTOR_COUNT + 10)
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
	}

	Application::~Application()
	{
		ImGui_ImplVulkan_Shutdown();
		vkDestroyDescriptorPool(BGLDevice::device(), imguiPool, nullptr);
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

		bglRenderer.setUpOffScreenRenderPass(WIDTH / 2, HEIGHT / 2);

		uint32_t offscreenRenderTargetHandle = descriptorManager->storeTexture(
			bglRenderer.getOffscreenImageInfo(),
			bglRenderer.getOffscreenMemory(),
			bglRenderer.getOffscreenImage(),
			"OffscreenRenderTarget", false, 0, false);

		uint32_t bloomMipHandles[BGLRenderer::BLOOM_MIPS];
		for (uint32_t i = 0; i < BGLRenderer::BLOOM_MIPS; i++) {
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

		std::vector<VkDescriptorSetLayout> pipelineDescriptorSetLayouts = { descriptorManager->getDescriptorSetLayout() };

		GBufferRenderSystem gBufferRenderSystem{
			bglRenderer.getDeferredRenderPass(),
			pipelineDescriptorSetLayouts,
			descriptorManager,
			registry };

		CompositRenderSystem compositRenderSystem{
			bglRenderer.getSwapChainRenderPass(),
			pipelineDescriptorSetLayouts,
			descriptorManager,
			registry };

		WireframeRenderSystem wireframeRenderSystem{
			bglRenderer.getSwapChainRenderPass(),
			pipelineDescriptorSetLayouts,
			descriptorManager,
			registry,
			bglDevice };

		PointLightSystem pointLightSystem{
			bglRenderer.getSwapChainRenderPass(),
			pipelineDescriptorSetLayouts,
			descriptorManager,
			registry,
			bglDevice };

		TransparentRenderSystem transparentRenderSystem{
			bglRenderer.getSwapChainRenderPass(),
			pipelineDescriptorSetLayouts,
			descriptorManager,
			registry };

		BloomRenderSystem bloomRenderSystem{
			bglRenderer.getBloomMipRenderPassClear(0),
			pipelineDescriptorSetLayouts,
			descriptorManager };

		RadiosityRenderSystem radiosityRenderSystem{
			bglRenderer.getRadiosityRenderPass(),
			pipelineDescriptorSetLayouts,
			descriptorManager };

		BGLCamera camera{};

		auto viewerObject = BGLGameObject::createGameObject();
		viewerObject.transform.setTranslation({0.0f, -3.0f, 0.0f});
		KeyboardMovementController cameraController{};

		auto currentTime = std::chrono::high_resolution_clock::now();

		initCommand();

		BGLJolt::GetInstance()->SetGravity({0,-0.0f,0});
		BGLJolt::GetInstance()->SetSimulationTimescale(0.5f);
		BGLJolt::GetInstance()->SetComponentActivityAll(true);

		OnSceneLoad();

		// Game loop
		float fpsAccum = 0.0f;
		int   fpsFrames = 0;
		float totalTime = 0.0f;

		// Per-section profiling accumulators (ms totals + sample counts)
		using Clock = std::chrono::high_resolution_clock;
		struct PerfSection { double total = 0.0; int n = 0; };
		enum Sect {
			S_POLL, S_CAMERA, S_UPDATE, S_HIERARCHY, S_PHYSICS,
			S_UBO, S_IMGUI, S_BEGINCMD, S_GBUFFER, S_BLOOM, S_COMPOSITE, S_ENDCMD,
			S_COUNT
		};
		static const char* sectName[S_COUNT] = {
			"poll_events", "camera     ", "on_update  ", "hierarchy  ", "physics    ",
			"ubo+lights ", "imgui      ", "begin_cmd  ", "gbuffer    ", "bloom      ",
			"composite  ", "end_cmd    "
		};
		PerfSection perf[S_COUNT]{};
		double profAccum = 0.0;
		int    profFrames = 0;
		double sectMs[S_COUNT]{};
		auto recordSection = [&](Sect s, double ms) {
			sectMs[s] = ms;
			perf[s].total += ms;
			perf[s].n++;
		};

		auto tMs = [](Clock::time_point a, Clock::time_point b) {
			return std::chrono::duration<double, std::milli>(b - a).count();
		};

		camera.setViewDirection(glm::vec3(-1.0f, -2.0f, -2.0f), glm::vec3(0.0f, 0.0f, 2.5f));

#ifdef _WIN32
		HANDLE hFpsTimer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
		if (!hFpsTimer) hFpsTimer = CreateWaitableTimerW(nullptr, FALSE, nullptr); // fallback for older Windows 10
#endif

		while (!bglWindow.shouldClose())
		{
			auto t0 = Clock::now();
			glfwPollEvents();
			auto t1 = Clock::now();
			recordSection(S_POLL, tMs(t0, t1));

			auto newTime = t1;
			float frameTime = std::chrono::duration<float, std::chrono::seconds::period>(newTime - currentTime).count();
			currentTime = newTime;
			totalTime += frameTime;

			t0 = Clock::now();
			if (freeFly) cameraController.moveInPlaneXZ(bglWindow.getGLFWWindow(), frameTime, viewerObject, 0);
			camera.setViewYXZ(viewerObject.transform.getTranslation(), viewerObject.transform.getRotation());
			float aspect = bglRenderer.getAspectRatio();
			camera.setPerspectiveProjection(glm::radians(100.0f), aspect, 0.1f, 300.0f);
			recordSection(S_CAMERA, tMs(t0, Clock::now()));

			t0 = Clock::now();
			OnUpdate(frameTime);
			recordSection(S_UPDATE, tMs(t0, Clock::now()));

			t0 = Clock::now();
			HierachySystem hs(registry);
			hs.ApplyHiarchialChange();
			recordSection(S_HIERARCHY, tMs(t0, Clock::now()));

			t0 = Clock::now();
			if (runPhys) {
				BGLJolt::GetInstance()->ApplyTransformToKinematic(frameTime);
				BGLJolt::GetInstance()->Step(frameTime, 3);
				BGLJolt::GetInstance()->ApplyPhysicsTransform();
			}
			recordSection(S_PHYSICS, tMs(t0, Clock::now()));

			t0 = Clock::now();
			GlobalUBO ubo{};
			ubo.updateCameraInfo(camera.getProjection(), camera.getView(), camera.getInverseView(),
				glm::inverse(camera.getProjection() * camera.getView()));
			pointLightSystem.update(ubo, 0);
			ubo.exposure = exposure;
			recordSection(S_UBO, tMs(t0, Clock::now()));

			t0 = Clock::now();
			ImGui_ImplVulkan_NewFrame();
			ImGui_ImplGlfw_NewFrame();
			ImGui::NewFrame();
			VkExtent2D ext = bglRenderer.getExtent();
			if(showInfo) DrawInfoPanels(registry, ext.width, ext.height, camera.getProjection(), camera.getView());
			ConsoleApp::Instance()->Draw("Console", nullptr);
			ImGui::Begin("Settings");
			ImGui::SliderFloat("Mouse Sensitivity", &cameraController.mouseSensitivity, 0.05f, 0.3f);
			ImGui::Separator();
			ImGui::Checkbox("Bloom", &bloomEnabled);
			if (bloomEnabled) {
				ImGui::SliderFloat("Bloom Intensity", &bloomIntensity, 0.0f, 2.0f);
				ImGui::SliderFloat("Bloom Threshold", &bloomThreshold, 0.001f, 2.0f, "%.4f", ImGuiSliderFlags_Logarithmic);
				ImGui::SliderFloat("Bloom Mip Decay", &bloomMipDecay, 0.5f, 1.0f);
			}
			ImGui::End();
			ImGui::Render();
			recordSection(S_IMGUI, tMs(t0, Clock::now()));

			if (vsyncDirty) {
				vsyncDirty = false;
				bglRenderer.applyVsync(vsync);
			}

			// Re-register G-buffer, radiosity, and bloom mip descriptor entries after a window resize
			if (bglRenderer.consumeGBufferRecreated()) {
				descriptorManager->writeDeferredRenderTargetToDescriptor(
					bglRenderer.getDRSampler(),
					bglRenderer.getDRDepthView(),
					bglRenderer.getDRNormalView(),
					bglRenderer.getDRAlbedoView(),
					bglRenderer.getDREmissionView());
				descriptorManager->storeTexture(
					bglRenderer.getRadiosityImageInfo(),
					bglRenderer.getRadiosityMemory(),//dsad
					bglRenderer.getRadiosityImage(),
					"RadiosityBuffer", true, radiosityHandle, false);
				for (uint32_t i = 0; i < BGLRenderer::BLOOM_MIPS; i++) {
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

			if (primaryCommandBuffer) {
				FrameInfo frameInfo{
					frameTime,
					totalTime,
					primaryCommandBuffer,
					camera,
					descriptorManager->getDescriptorSet(bglRenderer.getFrameIndex()),
					registry,
					fallbackAlbedoMap
				};

				int frameIdx = bglRenderer.getFrameIndex();
				uboBuffers->writeToIndex(&ubo, frameIdx);
				uboBuffers->flushIndex(frameIdx);

				compositRenderSystem.pushParams.debugMode       = (uint32_t)gbufferDebugMode;
				compositRenderSystem.pushParams.bloomHandle     = bloomEnabled ? bloomMipHandles[0] : 0u;
				compositRenderSystem.pushParams.bloomIntensity  = bloomIntensity;
				compositRenderSystem.pushParams.radiosityHandle = radiosityHandle;

				t0 = Clock::now();
				bglRenderer.beginDeferredRenderPass(primaryCommandBuffer);
				gBufferRenderSystem.renderEntities(frameInfo);
				bglRenderer.endCurrentRenderPass(primaryCommandBuffer);

				bglRenderer.beginRadiosityPass(primaryCommandBuffer);
				radiosityRenderSystem.render(frameInfo);
				bglRenderer.endCurrentRenderPass(primaryCommandBuffer);
				recordSection(S_GBUFFER, tMs(t0, Clock::now()));

				t0 = Clock::now();
				if (bloomEnabled) {
					for (uint32_t i = 0; i < BGLRenderer::BLOOM_MIPS; i++) {
						bglRenderer.beginBloomDownsamplePass(primaryCommandBuffer, i);
						BloomDownPush dp{};
						dp.inputHandle = (i == 0) ? radiosityHandle : bloomMipHandles[i - 1];
						dp.threshold   = (i == 0) ? bloomThreshold : 0.0f;
						dp.intensity   = 1.0f;
						bloomRenderSystem.renderDownsample(frameInfo, dp);
						bglRenderer.endCurrentRenderPass(primaryCommandBuffer);
					}

					for (int i = (int)BGLRenderer::BLOOM_MIPS - 2; i >= 0; i--) {
						bglRenderer.beginBloomUpsamplePass(primaryCommandBuffer, i);
						BloomUpPush up{};
						up.inputHandle  = bloomMipHandles[i + 1];
						up.filterRadius = 1.0f;
						up.weight = powf(bloomMipDecay, float(i));
						bloomRenderSystem.renderUpsample(frameInfo, up);
						bglRenderer.endCurrentRenderPass(primaryCommandBuffer);
					}
				}
				recordSection(S_BLOOM, tMs(t0, Clock::now()));

				t0 = Clock::now();
				bglRenderer.blitGBufferDepthToSwapchain(primaryCommandBuffer);
				bglRenderer.beginSwapChainRenderPass(primaryCommandBuffer);
				compositRenderSystem.render(frameInfo);
				transparentRenderSystem.render(frameInfo);
				if (showWireframe) wireframeRenderSystem.renderEntities(frameInfo);
			if (drawBBox) wireframeRenderSystem.renderBBoxes(frameInfo);
				ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), primaryCommandBuffer);
				bglRenderer.endCurrentRenderPass(primaryCommandBuffer);
				recordSection(S_COMPOSITE, tMs(t0, Clock::now()));

				t0 = Clock::now();
				bglRenderer.endPrimaryCMD();
				recordSection(S_ENDCMD, tMs(t0, Clock::now()));
			}

			if (stutterDetect && frameTime * 1000.0f > stutterThresholdMs) {
				// find the section that took the most time this frame
				int worst = 0;
				for (int s = 1; s < S_COUNT; s++)
					if (sectMs[s] > sectMs[worst]) worst = s;
				printf("[STUTTER] %.1f ms (%.0f fps) | worst: %s %.3f ms\n",
					frameTime * 1000.0f, 1.0f / frameTime,
					sectName[worst], sectMs[worst]);
			}

			if (showFPS) {
				fpsAccum += frameTime;
				fpsFrames++;
				if (fpsAccum >= 1.0f) {
					std::cout << fpsFrames << " fps\n";
					fpsAccum = 0.0f;
					fpsFrames = 0;
				}
			}

			if (showProfile) {
				profAccum += frameTime;
				profFrames++;
				if (profAccum >= 1.0) {
					double cpuSum = 0.0;
					for (int s = 0; s < S_COUNT; s++) cpuSum += perf[s].total;
					printf("=== Profile (%d frames, cpu_sum=%.3f ms) ===\n", profFrames, cpuSum / profFrames);
					for (int s = 0; s < S_COUNT; s++) {
						double avg = perf[s].n > 0 ? perf[s].total / perf[s].n : 0.0;
						double pct = cpuSum > 0.0 ? avg / (cpuSum / profFrames) * 100.0 : 0.0;
						char note = ' ';
						if (s == S_BEGINCMD) note = '*'; // fence wait / image acquire
						if (s == S_ENDCMD)   note = '*'; // queue submit + present
						printf("  %c %s : %7.3f ms  (%5.1f%%)\n", note, sectName[s], avg, pct);
					}
					printf("  * = includes GPU sync point\n\n");
					for (int s = 0; s < S_COUNT; s++) { perf[s].total = 0.0; perf[s].n = 0; }
					profAccum = 0.0; profFrames = 0;
				}
			}
			if (maxFps > 0) {
				double targetMs = 1000.0 / maxFps;
				double elapsedMs = tMs(newTime, Clock::now());
				double sleepMs = targetMs - elapsedMs;
				if (sleepMs > 0.5) {
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
		if (hFpsTimer) CloseHandle(hFpsTimer);
#endif
		vkDeviceWaitIdle(BGLDevice::device());
	}


	void Application::initCommand()
	{
		CONSOLE->AddCommand("FREEFLY",        this, ConsoleCommand::ToggleFly);
		CONSOLE->AddCommand("TOGGLEPHYSICS",  this, ConsoleCommand::TogglePhys);
		CONSOLE->AddCommand("SHOWFPS",        this, ConsoleCommand::ShowFPS);
		CONSOLE->AddCommand("SHOWINFO",       this, ConsoleCommand::ShowInfo);
		CONSOLE->AddCommand("SHOWWIREFRAME",  this, ConsoleCommand::ShowWireframe);
		CONSOLE->AddCommand("R_DRAWBBOX",     this, ConsoleCommand::DrawBBox);
		CONSOLE->AddCommand("PROFILE",        this, ConsoleCommand::ShowProfile);
		CONSOLE->AddCommandWithArg("R_DRAWMODE",  this, ConsoleCommand::SetDrawMode);
		CONSOLE->AddCommandWithArg("R_DRAWBLOOM", this, ConsoleCommand::SetBloom);
		CONSOLE->AddCommandWithArg("R_MAXFPS",   this, ConsoleCommand::SetMaxFPS);
		CONSOLE->AddCommandWithArg("R_VSYNC",    this, ConsoleCommand::SetVSync);
	}

	void Application::initJolt()
	{
		BGLJolt::Initialize(bglDevice, registry);
	}

	void Application::initImgui()
	{
		VkDescriptorPoolSize pool_sizes[] =
		{
			{ VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
			{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
			{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
			{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
			{ VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
			{ VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
			{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
			{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
			{ VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
		};

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

} // namespace bagel
