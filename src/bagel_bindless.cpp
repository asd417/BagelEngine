#include "bagel_bindless.hpp"

// vulkan headers
#include <vulkan/vulkan.h>

#include <array>

#define PER_BINDING_DESCRIPTORS 1000
namespace bagel {
    BindlessParams::BindlessParams(BGLDevice& _bglDevice, uint32_t minAlignment) : bglDevice{ _bglDevice }, mMinAlignment(minAlignment) {}

    BindlessParams::~BindlessParams()
    {
        vkDestroyDescriptorPool(BGLDevice::device(), mDescriptorPool, nullptr);
    }

    template<class TData>
    uint32_t BindlessParams::addRange(TData&& data) {
        // Copy data to heap and store void pointer
        // since we do not care about the type at
        // point
        size_t dataSize = sizeof(TData);
        auto* bytes = new TData;
        *bytes = data;

        // Add range
        uint32_t currentOffset = mLastOffset;
        mRanges.push_back({ currentOffset, dataSize, bytes });

        // Pad the data size to minimum alignment
        // and move the offset
        mLastOffset += padSizeToMinAlignment(dataSize, mMinAlignment);
        return currentOffset;
    }

    void BindlessParams::build(VkDescriptorPool descriptorPool) {

        VkBufferCreateInfo bufferCreateInfo{};
        bufferCreateInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        bufferCreateInfo.size = mLastOffset;
        // Other create flags
        //vmaCreateBuffer(allocator, &bufferCreateInfo, ..., &mBuffer, &mAllocation, ...);

        // Copy ranges to buffer
        uint8_t* data = nullptr;
        //vmaMapMemory(allocator, allocation, &data);
        for (const auto& range : mRanges) {
            memcpy(data + range.offset, range.data, range.size);
            //vmaUnmapMemory(allocator, allocation);
        }
        // Create layout for descriptor set
        VkDescriptorSetLayoutBinding binding{};

        //binding.binding = i;

        binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_ALL;

        VkDescriptorSetLayoutCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        createInfo.bindingCount = 1;
        createInfo.pBindings = &binding;
        vkCreateDescriptorSetLayout(BGLDevice::device(), &createInfo, nullptr, &mLayout);

        // Get maximum size of a single range
        uint32_t maxRangeSize = 0;
        for (auto& range : mRanges) {
            maxRangeSize = std::max(range.size, maxRangeSize);
        }

        // Create descriptor
        VkDescriptorSetAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocateInfo.pNext = nullptr;
        allocateInfo.descriptorPool = descriptorPool;
        allocateInfo.pSetLayouts = &mLayout;
        allocateInfo.descriptorSetCount = 1;
        vkAllocateDescriptorSets(BGLDevice::device(), &allocateInfo, &mDescriptorSet);

        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = mBuffer;
        bufferInfo.offset = 0;
        bufferInfo.range = maxRangeSize;

        VkWriteDescriptorSet write{};
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        write.dstBinding = 0;
        write.dstSet = mDescriptorSet;
        write.descriptorCount = 1;
        write.dstArrayElement = 0;
        write.pBufferInfo = &bufferInfo;
        vkUpdateDescriptorSets(BGLDevice::device(), 1, &write, 0, nullptr);
    }


    TextureHandle BindlessParams::storeTexture(VkImageView imageView, VkSampler sampler) {
        size_t newHandle = textures.size();
        textures.push_back(imageView);

        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = imageView;
        imageInfo.sampler = sampler;

        VkWriteDescriptorSet write{};
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.dstBinding = TextureBinding;
        write.dstSet = mDescriptorSet;
        // Write one texture that is being added
        write.descriptorCount = 1;
        // The array element that we are going to write to
        // is the index, which we refer to as our handles
        write.dstArrayElement = newHandle;
        write.pImageInfo = &imageInfo;

        vkUpdateDescriptorSets(BGLDevice::device(), 1, &write, 0, nullptr);
        return static_cast<TextureHandle>(newHandle);
    }

