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

#define GLOBAL_SET_IMAGE_COUNT 1
#define MAX_MODEL_COUNT 10

#define SHOW_FPS

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
			.setMaxSets(BGLSwapChain::MAX_FRAMES_IN_FLIGHT + MAX_MODEL_COUNT)
			.addPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, BGLSwapChain::MAX_FRAMES_IN_FLIGHT + MAX_MODEL_COUNT)
			.addPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, BGLSwapChain::MAX_FRAMES_IN_FLIGHT + MAX_MODEL_COUNT)
			.build();
		std::cout << "Finished Creating Global Pool" << "\n";

		registry = entt::registry{};
		std::cout << "Creating Entity Registry" << "\n";
	}
	FirstApp::~FirstApp()
	{
	}
	void FirstApp::run()
	{	
		//Create UBO buffers by the number of MAX_FRAMES_IN_FLIGHT. 3 for triple buffering
		std::vector<std::unique_ptr<BGLBuffer>> uboBuffers(BGLSwapChain::MAX_FRAMES_IN_FLIGHT);
		for (int i = 0; i < uboBuffers.size(); i++) {
			uboBuffers[i] = std::make_unique<BGLBuffer>(
				bglDevice,
				sizeof(GlobalUBO),
				1,
				VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
			uboBuffers[i]->map();
		}

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

		//loadGameObjects();

		//These are built here to pass into pipeline creation



		/*auto globalImageSetLayoutBuilder = BGLDescriptorSetLayout::Builder(bglDevice);
		for (int i = 1; i < MAX_IMAGE_DESCRIPTOR_COUNT+1; i++) {
			globalImageSetLayoutBuilder.addBinding(i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_SHADER_STAGE_ALL_GRAPHICS);
		}
		auto globalImageSetLayout = globalImageSetLayoutBuilder.build();*/

		//pseudocode for descriptorset
		//  1. create three buffers, each per frame buffer since we are using  triple buffering,
		//  2. bind ubo desciptor set
		// 	3. bind all VkDescriptorImageInfo by the number of images to draw
		//	4. repeat 1 and 2 for all three ubo buffers

		// Allocate the descriptors sets MAX_FRAMES_IN_FLIGHT times so that each frame buffer can use its own descriptorsets.

		//Some chatgpt advices:

		//Dynamic Descriptor Allocation: Instead of preallocating a fixed number of descriptor sets, allocate descriptor sets dynamically based on your current requirements. When you know the exact number of images or resources you need to bind, allocate and update descriptor sets only for those resources. This minimizes memory waste and allows for flexibility in managing resources efficiently.
		//Descriptor Pool Management : You can create and manage multiple descriptor pools, each with a limited number of descriptor sets, to handle different types of resources.This can help distribute the allocation of descriptor sets and better utilize memory.If one descriptor pool is exhausted, you can create another.
		//Resource Management : Track the resources used by each frame and allocate descriptor sets as needed.If your application needs more descriptor sets than initially allocated, you can dynamically create additional descriptor sets on demand.
		//Pooling and Recycling : Instead of creating new descriptor sets from scratch, you can reuse and recycle descriptor sets when they are no longer needed.This can help optimize memory usage over time.

		/*The choice between reallocating VkDescriptorImageInfo for all required images or finding and updating specific bindings depends on the specific use case and how frequently you anticipate changes in the set of images to be bound.

			Reallocating VkDescriptorImageInfo for All Required Images :

		If the set of images to be bound changes frequently and you have relatively few images, reallocating VkDescriptorImageInfo structures for all the required images can be a straightforward and efficient approach.
			This approach is particularly efficient when the number of images is relatively small and changes are frequent, as it avoids the need for complex tracking and updating logic.
			Finding and Updating Specific Bindings :

		If the number of images is significantly larger, and changes are less frequent, it might be more efficient to track which bindings are holding images and update specific bindings.
			This approach is useful when changes are infrequent, as it avoids the overhead of reallocating descriptor structures for all images.
			The choice between these two approaches depends on the specific use case and performance considerations.In practice, you might also consider a hybrid approach, where you track and update bindings for frequently changing images and reallocate VkDescriptorImageInfo for larger, less dynamic sets of images.*/

		//For now we will only use one descriptorset and render only 1 image
		std::unique_ptr<BGLTexture> texture1 = createTextureImage("../materials/texture.ktx");
		VkDescriptorImageInfo imageInfo1 = texture1->getDescriptorImageInfo();
		std::array<VkDescriptorImageInfo, 1> imageInfos = { imageInfo1 };

		std::vector<VkDescriptorSet> globalDescriptorSets(BGLSwapChain::MAX_FRAMES_IN_FLIGHT);
		for (int i = 0; i < globalDescriptorSets.size(); i++) {
			std::cout << "Binding descriptor set for frame index: " << i << "\n";
			
			VkDescriptorBufferInfo bufferInfo = uboBuffers[i]->descriptorInfo();
			BGLDescriptorWriter(*globalSetLayout, *globalPool)
				.writeBuffer(0, &bufferInfo)
				.writeImages(1, imageInfos.data(), imageInfos.size())
				.build(globalDescriptorSets[i]);
		}

		std::vector<VkDescriptorSetLayout> pipelineDescriptorSetLayouts = { globalSetLayout->getDescriptorSetLayout() , modelSetLayout->getDescriptorSetLayout() };

		loadECSObjects();
		//loadGameObjects();


		ModelRenderSystem modelRenderSystem{
			bglDevice,
			bglRenderer.getSwapChainRenderPass(),
			pipelineDescriptorSetLayouts };

		SimpleRenderSystem simpleRenderSystem{ 
			bglDevice, 
			bglRenderer.getSwapChainRenderPass(), 
			pipelineDescriptorSetLayouts };

		PointLightSystem pointLightSystem{
			bglDevice,
			bglRenderer.getSwapChainRenderPass(),
			globalSetLayout->getDescriptorSetLayout() };

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

				int frameIndex = bglRenderer.getFrameIndex();
				//std::cout << "Current Frame Index: " << frameIndex << "\n";
				FrameInfo frameInfo{
					frameIndex,
					frameTime,
					commandBuffer,
					camera,
					globalDescriptorSets[frameIndex],
					gameObjects
				};

				//Update
				GlobalUBO ubo{};
				ubo.projectionMatrix = camera.getProjection();
				ubo.viewMatrix = camera.getView();
				ubo.inverseViewMatrix = camera.getInverseView();

				pointLightSystem.update(frameInfo, ubo);

				uboBuffers[frameIndex]->writeToBuffer(&ubo);
				uboBuffers[frameIndex]->flush();

				//Render
				bglRenderer.beginSwapChainRenderPass(commandBuffer);

				//this means all the objects in simpleRenderSystem will be rendered before the objects in pointlight system
				//always render solid objects before rendering transparency
				//simpleRenderSystem.renderGameObjects(frameInfo);
				modelRenderSystem.renderEntities(registry,frameInfo);
				//simpleRenderSystem.renderGameObjects(frameInfo);
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
		auto textureBuilder = new TextureComponentBuilder(bglDevice, *globalPool, *modelSetLayout);
		for (auto i = 1u; i < 5u; ++i) {
			const auto entity = registry.create();
			auto& tfc = registry.emplace<bagel::TransformComponent>(entity, 2.0f*i, 2.0f * i, 2.0f * i);
			
			for (auto ii = 1u; ii < 10u; ++ii) {
				tfc.addTransform({ 2.0f * i, 2.0f * i - 1.0f * ii, 2.0f * i });
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
			//auto pointLight = BGLGameObject::makePointLight(2.f);
			auto rotateLight = glm::rotate(
				glm::mat4(1.0f),
				(i * glm::two_pi<float>() / lightColors.size()),
				{ 0.f,-1.f,0.f }); //axis of rotation
			const auto entity = registry.create();
			//registry.emplace<bagel::TransformComponent>(entity, (rotateLight * glm::vec4(glm::vec3{ 3.f,-2.0f,0.0f }, 1.0f)) + glm::vec4(0.f, 0.f, 3.0f, 1.0f));
			//registry.emplace<bagel::PointLightComponent>(entity);

			//pointLight.color = lightColors[i];
		}
	}

	void FirstApp::loadGameObjects()
	{
		std::shared_ptr<BGLModel> rocketLauncherModel = 
			BGLModel::createModelFromFile(bglDevice, "../models/rocketlauncher.obj", "../materials/models/c_rocketlauncher.ktx", modelSetLayout, *globalPool);
		auto rocketLauncher = BGLGameObject::createGameObject();
		rocketLauncher.model = rocketLauncherModel;
		TransformComponent tr1{};
		tr1.translation = { { 0.0f,0.0f,4.5f } };
		tr1.scale = { { -0.2f,-0.2f,0.2f } };
		TransformComponent tr2{};
		tr2.translation = { { 2.0f,4.0f,4.5f } };
		tr2.scale = { { -0.12f,-1.2f,0.2f} };
		//rocketLauncher.addTransformComponent(tr1);
		//rocketLauncher.addTransformComponent(tr2);
		gameObjects.emplace(rocketLauncher.getId(),std::move(rocketLauncher));

		std::shared_ptr<BGLModel> floorModel = BGLModel::createModelFromFile(bglDevice, "../models/floor.obj", "../materials/texture.ktx", modelSetLayout, *globalPool);
		auto floor = BGLGameObject::createGameObject();
		floor.model = floorModel;
		TransformComponent tr3{};
		tr3.translation = { { 0.0f,2.0f,4.5f } };
		tr3.scale = { { 10.f,1.f,10.f } };
		//floor.addTransformComponent(tr3);
		gameObjects.emplace(floor.getId(), std::move(floor));
		std::vector<glm::vec3> lightColors{
			 {1.f, .1f, .1f},
			 {.1f, .1f, 1.f},
			 {.1f, 1.f, .1f},
			 {1.f, 1.f, .1f},
			 {.1f, 1.f, 1.f},
			 {1.f, 1.f, 1.f}
		};
		for (int i = 0; i < lightColors.size(); i++) {
			auto pointLight = BGLGameObject::makePointLight(2.f);

			auto rotateLight = glm::rotate(
				glm::mat4(1.0f),
				(i * glm::two_pi<float>() / lightColors.size()),
				{ 0.f,-1.f,0.f }); //axis of rotation
			pointLight.transform.translation[0] = (rotateLight * glm::vec4(glm::vec3{ 3.f,-2.0f,0.0f }, 1.0f)) + glm::vec4(0.f, 0.f, 3.0f, 1.0f);
			pointLight.color = lightColors[i];
			gameObjects.emplace(pointLight.getId(), std::move(pointLight));
		}
	}

	uint32_t FirstApp::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
		VkPhysicalDeviceMemoryProperties memProperties;
		vkGetPhysicalDeviceMemoryProperties(bglDevice.getPhysicalDevice(), &memProperties);

		for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
			if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
				return i;
			}
		}

		throw std::runtime_error("failed to find suitable memory type!");
	}
	std::unique_ptr<BGLTexture> FirstApp::createTextureImage(std::string filepath)
	{
		//std::shared_ptr<BGLTexture> texture = BGLTexture::createTextureFromFile(bglDevice, "../textures/texture.ktx");
		/*BGLTexture::textureInfoStruct texture{};
		if (load_image_from_file(bglDevice, "../textures/texture.ktx", texture)) {
			std::cout << "Successfully loaded texture..?" << "\n";
		}*/
		std::cout << "Loading " << filepath << "\n";
		return BGLTexture::createTextureFromFile(bglDevice, filepath, VK_FORMAT_R8G8B8A8_SRGB);
	}
}

