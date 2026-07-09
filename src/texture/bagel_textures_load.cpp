#define STB_IMAGE_IMPLEMENTATION
#define STBI_FAILURE_USERMSG
#include "stb_image.h"
#include "bagel_util.hpp"
#include "texture/bagel_textures.hpp"
namespace bagel
{
    
	void populateBufferCopyRegionSTB(std::vector<VkBufferImageCopy>& buffer_copy_regions, uint32_t width, uint32_t height)
	{

		VkBufferImageCopy buffer_copy_region = {};
		buffer_copy_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		buffer_copy_region.imageSubresource.mipLevel = 0;
		buffer_copy_region.imageSubresource.baseArrayLayer = 0;
		buffer_copy_region.imageSubresource.layerCount = 1;
		buffer_copy_region.imageExtent.width = width; //divides the texture width by pow(2,i)
		buffer_copy_region.imageExtent.height = height;
		buffer_copy_region.imageExtent.depth = 1;
		buffer_copy_region.bufferOffset = 0;
		buffer_copy_region.bufferRowLength = 0;
		buffer_copy_regions.push_back(buffer_copy_region);

	}

	void populateBufferCopyRegionKTX(std::vector<VkBufferImageCopy> &buffer_copy_regions, ktxTexture* ktx_texture, uint32_t mip_levels)
	{
		// Setup buffer copy regions for each mip level
		for (uint32_t i = 0; i < mip_levels; i++)
		{
			//std::cout << "Mipmap index: " << i << "\n";
			VkBufferImageCopy buffer_copy_region = {};
			buffer_copy_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			buffer_copy_region.imageSubresource.mipLevel = i;
			buffer_copy_region.imageSubresource.baseArrayLayer = 0;
			buffer_copy_region.imageSubresource.layerCount = 1;
			buffer_copy_region.imageExtent.width = ktx_texture->baseWidth >> i; //divides the texture width by pow(2,i)
			buffer_copy_region.imageExtent.height = ktx_texture->baseHeight >> i;
			buffer_copy_region.imageExtent.depth = 1;
			ktx_size_t        offset;
			KTX_error_code    result = ktxTexture_GetImageOffset(ktx_texture, i, 0, 0, &offset);
			buffer_copy_region.bufferOffset = offset;
			buffer_copy_regions.push_back(buffer_copy_region);

			//std::cout << "	offset: " << offset << " WxH: " << buffer_copy_region.imageExtent.width << "x" << buffer_copy_region.imageExtent.height << "\n";
		}
	}
	uint16_t BGLTextureLoader::loadCombinedMetalRough(const char* roughPath, const char* metalPath)
	{
		std::string name = std::string("__mr_") + roughPath + "_" + metalPath;

		uint16_t storedIndex = descriptorManager.searchTextureName(name.c_str());
		bool designatedIndex = false;
		if (storedIndex != std::numeric_limits<uint32_t>::max())
		{
			if (!descriptorManager.checkMissingTexture(storedIndex)) return storedIndex;
			designatedIndex = true;
		}

		std::string fullRough = util::enginePath(roughPath);
		std::string fullMetal = util::enginePath(metalPath);

		int rw, rh, rc, mw, mh, mc;
		// Force single channel — we only need the gray value from each map
		unsigned char* roughPix = stbi_load(fullRough.c_str(), &rw, &rh, &rc, 1);
		unsigned char* metalPix = stbi_load(fullMetal.c_str(), &mw, &mh, &mc, 1);

		if (!roughPix || !metalPix)
		{
			std::cerr << "[BGLTextureLoader] failed to combine metalrough: "
			          << roughPath << " + " << metalPath << "\n";
			if (roughPix) stbi_image_free(roughPix);
			if (metalPix) stbi_image_free(metalPix);
			return 0;
		}

		// Use the smaller of the two dimensions if they differ
		uint32_t w = static_cast<uint32_t>(std::min(rw, mw));
		uint32_t h = static_cast<uint32_t>(std::min(rh, mh));

		std::vector<uint8_t> combined(w * h * 4);
		for (uint32_t i = 0; i < w * h; i++)
		{
			combined[i*4 + 0] = 255;          // R: AO (not available, default full)
			combined[i*4 + 1] = roughPix[i];  // G: roughness
			combined[i*4 + 2] = metalPix[i];  // B: metallic
			combined[i*4 + 3] = 255;          // A: unused
		}

		stbi_image_free(roughPix);
		stbi_image_free(metalPix);

		loadPixelDataInStagingBuffer(combined.data(), w, h);
		return buildAndStoreFromMemory(name.c_str(), VK_FORMAT_R8G8B8A8_UNORM, designatedIndex, storedIndex);
	}
    void BGLTextureLoader::loadSTBImageInStagingBuffer(const char *filePath, VkFormat format)
    {
        int width_int;
        int height_int;
        int channels;
        // STBI_rgb_alpha: stb expands to 4 channels itself (padding alpha to 255, and splatting
        // grayscale across rgb), so no manual re-pack buffer is needed. `channels` reports the
        // file's original channel count and is not used for the layout. Matches the other stb
        // call sites (bagel_texture_streamer.cpp, model_loaders/gltf.cpp).
        unsigned char *pixels = stbi_load(filePath, &width_int, &height_int, &channels, STBI_rgb_alpha);
        if (!pixels)
        {
            std::cout
                << "Unable to load image: "
                << stbi_failure_reason()
                << "\n";
            return;
        }
        width = width_int;
        height = height_int;
        mipLvl = 1;         // only level 0 is decoded here; the rest is generated on the GPU
        genMipchain = true; // stb source -> generate the mip chain via blits
        imageSize = static_cast<size_t>(width) * height * 4;

        stagingBuffer = new BGLBuffer(
            bglDevice,
            1,
            static_cast<uint32_t>(imageSize),
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        stagingBuffer->map();
        stagingBuffer->writeToBuffer(pixels);
        populateBufferCopyRegionSTB(buffCpyRegions, width, height);

        stbi_image_free(pixels); // stb allocates with malloc; `delete` here was UB
    }

    void BGLTextureLoader::loadKTXImageInStagingBuffer(const char *filePath, VkFormat format)
    {
        ktxTexture *ktx_texture;
        KTX_error_code result;
        result = ktxTexture_CreateFromNamedFile(filePath, KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &ktx_texture);
        std::string errlog = "Error loading texture: " + std::string(filePath);
        assert(ktx_texture != nullptr && errlog.c_str());

        width = ktx_texture->baseWidth;
        height = ktx_texture->baseHeight;
        mipLvl = ktx_texture->numLevels;
        genMipchain = false; // KTX ships its full mip pyramid; upload it as-is, don't generate

        ktx_uint8_t *ktxImageData = ktx_texture->pData;
        imageSize = ktx_texture->dataSize;
        /*for (int i = 0; i < mipLvl; i++) {
            ktx_size_t offset;
            KTX_error_code    result = ktxTexture_GetImageOffset(ktx_texture, i, 0, 0, &offset);
        }*/

        stagingBuffer = new BGLBuffer(
            bglDevice,
            1,
            static_cast<uint32_t>(imageSize),
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        stagingBuffer->map();
        stagingBuffer->writeToBuffer(ktxImageData);
        populateBufferCopyRegionKTX(buffCpyRegions, ktx_texture, mipLvl);

        // raw data no longer needed
        ktxTexture_Destroy(ktx_texture);
    }
    
	void BGLTextureLoader::loadPixelDataInStagingBuffer(const uint8_t* rgba, uint16_t w, uint16_t h, uint8_t bpp)
	{
		width     = w;
		height    = h;
		mipLvl    = 1;          // only level 0 is provided; the rest is generated on the GPU
		genMipchain = true;     // pre-decoded source -> generate the mip chain via blits
		imageSize = static_cast<size_t>(w) * h * bpp;

		stagingBuffer = new BGLBuffer(
			bglDevice, 1, static_cast<uint32_t>(imageSize),
			VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
		stagingBuffer->map();
		stagingBuffer->writeToBuffer(const_cast<uint8_t*>(rgba));
		populateBufferCopyRegionSTB(buffCpyRegions, width, height);
	}
}