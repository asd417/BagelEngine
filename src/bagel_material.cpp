#include "bagel_material.hpp"

#include <cassert>

namespace bagel {

	BGLMaterialManager::BGLMaterialManager(
		BGLDevice& device,
		BGLDescriptorPool& pool,
		BGLBindlessDescriptorManager& descriptorManager)
		: builder(device, pool, descriptorManager)
	{
		// Skin table: a mapped, host-visible SSBO registered once into the bindless
		// storage-buffer array. Models reserve blocks of entries via allocateSkinBlock.
		skinTableBuffer = std::make_unique<BGLBuffer>(
			device,
			sizeof(MaterialGPU),
			MAX_SKIN_ENTRIES,
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
		skinTableBuffer->map();
		descriptorManager.storeMaterialTable(skinTableBuffer->descriptorInfo());
		// Entry 0 is the all-unused material (a draw with materialRowBase==0 + slot 0 samples
		// nothing -> shader falls back to vertex color / grid).
		writeSkinEntry(0, 0, 0, 0, 0);
	}

	uint32_t BGLMaterialManager::allocateSkinBlock(uint32_t entryCount)
	{
		if (entryCount == 0) entryCount = 1;
		const uint32_t base = cursor;
		assert(base + entryCount <= MAX_SKIN_ENTRIES && "Exceeded MAX_SKIN_ENTRIES");
		// Zero-initialize the block so any slot the caller doesn't fill is the unused material.
		for (uint32_t i = 0; i < entryCount; ++i) writeSkinEntry(base + i, 0, 0, 0, 0);
		cursor += entryCount;
		return base;
	}

	void BGLMaterialManager::writeSkinEntry(uint32_t entry, uint32_t albedo, uint32_t normal, uint32_t metalRough, uint32_t emission)
	{
		assert(entry < MAX_SKIN_ENTRIES && "skin entry out of range");
		MaterialGPU m{ albedo, normal, metalRough, emission };
		skinTableBuffer->writeToBuffer(&m, sizeof(MaterialGPU), entry * sizeof(MaterialGPU));
		skinTableBuffer->flush();
	}

	uint32_t BGLMaterialManager::loadTexture(const char* path, VkFormat format)
	{
		return builder.loadTexture(path, format);
	}

	BGLModel::Material BGLMaterialManager::loadMaterial(
		const char* albedo,
		const char* normal,
		const char* metalRough,
		const char* emission)
	{
		BGLModel::Material mat{};
		if (albedo)      mat.albedoMap     = static_cast<uint16_t>(builder.loadTexture(albedo,      VK_FORMAT_R8G8B8A8_SRGB));
		if (normal)      mat.normalMap     = static_cast<uint16_t>(builder.loadTexture(normal,      VK_FORMAT_R8G8B8A8_UNORM));
		if (metalRough)  mat.metalRoughMap = static_cast<uint16_t>(builder.loadTexture(metalRough,  VK_FORMAT_R8G8B8A8_UNORM));
		if (emission)    mat.emissionMap   = static_cast<uint16_t>(builder.loadTexture(emission,    VK_FORMAT_R8G8B8A8_SRGB));
		return mat;
	}

	BGLModel::Material BGLMaterialManager::loadMaterial(const MaterialSource& src)
	{
		return loadMaterial(
			src.albedo.empty()     ? nullptr : src.albedo.c_str(),
			src.normal.empty()     ? nullptr : src.normal.c_str(),
			src.metalRough.empty() ? nullptr : src.metalRough.c_str(),
			src.emission.empty()   ? nullptr : src.emission.c_str());
	}

} // namespace bagel
