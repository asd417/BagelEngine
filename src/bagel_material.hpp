#pragma once
#include "bagel_textures.hpp"
#include "bagel_ecs_components.hpp"

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
			const char* normal    = nullptr,
			const char* roughness = nullptr,
			const char* metallic  = nullptr,
			const char* emission  = nullptr);

	private:
		BGLTextureLoader builder;
	};

} // namespace bagel
