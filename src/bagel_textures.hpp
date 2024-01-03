#pragma once
#include "bagel_engine_device.hpp"
#include "bagel_buffer.hpp"
#include "bagel_ecs_components.hpp"
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

	void populateBufferCopyRegion(std::vector<VkBufferImageCopy>& buffer_copy_regions, ktxTexture* ktx_texture, uint32_t mip_levels);
	
	//Prototype function. Will be deprecated
	bool load_image_from_file(BGLDevice& bglDevice, const char* file, BGLTexture::BGLTextureInfoComponent& texture);

	static constexpr uint32_t UniformBinding = 0;
	static constexpr uint32_t StorageBinding = 1;
	static constexpr uint32_t TextureBinding = 2;

	enum class TextureHandle : uint32_t { Invalid = 0 };
	enum class BufferHandle : uint32_t { Invalid = 0 };

	class TextureComponentBuilder
	{
	public:

		TextureComponentBuilder(
			BGLDevice& _bglDevice,
			BGLDescriptorPool& _globalPool,
			BGLBindlessDescriptorManager& descriptorManager);

		~TextureComponentBuilder();
		void setBuildTarget(TextureComponent* _tC) { targetComponent = _tC; }
		void buildComponent(std::string filePath, VkFormat imageFormat = VK_FORMAT_R8G8B8A8_SRGB);
		void buildComponent(const char* filePath, VkFormat imageFormat = VK_FORMAT_R8G8B8A8_SRGB);

	private:
		void loadKTXImageInStagingBuffer(const char* filePath, VkFormat format);
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

		ktx_size_t   ktxTextureSize = 0;

		VkImageSubresourceRange subresRange{};
		std::vector<VkBufferImageCopy> buffCpyRegions{};
		VkImageCreateInfo imageCreateInfo{};
		VkImageMemoryBarrier imageMemBarrier{};
		VkSamplerCreateInfo samplerCreateInfo{};
		VkImageViewCreateInfo imageViewCreateInfo{};

		BGLBuffer* stagingBuffer = nullptr;
		BGLDevice& bglDevice;
		BGLDescriptorPool& globalPool;
		TextureComponent* targetComponent = nullptr;

		BGLBindlessDescriptorManager& descriptorManager;
	};
}