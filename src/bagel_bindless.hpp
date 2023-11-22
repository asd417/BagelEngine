#pragma once
#include "bagel_engine_device.hpp"
namespace bagel {
	static constexpr uint32_t UniformBinding = 0;
	static constexpr uint32_t StorageBinding = 1;
	static constexpr uint32_t TextureBinding = 2;

	enum class TextureHandle : uint32_t { Invalid = 0 };
	enum class BufferHandle : uint32_t { Invalid = 0 };
	class BindlessParams {
		struct Range {
			uint32_t offset;
			uint32_t size;
			void* data;
		};
	public:
		BindlessParams(BGLDevice& _bglDevice, uint32_t minAlignment);
		~BindlessParams();

		//Delete copy constructors
		BindlessParams(const BindlessParams&) = delete;
		BindlessParams& operator=(const BindlessParams&) = delete;

		template<class TData>
		uint32_t addRange(TData&& data);
		void build(VkDescriptorPool descriptorPool);
		inline VkDescriptorSet getDescriptorSet() { return mDescriptorSet; }
		inline VkDescriptorSetLayout getDescriptorSetLayout() { return mLayout; }

		uint32_t BindlessParams::padSizeToMinAlignment(uint32_t originalSize, uint32_t minAlignment) {
			return (originalSize + minAlignment - 1) & ~(minAlignment - 1);
		}
		void createBindLessDescriptorPool();
		void createBindlessDescriptorSet();
		void allocateDescriptorSet();
		TextureHandle storeTexture(VkImageView imageView, VkSampler sampler);
		BufferHandle storeBuffer(VkBuffer buffer, VkBufferUsageFlagBits usage);

		BGLDevice& bglDevice;

		uint32_t mMinAlignment;
		uint32_t mLastOffset = 0;
		std::vector<Range> mRanges;

		VkDescriptorSetLayout mLayout;
		VkDescriptorSet mDescriptorSet;
		VkDescriptorPool mDescriptorPool;

		//VmaAllocation mAllocation;
		VkBuffer mBuffer;

		std::vector<VkImageView> textures;
		std::vector<VkBuffer> buffers;

	};
}