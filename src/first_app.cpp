#include "first_app.hpp"

// vulkan headers
#include <vulkan/vulkan.h>

// STL includes
#include <iostream>
#include <stdexcept>
#include <array>
#include <iostream>
#include <chrono>
#include <vector>

#include "entt.hpp"
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
//#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"

#include "bagel_frame_info.hpp"
#include "bagel_buffer.hpp"
#include "bgl_camera.hpp"
#include "bagel_ecs_components.hpp"
#include "bagel_jolt/bagel_jolt.hpp"
#include "keyboard_movement_controller.hpp"
#include "bagel_console_commands.hpp"
#include "bagel_hierachy.hpp"
#include "bagel_util.hpp"

#define GLOBAL_DESCRIPTOR_COUNT 1000
//#define SHOW_FPS

//#define INSTANCERENDERTEST
//#define PHYSICSTEST

namespace bagel {
	// PushConstantData is a performant and simple way to send data to vertex and fragment shader
	// It is typically faster than descriptor sets for frequently updated data
	// 
	// But its size is limited to 128 bytes (technically each device has different max size but only 128 is guaranteed)
	// This maximum size varies from device to device and is specified in bytes by the max_push_constants_size field of vk::PhysicalDeviceLimits
	// Even the high end device (RTX3080) has only 256 bytes available so it is unrealistic to send most data
	// 
	// 
	//Struct normally packs the data as close as possible so it 
	//packs like this: (host memory layout)
	//{x,y,r,g,b}

	//a float using 32bits is four bytes
	//a vec2 with float would therefore be 8 bytes
	//in device memory, vec3 requires to be aligned by multiple of 16 bytes
	//meaning that the device aligns like this:
	//{x,y,_,_,r,g,b}

	//this means r and g from the memory will be assigned to blank memory, causing those value to be unread
	//to fix this, align the vec3 variable to the multiple of 16 bytes with alignas(16)

