#pragma once

#include "bagel_engine_device.hpp"
#include "bagel_buffer.hpp"
// std
#include <memory>
#include <unordered_map>
#include <vector>

namespace bagel {

    class BGLDescriptorSetLayout {
    public:
        class Builder {
        public:
            Builder(BGLDevice& bglDevice) : bglDevice{ bglDevice } {}

            Builder& addBinding(
                uint32_t binding,
                VkDescriptorType descriptorType,
                VkShaderStageFlags stageFlags,
                uint32_t count = 1);
            std::unique_ptr<BGLDescriptorSetLayout> build() const;

        private:
            BGLDevice& bglDevice;
            std::unordered_map<uint32_t, VkDescriptorSetLayoutBinding> bindings{};
        };

        BGLDescriptorSetLayout(
            BGLDevice& lveDevice, std::unordered_map<uint32_t, VkDescriptorSetLayoutBinding> bindings);
        ~BGLDescriptorSetLayout();
        BGLDescriptorSetLayout(const BGLDescriptorSetLayout&) = delete;
        BGLDescriptorSetLayout& operator=(const BGLDescriptorSetLayout&) = delete;

        VkDescriptorSetLayout getDescriptorSetLayout() const { return descriptorSetLayout; }

    private:
        BGLDevice& bglDevice;
        VkDescriptorSetLayout descriptorSetLayout;
        std::unordered_map<uint32_t, VkDescriptorSetLayoutBinding> bindings;

        friend class BGLDescriptorWriter;
    };

    class BGLDescriptorPool {
    public:
        class Builder {
        public:
            Builder(BGLDevice& bglDevice) : bglDevice{ bglDevice } {}

            Builder& addPoolSize(VkDescriptorType descriptorType, uint32_t count);
            Builder& setPoolFlags(VkDescriptorPoolCreateFlags flags);
            Builder& setMaxSets(uint32_t count);
            std::unique_ptr<BGLDescriptorPool> build() const;

        private:
            BGLDevice& bglDevice;
            std::vector<VkDescriptorPoolSize> poolSizes{};
            uint32_t maxSets = 1000;
            VkDescriptorPoolCreateFlags poolFlags = 0;
        };

        BGLDescriptorPool(
            BGLDevice& bglDevice,
            uint32_t maxSets,
            VkDescriptorPoolCreateFlags poolFlags,
            const std::vector<VkDescriptorPoolSize>& poolSizes);
        ~BGLDescriptorPool();
        BGLDescriptorPool(const BGLDescriptorPool&) = delete;
        BGLDescriptorPool& operator=(const BGLDescriptorPool&) = delete;

        VkDescriptorPool getDescriptorPool() const { return descriptorPool; };

        bool allocateDescriptor(
            const VkDescriptorSetLayout descriptorSetLayout, VkDescriptorSet& descriptor) const;

        void freeDescriptors(std::vector<VkDescriptorSet>& descriptors) const;

        void resetPool();

    private:
        BGLDevice& bglDevice;
        VkDescriptorPool descriptorPool;

        friend class BGLDescriptorWriter;
    };

    class BGLDescriptorWriter {
    public:
        BGLDescriptorWriter(BGLDescriptorSetLayout& setLayout, BGLDescriptorPool& pool);

        BGLDescriptorWriter& writeBuffer(uint32_t binding, VkDescriptorBufferInfo* bufferInfo);
        BGLDescriptorWriter& writeImages(uint32_t binding, VkDescriptorImageInfo* imageInfo, uint32_t imageCount = 1);

        bool build(VkDescriptorSet& set);
        void overwrite(VkDescriptorSet& set);

    private:
        BGLDescriptorSetLayout& setLayout;
        BGLDescriptorPool& pool;
        std::vector<VkWriteDescriptorSet> writes;
    };

    class BGLBindlessDescriptorManager {
        const uint32_t UniformBinding = 0;
        const uint32_t BufferBinding = 1;
        const uint32_t TextureBinding = 2;
    public:
        BGLBindlessDescriptorManager(BGLDevice& bglDevice, BGLDescriptorPool& globalPool);
        ~BGLBindlessDescriptorManager();

        BGLBindlessDescriptorManager(const BGLBindlessDescriptorManager&) = delete;
        BGLBindlessDescriptorManager& operator=(const BGLBindlessDescriptorManager&) = delete;

        void createBindlessDescriptorSet(uint32_t descriptorCount);

        void storeUBO(VkDescriptorBufferInfo bufferInfo);
        uint32_t storeBuffer(BGLBuffer& buffer);
        uint32_t storeTexture(VkImageView imageView, VkSampler sampler);

        //Can use these functions to store handle if the last bound resource is the same as one being bound;
        uint32_t getLastBufferHandle() { return textures.size() - 1; };
        uint32_t getLastTextureHandle() { return textures.size() - 1; };

        VkDescriptorSetLayout getDescriptorSetLayout() const { return bindlessSetLayout; }
        VkDescriptorSet getDescriptorSet() const { return bindlessDescriptorSet; }
    private:
        BGLDevice& bglDevice;
        BGLDescriptorPool& globalPool;
        std::vector<VkBuffer> buffers{};
        std::vector<VkImageView> textures{};
        VkDescriptorSetLayout bindlessSetLayout = nullptr;
        VkDescriptorSet bindlessDescriptorSet = nullptr;
    };
}  // namespace lve
