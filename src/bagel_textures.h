#pragma once
#include "bagel_engine_device.hpp"
#include "bagel_buffer.hpp"

#include <ktx.h>
#include <memory>
namespace bagel {
	class BGLTexture
	{
	public:
		struct textureInfoStruct {
			VkSampler      sampler;
			VkImage        image;
			VkImageLayout  image_layout;
			VkDeviceMemory device_memory;
			VkImageView    view;
			uint32_t       width, height;
			uint32_t       mip_levels;

			textureInfoStruct() = default;
			textureInfoStruct(const textureInfoStruct&) = delete;
		};
		BGLTexture(BGLDevice &device, VkFormat format);
		~BGLTexture();
		BGLTexture(const BGLTexture&) = delete;

		static std::unique_ptr<BGLTexture> createTextureFromFile(BGLDevice& device, const std::string& filepath, VkFormat imageFormat);
		VkDescriptorImageInfo getDescriptorImageInfo() const { return { info.sampler , info.view , info.image_layout }; }
	private:
		std::unique_ptr<BGLBuffer> stagingBuffer;
		textureInfoStruct info{};
		BGLDevice& bglDevice;
		VkFormat imageFormat;
		std::vector<VkBufferImageCopy> buffer_copy_regions{};

		//bool load_image_from_file(BGLDevice& bglDevice, const char* file, BGLTexture& texture);
		//bool load_image_from_file2(BGLDevice& bglDevice, const char* file, BGLTexture& texture);
	};

	void populateBufferCopyRegion(std::vector<VkBufferImageCopy>& buffer_copy_regions, ktxTexture* ktx_texture, uint32_t mip_levels);
	bool load_image_from_file(BGLDevice& bglDevice, const char* file, BGLTexture::textureInfoStruct& texture);

}