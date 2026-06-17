#include "gizmo_render_system.hpp"
#include "wireframe_render_system.hpp"   // WireframePushConstantData (shared layout)
#include "../bagel_engine_device.hpp"
#include "../bagel_model.hpp"            // BGLModel::Vertex

#include <vulkan/vulkan.h>
#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <cstring>
#include <cmath>
#include <vector>

namespace bagel {

	// Line list, no cull — and no depth test/write so the gizmo always draws on top of the scene
	// (handles and joint markers stay visible/clickable even when a bone is behind geometry).
	static void GizmoPipelineConfigModifier(PipelineConfigInfo& cfg) {
		cfg.rasterizationInfo.polygonMode = VK_POLYGON_MODE_LINE;
		cfg.inputAssemblyInfo.topology    = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
		cfg.rasterizationInfo.cullMode    = VK_CULL_MODE_NONE;
		cfg.rasterizationInfo.lineWidth   = 1.0f;
		cfg.depthStencilInfo.depthTestEnable  = VK_FALSE;
		cfg.depthStencilInfo.depthWriteEnable = VK_FALSE;
	}

	static void uploadVB(BGLDevice& dev, const std::vector<BGLModel::Vertex>& verts,
	                     VkBuffer& dstBuf, VkDeviceMemory& dstMem)
	{
		const VkDeviceSize size = sizeof(BGLModel::Vertex) * verts.size();
		VkBuffer staging; VkDeviceMemory stagingMem;
		dev.createBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			staging, stagingMem);
		void* mapped;
		vkMapMemory(BGLDevice::device(), stagingMem, 0, VK_WHOLE_SIZE, 0, &mapped);
		memcpy(mapped, verts.data(), static_cast<size_t>(size));
		vkUnmapMemory(BGLDevice::device(), stagingMem);
		dev.createBuffer(size, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, dstBuf, dstMem);
		dev.copyBuffer(staging, dstBuf, size);
		vkDestroyBuffer(BGLDevice::device(), staging, nullptr);
		vkFreeMemory(BGLDevice::device(), stagingMem, nullptr);
	}

	static BGLModel::Vertex P(float x, float y, float z) {
		BGLModel::Vertex v{}; v.position = { x, y, z }; return v;
	}

	GizmoRenderSystem::GizmoRenderSystem(
		VkRenderPass renderPass,
		std::vector<VkDescriptorSetLayout> setLayouts,
		BGLDevice& bglDevice)
		: BGLRenderSystem{ renderPass, setLayouts, sizeof(WireframePushConstantData) }
		, device{ bglDevice }
	{
		createPipeline(renderPass, "/shaders/wireframe_shader.vert.spv",
			"/shaders/wireframe_shader.frag.spv", GizmoPipelineConfigModifier);

		// Arrow along +X: shaft + a 4-line head, unit length.
		std::vector<BGLModel::Vertex> arrow = {
			P(0,0,0),    P(1,0,0),
			P(1,0,0),    P(0.8f, 0.07f, 0),
			P(1,0,0),    P(0.8f,-0.07f, 0),
			P(1,0,0),    P(0.8f, 0, 0.07f),
			P(1,0,0),    P(0.8f, 0,-0.07f),
		};
		arrowCount = static_cast<uint32_t>(arrow.size());
		uploadVB(device, arrow, arrowVB, arrowMem);

		// Ring of unit radius in the local XY plane (normal +Z), as a line loop.
		constexpr int SEG = 48;
		std::vector<BGLModel::Vertex> ring;
		for (int i = 0; i < SEG; ++i) {
			const float a0 = glm::two_pi<float>() * (i)     / SEG;
			const float a1 = glm::two_pi<float>() * (i + 1) / SEG;
			ring.push_back(P(cosf(a0), sinf(a0), 0));
			ring.push_back(P(cosf(a1), sinf(a1), 0));
		}
		ringCount = static_cast<uint32_t>(ring.size());
		uploadVB(device, ring, ringVB, ringMem);

		// Octahedron edges (unit), as a cheap "sphere" joint marker.
		const BGLModel::Vertex px = P( 1,0,0), nx = P(-1,0,0);
		const BGLModel::Vertex py = P(0, 1,0), ny = P(0,-1,0);
		const BGLModel::Vertex pz = P(0,0, 1), nz = P(0,0,-1);
		std::vector<BGLModel::Vertex> marker = {
			px,py, px,ny, px,pz, px,nz,
			nx,py, nx,ny, nx,pz, nx,nz,
			py,pz, py,nz, ny,pz, ny,nz,
		};
		markerCount = static_cast<uint32_t>(marker.size());
		uploadVB(device, marker, markerVB, markerMem);

		// Unit segment origin -> +X, transformed per bone to connect a joint to its parent.
		std::vector<BGLModel::Vertex> seg = { P(0,0,0), P(1,0,0) };
		segCount = static_cast<uint32_t>(seg.size());
		uploadVB(device, seg, segVB, segMem);
	}

	GizmoRenderSystem::~GizmoRenderSystem()
	{
		auto destroy = [](VkBuffer b, VkDeviceMemory m) {
			if (b != VK_NULL_HANDLE) { vkDestroyBuffer(BGLDevice::device(), b, nullptr); vkFreeMemory(BGLDevice::device(), m, nullptr); }
		};
		destroy(arrowVB, arrowMem);
		destroy(ringVB, ringMem);
		destroy(markerVB, markerMem);
		destroy(segVB, segMem);
	}

	void GizmoRenderSystem::drawMesh(VkCommandBuffer cmd, VkBuffer vb, uint32_t vertCount,
	                                 const glm::mat4& model, float scale, const glm::vec4& color)
	{
		WireframePushConstantData push{};
		push.UsesBufferedTransform = 0;
		push.modelMatrix = model;
		push.scale = glm::vec4{ scale, scale, scale, 1.0f };
		push.color = color;
		vkCmdPushConstants(cmd, pipelineLayout,
			VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
			0, sizeof(WireframePushConstantData), &push);
		VkDeviceSize zero = 0;
		vkCmdBindVertexBuffers(cmd, 0, 1, &vb, &zero);
		vkCmdDraw(cmd, vertCount, 1, 0, 0);
	}

	void GizmoRenderSystem::render(FrameInfo& frameInfo, const PoseGizmo& gizmo)
	{
		if (!gizmo.active()) return;

		bglPipeline->bind(frameInfo.commandBuffer);
		vkCmdBindDescriptorSets(frameInfo.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
			pipelineLayout, 0, 1, &frameInfo.globalDescriptorSets, 0, nullptr);

		const glm::vec3 camPos = frameInfo.camera.getPosition();

		// Per-axis world rotations: map the arrow's local +X (and the ring's local +Z normal) onto
		// each world axis. Index 0/1/2 = X/Y/Z.
		const glm::mat4 I{ 1.0f };
		const glm::mat4 arrowRot[3] = {
			I,
			glm::rotate(I, glm::half_pi<float>(),  glm::vec3(0,0,1)), // +X -> +Y
			glm::rotate(I, -glm::half_pi<float>(), glm::vec3(0,1,0)), // +X -> +Z
		};
		const glm::mat4 ringRot[3] = {
			glm::rotate(I, glm::half_pi<float>(),  glm::vec3(0,1,0)), // normal +Z -> +X
			glm::rotate(I, -glm::half_pi<float>(), glm::vec3(1,0,0)), // normal +Z -> +Y
			I,                                                        // normal +Z (Z axis)
		};
		const glm::vec4 axisColor[3] = {
			{1.0f, 0.15f, 0.15f, 1.0f}, {0.15f, 1.0f, 0.15f, 1.0f}, {0.3f, 0.4f, 1.0f, 1.0f}
		};
		const glm::vec4 highlight{ 1.0f, 1.0f, 0.1f, 1.0f };

		const auto& jp = gizmo.jointWorldPositions();
		const int sel = gizmo.selectedJoint();

		// Bone lines: connect each joint to its parent so the skeleton hierarchy is visible.
		// The unit X segment is mapped so (0,0,0)->parent and (1,0,0)->joint; y/z columns never
		// contribute (segment verts have y=z=0), so a length-only first column suffices.
		const glm::vec4 boneColor{ 0.45f, 0.8f, 0.95f, 1.0f };
		const auto& parents = gizmo.jointParents();
		for (size_t j = 0; j < jp.size(); ++j) {
			const int p = (j < parents.size()) ? parents[j] : -1;
			if (p < 0 || p >= static_cast<int>(jp.size())) continue;
			glm::mat4 m{ 1.0f };
			m[0] = glm::vec4(jp[j] - jp[p], 0.0f);
			m[3] = glm::vec4(jp[p], 1.0f);
			drawMesh(frameInfo.commandBuffer, segVB, segCount, m, 1.0f, boneColor);
		}

		// Joint markers (all joints). Selected joint marker is brighter.
		for (size_t j = 0; j < jp.size(); ++j) {
			const float r = glm::max(0.04f, glm::length(camPos - jp[j]) * 0.03f);
			const glm::vec4 col = (static_cast<int>(j) == sel)
				? glm::vec4{ 1.0f, 0.6f, 0.0f, 1.0f } : glm::vec4{ 0.5f, 0.5f, 0.55f, 1.0f };
			drawMesh(frameInfo.commandBuffer, markerVB, markerCount,
				glm::translate(I, jp[j]), r, col);
		}

		if (sel < 0) return;

		// Handles at the selected joint.
		const glm::vec3 c = gizmo.selectedJointWorldPos();
		const float s = gizmo.handleScale();
		const glm::mat4 T = glm::translate(I, c);
		const int hov = gizmo.hoveredAxis();
		const int drg = gizmo.draggingAxis();

		// Selected bone's LOCAL axes (its own orientation), R/G/B, shorter than the world handles.
		{
			const glm::mat4 jw = gizmo.selectedJointWorldMatrix();
			const float axisLen = s * 0.6f;
			for (int i = 0; i < 3; ++i) {
				glm::vec3 dir = glm::vec3(jw[i]);
				if (glm::length(dir) < 1e-6f) continue;
				dir = glm::normalize(dir) * axisLen;
				glm::mat4 m{ 1.0f };
				m[0] = glm::vec4(dir, 0.0f);
				m[3] = glm::vec4(c, 1.0f);
				drawMesh(frameInfo.commandBuffer, segVB, segCount, m, 1.0f, axisColor[i]);
			}
		}

		// In local space the handles are rotated into the bone's frame; identity in global space.
		const glm::mat4 B = gizmo.handleBasis();
		if (gizmo.mode() == GizmoMode::Translate) {
			for (int i = 0; i < 3; ++i) {
				const glm::vec4 col = (i == hov || i == drg) ? highlight : axisColor[i];
				drawMesh(frameInfo.commandBuffer, arrowVB, arrowCount, T * B * arrowRot[i], s, col);
			}
		} else {
			for (int i = 0; i < 3; ++i) {
				const glm::vec4 col = (i == hov || i == drg) ? highlight : axisColor[i];
				drawMesh(frameInfo.commandBuffer, ringVB, ringCount, T * B * ringRot[i], s, col);
			}
		}
	}

} // namespace bagel
