// vulkan headers
#include <vulkan/vulkan.h>

// STL includes
#include <iostream>
#include <stdexcept>
#include <array>
#include <chrono>
#include <vector>

#include "entt.hpp"
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"

#include "bagel_frame_info.hpp"
#include "bagel_buffer.hpp"
#include "bgl_camera.hpp"
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
		descriptorManager->writeDeferredRenderTargetToDescriptor(bglRenderer.getDRSampler(), bglRenderer.getDRPositionView(), bglRenderer.getDRNormalView(), bglRenderer.getDRAlbedoView(), bglRenderer.getDREmissionView());

		CONSOLE->Log("FirstApp", "Finished Creating Global Pool");

		CONSOLE->Log("FirstApp", "Initializing ENTT Registry");
		registry = entt::registry{};

		CONSOLE->Log("FirstApp", "Initializing IMGUI");
		initImgui();

		CONSOLE->Log("FirstApp", "Initializing Jolt Physics Engine");
		initJolt();
		materialManager = std::make_unique<BGLMaterialManager>(bglDevice, *globalPool, *descriptorManager);
	}

	Application::~Application()
	{
		ImGui_ImplVulkan_Shutdown();
		vkDestroyDescriptorPool(BGLDevice::device(), imguiPool, nullptr);
	}

	void Application::run()
	{
		std::unique_ptr<BGLBuffer> uboBuffers = std::make_unique<BGLBuffer>(
			bglDevice,
			sizeof(GlobalUBO),
			1,
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
		uboBuffers->map();

		std::unique_ptr<BGLBuffer> uboComposition = std::make_unique<BGLBuffer>(
			bglDevice,
			sizeof(CompositionUBO),
			1,
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
		uboComposition->map();

		VkDescriptorBufferInfo bufferInfo = uboBuffers->descriptorInfo();
		descriptorManager->storeUBO(bufferInfo, 0);

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

		BGLCamera camera{};

		auto viewerObject = BGLGameObject::createGameObject();
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
		camera.setViewDirection(glm::vec3(-1.0f, -2.0f, -2.0f), glm::vec3(0.0f, 0.0f, 2.5f));
		while (!bglWindow.shouldClose())
		{
			glfwPollEvents();

			auto newTime = std::chrono::high_resolution_clock::now();
			float frameTime = std::chrono::duration<float, std::chrono::seconds::period>(newTime - currentTime).count();
			currentTime = newTime;
			totalTime += frameTime;

			if (freeFly) cameraController.moveInPlaneXZ(bglWindow.getGLFWWindow(), frameTime, viewerObject, 0);
			camera.setViewYXZ(viewerObject.transform.getTranslation(), viewerObject.transform.getRotation());
			float aspect = bglRenderer.getAspectRatio();
			camera.setPerspectiveProjection(glm::radians(100.0f), aspect, 0.1f, 300.0f);

			OnUpdate(frameTime);

			HierachySystem hs(registry);
			hs.ApplyHiarchialChange();

			if (runPhys) {
				BGLJolt::GetInstance()->ApplyTransformToKinematic(frameTime);
				BGLJolt::GetInstance()->Step(frameTime, 3);
				BGLJolt::GetInstance()->ApplyPhysicsTransform();
			}

			GlobalUBO ubo{};
			ubo.updateCameraInfo(camera.getProjection(), camera.getView(), camera.getInverseView());
			pointLightSystem.update(ubo, 0);

			ImGui_ImplVulkan_NewFrame();
			ImGui_ImplGlfw_NewFrame();
			ImGui::NewFrame();
			VkExtent2D ext = bglRenderer.getExtent();

			if(showInfo) DrawInfoPanels(registry, ext.width, ext.height, camera.getProjection(), camera.getView());
			ConsoleApp::Instance()->Draw("Console", nullptr);
			ImGui::Begin("Settings");
			ImGui::SliderFloat("Mouse Sensitivity", &cameraController.mouseSensitivity, 0.05f, 0.3f);
			ImGui::End();
			ImGui::Render();

			// Re-register G-buffer and bloom mip descriptor entries after a window resize
			if (bglRenderer.consumeGBufferRecreated()) {
				descriptorManager->writeDeferredRenderTargetToDescriptor(
					bglRenderer.getDRSampler(),
					bglRenderer.getDRPositionView(),
					bglRenderer.getDRNormalView(),
					bglRenderer.getDRAlbedoView(),
					bglRenderer.getDREmissionView());
				for (uint32_t i = 0; i < BGLRenderer::BLOOM_MIPS; i++) {
					descriptorManager->storeTexture(
						bglRenderer.getBloomMipImageInfo(i),
						bglRenderer.getBloomMipMemory(i),
						bglRenderer.getBloomMipImage(i),
						"BloomMip", true, bloomMipHandles[i], false);
				}
			}

			if (auto primaryCommandBuffer = bglRenderer.beginPrimaryCMD()) {
				FrameInfo frameInfo{
					frameTime,
					totalTime,
					primaryCommandBuffer,
					camera,
					descriptorManager->getDescriptorSet(bglRenderer.getFrameIndex()),
					registry
				};

				uboBuffers->writeToBuffer(&ubo);
				uboBuffers->flush();

				compositRenderSystem.pushParams.debugMode   = (uint32_t)gbufferDebugMode;
				compositRenderSystem.pushParams.bloomHandle = bloomEnabled ? bloomMipHandles[0] : 0u;

				bglRenderer.beginDeferredRenderPass(primaryCommandBuffer);
				gBufferRenderSystem.renderEntities(frameInfo);
				bglRenderer.endCurrentRenderPass(primaryCommandBuffer);

				if (bloomEnabled) {
					for (uint32_t i = 0; i < BGLRenderer::BLOOM_MIPS; i++) {
						bglRenderer.beginBloomDownsamplePass(primaryCommandBuffer, i);
						BloomDownPush dp{};
						dp.inputHandle = (i == 0) ? 0u : bloomMipHandles[i - 1];
						dp.threshold   = 0.0f;
						dp.intensity   = 1.0f;
						bloomRenderSystem.renderDownsample(frameInfo, dp);
						bglRenderer.endCurrentRenderPass(primaryCommandBuffer);
					}

					for (int i = (int)BGLRenderer::BLOOM_MIPS - 2; i >= 0; i--) {
						bglRenderer.beginBloomUpsamplePass(primaryCommandBuffer, i);
						BloomUpPush up{};
						up.inputHandle  = bloomMipHandles[i + 1];
						up.filterRadius = 1.0f;
						bloomRenderSystem.renderUpsample(frameInfo, up);
						bglRenderer.endCurrentRenderPass(primaryCommandBuffer);
					}
				}

				bglRenderer.blitGBufferDepthToSwapchain(primaryCommandBuffer);

				bglRenderer.beginSwapChainRenderPass(primaryCommandBuffer);
				compositRenderSystem.render(frameInfo);
				transparentRenderSystem.render(frameInfo);
				if (showWireframe) wireframeRenderSystem.renderEntities(frameInfo);
				ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), primaryCommandBuffer);
				bglRenderer.endCurrentRenderPass(primaryCommandBuffer);

				bglRenderer.endPrimaryCMD();
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
		}
#ifdef SYNC_DEVICEWAITIDLE
		vkDeviceWaitIdle(BGLDevice::device());
#endif
	}


	void Application::initCommand()
	{
		CONSOLE->AddCommand("FREEFLY",        this, ConsoleCommand::ToggleFly);
		CONSOLE->AddCommand("TOGGLEPHYSICS",  this, ConsoleCommand::TogglePhys);
		CONSOLE->AddCommand("SHOWFPS",        this, ConsoleCommand::ShowFPS);
		CONSOLE->AddCommand("SHOWINFO",       this, ConsoleCommand::ShowInfo);
		CONSOLE->AddCommand("SHOWWIREFRAME",  this, ConsoleCommand::ShowWireframe);
		CONSOLE->AddCommandWithArg("R_DRAWMODE",  this, ConsoleCommand::SetDrawMode);
		CONSOLE->AddCommandWithArg("R_DRAWBLOOM", this, ConsoleCommand::SetBloom);
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
		pool_info.poolSizeCount = std::size(pool_sizes);
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
