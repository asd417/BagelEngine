#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <string>
#include <cstdint>
#include <cassert>
#include <utility>

#include <glm/glm.hpp>

#include "bagel_engine_device.hpp"
#include "model_loaders/model_load_settings.hpp"
#include "animation/bagel_animation.hpp" // SkeletonData, Pose, JointTransform (manual posing)

namespace bagel {
	// PBR material — bindless texture handles for each map slot.
	// Used per-submesh inside ModelComponent. Single-mesh entities use materials[0].
	struct Material {
		uint32_t albedoMap   = 0;
		uint32_t normalMap   = 0;
		uint32_t metalRoughMap = 0;
		uint32_t emissionMap   = 0;
		float emissionLux = 800.0f;
	};

	// Serializable texture-source paths for a material. The bindless handles in
	// Material are transient (rebuilt per run), so for GENERATED models — whose
	// materials are assigned in code rather than loaded from an asset file — we
	// persist the source paths instead and re-load them on map rehydrate. Empty
	// strings mean "no map for that slot". OBJ/GLTF models leave this unused; their
	// materials come back from the asset via the loader.
	struct MaterialSource {
		std::string albedo;
		std::string normal;
		std::string metalRough;
		std::string emission;
	};

	struct ModelComponent {
		static constexpr uint32_t MAX_SUBMESHES = 128;
		// A model has at most this many distinct materials, shared across its submeshes
		// (each Submesh::materialIndex selects one). materialSources[] is sized to this,
		// NOT to the submesh count.
		static constexpr uint32_t MAX_MATERIALS = 8;

		struct Submesh {
			uint32_t firstIndex   = 0;
			uint32_t indexCount   = 0;
			uint32_t firstVertex  = 0;
			uint32_t vertexCount  = 0;
			uint32_t materialIndex = 0;
			glm::vec3 aabbMin{ 0.0f };
			glm::vec3 aabbMax{ 0.0f };
		};

		// Lightweight contiguous view over a [begin,end) run of submeshes (C++17, no <span>).
		struct SubmeshRange {
			const Submesh* b;
			const Submesh* e;
			const Submesh* begin() const { return b; }
			const Submesh* end()   const { return e; }
			uint32_t size()  const { return static_cast<uint32_t>(e - b); }
			bool     empty() const { return b == e; }
		};

		// The model's build recipe (source path + scale/build options). This is the
		// authored, serialized identity of the model; everything below it is rebuilt
		// from this by the model loader. loadSettings.source replaces the old modelName.
		ModelLoadSettings loadSettings{};
		Submesh  submeshes[MAX_SUBMESHES]{};
		// Material recipe for GENERATED models, indexed by Submesh::materialIndex. The
		// ModelComponent stores the image SOURCE PATHS, not the loaded bindless handles —
		// those are transient and live in the vertex buffer, re-baked by the loader on
		// rehydrate. This array exists purely so a generated model's materials survive
		// save/load. Empty/unused for OBJ/GLTF, whose materials come back from the asset.
		// materialCount = number of valid leading slots. See MaterialSource.
		bagel::MaterialSource materialSources[MAX_MATERIALS]{};
		uint32_t materialCount = 0;
		uint32_t submeshCount = 0;
		// Submeshes are stored solid-first: [0, solidSubmeshCount) are solid/opaque and
		// [solidSubmeshCount, submeshCount) are transparent. The loaders emit them in this
		// order, so a single split index is enough to filter by transparency at draw time.
		uint32_t solidSubmeshCount = 0;

		// Skin table block (transient; rebuilt by the loader). The material for a draw is
		//   skinTable[ skinBase + skinIndex*numSlots + vertex.materialIndex(=local slot) ].
		// skinBase/numSlots/numSkins describe the model's block (shared by deduped instances);
		// skinIndex is this entity's selected skin (per-entity, serialized). Packed small:
		// skinBase fits the table cap (<=65535), numSlots fits the vertex's uint16 slot, and
		// 256 skins is plenty.
		uint16_t skinBase   = 0;
		uint16_t numSlots   = 1;
		uint8_t  numSkins   = 1;
		uint8_t  skinIndex  = 0;

		// Skeletal skinning (bone animation). isSkinned models are drawn by the dedicated
		// SkinnedGBufferRenderSystem (per-entity, not buffered) and skipped by the static
		// G-buffer/transparent passes. skinVertexBase is this model's base offset into the
		// global per-vertex skin-influence SSBO; the shader reads v[skinVertexBase + gl_VertexIndex].
		// The matching skeleton/clip state lives on a separate AnimationComponent.
		bool     isSkinned      = false;
		uint32_t skinVertexBase = 0;

		// Switch this entity's skin. Instant (the next draw computes a new row base); ignored
		// if out of range. numSkins/skinBase come from the model's "<model>.yaml" sidecar.
		void setSkin(uint32_t i) { if (i < numSkins) skinIndex = static_cast<uint8_t>(i); }

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

