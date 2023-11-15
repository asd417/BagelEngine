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
        std::cout << "Creating DescriptorSetLayout with size " << descriptorSetLayoutInfo.bindingCount << "\n";
        if (vkCreateDescriptorSetLayout(
            bglDevice.device(),
            &descriptorSetLayoutInfo,
            nullptr,
            &descriptorSetLayout) != VK_SUCCESS) {
            throw std::runtime_error("failed to create descriptor set layout!");
        }
    }

    BGLDescriptorSetLayout::~BGLDescriptorSetLayout() {
        vkDestroyDescriptorSetLayout(bglDevice.device(), descriptorSetLayout, nullptr);
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
        descriptorPoolInfo.flags = poolFlags;
        std::cout << "Building Descriptor Pool with poolSizeCount: " << descriptorPoolInfo.poolSizeCount << "\n";
        for (auto& p : poolSizes) {
            std::cout << "  type: " << p.type << "\n";
            std::cout << "  descriptorCount: " << p.descriptorCount << "\n";
        }

        if (vkCreateDescriptorPool(bglDevice.device(), &descriptorPoolInfo, nullptr, &descriptorPool) !=
            VK_SUCCESS) {
            throw std::runtime_error("failed to create descriptor pool!");
        }
    }

    BGLDescriptorPool::~BGLDescriptorPool() {
        vkDestroyDescriptorPool(bglDevice.device(), descriptorPool, nullptr);
    }

    bool BGLDescriptorPool::allocateDescriptor(
        const VkDescriptorSetLayout descriptorSetLayout, VkDescriptorSet& descriptor) const {
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = descriptorPool;
        allocInfo.pSetLayouts = &descriptorSetLayout;
        allocInfo.descriptorSetCount = 1;

        // Might want to create a "DescriptorPoolManager" class that handles this case, and builds
        // a new pool whenever an old pool fills up. But this is beyond our current scope
        VK_CHECK(vkAllocateDescriptorSets(bglDevice.device(), &allocInfo, &descriptor));
        /*if ( != VK_SUCCESS) {
            return false;
        }*/
        return true;
    }

    void BGLDescriptorPool::freeDescriptors(std::vector<VkDescriptorSet>& descriptors) const {
        vkFreeDescriptorSets(
            bglDevice.device(),
            descriptorPool,
            static_cast<uint32_t>(descriptors.size()),
            descriptors.data());
    }

    void BGLDescriptorPool::resetPool() {
        vkResetDescriptorPool(bglDevice.device(), descriptorPool, 0);
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
        vkUpdateDescriptorSets(pool.bglDevice.device(), writes.size(), writes.data(), 0, nullptr);
    }

}  // namespace lve