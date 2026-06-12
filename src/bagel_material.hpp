#pragma once
#include "bagel_textures.hpp"
#include "bagel_buffer.hpp"
#include "bagel_ecs_components.hpp"

#include <memory>
#include <vector>
#include <unordered_map>

namespace bagel {

	// Assembles MaterialComponents from texture paths.
	// Deduplication is handled by BGLBindlessDescriptorManager — the same file is only
	// uploaded to the GPU once regardless of how many times loadMaterial references it.
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
		// slot unused. Used to re-create generated-model materials on map rehydrate.
		Material loadMaterial(const MaterialSource& src);

		// Register a material's 4 bindless texture handles into the global GPU material
		// table and return its index. Identical handle sets are deduplicated. Vertices
		// store this index; the vertex shader reads the table (getMaterialTableHandle())
		// to resolve it back into the handles. Index 0 is always the all-unused material.
		uint32_t registerMaterial(uint32_t albedo, uint32_t normal, uint32_t metalRough, uint32_t emission);
		// Load a MaterialSource's textures, then register the resulting handles.
		uint32_t registerMaterialSource(const MaterialSource& src);

		BGLTextureLoader& getTextureLoader() { return builder; }

	private:
		// One entry per material; 4 bindless texture handles. 16 bytes == std430 uvec4,
		// so the shader reads it as a single uvec4 (x=albedo, y=normal, z=metalRough, w=emission).
		struct MaterialGPU { uint32_t albedo = 0, normal = 0, metalRough = 0, emission = 0; };
		static constexpr uint32_t MAX_GLOBAL_MATERIALS = 4096;

		BGLTextureLoader builder;
		std::unique_ptr<BGLBuffer> materialBuffer;        // SSBO mirror of `materials` (binding MATERIAL)
		std::vector<MaterialGPU> materials;               // CPU mirror; index == GPU index
		std::unordered_map<uint64_t, uint32_t> dedup;     // packed 4x16-bit handles -> index
	};

} // namespace bagel
