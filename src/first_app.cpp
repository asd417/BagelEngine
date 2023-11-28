#include "first_app.hpp"

#include <iostream>
#include <stdexcept>
#include <array>
#include <iostream>
#include <chrono>
#include <vector>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

#include "bagel_frame_info.hpp"
#include "bagel_buffer.hpp"
#include "bgl_camera.hpp"
#include "bagel_ecs_components.hpp"
#include "keyboard_movement_controller.hpp"
#include "entt.hpp"

#define GLOBAL_DESCRIPTOR_COUNT 1000
#define BINDLESS
#define MAX_MODEL_COUNT 10
//#define SHOW_FPS

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

		std::cout << "Finished Creating Global Pool" << "\n";
		std::cout << "MAX_MODEL_COUNT is set to " << MAX_MODEL_COUNT << ". Be aware when loading too many models." << "\n";

		registry = entt::registry{};
		std::cout << "Creating Entity Registry" << "\n";
	}
	FirstApp::~FirstApp()
	{
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


#ifndef BINDLESS

		// Global Descriptor Set Layout
		auto globalSetLayout = BGLDescriptorSetLayout::Builder(bglDevice)
			.addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_ALL_GRAPHICS)
			.addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_ALL_GRAPHICS, GLOBAL_SET_IMAGE_COUNT)
			.build();
		// Model Descriptor Set Layout
		modelSetLayout = BGLDescriptorSetLayout::Builder(bglDevice)
			.addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1) //Diffuse texture
			.addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_FRAGMENT_BIT, 1) // 
			.build();

		std::unique_ptr<BGLTexture> texture1 = createTextureImage("../materials/texture.ktx");
		VkDescriptorImageInfo imageInfo1 = texture1->getDescriptorImageInfo();
		std::array<VkDescriptorImageInfo, 1> imageInfos = { imageInfo1 };

		std::vector<VkDescriptorSet> globalDescriptorSets(BGLSwapChain::MAX_FRAMES_IN_FLIGHT);
		for (int i = 0; i < globalDescriptorSets.size(); i++) {
			std::cout << "Binding descriptor set for frame index: " << i << "\n";
			
			VkDescriptorBufferInfo bufferInfo = uboBuffers[i]->descriptorInfo();
			BGLDescriptorWriter(*globalSetLayout, *globalPool)
				.writeBuffer(0, &bufferInfo)
				/*.writeImages(1, imageInfos.data(), imageInfos.size())*/
				.build(globalDescriptorSets[i]);
		}
#endif


		VkDescriptorBufferInfo bufferInfo = uboBuffers->descriptorInfo();
		descriptorManager->storeUBO(bufferInfo);
		
		loadECSObjects();
		std::vector<VkDescriptorSetLayout> pipelineDescriptorSetLayouts = { descriptorManager->getDescriptorSetLayout() };

		ModelRenderSystem modelRenderSystem{
			bglDevice,
			bglRenderer.getSwapChainRenderPass(),
			pipelineDescriptorSetLayouts };

		PointLightSystem pointLightSystem{
			bglDevice,
			bglRenderer.getSwapChainRenderPass(),
			pipelineDescriptorSetLayouts };

		BGLCamera camera{};
		float zrot = 1.0f;
		int dir = 1;
		auto currentTime = std::chrono::high_resolution_clock::now();

		auto viewerObject = BGLGameObject::createGameObject();
		//viewerObject.createDefaultTransform();
		KeyboardMovementController cameraController{};
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

			cameraController.moveInPlaneXZ(bglWindow.getGLFWWindow(), frameTime, viewerObject,0);
			
			camera.setViewYXZ(viewerObject.transform.translation[0], viewerObject.transform.rotation[0]);

			float aspect = bglRenderer.getAspectRatio();
			camera.setPerspectiveProjection(glm::radians(70.0f), aspect, 0.1f, 30.0f);

			//bglRenderer.beginFrame returns nullptr if the swapchain needs to be recreated
			if (auto commandBuffer = bglRenderer.beginFrame()) {
				FrameInfo frameInfo{
					frameTime,
					commandBuffer,
					camera,
					descriptorManager->getDescriptorSet(),
					registry
				};

				//Update
				GlobalUBO ubo{};
				ubo.projectionMatrix = camera.getProjection();
				ubo.viewMatrix = camera.getView();
				ubo.inverseViewMatrix = camera.getInverseView();

				pointLightSystem.update(frameInfo, ubo);

				uboBuffers->writeToBuffer(&ubo);
				uboBuffers->flush();

				//Render
				bglRenderer.beginSwapChainRenderPass(commandBuffer);

				//always render solid objects before rendering transparent objects

				modelRenderSystem.renderEntities(frameInfo);
				pointLightSystem.render(frameInfo);

				bglRenderer.endSwapChainRenderPass(commandBuffer);
				bglRenderer.endFrame();
				auto stop = std::chrono::high_resolution_clock::now();
				auto duration = std::chrono::duration_cast<std::chrono::microseconds>(stop - start);
#ifdef SHOW_FPS:
				std::cout << (long long)1000000/duration.count() << "fps\n";
#endif
			}
		}
		//CPU will block until all gpu operations are complete
		vkDeviceWaitIdle(bglDevice.device());
	}

	void FirstApp::loadECSObjects() {
		auto modelBuilder = new ModelDescriptionComponentBuilder(bglDevice);

		auto textureBuilder = new TextureComponentBuilder(bglDevice, *globalPool, *descriptorManager);

		for (auto i = 1u; i < 5u; ++i) {
			const auto entity = registry.create();
			auto& tfc = registry.emplace<bagel::TransformComponent>(entity, 2.0f*i, 2.0f * i, 2.0f * i);
			
			for (auto ii = 1u; ii < 2u; ++ii) {
				tfc.addTransform({ 2.0f * i, 2.0f * i - 1.0f * ii, 2.0f * i }, {-0.05f,-0.05f,-0.05f});
			}

			auto& mdc = registry.emplace<bagel::ModelDescriptionComponent>(entity, bglDevice);
			auto& tc = registry.emplace<bagel::TextureComponent>(entity, bglDevice);

			modelBuilder->setBuildTarget(&mdc);
			modelBuilder->buildComponent("../models/rocketlauncher.obj");

			textureBuilder->setBuildTarget(&tc);
			textureBuilder->buildComponent("../materials/models/c_rocketlauncher.ktx");
		}
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
	}
}