	FirstApp::FirstApp()
	{
		globalPool = BGLDescriptorPool::Builder(bglDevice)
			.setMaxSets(BGLSwapChain::MAX_FRAMES_IN_FLIGHT + GLOBAL_DESCRIPTOR_COUNT)
			.addPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1)
			.addPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, GLOBAL_DESCRIPTOR_COUNT)
			.addPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, GLOBAL_DESCRIPTOR_COUNT)
			.build();
		descriptorManager = std::make_unique<BGLBindlessDescriptorManager>(bglDevice, *globalPool);
		descriptorManager->createBindlessDescriptorSet(GLOBAL_DESCRIPTOR_COUNT);
		std::cout << "Finished Creating Global Pool\n";

		std::cout << "Initializing ENTT Registry\n";
		registry = entt::registry{};

		std::cout << "Initializing IMGUI\n";
		initImgui();

		std::cout << "Initializing Jolt Physics Engine\n";
		initJolt();
	}

	FirstApp::~FirstApp()
	{
		//add the destroy the imgui created structures
		ImGui_ImplVulkan_Shutdown();
		vkDestroyDescriptorPool(BGLDevice::device(), imguiPool, nullptr);
	}

	void FirstApp::run()
	{	
		//Create UBO buffer. Single for bindless design
		std::unique_ptr<BGLBuffer> uboBuffers;
		uboBuffers = std::make_unique<BGLBuffer>(
			bglDevice,
			sizeof(GlobalUBO),
			1,
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
		uboBuffers->map();

		VkDescriptorBufferInfo bufferInfo = uboBuffers->descriptorInfo();
		descriptorManager->storeUBO(bufferInfo);
		
		std::vector<VkDescriptorSetLayout> pipelineDescriptorSetLayouts = { descriptorManager->getDescriptorSetLayout() };

#ifdef MODELRENDER_ORIGINAL
		ModelRenderSystem modelRenderSystem{
			bglDevice,
			bglRenderer.getSwapChainRenderPass(),
			pipelineDescriptorSetLayouts,
			descriptorManager,
			registry};
#else
		ModelRenderSystem modelRenderSystem{
			bglRenderer.getSwapChainRenderPass(),
			pipelineDescriptorSetLayouts,
			descriptorManager,
			registry };
#endif

		WireframeRenderSystem wireframeRenderSystem{
			bglDevice,
			bglRenderer.getSwapChainRenderPass(),
			pipelineDescriptorSetLayouts,
			descriptorManager,
			registry };

		PointLightSystem pointLightSystem{
			bglRenderer.getSwapChainRenderPass(),
			pipelineDescriptorSetLayouts,
			descriptorManager,
			registry };

		BGLCamera camera{};

		auto viewerObject = BGLGameObject::createGameObject();
		KeyboardMovementController cameraController{};
		
		auto currentTime = std::chrono::high_resolution_clock::now();
		std::chrono::microseconds prevFrameTime;

		// Create Console Commands
		initCommand();

		bool moveF = true;
		entt::entity moved = loadECSObjects();

		BGLJolt::GetInstance()->SetGravity({0,-0.0f,0});
		BGLJolt::GetInstance()->SetSimulationTimescale(0.5f);
		
		// Game loop
		while (!bglWindow.shouldClose())
		{
			auto start = std::chrono::high_resolution_clock::now();
			camera.setViewDirection(glm::vec3(-1.0f,-2.0f,-2.0f), glm::vec3(0.0f, 0.0f,2.5f)); //yaw pitch roll

			//Event call function can block therefore we measure the newtime after
			glfwPollEvents();
			
			auto newTime = std::chrono::high_resolution_clock::now();
			float frameTime = std::chrono::duration<float, std::chrono::seconds::period>(newTime - currentTime).count();
			currentTime = newTime;
			bool camMoved = false;

			// Camera Control
			if(freeFly) camMoved = cameraController.moveInPlaneXZ(bglWindow.getGLFWWindow(), frameTime, viewerObject,0);
			camera.setViewYXZ(viewerObject.transform.getTranslation(), viewerObject.transform.getRotation());
			float aspect = bglRenderer.getAspectRatio();
			camera.setPerspectiveProjection(glm::radians(100.0f), aspect, 0.1f, 300.0f);

			// Example Game Logic
			{
				auto& tfc = registry.get<TransformComponent>(moved);
				glm::vec3 pos = tfc.getTranslation();
				if (pos.y >= 5.3f) {
					moveF = false;
				} else if (pos.y <= 0.0f) {
					moveF = true;
				}

				if (moveF) {
					pos.y = pos.y + 0.3 * frameTime;

				}
				else {
					pos.y = pos.y - 0.3 * frameTime;
				}
				tfc.setTranslation(pos);
			}

			//Hierarchy Update
			HierachySystem hs(registry);
			hs.ApplyHiarchialChange();

			// Physics
			if (runPhys) {
				BGLJolt::GetInstance()->ApplyTransformToKinematic(frameTime);
				BGLJolt::GetInstance()->Step(frameTime, 3);
				BGLJolt::GetInstance()->ApplyPhysicsTransform();
			}

			// Render
			// imgui new frame
			ImGui_ImplVulkan_NewFrame();
			ImGui_ImplGlfw_NewFrame();
			ImGui::NewFrame();

			//imgui draw commands
			console.Draw("Console", nullptr);
			
			ImGui::Render();
			
			// bglRenderer.beginFrame returns nullptr if the swapchain needs to be recreated
			if (auto primaryCommandBuffer = bglRenderer.beginPrimaryCMD()) {
				//Render
				bglRenderer.beginSwapChainRenderPass(primaryCommandBuffer);
				
				// Frame info hold secondary commandBuffer because that's where it will be recorded
				// Only the ImGUI will be recorded to the primary commandBuffer
				FrameInfo frameInfo{
					frameTime,
					primaryCommandBuffer,
					camera,
					descriptorManager->getDescriptorSet(),
					registry
				};

				//Update
				GlobalUBO ubo{};
				ubo.updateCameraInfo(camera.getProjection(), camera.getView(), camera.getInverseView());
				if (rotateLight) pointLightSystem.update(ubo, frameTime);
				else pointLightSystem.update(ubo, 0);
				//Apply UBO changes to buffer
				uboBuffers->writeToBuffer(&ubo);
				uboBuffers->flush();

				//always render solid objects before rendering transparent objects
				modelRenderSystem.renderEntities(frameInfo);
				//wireframeRenderSystem.renderEntities(frameInfo);
				pointLightSystem.render(frameInfo);
				
				ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), primaryCommandBuffer);

				bglRenderer.endSwapChainRenderPass(primaryCommandBuffer);
				bglRenderer.endPrimaryCMD();

			}
			auto stop = std::chrono::high_resolution_clock::now();
			prevFrameTime = std::chrono::duration_cast<std::chrono::microseconds>(stop - start);

			//std::cout << 1.0f / frameTime << "fps\n";
			if(showFPS) std::cout << (long long)1000000/ prevFrameTime.count() << "fps\n";

		}
		//CPU will block until all gpu operations are complete
		vkDeviceWaitIdle(BGLDevice::device());
	}

	entt::entity FirstApp::loadECSObjects() {
		auto modelBuilder = new ModelDescriptionComponentBuilder(bglDevice);
		auto textureBuilder = new TextureComponentBuilder(bglDevice, *globalPool, *descriptorManager);

#ifdef INSTANCERENDERTEST
		{
			const auto e11 = registry.create();
			auto& tfc = registry.emplace<bagel::TransformArrayComponent>(e11);
			auto& mdc = registry.emplace<bagel::ModelDescriptionComponent>(e11);
			auto& tc = registry.emplace<bagel::TextureComponent>(e11);
			auto& bufferE1Comp = registry.emplace<bagel::DataBufferComponent>(e11, bglDevice, *descriptorManager, sizeof(TransformArrayComponent::TransformBufferUnit));

			for (int i = 0; i < 1000; i++) tfc.addTransform({ -2.0f + 1.0f * i, 5.0f , 4.0f }, { 1.0f, 1.0f, 1.0f });
			tfc.ToBufferComponent(bufferE1Comp);
			
			modelBuilder->setBuildTarget(&mdc);
			modelBuilder->buildComponent(enginePath("/models/flatfaces.obj"));
			textureBuilder->setBuildTarget(&tc);
			textureBuilder->buildComponent("/materials/models/rocketlauncher.ktx");

			std::cout << "Creating entity 2\n"; 
			const auto e2 = registry.create();
			auto& tfc2 = registry.emplace<bagel::TransformArrayComponent>(e2);
			auto& bufferComp = registry.emplace<bagel::DataBufferComponent>(e2, bglDevice, *descriptorManager, sizeof(TransformArrayComponent::TransformBufferUnit));
			tfc2.setTransform(0, { -5.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f });

			for (int i = 0; i < 1000; i++) tfc2.addTransform({ -5.0f + 1.0f * i, 2.0f , 0.0f}, { 1.0f, 1.0f, 1.0f });

			tfc2.ToBufferComponent(bufferComp);
			std::cout << "tfc2 bufferhandle: " << bufferComp.getBufferHandle() << "\n";

			auto& mdc2 = registry.emplace<bagel::ModelDescriptionComponent>(e2);
			auto& tc2 = registry.emplace<bagel::TextureComponent>(e2);

			//Loading axis.ktx as first texture does not cause crash but loading it second does.
			modelBuilder->setBuildTarget(&mdc2);
			modelBuilder->buildComponent(enginePath("/models/axis.obj"));
			textureBuilder->setBuildTarget(&tc2);
			textureBuilder->buildComponent("/materials/models/axis.ktx");
		}
#endif
		
		auto e1 = registry.create();
		auto& tfc1 = registry.emplace<bagel::TransformComponent>(e1);
		auto& mdc1 = registry.emplace<bagel::ModelDescriptionComponent>(e1);
		auto& tc1 = registry.emplace<bagel::DiffuseTextureComponent>(e1);
		tfc1.setLocalTranslation({ 0.3f,0.5f,0.0 });

		BGLJolt::PhysicsBodyCreationInfo info{
			{0,0,0},{0,0,0},PhysicsType::KINEMATIC, true, PhysicsLayers::MOVING
		};
		BGLJolt::GetInstance()->AddSphere(e1, 0.3f,info);

		{
			auto e1 = registry.create();
			auto& tfc1 = registry.emplace<bagel::TransformComponent>(e1);
			auto& mdc1 = registry.emplace<bagel::ModelDescriptionComponent>(e1);
			auto& tc1 = registry.emplace<bagel::DiffuseTextureComponent>(e1);
			tfc1.setTranslation({ 0.55f,5.3f,0.0 });

			BGLJolt::PhysicsBodyCreationInfo info3{
				tfc1.getTranslation(),{0,0,0},PhysicsType::DYNAMIC, true, PhysicsLayers::MOVING
			};
			BGLJolt::GetInstance()->AddSphere(e1, 0.5f, info3);
			modelBuilder->setBuildTarget(&mdc1);
			modelBuilder->buildComponent(util::enginePath("/models/rocketlauncher.obj"));
			mdc1.textureMapFlag |= ModelDescriptionComponent::TextureCompositeFlag::DIFFUSE;
			textureBuilder->setBuildTarget(&tc1);
			textureBuilder->buildComponent("/materials/models/rocketlauncher.ktx");
		}

		{
			auto e1 = registry.create();
			auto& tfc1 = registry.emplace<bagel::TransformComponent>(e1);
			auto& mdc1 = registry.emplace<bagel::ModelDescriptionComponent>(e1);
			auto& tc1 = registry.emplace<bagel::DiffuseTextureComponent>(e1);
			tfc1.setTranslation({ -0.55f,5.3f,0.0 });

			BGLJolt::PhysicsBodyCreationInfo info3{
				tfc1.getTranslation(),{0,0,0},PhysicsType::DYNAMIC, true, PhysicsLayers::MOVING
			};
			BGLJolt::GetInstance()->AddSphere(e1, 0.5f, info3);
			modelBuilder->setBuildTarget(&mdc1);
			modelBuilder->buildComponent(util::enginePath("/models/rocketlauncher.obj"));
			mdc1.textureMapFlag |= ModelDescriptionComponent::TextureCompositeFlag::DIFFUSE;
			textureBuilder->setBuildTarget(&tc1);
			textureBuilder->buildComponent("/materials/models/rocketlauncher.ktx");
		}
		
		auto e2 = registry.create();
		auto& tfc2 = registry.emplace<bagel::TransformComponent>(e2);
		auto& mdc2 = registry.emplace<bagel::ModelDescriptionComponent>(e2);
		auto& tc2 = registry.emplace<bagel::DiffuseTextureComponent>(e2);
		tfc2.setTranslation({ 0.1f, 3.0f, 0.0f });

		BGLJolt::PhysicsBodyCreationInfo info2{
			tfc2.getTranslation(),tfc2.getRotation(),PhysicsType::DYNAMIC, true, PhysicsLayers::MOVING
		};
		BGLJolt::GetInstance()->AddSphere(e2, 0.5f, info2);

		mdc1.textureMapFlag |= ModelDescriptionComponent::TextureCompositeFlag::DIFFUSE;
		mdc2.textureMapFlag |= ModelDescriptionComponent::TextureCompositeFlag::DIFFUSE;

		modelBuilder->setBuildTarget(&mdc1); 
		modelBuilder->buildComponent(util::enginePath("/models/rocketlauncher.obj"));
		textureBuilder->setBuildTarget(&tc1);
		textureBuilder->buildComponent("/materials/models/rocketlauncher.ktx");
		modelBuilder->setBuildTarget(&mdc2);
		modelBuilder->buildComponent(util::enginePath("/models/rocketlauncher.obj"));
		textureBuilder->setBuildTarget(&tc2);
		textureBuilder->buildComponent("/materials/models/rocketlauncher.ktx");
		

		auto e_axis = registry.create();
		auto& tfc3 = registry.emplace<bagel::TransformComponent>(e_axis);
		auto& mdc3 = registry.emplace<bagel::ModelDescriptionComponent>(e_axis);
		auto& tc3 = registry.emplace<bagel::DiffuseTextureComponent>(e_axis);
		modelBuilder->setBuildTarget(&mdc3);
		modelBuilder->buildComponent(util::enginePath("/models/axis.obj"));
		textureBuilder->setBuildTarget(&tc3);
		textureBuilder->buildComponent("/materials/models/axis.ktx");
		mdc3.textureMapFlag |= ModelDescriptionComponent::TextureCompositeFlag::DIFFUSE;
		tfc3.setScale({ 1.0f,1.0f, 1.0f });
		tfc3.setLocalRotation({ 0.3f,0.5f,0.3f });
		HierachySystem hs(registry);
		hs.CreateHierachy(e_axis, e1);
		
		delete modelBuilder;
		delete textureBuilder;

		std::vector<glm::vec3> lightColors{
			 {1.f, .1f, .1f},
			 {.1f, .1f, 1.f},
			 {.1f, 1.f, .1f},
			 {1.f, 1.f, .1f},
			 {.1f, 1.f, 1.f},
			 {1.f, 1.f, 1.f}
		};
		for (int i = 0; i < lightColors.size(); i++) {
			auto rotateLight = glm::rotate(
				glm::mat4(1.0f),
				(i * glm::two_pi<float>() / lightColors.size()),
				{ 0.f,-1.f,0.f }); //axis of rotation
			const auto entity = registry.create();
			registry.emplace<bagel::TransformComponent>(entity, (rotateLight * glm::vec4(glm::vec3{ 3.f,-2.0f,0.0f }, 1.0f)) + glm::vec4(0.f, 0.f, 3.0f, 1.0f));
			auto& light = registry.emplace<bagel::PointLightComponent>(entity);
			light.color = glm::vec4(lightColors[i],4.0f);
		}
		return e_axis;
	}

	void FirstApp::resetScene() {

	}

	void FirstApp::initRenderSystems() {

	}

	void FirstApp::initCommand()
	{
		console.AddCommand("FREEFLY", this, ConsoleCommand::ToggleFly);
		console.AddCommand("TOGGLEPHYSICS", this, ConsoleCommand::TogglePhys);
		console.AddCommand("RESETSCENE", this, ConsoleCommand::ResetScene);
		console.AddCommand("ROTATELIGHT", this, ConsoleCommand::RotateLight);
		console.AddCommand("SHOWFPS", this, ConsoleCommand::ShowFPS);
	}

	void FirstApp::initJolt()
	{
		BGLJolt::Initialize(registry);
	}

	void FirstApp::initImgui()
	{
		//1: create descriptor pool for IMGUI
		// the size of the pool is very oversize, but it's copied from imgui demo itself.
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

		//this initializes imgui for Vulkan
		ImGui_ImplVulkan_InitInfo init_info = {};
		init_info.Instance = bglDevice.getInstance();
		init_info.PhysicalDevice = bglDevice.getPhysicalDevice();
		init_info.Device = BGLDevice::device();
		init_info.Queue = bglDevice.graphicsQueue();
		init_info.DescriptorPool = imguiPool;
		init_info.MinImageCount = 3;
		init_info.ImageCount = 3;
		init_info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

		ImGui_ImplVulkan_Init(&init_info, bglRenderer.getSwapChainRenderPass());

		//execute a gpu command to upload imgui font textures
		ImGui_ImplVulkan_CreateFontsTexture();
		//clear font textures from cpu data. Done automatically now
	}

}