		// True if this component must destroy the Vk buffers above. Deduplicated models
		// share one set of buffers — the original owns them (ownsBuffers=true), copies
		// borrow (false). Replaces the old `origin` self-pointer, which broke whenever
		// entt relocated the component (the `this`-identity comparison became stale).
		bool ownsBuffers = true;

		ModelComponent() = default;
		// Move-only. entt relocates components (swap-and-pop on erase/clear) by moving;
		// the move transfers buffer ownership and nulls the source so it frees nothing.
		// Copying is forbidden: a shallow copy would duplicate ownership of the same Vk
		// handles and double-free them on destruction (the original crash).
		ModelComponent(const ModelComponent&) = delete;
		ModelComponent& operator=(const ModelComponent&) = delete;
		ModelComponent(ModelComponent&& o) noexcept { stealFrom(o); }
		ModelComponent& operator=(ModelComponent&& o) noexcept {
			if (this != &o) { destroyBuffers(); stealFrom(o); }
			return *this;
		}
		~ModelComponent() { destroyBuffers(); }

	private:
		void destroyBuffers() noexcept {
			if (ownsBuffers) {
				vkDestroyBuffer(BGLDevice::device(), vertexBuffer, nullptr);
				vkDestroyBuffer(BGLDevice::device(), indexBuffer, nullptr);
				vkFreeMemory(BGLDevice::device(), vertexMemory, nullptr);
				vkFreeMemory(BGLDevice::device(), indexMemory, nullptr);
			}
			vertexBuffer = indexBuffer = VK_NULL_HANDLE;
			vertexMemory = indexMemory = VK_NULL_HANDLE;
			mappedVB = mappedIB = nullptr;
			ownsBuffers = false;
		}
		void stealFrom(ModelComponent& o) noexcept {
			loadSettings = std::move(o.loadSettings);
			for (uint32_t i = 0; i < MAX_SUBMESHES; ++i) submeshes[i] = o.submeshes[i];
			for (uint32_t i = 0; i < MAX_MATERIALS; ++i) materialSources[i] = std::move(o.materialSources[i]);
			materialCount = o.materialCount;
			submeshCount = o.submeshCount; solidSubmeshCount = o.solidSubmeshCount;
			skinBase = o.skinBase; numSlots = o.numSlots; numSkins = o.numSkins; skinIndex = o.skinIndex;
			isSkinned = o.isSkinned; skinVertexBase = o.skinVertexBase;
			aabbMin = o.aabbMin; aabbMax = o.aabbMax; frustumCull = o.frustumCull;
			vertexBuffer = o.vertexBuffer; indexBuffer = o.indexBuffer;
			vertexMemory = o.vertexMemory; indexMemory = o.indexMemory;
			indexCount = o.indexCount; vertexCount = o.vertexCount;
			ownsBuffers = o.ownsBuffers;
			o.vertexBuffer = o.indexBuffer = VK_NULL_HANDLE;
			o.vertexMemory = o.indexMemory = VK_NULL_HANDLE;
			o.ownsBuffers = false;
		}
	public:

		// Record a generated model's material source for slot `materialIdx` (= a submesh's
		// materialIndex). This is what gets serialized; the actual textures are loaded from
		// these paths and baked into the vertex buffer by the loader on rehydrate.
		void setMaterialSource(uint32_t materialIdx, const bagel::MaterialSource& src) {
			assert(materialIdx < MAX_MATERIALS);
			materialSources[materialIdx] = src;
			if (materialIdx + 1 > materialCount) materialCount = materialIdx + 1;
		}

		// Solid/opaque submeshes — drawn in the G-buffer (deferred) pass.
		SubmeshRange solidSubmeshes() const {
			return { submeshes, submeshes + solidSubmeshCount };
		}
		// Transparent submeshes — drawn in the forward alpha-blended pass.
		SubmeshRange transparentSubmeshes() const {
			return { submeshes + solidSubmeshCount, submeshes + submeshCount };
		}
		bool hasTransparent() const { return solidSubmeshCount < submeshCount; }
	};

	struct WireframeComponent : ModelComponent {
		glm::vec4 color = {1.0f,1.0f, 1.0f, 1.0f};
	};
	struct CollisionModelComponent : ModelComponent {
		glm::vec3 collisionScale = { 1.0,1.0,1.0 };
	};

	// Runtime skeletal-animation state for a skinned entity (paired with a skinned
	// ModelComponent). The baked layout (paletteBase/jointCount/fps + per-clip frame table)
	// is filled by the model builder at load; clip/time/playing are per-entity playback. The
	// SkinnedGBufferRenderSystem advances `time` and resolves animBaseOffset() to push.
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
				? (clipFrameCount[c] - 1) / fps : 0.0f;
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
