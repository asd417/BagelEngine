#include "bagel_material.hpp"

namespace bagel {

	BGLMaterialManager::BGLMaterialManager(
		BGLDevice& device,
		BGLDescriptorPool& pool,
		BGLBindlessDescriptorManager& descriptorManager)
		: builder(device, pool, descriptorManager)
	{}

	uint32_t BGLMaterialManager::loadTexture(const char* path, VkFormat format)
	{
		return builder.loadTexture(path, format);
	}

	Material BGLMaterialManager::loadMaterial(
		const char* albedo,
		const char* normal,
		const char* metalRough,
		const char* emission)
	{
		Material mat{};
		if (albedo)      mat.albedoMap     = builder.loadTexture(albedo,      VK_FORMAT_R8G8B8A8_SRGB);
		if (normal)      mat.normalMap     = builder.loadTexture(normal,      VK_FORMAT_R8G8B8A8_UNORM);
		if (metalRough)  mat.metalRoughMap = builder.loadTexture(metalRough,  VK_FORMAT_R8G8B8A8_UNORM);
		if (emission)    mat.emissionMap   = builder.loadTexture(emission,    VK_FORMAT_R8G8B8A8_SRGB);
		return mat;
	}

} // namespace bagel
