#include "gizmo_render_system.hpp"
#include "wireframe_render_system.hpp"   // WireframePushConstantData (shared layout)
#include "../bagel_engine_device.hpp"
#include "../bagel_model.hpp"            // BGLModel::Vertex
#include "../components/transform.hpp"   // TransformComponent
#include "../lego/lego_connection_component.hpp"

#include <vulkan/vulkan.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
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

		// Pointy "bone" (Blender-style octahedral): head at the origin, square cross-section just
		// past it, and a single tail tip at +Y=1. Drawn per joint oriented to the joint's frame so
		// the tip implies the bone's local +Y direction.
		const float rr = 0.12f, yy = 0.18f;
		const BGLModel::Vertex H  = P(0, 0, 0), Tt = P(0, 1, 0);
		const BGLModel::Vertex Ba = P( rr, yy, 0), Bb = P(0, yy,  rr), Bc = P(-rr, yy, 0), Bd = P(0, yy, -rr);
		std::vector<BGLModel::Vertex> marker = {
			H,Ba,  H,Bb,  H,Bc,  H,Bd,       // head -> cross-section
			Ba,Bb, Bb,Bc, Bc,Bd, Bd,Ba,      // cross-section ring
			Ba,Tt, Bb,Tt, Bc,Tt, Bd,Tt,      // cross-section -> pointy tail (+Y)
		};
		markerCount = static_cast<uint32_t>(marker.size());
		uploadVB(device, marker, markerVB, markerMem);

		// Symmetric "diamond" (octahedron centered on the joint, apexes at +/-X/Y/Z) for
		// DISCONNECTED bones — joints with no parent in the skin (e.g. Blender control bones and
		// the auto-added neutral_bone). Its centered, tail-less silhouette reads differently from
		// the pointy connected-bone marker, so bones stacked at the same spot stay distinguishable.
		{
			const BGLModel::Vertex Xp = P(1,0,0), Xn = P(-1,0,0);
			const BGLModel::Vertex Yp = P(0,1,0), Yn = P(0,-1,0);
			const BGLModel::Vertex Zp = P(0,0,1), Zn = P(0,0,-1);
			std::vector<BGLModel::Vertex> diamond = {
				Xp,Zp, Zp,Xn, Xn,Zn, Zn,Xp,   // equator ring (XZ plane)
				Yp,Xp, Yp,Zp, Yp,Xn, Yp,Zn,   // top apex -> equator
				Yn,Xp, Yn,Zp, Yn,Xn, Yn,Zn,   // bottom apex -> equator
			};
			diamondCount = static_cast<uint32_t>(diamond.size());
			uploadVB(device, diamond, diamondVB, diamondMem);
		}

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
		destroy(diamondVB, diamondMem);
		destroy(segVB, segMem);
	}

	void GizmoRenderSystem::drawMesh(VkCommandBuffer cmd, VkBuffer vb, uint32_t vertCount,
	                                 const glm::mat4& model, float scale, const glm::vec4& color)
	{
		// The wireframe shader no longer takes a separate push.scale — it applies the model matrix
		// directly. Fold the uniform scale into the matrix's basis columns (== model * scale(s)),
		// which reproduces the old per-column scaling exactly.
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

	void GizmoRenderSystem::renderConnections(FrameInfo& frameInfo)
	{
		auto view = frameInfo.registry.view<LegoConnectionComponent, TransformComponent>();
		if (view.begin() == view.end()) return;

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

		for (auto entity : view) {
			const auto& cc = view.get<LegoConnectionComponent>(entity);
			const glm::mat4 world = view.get<TransformComponent>(entity).computeMat4();

			for (const LegoConnectionPoint& p : cc.points) {
				// Orthonormal frame from the connector basis: axis = local +Y, u/v span the
				// perpendicular plane. Built in model-local space, then `world` maps it exactly
				// like the mesh (same scale/rotation), so markers track the part.
				glm::vec3 axis = glm::normalize(p.orient[1]);
				glm::vec3 u = p.orient[0] - axis * glm::dot(p.orient[0], axis);
				if (glm::length(u) < 1e-5f) u = glm::vec3(1, 0, 0);
				u = glm::normalize(u);
				glm::vec3 v = glm::cross(axis, u);

				const glm::vec4 col = typeColor[(p.type >= 0 && p.type < 4) ? p.type : 0];

				// Ring in the u/v plane (perpendicular to the axis), radius = p.radius.
				glm::mat4 ringLocal{ 1.0f };
				ringLocal[0] = glm::vec4(u * p.radius, 0.0f);
				ringLocal[1] = glm::vec4(v * p.radius, 0.0f);
				ringLocal[2] = glm::vec4(axis, 0.0f);
				ringLocal[3] = glm::vec4(p.pos, 1.0f);
				drawMesh(frameInfo.commandBuffer, ringVB, ringCount, world * ringLocal, 1.0f, col);

				// Short line from the point along +axis so the opening direction is visible.
				glm::mat4 segLocal{ 1.0f };
				segLocal[0] = glm::vec4(axis * (p.radius * 2.5f), 0.0f);
				segLocal[3] = glm::vec4(p.pos, 1.0f);
				drawMesh(frameInfo.commandBuffer, segVB, segCount, world * segLocal, 1.0f, col);
			}
		}
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
		const auto& selSet = gizmo.selectedJoints();
		auto inSelection = [&](int j) { return std::find(selSet.begin(), selSet.end(), j) != selSet.end(); };

		// Bone lines: connect each joint to its parent so the skeleton hierarchy is visible.
		// The unit X segment is mapped so (0,0,0)->parent and (1,0,0)->joint; y/z columns never
		// contribute (segment verts have y=z=0), so a length-only first column suffices.
		const glm::vec4 boneColor{ 0.0f, 0.0f, 0.0f, 1.0f };
		const auto& parents = gizmo.jointParents();
		for (size_t j = 0; j < jp.size(); ++j) {
			const int p = (j < parents.size()) ? parents[j] : -1;
			if (p < 0 || p >= static_cast<int>(jp.size())) continue;
			glm::mat4 m{ 1.0f };
			m[0] = glm::vec4(jp[j] - jp[p], 0.0f);
			m[3] = glm::vec4(jp[p], 1.0f);
			drawMesh(frameInfo.commandBuffer, segVB, segCount, m, 1.0f, boneColor);
		}

		// Joint bones, oriented to the joint's frame. CHAIN joints — those with a parent OR a child —
		// draw the pointy octahedron whose tip points along local +Y and reaches toward the first
		// child. Only ISOLATED joints (no parent AND no child in the skin — free control bones,
		// neutral_bone, etc.) draw the symmetric diamond at a screen-relative size, so bones stacked
		// at one spot read apart from the chain. Selected joint is brighter.
		const auto& jwm = gizmo.jointWorldMatrices();
		for (size_t j = 0; j < jp.size(); ++j) {
			const bool hasParent = (j < parents.size()) && parents[j] >= 0;

			// Reach toward the first child if any; finding one also marks this as a chain bone.
			bool hasChild = false;
			float boneLen = glm::max(0.04f, glm::length(camPos - jp[j]) * 0.06f);
			for (size_t c = 0; c < parents.size(); ++c)
				if (parents[c] == static_cast<int>(j)) { boneLen = glm::length(jp[c] - jp[j]); hasChild = true; break; }

			// Isolated bone: neither end is wired into the hierarchy. Tail-less marker at a small
			// constant screen-size instead of a child-reaching length.
			const bool disconnected = !hasParent && !hasChild;
			if (disconnected)
				boneLen = glm::max(0.03f, glm::length(camPos - jp[j]) * 0.035f);

			glm::mat4 m{ 1.0f };
			if (j < jwm.size()) {
				const glm::vec3 x = glm::vec3(jwm[j][0]), y = glm::vec3(jwm[j][1]), z = glm::vec3(jwm[j][2]);
				const float lx = glm::length(x), ly = glm::length(y), lz = glm::length(z);
				if (lx > 1e-6f && ly > 1e-6f && lz > 1e-6f) {
					m[0] = glm::vec4(x / lx, 0.0f);
					m[1] = glm::vec4(y / ly, 0.0f); // local +Y = the pointy direction
					m[2] = glm::vec4(z / lz, 0.0f);
				}
			}
			m[3] = glm::vec4(jp[j], 1.0f);
			// Distinct base tint for disconnected bones so the shape difference is reinforced.
			const glm::vec4 baseCol = disconnected ? glm::vec4{ 0.35f, 0.7f, 0.9f, 1.0f }
			                                       : glm::vec4{ 0.5f, 0.5f, 0.55f, 1.0f };
			// Active joint = bright orange; other multi-selected joints = yellow; rest = base tint.
			glm::vec4 col = baseCol;
			if (static_cast<int>(j) == sel)         col = glm::vec4{ 1.0f, 0.6f, 0.0f, 1.0f };
			else if (inSelection(static_cast<int>(j))) col = glm::vec4{ 1.0f, 0.85f, 0.2f, 1.0f };
			if (disconnected)
				drawMesh(frameInfo.commandBuffer, diamondVB, diamondCount, m, boneLen, col);
			else
				drawMesh(frameInfo.commandBuffer, markerVB, markerCount, m, boneLen, col);
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
