#pragma once

#include "bagel_engine_device.hpp"

namespace bagel {
	class BGLBuffer {
	public:
		BGLBuffer(
			BGLDevice& device,
			VkDeviceSize instanceSize,
			uint32_t instanceCount,
			VkBufferUsageFlags usageFlags,
			VkMemoryPropertyFlags memoryPropertyFlags,
			VkDeviceSize minOffsetAlignment = 1
		);
		~BGLBuffer();

		BGLBuffer(const BGLBuffer&) = delete;
		BGLBuffer& operator=(const BGLBuffer&) = delete;

		VkResult map(VkDeviceSize size = VK_WHOLE_SIZE, VkDeviceSize offset = 0);
		void unmap();

		void writeToBuffer(void* data, VkDeviceSize = VK_WHOLE_SIZE, VkDeviceSize offset = 0);
		VkResult flush(VkDeviceSize size = VK_WHOLE_SIZE, VkDeviceSize offset = 0);
		VkDescriptorBufferInfo descriptorInfo(VkDeviceSize size = VK_WHOLE_SIZE, VkDeviceSize offset = 0);
		VkResult invalidate(VkDeviceSize size = VK_WHOLE_SIZE, VkDeviceSize offset = 0);

		void writeToIndex(void* data, int index);
		VkResult flushIndex(int index);
		VkDescriptorBufferInfo descriptorInfoForIndex(int index);
		VkResult invalidateIndex(int index);

		VkBuffer getBuffer() const { return buffer; }
		void* getMappedMemory() const { return mapped; }
		uint32_t getInstanceCount() const { return instanceCount; }
		VkDeviceSize getInstanceSize() const { return instanceSize; }
		VkDeviceSize getAlignmentSize() const { return instanceSize; }
		VkBufferUsageFlags getUsageFlags() const { return usageFlags; }
		VkMemoryPropertyFlags getMemoryPropertyFlags() const { return memoryPropertyFlags; }
		VkDeviceSize getBufferSize() const { return bufferSize; }

	private:
		static VkDeviceSize getAlignment(VkDeviceSize instanceSize, VkDeviceSize minOffsetAlignment);

		BGLDevice& bglDevice;
		void* mapped = nullptr;
		VkBuffer buffer = VK_NULL_HANDLE;
		VkDeviceMemory memory = VK_NULL_HANDLE;

		VkDeviceSize bufferSize;
		uint32_t instanceCount;
		VkDeviceSize instanceSize;
		VkDeviceSize alignmentSize;
		VkBufferUsageFlags usageFlags;
		VkMemoryPropertyFlags memoryPropertyFlags;

	};
}