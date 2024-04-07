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
#include "keyboard_movement_controller.hpp"
#include "bagel_console_commands.hpp"
#include "bagel_hierachy.hpp"
#include "bagel_util.hpp"
#include "bagel_imgui.hpp"

#include "physics/bagel_jolt.hpp"
#include "math/bagel_math.hpp"


#include "Jolt/Jolt.h"

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
			.setMaxSets(BGLSwapChain::MAX_FRAMES_IN_FLIGHT * GLOBAL_DESCRIPTOR_COUNT)
			.addPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, GLOBAL_UBO_COUNT) //UBO count is decided in bagel_descriptors.hpp GLOBAL_UBO_COUNT
			.addPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, GLOBAL_DESCRIPTOR_COUNT)
			.addPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, GLOBAL_DESCRIPTOR_COUNT)
			.build();

		descriptorManager = std::make_unique<BGLBindlessDescriptorManager>(bglDevice, *globalPool);
		descriptorManager->createBindlessDescriptorSet(GLOBAL_DESCRIPTOR_COUNT);


		CONSOLE->Log("FirstApp", "Finished Creating Global Pool");

		CONSOLE->Log("FirstApp", "Initializing ENTT Registry");
		registry = entt::registry{};

		CONSOLE->Log("FirstApp", "Initializing IMGUI");
		initImgui();

		CONSOLE->Log("FirstApp", "Initializing Jolt Physics Engine");
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
		descriptorManager->storeUBO(bufferInfo, 0);
		
		bglRenderer.setUpOffScreenRenderPass(WIDTH / 2, HEIGHT / 2);

		uint32_t offscreenRenderTargetHandle = descriptorManager->storeTexture(
			bglRenderer.getOffscreenImageInfo(),
			bglRenderer.getOffscreenMemory(),
			bglRenderer.getOffscreenImage(),
			"OffscreenRenderTarget", false, 0); // Use this name to access

		std::vector<VkDescriptorSetLayout> pipelineDescriptorSetLayouts = { descriptorManager->getDescriptorSetLayout() };

		ModelRenderSystem modelRenderSystem{
			bglRenderer.getSwapChainRenderPass(),
			pipelineDescriptorSetLayouts,
			descriptorManager,
			registry};

		WireframeRenderSystem wireframeRenderSystem{
			bglRenderer.getSwapChainRenderPass(),
			pipelineDescriptorSetLayouts,
			descriptorManager,
			registry,
			bglDevice}; //Wireframe render system needs BGLDevice reference because it creates WireframeUBO

		PointLightSystem pointLightSystem{
			bglRenderer.getSwapChainRenderPass(),
			pipelineDescriptorSetLayouts,
			descriptorManager,
			registry,
			bglDevice };

		ModelRenderSystem modelRenderSystemOffscreen{
			bglRenderer.getOffscreenRenderPass(),
			pipelineDescriptorSetLayouts,
			descriptorManager,
			registry };

		PointLightSystem pointLightSystemOffscreen{
			bglRenderer.getOffscreenRenderPass(),
			pipelineDescriptorSetLayouts,
			descriptorManager,
			registry,
			bglDevice };

		BGLCamera camera{};

		auto viewerObject = BGLGameObject::createGameObject();
		KeyboardMovementController cameraController{};
		
		auto currentTime = std::chrono::high_resolution_clock::now();
		std::chrono::microseconds prevFrameTime;

		// Register Console Commands
		initCommand();

		//TEST codes-------------------------------------------
		/*bool moveF = true;
		entt::entity moved = loadECSObjects();
		registry.emplace<InfoComponent>(moved);
		entt::entity pointerAxis = makeAxisModel({ 0.0,0.0,0.0 });
		{
			auto& tfc_ = registry.get<TransformComponent>(pointerAxis);
			tfc_.setScale({0.1f, 0.1f, 1.5f});
			registry.emplace<InfoComponent>(pointerAxis);
		}
		entt::entity axis1 = makeAxisModel({ 1.0,0.5,2.0 }); 
		*/
		makeGrid();
		BGLJolt::GetInstance()->SetGravity({0,-0.0f,0});
		BGLJolt::GetInstance()->SetSimulationTimescale(0.5f);
		BGLJolt::GetInstance()->SetComponentActivityAll(true);
		//entt::entity monitor = createMonitor();
		entt::entity ent = createChineseDragon();
		createLights();
		//------------------------------------------------------
		// Game loop
		bool forward = true;
		while (!bglWindow.shouldClose())
		{
			auto start = std::chrono::high_resolution_clock::now();
			camera.setViewDirection(glm::vec3(-1.0f, -2.0f, -2.0f), glm::vec3(0.0f, 0.0f, 2.5f)); //yaw pitch roll

			//Event call function can block therefore we measure the newtime after
			glfwPollEvents();

			auto newTime = std::chrono::high_resolution_clock::now();
			float frameTime = std::chrono::duration<float, std::chrono::seconds::period>(newTime - currentTime).count();
			currentTime = newTime;
			bool camMoved = false;

			// Example Game Logic-----------------------------------------------------------------------------------------

			// Camera Control
			if (freeFly) camMoved = cameraController.moveInPlaneXZ(bglWindow.getGLFWWindow(), frameTime, viewerObject, 0);
			camera.setViewYXZ(viewerObject.transform.getTranslation(), viewerObject.transform.getRotation());
			float aspect = bglRenderer.getAspectRatio();
			camera.setPerspectiveProjection(glm::radians(100.0f), aspect, 0.1f, 300.0f);


			auto& Transform = registry.get<TransformComponent>(ent);
			glm::vec3 pos = Transform.getWorldTranslation();
			if (pos.x > 5) forward = false;
			if (pos.x < -5) forward = true;
			if(forward) Transform.setTranslation(pos + glm::vec3(0.001f, 0, 0));
			if(!forward) Transform.setTranslation(pos - glm::vec3(0.001f, 0, 0));
			
			//monitorTransform.setRotation(GetLookVector(pos, camera.getPosition(), {0,1,0}));
			// -----------------------------------------------------------------------------------------------------------
			
			//Hierarchy Update
			HierachySystem hs(registry);
			hs.ApplyHiarchialChange();

			// Physics
			if (runPhys) {
				BGLJolt::GetInstance()->ApplyTransformToKinematic(frameTime);
				BGLJolt::GetInstance()->Step(frameTime, 3);
				BGLJolt::GetInstance()->ApplyPhysicsTransform();
			}

			//Update UBO with light info
			GlobalUBO ubo{};
			ubo.updateCameraInfo(camera.getProjection(), camera.getView(), camera.getInverseView());
			if (rotateLight) pointLightSystem.update(ubo, frameTime);
			else pointLightSystem.update(ubo, 0);

			// Render
			// imgui new frame
			ImGui_ImplVulkan_NewFrame();
			ImGui_ImplGlfw_NewFrame();
			ImGui::NewFrame();
			VkExtent2D ext = bglRenderer.getExtent();

			//imgui draw commands
			if(showInfo) DrawInfoPanels(registry, ext.width, ext.height, camera.getProjection(), camera.getView());
			ConsoleApp::Instance()->Draw("Console", nullptr);
			ImGui::ShowMetricsWindow();
			ImGui::Render();
			
			// bglRenderer.beginFrame returns nullptr if the swapchain needs to be recreated
			if (auto primaryCommandBuffer = bglRenderer.beginPrimaryCMD()) {
				
				// Frame info hold secondary commandBuffer because that's where it will be recorded
				// Only the ImGUI will be recorded to the primary commandBuffer
				FrameInfo frameInfo{
					frameTime,
					primaryCommandBuffer,
					camera,
					descriptorManager->getDescriptorSet(bglRenderer.getFrameIndex()),
					registry
				};

				//Apply UBO changes to buffer
				
				uboBuffers->writeToBuffer(&ubo);
				uboBuffers->flush();
				
				//Offscreen Render
				bglRenderer.beginOffScreenRenderPass(primaryCommandBuffer);
				modelRenderSystemOffscreen.renderEntities(frameInfo);
				pointLightSystemOffscreen.render(frameInfo);
				bglRenderer.endCurrentRenderPass(primaryCommandBuffer);


				bglRenderer.beginSwapChainRenderPass(primaryCommandBuffer);
				//always render solid objects before rendering transparent objects
				modelRenderSystem.renderEntities(frameInfo);
				if(showWireframe) wireframeRenderSystem.renderEntities(frameInfo);
				pointLightSystem.render(frameInfo);
				
				ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), primaryCommandBuffer);

				bglRenderer.endCurrentRenderPass(primaryCommandBuffer);
				bglRenderer.endPrimaryCMD();
			}
			auto stop = std::chrono::high_resolution_clock::now();
			prevFrameTime = std::chrono::duration_cast<std::chrono::microseconds>(stop - start);

			if(showFPS) std::cout << (long long)1000000/ prevFrameTime.count() << "fps\n";
		}
		//CPU will block until all gpu operations are complete
