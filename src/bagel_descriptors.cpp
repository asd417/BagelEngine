#include "bagel_descriptors.hpp"


#define VK_CHECK(x)                                                     \
	do                                                                  \
	{                                                                   \
		VkResult err = x;                                               \
		if (err)                                                        \
		{                                                               \
			std::cout <<"Detected Vulkan error: " << err << std::endl;  \
			abort();                                                    \
		}                                                               \
	} while (0)

// std
#include <iostream>
#include <cassert>
#include <stdexcept>
#include <array>

// vulkan headers
#include <vulkan/vulkan.h>

#define GLOBAL_DESCRIPTOR_COUNT 1000
#define BINDLESS

namespace bagel {

    // *************** Descriptor Set Layout Builder *********************
    //Checks if the binding at the specified index hasnt already been a
    BGLDescriptorSetLayout::Builder& BGLDescriptorSetLayout::Builder::addBinding(
        uint32_t binding,
        VkDescriptorType descriptorType,
        VkShaderStageFlags stageFlags,
        uint32_t count) {
        assert(bindings.count(binding) == 0 && "Binding already in use");
        VkDescriptorSetLayoutBinding layoutBinding{};
        layoutBinding.binding = binding;
        layoutBinding.descriptorType = descriptorType;
        layoutBinding.descriptorCount = count;
        layoutBinding.stageFlags = stageFlags;
        bindings[binding] = layoutBinding;
        std::cout << "Bound DescriptorSetLayout at index " << binding << " " << descriptorType << " " << count << "\n";
        return *this;
    }

    std::unique_ptr<BGLDescriptorSetLayout> BGLDescriptorSetLayout::Builder::build() const {
        return std::make_unique<BGLDescriptorSetLayout>(bglDevice, bindings);
    }

    // *************** Descriptor Set Layout *********************

    BGLDescriptorSetLayout::BGLDescriptorSetLayout(
        BGLDevice& bglDevice, std::unordered_map<uint32_t, VkDescriptorSetLayoutBinding> bindings)
        : bglDevice{ bglDevice }, bindings{ bindings } {
        std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings{};
        for (auto kv : bindings) {
            setLayoutBindings.push_back(kv.second);
        }

        VkDescriptorSetLayoutCreateInfo descriptorSetLayoutInfo{};
        descriptorSetLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        descriptorSetLayoutInfo.bindingCount = static_cast<uint32_t>(setLayoutBindings.size());
        descriptorSetLayoutInfo.pBindings = setLayoutBindings.data();

        descriptorSetLayoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT_EXT;
        VkDescriptorBindingFlags bindless_flags = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT_EXT | VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT_EXT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT_EXT;
        VkDescriptorSetLayoutBindingFlagsCreateInfoEXT extended_info{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO_EXT, nullptr };
        extended_info.pBindingFlags = &bindless_flags;
        descriptorSetLayoutInfo.pNext = &extended_info;

        std::cout << "Creating DescriptorSetLayout with size " << descriptorSetLayoutInfo.bindingCount << "\n";
        if (vkCreateDescriptorSetLayout(
            BGLDevice::device(),
            &descriptorSetLayoutInfo,
            nullptr,
            &descriptorSetLayout) != VK_SUCCESS) {
            throw std::runtime_error("failed to create descriptor set layout!");
        }
    }

    BGLDescriptorSetLayout::~BGLDescriptorSetLayout() {
        vkDestroyDescriptorSetLayout(BGLDevice::device(), descriptorSetLayout, nullptr);
    }

    // *************** Descriptor Pool Builder *********************
    BGLDescriptorPool::Builder& BGLDescriptorPool::Builder::addPoolSize(
        VkDescriptorType descriptorType, uint32_t count) {
        poolSizes.push_back({ descriptorType, count });
        return *this;
    }

    BGLDescriptorPool::Builder& BGLDescriptorPool::Builder::setPoolFlags(
        VkDescriptorPoolCreateFlags flags) {
        poolFlags = flags;
        return *this;
    }
    BGLDescriptorPool::Builder& BGLDescriptorPool::Builder::setMaxSets(uint32_t count) {
        maxSets = count;
        return *this;
    }

    std::unique_ptr<BGLDescriptorPool> BGLDescriptorPool::Builder::build() const {
        return std::make_unique<BGLDescriptorPool>(bglDevice, maxSets, poolFlags, poolSizes);
    }

    // *************** Descriptor Pool *********************

    BGLDescriptorPool::BGLDescriptorPool(
        BGLDevice& bglDevice,
        uint32_t maxSets,
        VkDescriptorPoolCreateFlags poolFlags,
        const std::vector<VkDescriptorPoolSize>& poolSizes)
        : bglDevice{ bglDevice } {
        VkDescriptorPoolCreateInfo descriptorPoolInfo{};
        descriptorPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        descriptorPoolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        descriptorPoolInfo.pPoolSizes = poolSizes.data();
        descriptorPoolInfo.maxSets = maxSets;

        descriptorPoolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT_EXT | poolFlags;

        std::cout << "Building Descriptor Pool with poolSizeCount: " << descriptorPoolInfo.poolSizeCount << "\n";
        for (auto& p : poolSizes) {
            std::cout << "  type: " << p.type << "\n";
            std::cout << "  descriptorCount: " << p.descriptorCount << "\n";
        }

        if (vkCreateDescriptorPool(BGLDevice::device(), &descriptorPoolInfo, nullptr, &descriptorPool) !=
            VK_SUCCESS) {
            throw std::runtime_error("failed to create descriptor pool!");
        }
    }

