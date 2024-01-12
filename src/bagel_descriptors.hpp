#pragma once

#include "bagel_engine_device.hpp"
#include "bagel_buffer.hpp"
#include "bagel_engine_swap_chain.hpp"
// std
#include <map>
#include <memory>
#include <unordered_map>
#include <vector>
#include <array>
#include <iostream>

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
        enum BINDINGS {
            UNIFORM,
            BUFFER,
            TEXTURE
        };
        struct TexturePackage {
            VkDescriptorImageInfo imageInfo;
            VkDeviceMemory memory;
            VkImage image;
            // Texture can only be missing when it's bound but it's using temporary texture
            // Or when marked as missing so that it can be overriden
            bool isMissing = false;
        };
    public:
        BGLBindlessDescriptorManager(BGLDevice& bglDevice, BGLDescriptorPool& globalPool);
        ~BGLBindlessDescriptorManager();

        BGLBindlessDescriptorManager(const BGLBindlessDescriptorManager&) = delete;
        BGLBindlessDescriptorManager& operator=(const BGLBindlessDescriptorManager&) = delete;

        void createBindlessDescriptorSet(uint32_t descriptorCount);

        void storeUBO(VkDescriptorBufferInfo bufferInfo);
        uint32_t storeBuffer(VkDescriptorBufferInfo bufferInfo, const char* name);
        //If useDesignatedHandle == true, write to the specified descriptor array element, overriding existing texture. 
        //Existing texture sampler, view, etc are all destroyed.
        uint32_t storeTexture(
            VkDescriptorImageInfo imageInfo,
            VkDeviceMemory memory, 
            VkImage image,
            const char* name, 
            bool useDesignatedHandle,
            uint32_t handle = 0);

        uint32_t searchBufferName(std::string bufferName);
        uint32_t searchTextureName(std::string textureName);

        //Can use these functions to store handle if the last bound resource is the same as one being bound;
        uint32_t getLastBufferHandle() { return textures.size() - 1; };
        uint32_t getLastTextureHandle() { return textures.size() - 1; };

        //Check if texture is missing
        //When loading model, missing textures will be loaded in place of the real textures before texture allocation
        bool checkMissingTexture(uint32_t index);

        VkDescriptorSetLayout getDescriptorSetLayout() const { return bindlessSetLayout; }
        VkDescriptorSet getDescriptorSet(int i) const { return bindlessDescriptorSet[i]; }
    private:
        BGLDevice& bglDevice;
        BGLDescriptorPool& globalPool;
        
        //Never remove elements that is NOT the last element in these vectors.
        //Removing an element in the middle of these vectors will cause storeBuffer/storeTexture 
        // functions to write to descriptor index that is already being used by another undeleted buffer/image.
        //It is valid to clear these vectors (for example when loading new scene) although in that case offscreen renderpass 
        // and its attachments (or other non-dynamically created resources) will have to be remade.
        std::vector<VkDescriptorBufferInfo> buffers{};
        std::vector<TexturePackage> textures{};

        std::unordered_map<std::string, uint32_t> bufferIndexMap;
        std::unordered_map<std::string, uint32_t> textureIndexMap;

        VkDescriptorSetLayout bindlessSetLayout = nullptr;
        std::array<VkDescriptorSet, BGLSwapChain::MAX_FRAMES_IN_FLIGHT> bindlessDescriptorSet = std::array<VkDescriptorSet, BGLSwapChain::MAX_FRAMES_IN_FLIGHT>();
    };
}  // namespace lve
