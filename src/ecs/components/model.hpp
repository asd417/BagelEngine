#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <string>
#include <cstdint>
#include <cassert>
#include <utility>

#include <glm/glm.hpp>

#include "model/bagel_model.hpp"
#include "engine/bagel_engine_device.hpp"
#include "model/model_loaders/model_load_settings.hpp"
#include "animation/bagel_animation.hpp" // SkeletonData, Pose, JointTransform (manual posing)

namespace bagel {
	// Per-entity model instance: a light handle to a shared Model (owned by ModelCacheManager)
	// plus this entity's per-instance state. Holds NO GPU resources, so destroying the entity
	// frees nothing GPU-side and can never dangle another instance's buffers — the fix for the
	// old owner/borrower double-free. `model` is resolved from the cache at build/load;
	// loadSettings.source is the serialized identity used to re-resolve it.
	struct ModelComponent {
		// Compat aliases so existing `ModelComponent::Submesh` / `::MAX_SUBMESHES` keep working.
		using Submesh      = Model::Submesh;
		using SubmeshRange = Model::SubmeshRange;
		static constexpr uint32_t MAX_SUBMESHES = Model::MAX_SUBMESHES;
		static constexpr uint32_t MAX_MATERIALS = Model::MAX_MATERIALS;

		// Shared model (buffers + submeshes + bounds + skin block). Owned by the cache; never
		// freed here. Resolved by ModelComponentBuilder::buildComponent (or on map rehydrate).
		Model* model = nullptr;

		// The build recipe — serialized identity of this instance's model. loadSettings.source
		// is the cache key used to (re)resolve `model` on load.
		ModelLoadSettings loadSettings{};

		// Material recipe for GENERATED models (source paths, indexed by Submesh::materialIndex),
		// kept per-entity so a generated model's authored materials survive save/load. Empty for
		// OBJ/GLTF/LDraw, whose materials come back from the asset. materialCount = valid slots.
		bagel::MaterialSource materialSources[MAX_MATERIALS]{};
		uint32_t materialCount = 0;

		// This entity's selected skin row (per-instance) and frustum-cull toggle.
		uint8_t skinIndex   = 0;
		bool    frustumCull = true;

		// Direct access to the shared model data (buffers, submeshes, bounds, skin block).
		Model&       mesh()       { return *model; }
		const Model& mesh() const { return *model; }

		// Switch this entity's skin. Instant (next draw computes a new row base); ignored if
		// out of range. numSkins/skinBase come from the model's "<model>.yaml" sidecar.
		void setSkin(uint32_t i) { if (model && i < model->numSkins) skinIndex = static_cast<uint8_t>(i); }

		// Record a generated model's material source for slot `materialIdx` (= a submesh's
		// materialIndex). Serialized; the loader re-bakes textures from these paths on rehydrate.
		void setMaterialSource(uint32_t materialIdx, const bagel::MaterialSource& src) {
			assert(materialIdx < MAX_MATERIALS);
			materialSources[materialIdx] = src;
			if (materialIdx + 1 > materialCount) materialCount = materialIdx + 1;
		}

		// Submesh ranges + transparency test — forwarded from the shared model.
		SubmeshRange solidSubmeshes()       const { return model->solidSubmeshes(); }
		SubmeshRange transparentSubmeshes() const { return model->transparentSubmeshes(); }
		bool         hasTransparent()       const { return model->hasTransparent(); }
	};

	struct WireframeComponent{
		static constexpr uint32_t MAX_SUBMESHES = 128;
		struct Submesh {
			uint32_t firstIndex   = 0;
			uint32_t indexCount   = 0;
			uint32_t firstVertex  = 0;
			uint32_t vertexCount  = 0;
			uint32_t materialIndex = 0;
			glm::vec3 aabbMin{ 0.0f };
			glm::vec3 aabbMax{ 0.0f };
		};
		ModelLoadSettings loadSettings{};
		Submesh  submeshes[MAX_SUBMESHES]{};
		uint32_t submeshCount = 0;
		glm::vec3 aabbMin{ 0.0f };
		glm::vec3 aabbMax{ 0.0f };
		bool frustumCull = true;

		// Transient GPU handles — never serialized; rebuilt by the model loader on load.
		// Default to VK_NULL_HANDLE so a default-constructed/half-loaded component is
		// safe to destroy (vkDestroyBuffer/vkFreeMemory on null are no-ops).
		VkBuffer vertexBuffer = VK_NULL_HANDLE;
		VkBuffer indexBuffer = VK_NULL_HANDLE;
		VkDeviceMemory vertexMemory = VK_NULL_HANDLE;
		VkDeviceMemory indexMemory = VK_NULL_HANDLE;
		void *mappedVB = nullptr; // host visible vertex buffer will be mapped here
		void *mappedIB = nullptr; // host visible index buffer will be mapped here

		uint32_t indexCount  = 0;
		uint32_t vertexCount = 0;
		glm::vec4 color = {1.0f,1.0f, 1.0f, 1.0f};
	};

