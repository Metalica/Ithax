// Copyright © 2026 Ithax contributors.

#pragma once

#if TRINITY_PLATFORM == TRINITY_VULKAN

#include "ALResult.h"
#include "Tr2RenderContextEnum.h"
#include "Tr2AdapterStructures.h"
#include "Tr2VertexDefinition.h"

#include <vulkan/vulkan.h>
#include <vma/vk_mem_alloc.h>

#include <cstdint>
#include <vector>
#include <string>

class Tr2RenderContextAL;

namespace TrinityALImpl
{

constexpr uint32_t VULKAN_FRAME_CONTEXT_COUNT = 2;

struct VulkanFrameContext
{
	VkCommandPool commandPool = VK_NULL_HANDLE;
	VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
	VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
	VkSemaphore acquireSemaphore = VK_NULL_HANDLE;
	VkSemaphore renderFinishedSemaphore = VK_NULL_HANDLE;
	VkFence inFlightFence = VK_NULL_HANDLE;
	VkSemaphore timelineSemaphore = VK_NULL_HANDLE;
	uint64_t timelineValue = 0;
};

struct VulkanSwapchainState
{
	VkSwapchainKHR swapchain = VK_NULL_HANDLE;
	VkFormat format = VK_FORMAT_UNDEFINED;
	VkColorSpaceKHR colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
	VkExtent2D extent = { 0, 0 };
	uint32_t imageCount = 0;
	std::vector<VkImage> images;
	std::vector<VkImageView> imageViews;
	VkImage depthImage = VK_NULL_HANDLE;
	VmaAllocation depthAllocation = VK_NULL_HANDLE;
	VkImageView depthView = VK_NULL_HANDLE;
	VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;
	VkImageLayout depthLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	uint32_t currentImageIndex = 0;
	bool suspended = false;
};

struct VulkanDeviceState
{
	VkInstance instance = VK_NULL_HANDLE;
	VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
	VkDevice device = VK_NULL_HANDLE;
	VkSurfaceKHR surface = VK_NULL_HANDLE;
	VkQueue graphicsQueue = VK_NULL_HANDLE;
	VkQueue presentQueue = VK_NULL_HANDLE;
	uint32_t graphicsFamily = VK_QUEUE_FAMILY_IGNORED;
	uint32_t presentFamily = VK_QUEUE_FAMILY_IGNORED;
	VmaAllocator allocator = VK_NULL_HANDLE;
	VkPhysicalDeviceProperties properties = {};
	VkPhysicalDeviceFeatures features = {};
	VkPhysicalDeviceFeatures2 features2 = {};
	VkPhysicalDeviceMemoryProperties memoryProperties = {};
	VkPhysicalDeviceProperties2 properties2 = {};
	VkPhysicalDeviceVulkan13Features vulkan13Features = {};
	VkPhysicalDeviceVulkan12Features vulkan12Features = {};
	VkPhysicalDeviceSynchronization2FeaturesKHR sync2Features = {};
	VkPhysicalDeviceDynamicRenderingFeaturesKHR dynamicRenderingFeatures = {};
	VkPhysicalDeviceTimelineSemaphoreFeaturesKHR timelineSemaphoreFeatures = {};
	VkPhysicalDeviceHostQueryResetFeatures hostQueryResetFeatures = {};
	VkPhysicalDeviceFaultFeaturesEXT deviceFaultFeatures = {};
	VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
	VkPipelineCache pipelineCache = VK_NULL_HANDLE;
	VkQueryPool timestampQueryPool = VK_NULL_HANDLE;
	uint32_t timestampValidBits = 0;
	float timestampPeriod = 1.0f;
	bool validationEnabled = false;
	bool deviceFaultEnabled = false;
	bool swapchainMaintenance1 = false;
	VkSurfaceFormatKHR surfaceFormat = {};
	VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
	VulkanSwapchainState swapchain;
	VulkanFrameContext frames[VULKAN_FRAME_CONTEXT_COUNT];
	uint32_t frameIndex = 0;
	uint32_t lastPresentedImageIndex = 0;
	uint64_t frameNumber = 0;
	uint64_t renderedFrameNumber = 0;
	Tr2WindowHandle window = nullptr;
	Tr2PresentParametersAL presentParameters = {};
	bool windowless = false;
	bool deviceLost = false;
};

struct VulkanDeferredDestroy
{
	VkDevice device;
	VmaAllocator allocator;
	uint64_t timelineValue;
	VkImage image;
	VkImageView imageView;
	VkBuffer buffer;
	VmaAllocation allocation;
	VkDescriptorSetLayout descriptorSetLayout;
	VkPipelineLayout pipelineLayout;
	VkPipeline pipeline;
	VkSampler sampler;
	VkQueryPool queryPool;
	VkSwapchainKHR swapchain;
	VkSemaphore semaphore;
	VkFence fence;
	VkCommandPool commandPool;
};

class VulkanContext
{
public:
	VulkanContext();
	~VulkanContext();