    BGLDescriptorPool::~BGLDescriptorPool() {
        vkDestroyDescriptorPool(BGLDevice::device(), descriptorPool, nullptr);
    }

    bool BGLDescriptorPool::allocateDescriptor(
        const VkDescriptorSetLayout descriptorSetLayout, VkDescriptorSet& descriptor) const {
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.pSetLayouts = &descriptorSetLayout;
        allocInfo.descriptorSetCount = 1;

#ifdef BINDLESS
        VkDescriptorSetVariableDescriptorCountAllocateInfoEXT countInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO_EXT };
        uint32_t max_binding = GLOBAL_DESCRIPTOR_COUNT;
        countInfo.descriptorSetCount = 1;
        // This number is the max allocatable count
        countInfo.pDescriptorCounts = &max_binding;
        allocInfo.pNext = &countInfo;
#endif

        // Might want to create a "DescriptorPoolManager" class that handles this case, and builds
        // a new pool whenever an old pool fills up. But this is beyond our current scope
        VK_CHECK(vkAllocateDescriptorSets(BGLDevice::device(), &allocInfo, &descriptor));
        /*if ( != VK_SUCCESS) {
            return false;
        }*/
        return true;
    }

    void BGLDescriptorPool::freeDescriptors(std::vector<VkDescriptorSet>& descriptors) const {
        vkFreeDescriptorSets(
            BGLDevice::device(),
            descriptorPool,
            static_cast<uint32_t>(descriptors.size()),
            descriptors.data());
    }

    void BGLDescriptorPool::resetPool() {
        vkResetDescriptorPool(BGLDevice::device(), descriptorPool, 0);
    }

    // *************** Descriptor Writer *********************

    BGLDescriptorWriter::BGLDescriptorWriter(BGLDescriptorSetLayout& setLayout, BGLDescriptorPool& pool)
        : setLayout{ setLayout }, pool{ pool } {}

    BGLDescriptorWriter& BGLDescriptorWriter::writeBuffer(
        uint32_t binding, VkDescriptorBufferInfo* bufferInfo) {
        assert(setLayout.bindings.count(binding) == 1 && "Layout does not contain specified binding");

        auto& bindingDescription = setLayout.bindings[binding];

        assert(
            bindingDescription.descriptorCount == 1 &&
            "Binding single descriptor info, but binding expects multiple");

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.descriptorType = bindingDescription.descriptorType;
        write.dstBinding = binding;
        write.pBufferInfo = bufferInfo;
        write.descriptorCount = 1;

        writes.push_back(write);
        return *this;
    }

    BGLDescriptorWriter& BGLDescriptorWriter::writeImages(
        uint32_t binding, VkDescriptorImageInfo* imageInfo, uint32_t imageCount) {
        assert(setLayout.bindings.count(binding) == 1 && "Layout does not contain specified binding");

        auto& bindingDescription = setLayout.bindings[binding];

        //assert(
        //    bindingDescription.descriptorCount == 1 &&
        //    "Binding single descriptor info, but binding expects multiple");

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.descriptorType = bindingDescription.descriptorType;
        write.dstBinding = binding;
        write.pImageInfo = imageInfo;
        write.descriptorCount = imageCount;

        writes.push_back(write);
        return *this;
    }
    // Need to 
    bool BGLDescriptorWriter::build(VkDescriptorSet& set) {
        bool success = pool.allocateDescriptor(setLayout.getDescriptorSetLayout(), set);
        if (!success) {
            throw "Could not allocate descriptor";
            return false;
        }
        overwrite(set); 
        return true;
    }

    void BGLDescriptorWriter::overwrite(VkDescriptorSet& set) {
        for (auto& write : writes) {
            write.dstSet = set;
        }
        vkUpdateDescriptorSets(BGLDevice::device(), writes.size(), writes.data(), 0, nullptr);
    }

    // *************** BGLBindlessDescriptorManager *********************

    BGLBindlessDescriptorManager::BGLBindlessDescriptorManager(BGLDevice& _bglDevice, BGLDescriptorPool& _globalPool) : bglDevice{ _bglDevice }, globalPool{_globalPool}
    {
    }

    BGLBindlessDescriptorManager::~BGLBindlessDescriptorManager()
    {
        vkDestroyDescriptorSetLayout(BGLDevice::device(), bindlessSetLayout, nullptr);
    }

    void BGLBindlessDescriptorManager::createBindlessDescriptorSet(uint32_t descriptorCount)
    {
        // One descriptorSet per swapchain count
        // Create three bindings: storage buffer,
        // uniform buffer, and combined image sampler

        VkDescriptorSetLayoutBinding uboBinding{};
        uboBinding.binding = 0;
        uboBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        uboBinding.descriptorCount = 1;
        uboBinding.stageFlags = VK_SHADER_STAGE_ALL;
        VkDescriptorSetLayoutBinding storageBinding{};
        storageBinding.binding = 1;
        storageBinding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        storageBinding.descriptorCount = descriptorCount;
        storageBinding.stageFlags = VK_SHADER_STAGE_ALL;
        VkDescriptorSetLayoutBinding imageBinding{};
        imageBinding.binding = 2;
        imageBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        imageBinding.descriptorCount = descriptorCount;
        imageBinding.stageFlags = VK_SHADER_STAGE_ALL;

        VkDescriptorBindingFlags bindFlags = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
        std::array<VkDescriptorBindingFlags, 3> flagsArray = { bindFlags, bindFlags, bindFlags };

        VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlags{};
        bindingFlags.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
        bindingFlags.pNext = nullptr;
        bindingFlags.pBindingFlags = flagsArray.data();
        bindingFlags.bindingCount = 3;

        std::array<VkDescriptorSetLayoutBinding, 3> bindings = { uboBinding ,storageBinding,imageBinding };

        VkDescriptorSetLayoutCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        createInfo.bindingCount = 3;
        createInfo.pBindings = bindings.data();
        createInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
        createInfo.pNext = &bindingFlags;


        // Create layout
        VK_CHECK(vkCreateDescriptorSetLayout(BGLDevice::device(), &createInfo, nullptr, &bindlessSetLayout));
        // Create Descriptor Sets with the layout, one per swapchain
        for (int i = 0; i < BGLSwapChain::MAX_FRAMES_IN_FLIGHT; i++) {
            globalPool.allocateDescriptor(bindlessSetLayout, bindlessDescriptorSet[i]);
        }
    }

    void BGLBindlessDescriptorManager::storeUBO(VkDescriptorBufferInfo bufferInfo)
    {
        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        // Write one buffer that is being added
        write.descriptorCount = 1;
        // The array element that we are going to write to
        // is the index, which we refer to as our handles
        write.dstArrayElement = 0;
        write.pBufferInfo = &bufferInfo;

        write.dstBinding = BINDINGS::UNIFORM;
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        for (int i = 0; i < BGLSwapChain::MAX_FRAMES_IN_FLIGHT; i++) {
            write.dstSet = bindlessDescriptorSet[i];
            vkUpdateDescriptorSets(BGLDevice::device(), 1, &write, 0, nullptr);
        }
    }

    uint32_t BGLBindlessDescriptorManager::storeBuffer(VkDescriptorBufferInfo bufferInfo, const char* name = NULL)
    {
        size_t newHandle = buffers.size();
        buffers.push_back(bufferInfo);

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        write.dstBinding = BINDINGS::BUFFER;

        // Write one buffer that is being added
        write.descriptorCount = 1;
        write.pBufferInfo = &bufferInfo;
        write.dstArrayElement = newHandle;

        for (int i = 0; i < BGLSwapChain::MAX_FRAMES_IN_FLIGHT; i++) {
            write.dstSet = bindlessDescriptorSet[i];
            vkUpdateDescriptorSets(BGLDevice::device(), 1, &write, 0, nullptr);
        }

        if (name != NULL) {
            bufferIndexMap.emplace(std::string(name), newHandle);
        }
        return newHandle;
    }

    uint32_t BGLBindlessDescriptorManager::storeTexture(VkImageView imageView, VkSampler sampler, const char* name = NULL)
    {
        size_t newHandle = textures.size();
        textures.push_back(imageView);

        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = imageView;
        imageInfo.sampler = sampler;

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.dstBinding = BINDINGS::TEXTURE;
        // Write one texture that is being added
        write.descriptorCount = 1;
        // The array element that we are going to write to
        // is the index, which we refer to as our handles
        write.dstArrayElement = newHandle;
        write.pImageInfo = &imageInfo;

        for (int i = 0; i < BGLSwapChain::MAX_FRAMES_IN_FLIGHT; i++) {
            write.dstSet = bindlessDescriptorSet[i];
            vkUpdateDescriptorSets(BGLDevice::device(), 1, &write, 0, nullptr);
        }

        if (name != NULL) {
            textureIndexMap.emplace(std::string(name), newHandle);
        }
        return newHandle;
    }

    uint32_t BGLBindlessDescriptorManager::searchBufferName(std::string bufferName)
    {
        auto it = bufferIndexMap.find(bufferName);
        if (it == bufferIndexMap.end()) return std::numeric_limits<uint32_t>::max();
        return it->second;
    }

    uint32_t BGLBindlessDescriptorManager::searchTextureName(std::string textureName)
    {
        auto it = textureIndexMap.find(textureName);
        if (it == textureIndexMap.end()) return std::numeric_limits<uint32_t>::max();
        return it->second;
    }
}