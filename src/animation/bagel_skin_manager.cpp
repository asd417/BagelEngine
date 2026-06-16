#include "animation/bagel_skin_manager.hpp"

#include <cassert>
#include <cstring>
#include <iostream>

namespace bagel {

	BGLSkinManager::BGLSkinManager(BGLDevice& device, BGLBindlessDescriptorManager& descriptorManager)
		: device{ device }
	{
		std::cout << "Creating Skin Manager\n";

		skinBuffer = std::make_unique<BGLBuffer>(
			device,
			INFLUENCE_STRIDE,
			MAX_SKIN_VERTICES,
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
		skinBuffer->map();
		descriptorManager.storeSkinBuffer(skinBuffer->descriptorInfo());

		paletteBuffer = std::make_unique<BGLBuffer>(
			device,
			sizeof(glm::mat4),
			MAX_PALETTE_MATRICES,
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
		paletteBuffer->map();
		descriptorManager.storePaletteBuffer(paletteBuffer->descriptorInfo());
	}

	uint32_t BGLSkinManager::uploadInfluences(const void* data, uint32_t vertexCount)
	{
		assert(skinCursor + vertexCount <= MAX_SKIN_VERTICES && "skin influence buffer overflow");
		const uint32_t base = skinCursor;
		skinBuffer->writeToBuffer(const_cast<void*>(data),
			static_cast<VkDeviceSize>(vertexCount) * INFLUENCE_STRIDE,
			static_cast<VkDeviceSize>(base) * INFLUENCE_STRIDE);
		skinBuffer->flush();
		skinCursor += vertexCount;
		return base;
	}

	uint32_t BGLSkinManager::uploadPalette(const glm::mat4* data, uint32_t matrixCount)
	{
		assert(paletteCursor + matrixCount <= MAX_PALETTE_MATRICES && "joint palette buffer overflow");
		const uint32_t base = paletteCursor;
		paletteBuffer->writeToBuffer(const_cast<glm::mat4*>(data),
			static_cast<VkDeviceSize>(matrixCount) * sizeof(glm::mat4),
			static_cast<VkDeviceSize>(base) * sizeof(glm::mat4));
		paletteBuffer->flush();
		paletteCursor += matrixCount;
		return base;
	}

} // namespace bagel