    BufferHandle BindlessParams::storeBuffer(VkBuffer buffer, VkBufferUsageFlagBits usage) {
        size_t newHandle = buffers.size();
        buffers.push_back(buffer);

        std::array<VkWriteDescriptorSet, 2> writes{};
        for (auto& write : writes) {
            VkDescriptorBufferInfo bufferInfo{};
            bufferInfo.buffer = buffer;
            bufferInfo.offset = 0;
            bufferInfo.range = VK_WHOLE_SIZE;

            write.dstSet = mDescriptorSet;
            // Write one buffer that is being added
            write.descriptorCount = 1;
            // The array element that we are going to write to
            // is the index, which we refer to as our handles
            write.dstArrayElement = newHandle;
            write.pBufferInfo = &bufferInfo;
        }

        size_t index = 0;
        if ((usage & VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT) == VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT) {
            writes.at(index).dstBinding = UniformBinding;
            writes.at(index).descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            index++;
        }

        if ((usage & VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) == VK_BUFFER_USAGE_STORAGE_BUFFER_BIT) {
            writes.at(index).dstBinding = StorageBinding;
            writes.at(index).descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        }

        vkUpdateDescriptorSets(BGLDevice::device(), index, writes.data(), 0, nullptr);

        return static_cast<BufferHandle>(newHandle);
    }

    void BindlessParams::createBindLessDescriptorPool()
    {
        std::array<VkDescriptorPoolSize, 3> poolSizes{};
        poolSizes[0] = { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,PER_BINDING_DESCRIPTORS };
        poolSizes[1] = { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, PER_BINDING_DESCRIPTORS };
        poolSizes[2] = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, PER_BINDING_DESCRIPTORS };

        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        //The whole point of this bindless system is to have single large descriptorset and update parts of it.
        poolInfo.maxSets = 1;
        poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
        
        vkCreateDescriptorPool(BGLDevice::device(), &poolInfo, nullptr, &mDescriptorPool);
    }

    void BindlessParams::createBindlessDescriptorSet()
    {
        // Create three bindings: storage buffer,
    // uniform buffer, and combined image sampler
        std::array<VkDescriptorSetLayoutBinding, 3> bindings{};
        std::array<VkDescriptorBindingFlags, 3> flags{};
        std::array<VkDescriptorType, 3> types{
          VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
          VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
          VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER
        };

        for (uint32_t i = 0; i < 3; ++i) {
            bindings.at(i).binding = i;
            bindings.at(i).descriptorType = types.at(i);
            // Due to partially bound bit, this value
            // is used as an upper bound, which we have set to
            // 1000 to keep it simple for the sake of this post
            bindings.at(i).descriptorCount = PER_BINDING_DESCRIPTORS;
            bindings.at(i).stageFlags = VK_SHADER_STAGE_ALL;
            flags.at(i) = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
        }

        VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlags{};
        bindingFlags.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
        bindingFlags.pNext = nullptr;
        bindingFlags.pBindingFlags = flags.data();
        bindingFlags.bindingCount = 3;

        VkDescriptorSetLayoutCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        createInfo.bindingCount = 3;
        createInfo.pBindings = bindings.data();
        // Create if from a descriptor pool that has update after bind
        // flag set
        createInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;

        // Set binding flags
        createInfo.pNext = &bindingFlags;

        // Create layout
        vkCreateDescriptorSetLayout(BGLDevice::device(), &createInfo, nullptr, &mLayout);
    }

    void BindlessParams::allocateDescriptorSet() {
        VkDescriptorSetAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocateInfo.pNext = nullptr;
        // Pass the pool that is created with update after bind flag
        allocateInfo.descriptorPool = mDescriptorPool;
        // Pass the bindless layout
        allocateInfo.pSetLayouts = &mLayout;
        allocateInfo.descriptorSetCount = 1;

        // Create descriptor
        VkDescriptorSet bindlessDescriptorSet = VK_NULL_HANDLE;
        vkAllocateDescriptorSets(BGLDevice::device(), &allocateInfo, &bindlessDescriptorSet);
    }
}