	VulkanContext( const VulkanContext& ) = delete;
	VulkanContext& operator=( const VulkanContext& ) = delete;

	ALResult CreateInstance( bool enableValidation );
	ALResult CreateDevice( uint32_t adapter, Tr2WindowHandle window, const Tr2PresentParametersAL& params );
	ALResult CreateSwapchain();
	ALResult RecreateSwapchain();
	void DestroySwapchain();
	void Destroy();
	void DestroyDevice();
	void DestroyInstance();

	ALResult BeginFrame();
	ALResult EndFrame();
	ALResult Present();

	VkCommandBuffer GetCurrentCommandBuffer() const;
	VkDescriptorPool GetCurrentDescriptorPool() const;
	uint64_t GetCurrentTimelineValue() const;
	uint32_t GetCurrentImageIndex() const;
	uint32_t GetLastPresentedImageIndex() const;

	void Retire( const VulkanDeferredDestroy& entry );
	void DrainDeferred( uint64_t completedValue );
	void WaitIdle();

	VkFormat FindDepthFormat() const;
	VkFormat FindSupportedFormat( const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features ) const;
	uint32_t FindMemoryType( uint32_t typeFilter, VkMemoryPropertyFlags properties ) const;

	VulkanDeviceState state;

private:
	ALResult PickPhysicalDevice( uint32_t adapter, bool requireSurface );
	ALResult CreateLogicalDevice();
	ALResult CreateAllocator();
	ALResult CreateSwapchainInternal( VkSwapchainKHR oldSwapchain );
	ALResult CreateFrameContexts();
	void DestroyFrameContexts();
	ALResult CreateDepthResources();
	void DestroyDepthResources();
	ALResult CreateTimestampQueryPool();
	void DestroyTimestampQueryPool();
	ALResult CreatePipelineCache();
	void DestroyPipelineCache();
	ALResult CreateDebugMessenger();
	void DestroyDebugMessenger();
	ALResult CreateSurface( Tr2WindowHandle window );
	void DestroySurface();
	ALResult QuerySurfaceCapabilities( VkSurfaceCapabilitiesKHR& caps, std::vector<VkSurfaceFormatKHR>& formats, std::vector<VkPresentModeKHR>& modes ) const;
	VkSurfaceFormatKHR SelectSurfaceFormat( const std::vector<VkSurfaceFormatKHR>& formats ) const;
	VkPresentModeKHR SelectPresentMode( const std::vector<VkPresentModeKHR>& modes ) const;
	VkExtent2D SelectExtent( const VkSurfaceCapabilitiesKHR& caps ) const;

	std::vector<VulkanDeferredDestroy> m_deferred;
};

ALResult MapVkResult( VkResult result );
VkFormat ConvertPixelFormat( Tr2RenderContextEnum::PixelFormat format );
Tr2RenderContextEnum::PixelFormat ConvertVkFormat( VkFormat format );
VkFormat ConvertDepthStencilFormat( Tr2RenderContextEnum::DepthStencilFormat format );
VkPrimitiveTopology ConvertTopology( Tr2RenderContextEnum::Topology topology );
VkCompareOp ConvertCompareFunc( Tr2RenderContextEnum::CompareFunc func );
VkBlendFactor ConvertBlendMode( Tr2RenderContextEnum::BlendMode mode );
VkBlendOp ConvertBlendOperation( Tr2RenderContextEnum::BlendOperation op );
VkStencilOp ConvertStencilOperation( Tr2RenderContextEnum::StencilOperation op );
VkFilter ConvertTextureFilter( Tr2RenderContextEnum::TextureFilter filter );
VkSamplerAddressMode ConvertAddressMode( Tr2RenderContextEnum::TextureAddressMode mode );
VkFormat ConvertVertexDataType( Tr2VertexDefinition::DataType dataType );
VkShaderStageFlagBits ConvertShaderType( Tr2RenderContextEnum::ShaderType type );
Tr2RenderContextEnum::ShaderType ConvertVkShaderStage( VkShaderStageFlagBits stage );
VkImageLayout ConvertGpuUsageToLayout( Tr2GpuUsage::Type usage );
VkImageUsageFlags ConvertGpuUsageToImageUsage( Tr2GpuUsage::Type usage );
VkBufferUsageFlags ConvertGpuUsageToBufferUsage( Tr2GpuUsage::Type usage );
VkMemoryPropertyFlags ConvertCpuUsageToMemoryFlags( Tr2CpuUsage::Type usage );
VkImageAspectFlags GetImageAspectFlags( VkFormat format );
bool IsDepthFormat( VkFormat format );

}

#endif
