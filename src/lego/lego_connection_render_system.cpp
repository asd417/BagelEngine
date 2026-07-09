#include "lego_connection_render_system.hpp"

#include "../render_systems/wireframe_render_system.hpp" // WireframePushConstantData (shared layout)
#include "../bagel_engine_device.hpp"
#include "../bagel_model.hpp"                              // BGLModel::Vertex
#include "../components/transform.hpp"                     // TransformComponent
#include "components/lego_connection_component.hpp"        // LegoConnectionComponent / LegoConnectionPoint

#include <vulkan/vulkan.h>
#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <cstring>
#include <cmath>
#include <vector>

namespace bagel
{

	// Line list, no cull, no depth test/write — the markers always draw on top of the scene.
	static void LegoPipelineConfigModifier(PipelineConfigInfo &cfg)
	{
		cfg.rasterizationInfo.polygonMode = VK_POLYGON_MODE_LINE;
		cfg.inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
		cfg.rasterizationInfo.cullMode = VK_CULL_MODE_NONE;
		cfg.rasterizationInfo.lineWidth = 1.0f;
		cfg.depthStencilInfo.depthTestEnable = VK_FALSE;
		cfg.depthStencilInfo.depthWriteEnable = VK_FALSE;
	}

	static void uploadVB(BGLDevice &dev, const std::vector<BGLModel::Vertex> &verts,
						 VkBuffer &dstBuf, VkDeviceMemory &dstMem)
	{
		const VkDeviceSize size = sizeof(BGLModel::Vertex) * verts.size();
		VkBuffer staging;
		VkDeviceMemory stagingMem;
		dev.createBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
						 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
						 staging, stagingMem);
		void *mapped;
		vkMapMemory(BGLDevice::device(), stagingMem, 0, VK_WHOLE_SIZE, 0, &mapped);
		memcpy(mapped, verts.data(), static_cast<size_t>(size));
		vkUnmapMemory(BGLDevice::device(), stagingMem);
		dev.createBuffer(size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
						 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, dstBuf, dstMem);
		dev.copyBuffer(staging, dstBuf, size);
		vkDestroyBuffer(BGLDevice::device(), staging, nullptr);
		vkFreeMemory(BGLDevice::device(), stagingMem, nullptr);
	}

	static BGLModel::Vertex P(float x, float y, float z)
	{
		BGLModel::Vertex v{};
		v.position = {x, y, z};
		return v;
	}

	LegoConnectionRenderSystem::LegoConnectionRenderSystem(
		VkRenderPass renderPass,
		std::vector<VkDescriptorSetLayout> setLayouts,
		BGLDevice &bglDevice)
		: BGLRenderSystem{renderPass, setLayouts, sizeof(WireframePushConstantData)}, device{bglDevice}
	{
		createPipeline(renderPass, "/shaders/wireframe_shader.vert.spv",
					   "/shaders/wireframe_shader.frag.spv", LegoPipelineConfigModifier);

		// Ring of unit radius in the local XY plane (normal +Z), as a line loop.
		constexpr int SEG = 48;
		std::vector<BGLModel::Vertex> ring;
		for (int i = 0; i < SEG; ++i)
		{
			const float a0 = glm::two_pi<float>() * (i) / SEG;
			const float a1 = glm::two_pi<float>() * (i + 1) / SEG;
			ring.push_back(P(cosf(a0), sinf(a0), 0));
			ring.push_back(P(cosf(a1), sinf(a1), 0));
		}
		ringCount = static_cast<uint32_t>(ring.size());
		uploadVB(device, ring, ringVB, ringMem);

		// Unit segment origin -> +X, transformed per marker into the connector axis.
		std::vector<BGLModel::Vertex> seg = {P(0, 0, 0), P(1, 0, 0)};
		segCount = static_cast<uint32_t>(seg.size());
		uploadVB(device, seg, segVB, segMem);
	}

	LegoConnectionRenderSystem::~LegoConnectionRenderSystem()
	{
		auto destroy = [](VkBuffer b, VkDeviceMemory m)
		{
			if (b != VK_NULL_HANDLE)
			{
				vkDestroyBuffer(BGLDevice::device(), b, nullptr);
				vkFreeMemory(BGLDevice::device(), m, nullptr);
			}
		};
		destroy(ringVB, ringMem);
		destroy(segVB, segMem);
	}

	void LegoConnectionRenderSystem::drawMesh(VkCommandBuffer cmd, VkBuffer vb, uint32_t vertCount,
											  const glm::mat4 &model, float scale, const glm::vec4 &color)
	{
		// Fold the uniform scale into the matrix's basis columns (== model * scale(s)).
		glm::mat4 scaledModel = model;
		scaledModel[0] *= scale;
		scaledModel[1] *= scale;
		scaledModel[2] *= scale;
		WireframePushConstantData push{};
		push.UsesBufferedTransform = 0;
		push.modelMatrix = scaledModel;
		push.color = color;
		vkCmdPushConstants(cmd, pipelineLayout,
						   VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
						   0, sizeof(WireframePushConstantData), &push);
		VkDeviceSize zero = 0;
		vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &zero);
		vkCmdDraw(cmd, vertCount, 1, 0, 0);
	}

	void LegoConnectionRenderSystem::render(FrameInfo &frameInfo)
	{
		auto view = frameInfo.registry.view<LegoConnectionComponent, TransformComponent>();
		// Multi-component views expose no empty()/begin()==end(); size_hint() is the smallest
		// candidate pool. 0 => nothing to draw, so skip the pipeline bind.
		if (view.size_hint() == 0)
			return;

		bglPipeline->bind(frameInfo.commandBuffer);
		vkCmdBindDescriptorSets(frameInfo.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
								pipelineLayout, 0, 1, &frameInfo.globalDescriptorSets, 0, nullptr);

		// Marker color per connector type (see LegoConnectionPoint::type).
		const glm::vec4 typeColor[4] = {
			{0.2f, 1.0f, 0.3f, 1.0f}, // 0 male stud   -> green
			{1.0f, 0.3f, 0.3f, 1.0f}, // 1 female tube -> red
			{0.3f, 0.7f, 1.0f, 1.0f}, // 2 pin hole    -> cyan
			{1.0f, 0.6f, 0.1f, 1.0f}, // 3 axle hole   -> orange
		};

		view.each([&](const LegoConnectionComponent &cc, const TransformComponent &tc)
		{
			const glm::mat4 world = tc.computeMat4();

			for (const LegoConnectionPoint &p : cc.points)
			{
				// Orthonormal frame from the connector basis: axis = local +Y, u/v span the
				// perpendicular plane. Built in model-local space, then `world` maps it exactly
				// like the mesh (same scale/rotation), so markers track the part.
				glm::vec3 axis = glm::normalize(p.orient[1]);
				glm::vec3 u = p.orient[0] - axis * glm::dot(p.orient[0], axis);
				if (glm::length(u) < 1e-5f)
					u = glm::vec3(1, 0, 0);
				u = glm::normalize(u);
				glm::vec3 v = glm::cross(axis, u);

				const glm::vec4 col = typeColor[(p.type >= 0 && p.type < 4) ? p.type : 0];

				// Ring in the u/v plane (perpendicular to the axis), radius = p.radius.
				glm::mat4 ringLocal{1.0f};
				ringLocal[0] = glm::vec4(u * p.radius, 0.0f);
				ringLocal[1] = glm::vec4(v * p.radius, 0.0f);
				ringLocal[2] = glm::vec4(axis, 0.0f);
				ringLocal[3] = glm::vec4(p.pos, 1.0f);
				drawMesh(frameInfo.commandBuffer, ringVB, ringCount, world * ringLocal, 1.0f, col);

				// Short line from the point along +axis so the opening direction is visible.
				glm::mat4 segLocal{1.0f};
				segLocal[0] = glm::vec4(axis * (p.radius * 2.5f), 0.0f);
				segLocal[3] = glm::vec4(p.pos, 1.0f);
				drawMesh(frameInfo.commandBuffer, segVB, segCount, world * segLocal, 1.0f, col);
			}
		});
	}

} // namespace bagel