#ifdef SYNC_DEVICEWAITIDLE
		vkDeviceWaitIdle(BGLDevice::device());
#endif
	}
	// Creates entities for test purpose
	entt::entity FirstApp::loadECSObjects() {
		auto modelBuilder = new ModelComponentBuilder(bglDevice, registry);
		auto textureBuilder = new TextureComponentBuilder(bglDevice, *globalPool, *descriptorManager);
		{
			auto e1 = registry.create();
			auto& tfc1 = registry.emplace<TransformComponent>(e1);
			auto& tc1 = registry.emplace<DiffuseTextureComponent>(e1);
			tfc1.setTranslation({ 0.55f,5.3f,0.0 });

			BGLJolt::PhysicsBodyCreationInfo info3{
				tfc1.getTranslation(),{0,0,0},PhysicsType::DYNAMIC, false, PhysicsLayers::MOVING
			};
			BGLJolt::GetInstance()->AddSphere(e1, 0.5f, info3);
			ModelComponent& comp = modelBuilder->buildComponent<ModelComponent>(e1, "/models/cube.obj", ComponentBuildMode::FACES);
			//mdc1.textureMapFlag |= ModelComponent::TextureCompositeFlag::DIFFUSE;
			textureBuilder->setBuildTarget(&tc1);
			textureBuilder->buildComponent("/materials/Bricks089_1K-PNG_Color.png");
			comp.setDiffuseTextureToSubmesh(0, tc1.textureHandle[0]);
		}

		{
			auto e1 = registry.create();
			auto& tfc1 = registry.emplace<TransformComponent>(e1);
			auto& tc1 = registry.emplace<DiffuseTextureComponent>(e1);
			tfc1.setTranslation({ -0.55f,5.3f,0.0 });

			BGLJolt::PhysicsBodyCreationInfo info3{
				tfc1.getTranslation(),{0,0,0},PhysicsType::DYNAMIC, false, PhysicsLayers::MOVING
			};
			BGLJolt::GetInstance()->AddSphere(e1, 0.5f, info3);
			ModelComponent& comp = modelBuilder->buildComponent<ModelComponent>(e1, "/models/cube.obj", ComponentBuildMode::FACES);
			textureBuilder->setBuildTarget(&tc1);
			textureBuilder->buildComponent("/materials/Bricks089_1K-PNG_Color.png");
			comp.setDiffuseTextureToSubmesh(0, tc1.textureHandle[0]);
		}
		auto e1 = registry.create();
		auto& tfc1 = registry.emplace<TransformComponent>(e1);
		auto& tc1 = registry.emplace<DiffuseTextureComponent>(e1);
		tfc1.setLocalTranslation({ 0.3f,0.5f,0.0 });

		BGLJolt::PhysicsBodyCreationInfo info{
			{0,0,0},{0,0,0},PhysicsType::KINEMATIC, false, PhysicsLayers::MOVING
		};
		BGLJolt::GetInstance()->AddSphere(e1, 0.3f, info);

		auto e2 = registry.create();
		auto& tfc2 = registry.emplace<TransformComponent>(e2);
		auto& tc2 = registry.emplace<DiffuseTextureComponent>(e2);
		tfc2.setScale({ 0.2f,0.2f,0.2f });
		tfc2.setRotation({ glm::pi<float>()/2,0,0});
		tfc2.setTranslation({ 0.0f, -0.5f, 0.0f });

		BGLJolt::PhysicsBodyCreationInfo info2{
			tfc2.getTranslation(),tfc2.getRotation(),PhysicsType::DYNAMIC, false, PhysicsLayers::MOVING
		};
		BGLJolt::GetInstance()->AddSphere(e2, 0.5f, info2);

		ModelComponent& comp1 = modelBuilder->buildComponent<ModelComponent>(e1, "/models/cube.obj", ComponentBuildMode::FACES);
		ModelComponent& comp2 = modelBuilder->buildComponent<ModelComponent>(e2, "/models/cube.obj", ComponentBuildMode::FACES);
		textureBuilder->setBuildTarget(&tc1);
		textureBuilder->buildComponent("/materials/Bricks089_1K-PNG_Color.png");
		textureBuilder->setBuildTarget(&tc2);
		textureBuilder->buildComponent("/materials/Bricks089_1K-PNG_Color.png");
		comp1.setDiffuseTextureToSubmesh(0, tc1.textureHandle[0]);
		comp2.setDiffuseTextureToSubmesh(1, tc2.textureHandle[0]);
		
		auto e_axis = registry.create();
		auto& tfc3 = registry.emplace<bagel::TransformComponent>(e_axis);
		auto& tc3 = registry.emplace<bagel::DiffuseTextureComponent>(e_axis);
		//auto& mdc3 = registry.emplace<bagel::ModelComponent>(e_axis);
		//modelBuilder->buildComponent("/models/axis.obj", ComponentBuildMode::FACES, mdc3.modelName, mdc3.vertexCount, mdc3.indexCount);
		ModelComponent& axisComp = modelBuilder->buildComponent<ModelComponent>(e_axis, "/models/axis.obj", ComponentBuildMode::FACES);
		textureBuilder->setBuildTarget(&tc3);
		textureBuilder->buildComponent("/materials/models/axis.ktx");
		axisComp.setDiffuseTextureToSubmesh(0,tc3.textureHandle[0]);

		tfc3.setScale({ 1.0f,1.0f, 1.0f });

		//Parent e1 to e_axis
		HierachySystem hs(registry);
		hs.CreateHierachy(e_axis, e1);
		
		delete modelBuilder;
		delete textureBuilder;

		return e_axis;
	}
	
	entt::entity FirstApp::makeTestEntity(glm::vec3 translation) {
		auto modelBuilder = new ModelComponentBuilder(bglDevice, registry);
		auto textureBuilder = new TextureComponentBuilder(bglDevice, *globalPool, *descriptorManager);

		auto e1 = registry.create();
		auto& tfc1 = registry.emplace<TransformComponent>(e1);
		registry.emplace<InfoComponent>(e1);
		//auto& mdc1 = registry.emplace<ModelComponent>(e1);
		auto& tc1 = registry.emplace<DiffuseTextureComponent>(e1);
		tfc1.setTranslation(translation);

		BGLJolt::PhysicsBodyCreationInfo info3{
			tfc1.getTranslation(),{0,0,0},PhysicsType::DYNAMIC, false, PhysicsLayers::MOVING
		};
		BGLJolt::GetInstance()->AddSphere(e1, 0.5f, info3);
		//modelBuilder->buildComponent("/models/cube.obj", ComponentBuildMode::FACES, mdc1.modelName, mdc1.vertexCount, mdc1.indexCount);
		ModelComponent& comp1 = modelBuilder->buildComponent<ModelComponent>(e1, "/models/cube.obj", ComponentBuildMode::FACES);
		//mdc1.textureMapFlag |= ModelComponent::TextureCompositeFlag::DIFFUSE;
		textureBuilder->setBuildTarget(&tc1);
		textureBuilder->buildComponent("/materials/Bricks089_1K-PNG_Color.png");
		
		delete modelBuilder;
		delete textureBuilder;
		
		return e1;
	}
	void FirstApp::makeGrid() {
		auto modelBuilder = new ModelComponentBuilder(bglDevice, registry);
		{
			auto e1 = registry.create();
			auto& tfc1 = registry.emplace<bagel::TransformComponent>(e1);
			tfc1.setScale({ 1,1,1 });

			CONSOLE->Log("FirstApp::makeGrid", "Building Grid");
			modelBuilder->buildComponent<WireframeComponent>(e1, "grid", ComponentBuildMode::LINES);
		}
		{
			auto e1 = registry.create();
			auto& tfc1 = registry.emplace<bagel::TransformComponent>(e1);
			tfc1.setScale({ 1,1,1 });
			
			CONSOLE->Log("FirstApp::makeGrid", "Building Wiresphere");
			modelBuilder->buildComponent<WireframeComponent>(e1, "/models/wiresphere.obj", ComponentBuildMode::LINES);
		}

		delete modelBuilder;
	}
	entt::entity FirstApp::makeAxisModel(glm::vec3 pos)
	{
		auto modelBuilder = new ModelComponentBuilder(bglDevice, registry);
		auto textureBuilder = new TextureComponentBuilder(bglDevice, *globalPool, *descriptorManager);

		auto e1 = registry.create();
		auto& tfc1 = registry.emplace<bagel::TransformComponent>(e1);
		auto& tc1 = registry.emplace<bagel::DiffuseTextureComponent>(e1);
		tfc1.setTranslation(pos);

		//auto& mdc1 = registry.emplace<bagel::ModelComponent>(e1);
		//modelBuilder->buildComponent("/models/axis.obj", ComponentBuildMode::FACES, mdc1.modelName, mdc1.vertexCount, mdc1.indexCount);
		ModelComponent& comp1 = modelBuilder->buildComponent<ModelComponent>(e1, "/models/axis.obj", ComponentBuildMode::FACES);
		textureBuilder->setBuildTarget(&tc1);
		textureBuilder->buildComponent("/materials/models/axis.ktx");
		comp1.setDiffuseTextureToSubmesh(0, tc1.textureHandle[0]);
		//mdc1.textureMapFlag |= ModelComponent::TextureCompositeFlag::DIFFUSE;

		delete modelBuilder;
		delete textureBuilder;

		return e1;
	}
	entt::entity FirstApp::createMonitor() {
		auto modelBuilder = new ModelComponentBuilder(bglDevice, registry);
		auto textureBuilder = new TextureComponentBuilder(bglDevice, *globalPool, *descriptorManager);
		
		auto e1 = registry.create();
		auto& tfc1 = registry.emplace<TransformComponent>(e1);
		auto& tc1 = registry.emplace<DiffuseTextureComponent>(e1);
		//auto& mdc1 = registry.emplace<ModelComponent>(e1);
		//modelBuilder->buildComponent("/models/floor.obj", ComponentBuildMode::FACES, mdc1.modelName, mdc1.vertexCount, mdc1.indexCount);
		ModelComponent& comp1 = modelBuilder->buildComponent<ModelComponent>(e1, "/models/floor.obj", ComponentBuildMode::FACES);
		//mdc1.textureMapFlag |= ModelComponent::TextureCompositeFlag::DIFFUSE;

		textureBuilder->setBuildTarget(&tc1);
		textureBuilder->buildComponent("OffscreenRenderTarget");  // "/materials/texture.jpg"
		comp1.setDiffuseTextureToSubmesh(0, tc1.textureHandle[0]);
		tfc1.setScale({ 5.0,5.0,5.0 });
		tfc1.setTranslation({ 6.0,6.0,6.0 });
		CONSOLE->Log("FirstApp::createMonitor", "Designating OffscreenRenderTarget as texture");
		
		delete modelBuilder;
		delete textureBuilder;
		return e1;
	}
	void FirstApp::createLights()
	{
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
			registry.emplace<TransformComponent>(entity, (rotateLight * glm::vec4(glm::vec3{ 3.f,1.0f,0.0f }, 1.0f)));
			registry.emplace<InfoComponent>(entity);
			auto& light = registry.emplace<PointLightComponent>(entity);
			light.color = glm::vec4(lightColors[i], 4.0f);
		}
	}
	entt::entity FirstApp::createChineseDragon()
	{
		auto modelBuilder = new ModelComponentBuilder(bglDevice, registry);
		auto textureBuilder = new TextureComponentBuilder(bglDevice, *globalPool, *descriptorManager);
		
		entt::entity entity = registry.create();
		
		auto& tfc = registry.emplace<TransformComponent>(entity);
		tfc.setScale({ 2.0f,2.0f,2.0f });
		//tfc.setScale({ 0.2f,0.2f,0.2f });
		//tfc.setRotation({ glm::pi<float>() / 2,0,0 });

		auto& dc = registry.emplace<DiffuseTextureComponent>(entity);
		auto& nc = registry.emplace<NormalTextureComponent>(entity);
		auto& rc = registry.emplace<RoughnessMetalTextureComponent>(entity);
		textureBuilder->setBuildTarget(&dc);
		textureBuilder->buildComponent("/materials/Bricks089_1K-PNG_Color.png");
		textureBuilder->setBuildTarget(&nc);
		//normal maps must be loaded with VK_FORMAT_R8G8B8A8_UNORM
		textureBuilder->buildComponent("/materials/Bricks089_1K-PNG_NormalGL.png", VK_FORMAT_R8G8B8A8_UNORM);
		textureBuilder->setBuildTarget(&rc);
		textureBuilder->buildComponent("/materials/Bricks089_1K-PNG_Roughness.png");

		Material material{};
		material.name = "New Material";
		material.albedoMap = dc.textureHandle[0];
		material.normalMap = nc.textureHandle[0];
		material.roughMap = rc.textureHandle[0];

		std::vector<Material> materials = { material };
		
		modelBuilder->saveNormalData();
		modelBuilder->configureModelMaterialSet(&materials);
		ModelComponent& model = modelBuilder->buildComponent<ModelComponent>(entity, "/models/cylinder.obj", ComponentBuildMode::FACES);
		WireframeComponent& normals = modelBuilder->getNormalDataAsWireframe(entity);

		delete modelBuilder;
		delete textureBuilder;
		return entity;
	}
	void FirstApp::initCommand()
	{
		CONSOLE->AddCommand("FREEFLY", this, ConsoleCommand::ToggleFly);
		CONSOLE->AddCommand("TOGGLEPHYSICS", this, ConsoleCommand::TogglePhys);
		CONSOLE->AddCommand("ROTATELIGHT", this, ConsoleCommand::RotateLight);
		CONSOLE->AddCommand("SHOWFPS", this, ConsoleCommand::ShowFPS);
		CONSOLE->AddCommand("SHOWINFO", this, ConsoleCommand::ShowInfo);
		CONSOLE->AddCommand("SHOWWIREFRAME", this, ConsoleCommand::ShowWireframe);
	}

	void FirstApp::initJolt()
	{
		BGLJolt::Initialize(bglDevice, registry);
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

