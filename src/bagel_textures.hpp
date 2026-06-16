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
		void loadPixelDataInStagingBuffer(const uint8_t* rgba, uint32_t w, uint32_t h, uint32_t bpp = 4);
		// Bytes per texel for an uncompressed VkFormat (sizes staging uploads for non-RGBA formats).
		static uint32_t bytesPerTexel(VkFormat format);
		void generateImageCreateInfo(VkFormat imageFormat);
		void generateSubresRange();
		void setImageLayoutTransfer(VkImage image);
		void setImageLayoutShaderRead();
		void generateSamplerCreateInfo();
		void generateImageViewCreateInfo(VkFormat imageFormat, VkImage image);

		// --- Mipmapping --------------------------------------------------------------------
		// Full mip pyramid level count for a base size: floor(log2(max(w,h))) + 1.
		static uint32_t computeMipLevels(uint32_t w, uint32_t h);
		// True if `format` supports a linear-filtered blit (required to downsample mips on the
		// GPU with vkCmdBlitImage). Guards the runtime generation path; falls back to 1 mip.
		bool formatSupportsLinearBlit(VkFormat format) const;
		// Generate mips 1..mipLevels-1 from level 0 by successive half-size linear blits.
		// Precondition: level 0 is in TRANSFER_DST_OPTIMAL with its pixels uploaded; all other
		// levels are TRANSFER_DST_OPTIMAL/UNDEFINED. Postcondition: every level is left in
		// SHADER_READ_ONLY_OPTIMAL. Recorded into `cmd`.
		void generateMipmaps(VkCommandBuffer cmd, VkImage image, int32_t w, int32_t h, uint32_t mipLevels);

		// --- Shared sampler ----------------------------------------------------------------
		// All content textures share ONE sampler instead of creating one per texture: it makes
		// the LOD knobs (below) a single source of truth and lets them be retuned live (see
		// setMipLodBias). The image VIEW's levelCount bounds the mip range per texture, so the
		// shared sampler uses VK_LOD_CLAMP_NONE and works for any texture's mip count.
		void createSharedSampler();
		// Recreate the shared sampler with a new mip LOD bias and re-point every content
		// texture's descriptor at it. Negative bias = sharper (mips kick in farther, more
		// shimmer); positive = blurrier (mips kick in closer). Drives "mip distance".
	public:
		void setMipLodBias(float bias);
		float getMipLodBias() const { return mipLodBias; }
	private:

		VkSampler sharedSampler = VK_NULL_HANDLE;
		// LOD controls baked into sharedSampler. Anisotropy is the biggest quality lever for
		// oblique surfaces; mipLodBias is the direct sharpness/shimmer dial.
		float    mipLodBias    = 0.0f;
		// Bindless handles of textures this loader created (content textures), so a sampler
		// retune can rebind exactly those — NOT render targets / shadow maps, which own their
		// own samplers elsewhere.
		std::vector<uint32_t> contentTextureHandles{};

		uint32_t width = 0;
		uint32_t height = 0;
		uint32_t mipLvl = 0;
		// True when the loaded source has ONLY level 0 (stb/PNG/generated) and the rest of the
		// pyramid must be generated on the GPU. False for KTX, which ships all mip levels.
		bool genMipchain = false;

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