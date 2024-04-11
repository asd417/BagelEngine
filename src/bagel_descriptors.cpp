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

    inline VkDescriptorSetLayoutBinding createDescriptorSetLayoutBinding(int bindingNum, VkDescriptorType descriptorType, int descriptorCount, VkShaderStageFlags stageFlags)
    {
        VkDescriptorSetLayoutBinding b{};
        b.binding = bindingNum;
        b.descriptorType = descriptorType;
        b.descriptorCount = descriptorCount;
        b.stageFlags = stageFlags;
        return b;
    }

    inline VkDescriptorImageInfo descriptorImageInfo(VkSampler sampler, VkImageView imageView, VkImageLayout imageLayout)
    {
        VkDescriptorImageInfo descriptorImageInfo{};
        descriptorImageInfo.sampler = sampler;
        descriptorImageInfo.imageView = imageView;
        descriptorImageInfo.imageLayout = imageLayout;
        return descriptorImageInfo;
    }

    inline VkWriteDescriptorSet writeDescriptorSet(
        VkDescriptorSet dstSet,
        VkDescriptorType type,
        uint32_t binding,
        VkDescriptorImageInfo* imageInfo,
        uint32_t descriptorCount = 1)
    {
        VkWriteDescriptorSet writeDescriptorSet{};
        writeDescriptorSet.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writeDescriptorSet.dstSet = dstSet;
        writeDescriptorSet.descriptorType = type;
        writeDescriptorSet.dstBinding = binding;
        writeDescriptorSet.pImageInfo = imageInfo;
        writeDescriptorSet.descriptorCount = descriptorCount;
        return writeDescriptorSet;
    }

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
        const VkDescriptorSetLayout descriptorSetLayout, VkDescriptorSet& descriptor, uint32_t descriptorCount) const {
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.pSetLayouts = &descriptorSetLayout;
        allocInfo.descriptorSetCount = descriptorCount;

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

    // *************** BGLBindlessDescriptorManager *********************

    BGLBindlessDescriptorManager::BGLBindlessDescriptorManager(BGLDevice& _bglDevice, BGLDescriptorPool& _globalPool) : bglDevice{ _bglDevice }, globalPool{_globalPool}
    {
    }

    BGLBindlessDescriptorManager::~BGLBindlessDescriptorManager()
    {
        std::cout << "Destroying Textures\n";
        vkDestroyDescriptorSetLayout(BGLDevice::device(), bindlessSetLayout, nullptr);
        for (auto& bufferInfo : buffers) {
            vkDestroyBuffer(BGLDevice::device(), bufferInfo.buffer, nullptr);
        }
        for (auto& package : textures) {
            vkDestroyImageView(BGLDevice::device(), package.imageInfo.imageView, nullptr);
            vkDestroySampler(BGLDevice::device(), package.imageInfo.sampler, nullptr);
            vkDestroyImage(BGLDevice::device(), package.image, nullptr);
            vkFreeMemory(BGLDevice::device(), package.memory, nullptr);
        }
    }

    void BGLBindlessDescriptorManager::createBindlessDescriptorSet(uint32_t descriptorCount)
    {
        // One descriptorSet per swapchain count
        // Create three bindings: storage buffer,
        // uniform buffer, and combined image sampler

        //Bindings for deferred rendering
        VkDescriptorSetLayoutBinding deferredPosition = createDescriptorSetLayoutBinding(BINDINGS::DR_POS, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT);
        VkDescriptorSetLayoutBinding deferredNormal = createDescriptorSetLayoutBinding(BINDINGS::DR_NORMAL, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT);
        VkDescriptorSetLayoutBinding deferredAlbedo = createDescriptorSetLayoutBinding(BINDINGS::DR_ALBEDO, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT);

        VkDescriptorSetLayoutBinding uboBinding = createDescriptorSetLayoutBinding(BINDINGS::UNIFORM, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, descriptorCount, VK_SHADER_STAGE_ALL);
        VkDescriptorSetLayoutBinding storageBinding = createDescriptorSetLayoutBinding(BINDINGS::BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, descriptorCount, VK_SHADER_STAGE_ALL);
        VkDescriptorSetLayoutBinding imageBinding = createDescriptorSetLayoutBinding(BINDINGS::TEXTURE, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, descriptorCount, VK_SHADER_STAGE_ALL);

        VkDescriptorBindingFlags bindFlags = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
        
        constexpr int bindingCount = 6;
        std::array<VkDescriptorBindingFlags, bindingCount> flagsArray = { bindFlags, bindFlags, bindFlags, bindFlags, bindFlags, bindFlags };

        VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlags{};
        bindingFlags.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
        bindingFlags.pNext = nullptr;
        bindingFlags.pBindingFlags = flagsArray.data();
        bindingFlags.bindingCount = bindingCount;

        std::array<VkDescriptorSetLayoutBinding, bindingCount> bindings = { 
            uboBinding, 
            storageBinding, 
            imageBinding, 
            deferredPosition, 
            deferredNormal, 
            deferredAlbedo };

        VkDescriptorSetLayoutCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        createInfo.bindingCount = bindingCount;
        createInfo.pBindings = bindings.data();
        createInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
        createInfo.pNext = &bindingFlags;

        // Create layout
        VK_CHECK(vkCreateDescriptorSetLayout(BGLDevice::device(), &createInfo, nullptr, &bindlessSetLayout));

        //Create descriptor set for all swapchains
        for (int i = 0; i < BGLSwapChain::MAX_FRAMES_IN_FLIGHT; i++) {
            globalPool.allocateDescriptor(bindlessSetLayout, bindlessDescriptorSet[i], 1);
        }
    }



    void BGLBindlessDescriptorManager::storeUBO(VkDescriptorBufferInfo bufferInfo, uint32_t targetIndex)
    {
        UBObuffers[targetIndex] = bufferInfo;

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        write.dstBinding = BINDINGS::UNIFORM;

        // Write one buffer that is being added
        write.descriptorCount = 1;
        write.pBufferInfo = &bufferInfo;
        write.dstArrayElement = targetIndex;

        //Create descriptor set for all swapchains
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

        //Create descriptor set for all swapchains
        for (int i = 0; i < BGLSwapChain::MAX_FRAMES_IN_FLIGHT; i++) {
            write.dstSet = bindlessDescriptorSet[i];
            vkUpdateDescriptorSets(BGLDevice::device(), 1, &write, 0, nullptr);
        }

        if (name != NULL) {
            bufferIndexMap.emplace(std::string(name), newHandle);
        }
        return newHandle;
    }

    uint32_t BGLBindlessDescriptorManager::storeTexture(
        VkDescriptorImageInfo imageInfo,
        VkDeviceMemory memory, 
        VkImage image, 
        const char* name, 
        bool useDesignatedHandle,
        uint32_t _handle)
    {
        size_t handle;
        if (useDesignatedHandle) {
            std::cout << "Using designated handle. Destroying existing textures\n";
            handle = _handle;
            //Destroy existing data at the _handle index
            vkDestroyImageView(BGLDevice::device(), textures[handle].imageInfo.imageView, nullptr);
            vkDestroySampler(BGLDevice::device(), textures[handle].imageInfo.sampler, nullptr);
            vkDestroyImage(BGLDevice::device(), textures[handle].image, nullptr);
            vkFreeMemory(BGLDevice::device(), textures[handle].memory, nullptr);

            textures[handle].imageInfo = imageInfo;
            textures[handle].memory = memory;
            textures[handle].image = image;
            textures[handle].isMissing = false;
        }
        else {
            handle = textures.size();
            textures.push_back({ imageInfo, memory, image });
            std::cout << "Using new handle\n";
        }

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.dstBinding = BINDINGS::TEXTURE;
        // Write one texture that is being added
        write.descriptorCount = 1;
        // The array element that we are going to write to
        // is the index, which we refer to as our handles
        write.dstArrayElement = handle;
        write.pImageInfo = &imageInfo;

        for (int i = 0; i < BGLSwapChain::MAX_FRAMES_IN_FLIGHT; i++) {
            write.dstSet = bindlessDescriptorSet[i];
            vkUpdateDescriptorSets(BGLDevice::device(), 1, &write, 0, nullptr);
        }

        if (name != NULL) {
            textureIndexMap.emplace(std::string(name), handle);
        }
        return handle;
    }

    void BGLBindlessDescriptorManager::writeDeferredRenderTargetToDescriptor(VkSampler colorSampler, VkImageView positionView, VkImageView normalView, VkImageView albedoView) {
        VkDescriptorImageInfo texDescriptorPosition =
            descriptorImageInfo(
                colorSampler,
                positionView,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        VkDescriptorImageInfo texDescriptorNormal =
            descriptorImageInfo(
                colorSampler,
                normalView,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        VkDescriptorImageInfo texDescriptorAlbedo =
            descriptorImageInfo(
                colorSampler,
                albedoView,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

        // Deferred composition
        for (int i = 0; i < BGLSwapChain::MAX_FRAMES_IN_FLIGHT; i++) {
            std::vector<VkWriteDescriptorSet> writeDescriptorSets{};
            writeDescriptorSets = {
                // Binding 1 : Position texture target
                writeDescriptorSet(bindlessDescriptorSet[i], VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, BINDINGS::DR_POS, &texDescriptorPosition),
                // Binding 2 : Normals texture target
                writeDescriptorSet(bindlessDescriptorSet[i], VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, BINDINGS::DR_NORMAL, &texDescriptorNormal),
                // Binding 3 : Albedo texture target
                writeDescriptorSet(bindlessDescriptorSet[i], VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, BINDINGS::DR_ALBEDO, &texDescriptorAlbedo),
            };
            vkUpdateDescriptorSets(BGLDevice::device(), static_cast<uint32_t>(writeDescriptorSets.size()), writeDescriptorSets.data(), 0, nullptr);

        }
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
    //Returns true if the texture at index is considered 'missing'
    //A texture isn't missing if the index is bigger than the texture vector: it's simply not bound
    //A texture that is bound, during model loading process, with placeholder texture is considered missing
    //Because 'missing' is when thing that is supposed to be there isn't there
    //Like, a hamburger can be missing a petty but can't be missing a Ford GT
    bool BGLBindlessDescriptorManager::checkMissingTexture(uint32_t index)
    {
        if (textures.size() <= index) return false;
        return textures[index].isMissing;
    }
}