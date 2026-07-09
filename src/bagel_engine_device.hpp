#pragma once

#include "bagel_window.hpp"

#include <vulkan/vulkan.h>

// std lib headers
#include <string>
#include <vector>
#include <iostream>
#include <cstdlib>

// Single engine-wide definition. Every Vulkan TU already includes this header (VK_CHECK
// wraps calls that use BGLDevice::device()), so the macro lives here instead of being
// re-#defined per file.
#ifndef VK_CHECK
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
#endif

namespace bagel {
    struct SwapChainSupportDetails {
        VkSurfaceCapabilitiesKHR capabilities;
        std::vector<VkSurfaceFormatKHR> formats;
        std::vector<VkPresentModeKHR> presentModes;
    };

    struct QueueFamilyIndices {
        uint32_t graphicsFamily;
        uint32_t presentFamily;
        bool graphicsFamilyHasValue = false;
        bool presentFamilyHasValue = false;
        bool isComplete() { return graphicsFamilyHasValue && presentFamilyHasValue; }
    };

    struct ImmediateUploadContext {
        VkFence _uploadFence;
        VkCommandPool _commandPool;
        VkCommandBuffer _commandBuffer;
    };

    class BGLDevice {
    public:
        const bool enableValidationLayers = true;

        BGLDevice(BGLWindow &window);
        ~BGLDevice();
        
        //VkDevice device();
        // Not copyable or movable
        BGLDevice(const BGLDevice &) = delete;
        BGLDevice operator=(const BGLDevice &) = delete;
        BGLDevice(BGLDevice &&) = delete;
        BGLDevice &operator=(BGLDevice &&) = delete;

        VkInstance getInstance() const { return instance; }
        VkCommandPool getCommandPool() const { return commandPool; }
        VkPhysicalDevice getPhysicalDevice() const { return physicalDevice; }
        VkSurfaceKHR surface() const { return surface_; }
        VkQueue graphicsQueue() const { return graphicsQueue_; }
        VkQueue presentQueue() const { return presentQueue_; }
        // A second graphics-family queue reserved for background uploads (e.g. BGLTextureStreamer).
        // Present only when the graphics family exposes >= 2 queues; falls back to the graphics
        // queue otherwise (callers must then externally synchronize submits themselves).
        VkQueue uploadQueue() const { return hasUploadQueue_ ? uploadQueue_ : graphicsQueue_; }
        bool    hasUploadQueue() const { return hasUploadQueue_; }

        SwapChainSupportDetails getSwapChainSupport() { return querySwapChainSupport(physicalDevice); }
        uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
        QueueFamilyIndices findPhysicalQueueFamilies() { return findQueueFamilies(physicalDevice); }
        VkFormat findSupportedFormat(
            const std::vector<VkFormat> &candidates, VkImageTiling tiling, VkFormatFeatureFlags features);

        // Buffer Helper Functions
        void createBuffer(
            VkDeviceSize size,
            VkBufferUsageFlags usage,
            VkMemoryPropertyFlags properties,
            VkBuffer &buffer,
            VkDeviceMemory &bufferMemory);

        VkCommandBuffer beginSingleTimeCommands();
        void beginSingleTimeCommands(VkCommandBuffer* existingBuffer);
        void endSingleTimeCommands(VkCommandBuffer commandBuffer, VkFence* fence = VK_NULL_HANDLE);
        
        void copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);
        void copyBufferToImage(
            VkBuffer buffer, VkImage image, uint32_t width, uint32_t height, uint32_t layerCount);

        void createImageWithInfo(
            const VkImageCreateInfo &imageInfo,
            VkMemoryPropertyFlags properties,
            VkImage &image,
            VkDeviceMemory &imageMemory);

        VkFenceCreateInfo fenceCreateInfo(VkFenceCreateFlags flags = 0);
        VkSemaphoreCreateInfo semaphoreCreateInfo(VkSemaphoreCreateFlags flags = 0);
        void createUploadFence();

        VkPhysicalDeviceFeatures supportedFeatures;
        VkPhysicalDeviceProperties properties;

        VkBool32 getSupportedDepthsFormat(VkFormat* depthFormat);

        void BeginDebugUtilsLabel(VkCommandBuffer commandBuffer, std::string name);
        void EndDebugUtilsLabel(VkCommandBuffer commandBuffer);

        static VkDevice& device() { return _device; }

    private:
        void createInstance();
        void setupDebugMessenger();
        void createSurface();
        void pickPhysicalDevice();
        void createLogicalDevice();
        void createCommandPool();

        // helper functions
        bool isDeviceSuitable(VkPhysicalDevice device);
        std::vector<const char *> getRequiredExtensions();
        bool checkValidationLayerSupport();
        QueueFamilyIndices findQueueFamilies(VkPhysicalDevice device);
        void populateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT &createInfo);
        void hasGflwRequiredInstanceExtensions();
        bool checkDeviceExtensionSupport(VkPhysicalDevice device);
        SwapChainSupportDetails querySwapChainSupport(VkPhysicalDevice device);

        VkInstance instance;
        VkDebugUtilsMessengerEXT debugMessenger;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        BGLWindow& window;
        VkCommandPool commandPool;

        static VkDevice _device;
        VkSurfaceKHR surface_;
        VkQueue graphicsQueue_;
        VkQueue presentQueue_;
        VkQueue uploadQueue_ = VK_NULL_HANDLE;   // 2nd graphics-family queue (background uploads)
        bool    hasUploadQueue_ = false;

        ImmediateUploadContext _uploadContext;

        PFN_vkCmdBeginDebugUtilsLabelEXT ObjCmdBeginDebugUtilsLabel = nullptr;
        PFN_vkCmdEndDebugUtilsLabelEXT   ObjCmdEndDebugUtilsLabel   = nullptr;

        const std::vector<const char *> validationLayers = {"VK_LAYER_KHRONOS_validation"};
        const std::vector<const char *> deviceExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    };

}  // namespace lve