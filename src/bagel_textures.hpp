#pragma once
#include "bagel_engine_device.hpp"
#include "bagel_buffer.hpp"
#include "bagel_descriptors.hpp"


#include <ktx.h>
#include <memory>

namespace bagel {
	class BGLTexture
	{
	public:
		struct BGLTextureInfoComponent {
			VkSampler      sampler;
			VkImage        image;
			VkImageLayout  image_layout;
			VkDeviceMemory device_memory;
			VkImageView    view;
			uint32_t       width, height;
			uint32_t       mip_levels;

			BGLTextureInfoComponent() = default;
			BGLTextureInfoComponent(const BGLTextureInfoComponent&) = delete;
		};
		BGLTexture(BGLDevice &device, VkFormat format);
		~BGLTexture();
		BGLTexture(const BGLTexture&) = delete;

		static std::unique_ptr<BGLTexture> createTextureFromFile(BGLDevice& device, const std::string& filepath, VkFormat imageFormat);
		VkDescriptorImageInfo getDescriptorImageInfo() const { return { info.sampler , info.view , info.image_layout }; }
	private:
		std::unique_ptr<BGLBuffer> stagingBuffer;
		BGLTextureInfoComponent info{};
		BGLDevice& bglDevice;
		VkFormat imageFormat;
		std::vector<VkBufferImageCopy> buffer_copy_regions{};

	};

	inline void populateBufferCopyRegionKTX(std::vector<VkBufferImageCopy>& buffer_copy_regions, ktxTexture* ktx_texture, uint32_t mip_levels);
	
	static constexpr uint32_t UniformBinding = 0;
	static constexpr uint32_t StorageBinding = 1;
	static constexpr uint32_t TextureBinding = 2;

	enum class TextureHandle : uint32_t { Invalid = 0 };
	enum class BufferHandle : uint32_t { Invalid = 0 };

	class BGLTextureLoader
	{
	public:

		BGLTextureLoader(
			BGLDevice& _bglDevice,
			BGLDescriptorPool& _globalPool,
			BGLBindlessDescriptorManager& descriptorManager);

		~BGLTextureLoader();

		// Load a texture file and return its bindless handle.
		// Deduplication: same path returns the same handle.
		uint32_t loadTexture(const char* filePath, VkFormat imageFormat = VK_FORMAT_R8G8B8A8_SRGB);

		// Upload pre-decoded RGBA pixels and return a bindless handle.
		// 'name' is the deduplication key (use a unique synthetic string for generated textures).
		uint32_t loadTextureFromMemory(const char* name, const uint8_t* rgba, uint32_t w, uint32_t h,
		                               VkFormat imageFormat = VK_FORMAT_R8G8B8A8_UNORM);

		// Combine separate grayscale roughness and metallic maps into a single ORM texture
		// (R=255, G=roughness, B=metallic) and upload it. Both paths are engine-relative.
		uint32_t loadCombinedMetalRough(const char* roughPath, const char* metalPath);

	private:
		uint32_t buildAndStore(const char* filePath, VkFormat imageFormat);
		uint32_t buildAndStoreFromMemory(const char* name, VkFormat imageFormat, bool designatedIndex, uint32_t storedIndex);
		void loadSTBImageInStagingBuffer(const char* filePath, VkFormat format);
		void loadKTXImageInStagingBuffer(const char* filePath, VkFormat format);
		void loadPixelDataInStagingBuffer(const uint8_t* rgba, uint32_t w, uint32_t h);
		void generateImageCreateInfo(VkFormat imageFormat);
		void generateSubresRange();
		void setImageLayoutTransfer(VkImage image);
		void setImageLayoutShaderRead();
		void generateSamplerCreateInfo();
		void generateImageViewCreateInfo(VkFormat imageFormat, VkImage image);
		//BINDLESS

		uint32_t width = 0;
		uint32_t height = 0;
		uint32_t mipLvl = 0;

		std::string lastBoundTextureName = "";

		size_t imageSize = 0;

		VkImageSubresourceRange subresRange{};
		std::vector<VkBufferImageCopy> buffCpyRegions{};
		VkImageCreateInfo imageCreateInfo{};
		VkImageMemoryBarrier imageMemBarrier{};
		VkSamplerCreateInfo samplerCreateInfo{};
		VkImageViewCreateInfo imageViewCreateInfo{};

		BGLBuffer* stagingBuffer = nullptr;
		BGLDevice& bglDevice;
		BGLDescriptorPool& globalPool;
		BGLBindlessDescriptorManager& descriptorManager;
	};
}