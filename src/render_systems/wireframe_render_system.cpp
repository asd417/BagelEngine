#include "wireframe_render_system.hpp"

#include <array>
#include <cstring>
#include <iostream>
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>
#include <glm/gtc/matrix_transform.hpp>

#include "bagel_util.hpp"
#include "math/bagel_math.hpp"
#include "model/bagel_model.hpp"
#include "ecs/components/model.hpp"
#include "ecs/components/transform.hpp"
#include "engine/bagel_engine_device.hpp"
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


	namespace ConsoleCommand {
		//WireframeRenderSystem controllers here
		
	}

	inline void SendPushConstantData(VkCommandBuffer cmdBuffer, VkPipelineLayout pipelineLayout, WireframePushConstantData& push)
	{
		vkCmdPushConstants(
			cmdBuffer,
			pipelineLayout,
			VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
			0,
			sizeof(WireframePushConstantData),
			&push);
	}

	//Function that binds index buffer if the index count of the component is more than 0
	inline void BindVertexIndexBuffer(VkCommandBuffer cmdBuffer, const VkBuffer* pBuffers, const VkDeviceSize* pOffsets, uint32_t indexCount, VkBuffer buffer) {
		vkCmdBindVertexBuffers(cmdBuffer, 0, 1, pBuffers, pOffsets);
		if (indexCount > 0) vkCmdBindIndexBuffer(cmdBuffer, buffer, 0, VK_INDEX_TYPE_UINT32);
	}
	inline void DrawByIndexCount(VkCommandBuffer cmdBuffer, uint32_t vertexCount, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex) {
		if (indexCount > 0) {
			vkCmdDrawIndexed(cmdBuffer, indexCount, instanceCount, firstIndex, 0, 0);
		}
		else {
			vkCmdDraw(cmdBuffer, vertexCount, instanceCount, firstIndex, 0);
		}
	}

	//Set up pipeline configuration here
	void WireframePipelineConfigModifier(PipelineConfigInfo& pipelineConfig) {
		pipelineConfig.rasterizationInfo.polygonMode = VK_POLYGON_MODE_LINE;
		pipelineConfig.inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
		pipelineConfig.rasterizationInfo.cullMode = VK_CULL_MODE_NONE;
		pipelineConfig.rasterizationInfo.lineWidth = 1.0f;
	}

	// Pipeline for overlaying ordinary triangle meshes as wireframe (renderModelsWireframe).
	// TRIANGLE_LIST input rasterized as edges (POLYGON_MODE_LINE), back faces culled so only the
	// front-facing edges draw. The swapchain pass runs after blitGBufferDepthToSwapchain, so the
	// depth buffer holds the solid surface depth: LESS_OR_EQUAL lets the coplanar edges pass (the
	// planet/model vertex positions are identical to what the G-buffer drew), and depth-write is
	// off so this overlay doesn't perturb depth for anything drawn afterwards.
	void ModelWireframePipelineConfigModifier(PipelineConfigInfo& pipelineConfig) {
		pipelineConfig.rasterizationInfo.polygonMode = VK_POLYGON_MODE_LINE;
		pipelineConfig.inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		pipelineConfig.rasterizationInfo.cullMode = VK_CULL_MODE_BACK_BIT;
		pipelineConfig.rasterizationInfo.lineWidth = 1.0f;
		pipelineConfig.depthStencilInfo.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
		pipelineConfig.depthStencilInfo.depthWriteEnable = VK_FALSE;
	}

	static void uploadBuffer(BGLDevice& dev, VkBufferUsageFlags usage,
	                         const void* src, VkDeviceSize size,
	                         VkBuffer& dstBuf, VkDeviceMemory& dstMem)
	{
		VkBuffer stagingBuf; VkDeviceMemory stagingMem;
		dev.createBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			stagingBuf, stagingMem);
		void* mapped;
		vkMapMemory(BGLDevice::device(), stagingMem, 0, VK_WHOLE_SIZE, 0, &mapped);
		memcpy(mapped, src, size);
		vkUnmapMemory(BGLDevice::device(), stagingMem);
		dev.createBuffer(size, usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, dstBuf, dstMem);
		dev.copyBuffer(stagingBuf, dstBuf, size);
		vkDestroyBuffer(BGLDevice::device(), stagingBuf, nullptr);
		vkFreeMemory(BGLDevice::device(), stagingMem, nullptr);
	}

	WireframeRenderSystem::WireframeRenderSystem(
		VkRenderPass renderPass,
		std::vector<VkDescriptorSetLayout> setLayouts,
		std::unique_ptr<BGLBindlessDescriptorManager> const& _descriptorManager,
		entt::registry& _registry,
		BGLDevice &bglDevice) :
		BGLRenderSystem{ renderPass, setLayouts, sizeof(WireframePushConstantData) },
		registry{ _registry },
		descriptorManager{ _descriptorManager },
		device{ bglDevice }
	{
		std::cout << "Creating Wireframe Render System\n";
		createPipeline(renderPass, "/shaders/wireframe_shader.vert.spv", "/shaders/wireframe_shader.frag.spv", WireframePipelineConfigModifier);

		// Second pipeline: same shader + layout, but TRIANGLE_LIST polygon-line for drawing solid
		// meshes as wireframe. Built here (not via the base createPipeline, which owns the single
		// bglPipeline) so both pipelines coexist.
		{
			PipelineConfigInfo cfg{};
			BGLPipeline::defaultPipelineConfigInfo(cfg);
			cfg.renderPass = renderPass;
			cfg.pipelineLayout = pipelineLayout;
			ModelWireframePipelineConfigModifier(cfg);
			modelWirePipeline = std::make_unique<BGLPipeline>(
				util::enginePath("/shaders/wireframe_shader.vert.spv"),
				util::enginePath("/shaders/wireframe_shader.frag.spv"),
				cfg);
		}

		// Build shared unit wire cube [-1,-1,-1]→[1,1,1] for bbox drawing
		auto corner = [](float x, float y, float z) {
			BGLModel::Vertex v{};
			v.position = {x, y, z};
			return v;
		};
		const std::array<BGLModel::Vertex, 8> verts = {
			corner(-1,-1,-1), corner( 1,-1,-1),
			corner( 1, 1,-1), corner(-1, 1,-1),
			corner(-1,-1, 1), corner( 1,-1, 1),
			corner( 1, 1, 1), corner(-1, 1, 1),
		};
		// 12 edges: bottom face, top face, 4 verticals
		const std::array<uint32_t, BBOX_INDEX_COUNT> idx = {
			0,1, 1,2, 2,3, 3,0,
			4,5, 5,6, 6,7, 7,4,
			0,4, 1,5, 2,6, 3,7,
		};
		uploadBuffer(bglDevice, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
			verts.data(), sizeof(verts), bboxVertexBuffer, bboxVertexMemory);
		uploadBuffer(bglDevice, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
			idx.data(), sizeof(idx), bboxIndexBuffer, bboxIndexMemory);
	}

	WireframeRenderSystem::~WireframeRenderSystem()
	{
		if (bboxVertexBuffer != VK_NULL_HANDLE) {
			vkDestroyBuffer(BGLDevice::device(), bboxVertexBuffer, nullptr);
			vkFreeMemory(BGLDevice::device(), bboxVertexMemory, nullptr);
		}
		if (bboxIndexBuffer != VK_NULL_HANDLE) {
			vkDestroyBuffer(BGLDevice::device(), bboxIndexBuffer, nullptr);
			vkFreeMemory(BGLDevice::device(), bboxIndexMemory, nullptr);
		}
	}

	void WireframeRenderSystem::renderEntities(FrameInfo& frameInfo)
	{
		bglPipeline->bind(frameInfo.commandBuffer);
		vkCmdBindDescriptorSets(
			frameInfo.commandBuffer,
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			pipelineLayout,
			0, //first set
			1, //descriptorSet Count 
			&frameInfo.globalDescriptorSets,
			0, nullptr);

		VkDeviceSize offsets[] = { 0 };
		auto transformCompView = registry.group<>(entt::get<TransformComponent, WireframeComponent>);
		for (auto [entity, transformComp, modelDescComp] : transformCompView.each()) {
			BindVertexIndexBuffer(frameInfo.commandBuffer, &modelDescComp.vertexBuffer, offsets, modelDescComp.indexCount, modelDescComp.indexBuffer);

			WireframePushConstantData push{};
			push.UsesBufferedTransform = 0;
			push.modelMatrix = transformComp.getMat4();
			for (uint32_t i = 0; i < modelDescComp.submeshCount; i++) {
				SendPushConstantData(frameInfo.commandBuffer, pipelineLayout, push);
				DrawByIndexCount(frameInfo.commandBuffer, modelDescComp.vertexCount, modelDescComp.indexCount, 1, modelDescComp.submeshes[i].firstIndex);
			}
		}
		
		auto instancedRenderView = registry.group<>(entt::get<TransformArrayComponent, WireframeComponent>);
		for (auto [entity, transformComp, modelDescComp] : instancedRenderView.each()) {
			BindVertexIndexBuffer(frameInfo.commandBuffer, &modelDescComp.vertexBuffer, offsets, modelDescComp.indexCount, modelDescComp.indexBuffer);

			WireframePushConstantData push{};
			push.UsesBufferedTransform   = transformComp.useBuffer() ? 1 : 0;
			push.BufferedTransformHandle = transformComp.bufferHandle;
			if (!transformComp.useBuffer()) {
				push.modelMatrix = transformComp.mat4(0);
			}
			for (uint32_t i = 0; i < modelDescComp.submeshCount; i++) {
				SendPushConstantData(frameInfo.commandBuffer, pipelineLayout, push);
				DrawByIndexCount(frameInfo.commandBuffer, modelDescComp.vertexCount, modelDescComp.indexCount, transformComp.count(), modelDescComp.submeshes[i].firstIndex);
			}
		}
	}

	void WireframeRenderSystem::renderModelsWireframe(FrameInfo& frameInfo)
	{
		Frustum frustum;
		frustum.extractFromVP(frameInfo.camera.getProjection() * frameInfo.camera.getView());

		modelWirePipeline->bind(frameInfo.commandBuffer);
		vkCmdBindDescriptorSets(
			frameInfo.commandBuffer,
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			pipelineLayout,
			0, 1,
			&frameInfo.globalDescriptorSets,
			0, nullptr);

		VkDeviceSize offsets[] = { 0 };
		// Distinct from the green bbox lines so the two overlays are tellable apart.
		const glm::vec4 wireColor{ 0.6f, 1.0f, 0.6f, 1.0f };

		// Single-transform models (the planet is one of these — keep it, that's the whole point).
		auto singleView = registry.view<TransformComponent, ModelComponent>();
		for (auto [entity, transform, model] : singleView.each()) {
			if (model.mesh().isSkinned) continue; // skinned verts are bind-pose here; they'd misalign the deformed surface
			if (model.mesh().vertexBuffer == VK_NULL_HANDLE || model.mesh().vertexCount == 0) continue;
			glm::mat4 modelMatrix = transform.getMat4();
			if (model.frustumCull && !frustum.testAABB(model.mesh().aabbMin, model.mesh().aabbMax, modelMatrix))
				continue;

			vkCmdBindVertexBuffers(frameInfo.commandBuffer, 0, 1, &model.mesh().vertexBuffer, offsets);
			if (model.mesh().indexCount > 0)
				vkCmdBindIndexBuffer(frameInfo.commandBuffer, model.mesh().indexBuffer, 0, VK_INDEX_TYPE_UINT32);

			WireframePushConstantData push{};
			push.UsesBufferedTransform = 0;
			push.modelMatrix = modelMatrix;
			push.color       = wireColor;
			SendPushConstantData(frameInfo.commandBuffer, pipelineLayout, push);
			// Solid submeshes only — transparent ones (e.g. the planet ocean) keep their own look.
			for (const ModelComponent::Submesh& sm : model.solidSubmeshes()) {
				if (model.mesh().indexCount > 0)
					vkCmdDrawIndexed(frameInfo.commandBuffer, sm.indexCount, 1, sm.firstIndex, 0, 0);
				else
					vkCmdDraw(frameInfo.commandBuffer, sm.vertexCount, 1, sm.firstVertex, 0);
			}
		}

		// Instanced/buffered-transform models.
		auto instancedView = registry.view<TransformArrayComponent, ModelComponent>();
		for (auto [entity, transform, model] : instancedView.each()) {
			if (model.mesh().isSkinned) continue;
			if (model.mesh().vertexBuffer == VK_NULL_HANDLE || model.mesh().vertexCount == 0) continue;

			vkCmdBindVertexBuffers(frameInfo.commandBuffer, 0, 1, &model.mesh().vertexBuffer, offsets);
			if (model.mesh().indexCount > 0)
				vkCmdBindIndexBuffer(frameInfo.commandBuffer, model.mesh().indexBuffer, 0, VK_INDEX_TYPE_UINT32);

			WireframePushConstantData push{};
			push.UsesBufferedTransform   = transform.useBuffer() ? 1 : 0;
			push.BufferedTransformHandle = transform.bufferHandle;
			if (!transform.useBuffer()) {
				push.modelMatrix = transform.mat4(0);
			}
			push.color = wireColor;
			SendPushConstantData(frameInfo.commandBuffer, pipelineLayout, push);
			for (const ModelComponent::Submesh& sm : model.solidSubmeshes()) {
				if (model.mesh().indexCount > 0)
					vkCmdDrawIndexed(frameInfo.commandBuffer, sm.indexCount, transform.count(), sm.firstIndex, 0, 0);
				else
					vkCmdDraw(frameInfo.commandBuffer, sm.vertexCount, transform.count(), sm.firstVertex, 0);
			}
		}
	}

	void WireframeRenderSystem::renderBBoxes(FrameInfo& frameInfo)
	{
		bglPipeline->bind(frameInfo.commandBuffer);
		vkCmdBindDescriptorSets(
			frameInfo.commandBuffer,
			VK_PIPELINE_BIND_POINT_GRAPHICS,
			pipelineLayout,
			0, 1,
			&frameInfo.globalDescriptorSets,
			0, nullptr);

		VkDeviceSize zero = 0;
		vkCmdBindVertexBuffers(frameInfo.commandBuffer, 0, 1, &bboxVertexBuffer, &zero);
		vkCmdBindIndexBuffer(frameInfo.commandBuffer, bboxIndexBuffer, 0, VK_INDEX_TYPE_UINT32);

		auto view = registry.view<TransformComponent, ModelComponent>();
		for (auto [entity, transformComp, modelComp] : view.each()) {
			if (modelComp.mesh().aabbMin == modelComp.mesh().aabbMax) continue;

			glm::vec3 center  = (modelComp.mesh().aabbMin + modelComp.mesh().aabbMax) * 0.5f;
			glm::vec3 halfExt = (modelComp.mesh().aabbMax - modelComp.mesh().aabbMin) * 0.5f;

			glm::mat4 bboxMat = transformComp.getMat4()
				* glm::translate(glm::mat4{1.0f}, center)
				* glm::scale(glm::mat4{1.0f}, halfExt);

			WireframePushConstantData push{};
			push.UsesBufferedTransform = 0;
			push.modelMatrix = bboxMat;
			push.color = glm::vec4{0.0f, 1.0f, 0.0f, 1.0f};
			SendPushConstantData(frameInfo.commandBuffer, pipelineLayout, push);
			vkCmdDrawIndexed(frameInfo.commandBuffer, BBOX_INDEX_COUNT, 1, 0, 0, 0);
		}
	}

	void WireframeRenderSystem::renderSelection(FrameInfo& frameInfo, entt::entity entity)
	{
		if (entity == entt::null || !registry.valid(entity)) return;
		auto* transformComp = registry.try_get<TransformComponent>(entity);
		auto* modelComp     = registry.try_get<ModelComponent>(entity);
		if (!transformComp || !modelComp) return;
		if (modelComp->mesh().aabbMin == modelComp->mesh().aabbMax) return; // no drawable bounds

		bglPipeline->bind(frameInfo.commandBuffer);
		vkCmdBindDescriptorSets(frameInfo.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
			pipelineLayout, 0, 1, &frameInfo.globalDescriptorSets, 0, nullptr);

		VkDeviceSize zero = 0;
		vkCmdBindVertexBuffers(frameInfo.commandBuffer, 0, 1, &bboxVertexBuffer, &zero);
		vkCmdBindIndexBuffer(frameInfo.commandBuffer, bboxIndexBuffer, 0, VK_INDEX_TYPE_UINT32);

		glm::vec3 center  = (modelComp->mesh().aabbMin + modelComp->mesh().aabbMax) * 0.5f;
		glm::vec3 halfExt = (modelComp->mesh().aabbMax - modelComp->mesh().aabbMin) * 0.5f;
		glm::mat4 bboxMat = transformComp->getMat4()
			* glm::translate(glm::mat4{1.0f}, center)
			* glm::scale(glm::mat4{1.0f}, halfExt);

		WireframePushConstantData push{};
		push.UsesBufferedTransform = 0;
		push.modelMatrix = bboxMat;
		push.color = glm::vec4{1.0f, 0.85f, 0.1f, 1.0f}; // amber selection outline
		SendPushConstantData(frameInfo.commandBuffer, pipelineLayout, push);
		vkCmdDrawIndexed(frameInfo.commandBuffer, BBOX_INDEX_COUNT, 1, 0, 0, 0);
	}
}

