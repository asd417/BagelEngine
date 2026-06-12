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
			ownsBuffers = false;
		}
		void stealFrom(ModelComponent& o) noexcept {
			loadSettings = std::move(o.loadSettings);
			for (uint32_t i = 0; i < MAX_SUBMESHES; ++i) submeshes[i] = o.submeshes[i];
			for (uint32_t i = 0; i < MAX_MATERIALS; ++i) materialSources[i] = std::move(o.materialSources[i]);
			materialCount = o.materialCount;
			submeshCount = o.submeshCount; solidSubmeshCount = o.solidSubmeshCount;
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
}
