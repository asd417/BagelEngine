#include "bagel_material.hpp"

#include <cassert>

namespace bagel {

	BGLMaterialManager::BGLMaterialManager(
		BGLDevice& device,
		BGLDescriptorPool& pool,
		BGLBindlessDescriptorManager& descriptorManager)
		: builder(device, pool, descriptorManager)
	{
		// Global material table: a mapped, host-visible SSBO registered once into the
		// bindless storage-buffer array. Vertices index into it via their materialIndex.
		materialBuffer = std::make_unique<BGLBuffer>(
			device,
			sizeof(MaterialGPU),
			MAX_GLOBAL_MATERIALS,
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
		materialBuffer->map();
		descriptorManager.storeMaterialTable(materialBuffer->descriptorInfo());
		// Reserve index 0 as the all-unused material so a vertex with materialIndex 0
		// samples nothing (shader falls back to vertex color / grid), matching the old
		// "texture handle == 0" behaviour.
		registerMaterial(0, 0, 0, 0);
	}

	uint32_t BGLMaterialManager::registerMaterial(uint32_t albedo, uint32_t normal, uint32_t metalRough, uint32_t emission)
	{
		const uint64_t key = (uint64_t(albedo     & 0xFFFF))
		                   | (uint64_t(normal     & 0xFFFF) << 16)
		                   | (uint64_t(metalRough & 0xFFFF) << 32)
		                   | (uint64_t(emission   & 0xFFFF) << 48);
		if (auto it = dedup.find(key); it != dedup.end()) return it->second;

		const uint32_t index = static_cast<uint32_t>(materials.size());
		assert(index < MAX_GLOBAL_MATERIALS && "Exceeded MAX_GLOBAL_MATERIALS");
		MaterialGPU m{ albedo, normal, metalRough, emission };
		materials.push_back(m);
		materialBuffer->writeToBuffer(&m, sizeof(MaterialGPU), index * sizeof(MaterialGPU));
		materialBuffer->flush();
		dedup.emplace(key, index);
		return index;
	}

	uint32_t BGLMaterialManager::registerMaterialSource(const MaterialSource& src)
	{
		const Material mat = loadMaterial(src);
		return registerMaterial(mat.albedoMap, mat.normalMap, mat.metalRoughMap, mat.emissionMap);
	}

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

	Material BGLMaterialManager::loadMaterial(const MaterialSource& src)
	{
		return loadMaterial(
			src.albedo.empty()     ? nullptr : src.albedo.c_str(),
			src.normal.empty()     ? nullptr : src.normal.c_str(),
			src.metalRough.empty() ? nullptr : src.metalRough.c_str(),
			src.emission.empty()   ? nullptr : src.emission.c_str());
	}

} // namespace bagel
