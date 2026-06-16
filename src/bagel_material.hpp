#pragma once
#include "bagel_textures.hpp"
#include "bagel_buffer.hpp"
#include "bagel_ecs_components.hpp"

#include <memory>
#include <vector>
#include <unordered_map>

namespace bagel {

	// Owns the GPU "skin table": one big SSBO of uvec4 entries (4 bindless texture handles
	// each: x=albedo, y=normal, z=metalRough, w=emission). Each model reserves a contiguous
	// block of numSkins*numSlots entries laid out skin-major, so the material for a draw is
	//   skinTable[ skinBase + skinIndex*numSlots + vertexLocalSlot ].
	// Switching skin is just changing skinIndex (no GPU writes). Textures are still
	// deduplicated by BGLTextureLoader; only the small handle-quads here can repeat.
	class BGLMaterialManager {
	public:
		BGLMaterialManager(
			BGLDevice& device,
			BGLDescriptorPool& pool,
			BGLBindlessDescriptorManager& descriptorManager);

		// Load a single texture and return its bindless handle.
		uint32_t loadTexture(const char* path, VkFormat format = VK_FORMAT_R8G8B8A8_SRGB);

		// Build a Material from paths. Any null path leaves that slot as 0 (no map).
		Material loadMaterial(
			const char* albedo,
			const char* normal      = nullptr,
			const char* metalRough  = nullptr,
			const char* emission    = nullptr);

		// Build a Material from a serializable MaterialSource. Empty strings leave that
		// slot unused.
		Material loadMaterial(const MaterialSource& src);

		// Reserve a contiguous run of `entryCount` skin-table entries; returns the base index.
		// Entries are zero-initialized (the all-unused material). Caller fills them with
		// writeSkinEntry. entryCount is numSkins*numSlots for a model.
		uint32_t allocateSkinBlock(uint32_t entryCount);
		// Write one skin-table entry (4 bindless handles) at absolute index `entry`.
		void writeSkinEntry(uint32_t entry, uint32_t albedo, uint32_t normal, uint32_t metalRough, uint32_t emission);

		// Reset the skin-table allocator so the next scene's blocks start fresh. Call on
		// scene unload (GPU must be idle — the old blocks are about to be overwritten).
		// Entry 0 stays the all-unused material. Textures in the bindless array are NOT freed.
		void clearSkinTable() { cursor = 1; }

		BGLTextureLoader& getTextureLoader() { return builder; }

	private:
		// One entry: 4 bindless texture handles. 16 bytes == std430 uvec4.
		struct MaterialGPU { uint32_t albedo = 0, normal = 0, metalRough = 0, emission = 0; };
		// Must fit uint16: ModelComponent::skinBase indexes this table.
		static constexpr uint32_t MAX_SKIN_ENTRIES = 16384;
		static_assert(MAX_SKIN_ENTRIES <= 0x10000, "skinBase is uint16");

		BGLTextureLoader builder;
		std::unique_ptr<BGLBuffer> skinTableBuffer; // SSBO at binding MATERIAL
		uint32_t cursor = 1;                        // next free entry; 0 reserved as all-unused
	};

} // namespace bagel