	// Runtime skeletal-animation state for a skinned entity (paired with a skinned
	// ModelComponent). The baked layout (paletteBase/jointCount/fps + per-clip frame table)
	// is filled by the model builder at load; clip/time/playing are per-entity playback. The
	// AnimatedGBufferRenderSystem advances `time` and resolves animBaseOffset() to push.
	struct AnimationComponent {
		// Baked layout (shared identity of the model's animation set).
		uint32_t paletteBase = 0; // matrix index of (clip 0, frame 0) in the global palette SSBO
		uint32_t jointCount  = 0;
		float    fps         = 60.0f;
		std::vector<uint32_t> clipFrameBase{};  // per clip: first frame row (in frames)
		std::vector<uint32_t> clipFrameCount{}; // per clip: baked frame count
		std::vector<std::string> clipNames{};   // per clip: glTF animation name (parallel to clipFrameBase)

		// Playback state.
		uint32_t clip    = 0;
		float    time    = 0.0f;
		bool     playing = true;
		bool     loop    = true;

		// ---- Manual posing -----------------------------------------------------------------
		// When manualPose is set, draws read from a dedicated dynamic palette region
		// (dynamicPaletteBase) that the engine fills each frame by resolving `editPose`,
		// instead of from a baked clip frame. The skeleton (kept from load) is needed to
		// resolve a local pose into skinning matrices; editPose is the per-joint TRS the
		// gizmo authors. poseDirty gates the (re-)resolve+upload so we only do it on change.
		// The AUTHORED fields here — manualPose, editPose, ikSetups — are serialized with the
		// map (see bagel_ecs_serialize.hpp) and re-applied after the model builder rebuilds the
		// rest (skeleton / palette / dynamicPaletteBase) on rehydrate. The rest is transient.
		SkeletonData skeleton{};             // restPose / parents / inverseBind, retained for runtime resolve
		Pose         editPose{};             // per-joint local TRS being authored
		bool         manualPose = false;     // route draws to the dynamic region when true
		uint32_t     dynamicPaletteBase = 0; // base of the reserved jointCount-matrix scratch region
		bool         poseDirty   = true;     // re-resolve + re-upload editPose when set
		std::vector<IKSetup> ikSetups{};     // per-armature IK chains, applied on top of editPose

		// Model-space joint matrices for THIS frame (joint local -> model space), resolved from the
		// current pose by the engine BEFORE the hierarchy pass ("resolve bones before parents") so
		// attachment-parented children read up-to-date bone transforms. Transient (never serialized).
		std::vector<glm::mat4> currentGlobals{};

		uint32_t clipCount() const { return static_cast<uint32_t>(clipFrameBase.size()); }
		const char* clipName(uint32_t c) const {
			return (c < clipNames.size() && !clipNames[c].empty()) ? clipNames[c].c_str() : "(unnamed)";
		}
		float    clipDuration(uint32_t c) const {
			return (c < clipFrameCount.size() && fps > 0.0f && clipFrameCount[c] > 0)
				? static_cast<float>(clipFrameCount[c] - 1) / fps : 0.0f;
		}
		// Palette row base for the current clip/time — pushed to the shader as animBaseOffset.
		// Snaps to the nearest baked frame (clamped to the clip's last frame).
		uint32_t animBaseOffset() const {
			if (manualPose) return dynamicPaletteBase; // hand-posed: read the dynamic region
			if (clip >= clipFrameBase.size() || jointCount == 0) return paletteBase;
			const uint32_t frames = clipFrameCount[clip];
			uint32_t frame = (fps > 0.0f) ? static_cast<uint32_t>(time * fps) : 0;
			if (frames > 0 && frame >= frames) frame = frames - 1;
			return paletteBase + (clipFrameBase[clip] + frame) * jointCount;
		}
	};

	// Named bone attach points baked from the model's "<model>.yaml" sidecar — the Source-engine
	// $attachment analog. Each point is a local offset (translation + rotation) within a skeleton
	// bone's space; its world transform is bone_world * localOffset. Transient: the model builder
	// rebuilds it from the sidecar on load/rehydrate (not serialized — the sidecar owns it).
	// Query via lookupAttachment() / getAttachmentWorld() in bagel_hierachy.hpp. A
	// TransformHierachyComponent can name one of these to parent a child to the point.
	struct AttachmentComponent {
		struct Point {
			std::string name;              // referenced by TransformHierachyComponent::attachment / code
			int       joint = -1;          // skeleton joint the point rides (resolved from bone name)
			glm::mat4 localOffset{ 1.0f }; // offset within the bone's local space
		};
		std::vector<Point> points;

		// Attachment name -> index into points (or -1). Mirrors Source's LookupAttachment.
		int lookup(const std::string& name) const {
			for (size_t i = 0; i < points.size(); ++i)
				if (points[i].name == name) return static_cast<int>(i);
			return -1;
		}
	};
}
