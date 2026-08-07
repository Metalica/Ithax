// Copyright © 2026 Ithax contributors.

#include "StdAfx.h"
#include "Tr2VulkanContext.h"

#if TRINITY_PLATFORM == TRINITY_VULKAN

#include "ALLog.h"
#include "Tr2VertexDefinition.h"

#include <algorithm>
#include <array>
#include <iterator>
#include <limits>

namespace TrinityALImpl
{

namespace
{

constexpr uint32_t VULKAN_API_VERSION = VK_API_VERSION_1_3;
constexpr uint32_t MAX_DEFERRED_ENTRIES = 4096;

#define VK_CHECK( expr)                                                         \
	do                                                                         \
	{                                                                          \
		VkResult _vkResult = ( expr );                                         \
		if( _vkResult != VK_SUCCESS )                                          \
		{                                                                      \
			CCP_AL_LOGERR( "%s failed: %d", #expr, int( _vkResult ) );         \
			return MapVkResult( _vkResult );                                   \
		}                                                                      \
	} while( 0 )

const char* const REQUIRED_DEVICE_EXTENSIONS[] = {
	VK_KHR_SWAPCHAIN_EXTENSION_NAME,
};

const char* const OPTIONAL_DEVICE_EXTENSIONS[] = {
	VK_EXT_DEVICE_FAULT_EXTENSION_NAME,
	VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME,
};

bool HasExtension( const std::vector<VkExtensionProperties>& available, const char* name )
{
	for( const auto& extension : available )
	{
		if( strcmp( extension.extensionName, name ) == 0 )
		{
			return true;
		}
	}
	return false;
}

bool HasLayer( const std::vector<VkLayerProperties>& available, const char* name )
{
	for( const auto& layer : available )
	{
		if( strcmp( layer.layerName, name ) == 0 )
		{
			return true;
		}
	}
	return false;
}

VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
	VkDebugUtilsMessageSeverityFlagBitsEXT severity,
	VkDebugUtilsMessageTypeFlagsEXT type,
	const VkDebugUtilsMessengerCallbackDataEXT* data,
	void* userData )
{
	(void)type;
	(void)userData;

	const char* level = "INFO";
	if( severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT )
	{
		level = "ERROR";
	}
	else if( severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT )
	{
		level = "WARN";
	}
	else if( severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT )
	{
		level = "VERBOSE";
	}

	if( data && data->pMessage )
	{
		std::fprintf( stderr, "Vulkan[%s]: %s\n", level, data->pMessage );
	}
	return VK_FALSE;
}

}

VulkanContext::VulkanContext()
{
}

VulkanContext::~VulkanContext()
{
	WaitIdle();
	DrainDeferred( std::numeric_limits<uint64_t>::max() );
	DestroyFrameContexts();
	DestroySwapchain();
	DestroyDevice();
	DestroyInstance();
}

ALResult VulkanContext::CreateInstance( bool enableValidation )
{
	VkApplicationInfo appInfo = {};
	appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	appInfo.pApplicationName = "Ithax";
	appInfo.applicationVersion = VK_MAKE_VERSION( 0, 1, 0 );
	appInfo.pEngineName = "TrinityAL";
	appInfo.engineVersion = VK_MAKE_VERSION( 4, 0, 2 );
	appInfo.apiVersion = VULKAN_API_VERSION;

	uint32_t instanceLayerCount = 0;
	VK_CHECK( vkEnumerateInstanceLayerProperties( &instanceLayerCount, nullptr ) );
	std::vector<VkLayerProperties> instanceLayers( instanceLayerCount );
	VK_CHECK( vkEnumerateInstanceLayerProperties( &instanceLayerCount, instanceLayers.data() ) );

	uint32_t instanceExtensionCount = 0;
	VK_CHECK( vkEnumerateInstanceExtensionProperties( nullptr, &instanceExtensionCount, nullptr ) );
	std::vector<VkExtensionProperties> instanceExtensions( instanceExtensionCount );
	VK_CHECK( vkEnumerateInstanceExtensionProperties( nullptr, &instanceExtensionCount, instanceExtensions.data() ) );

	std::vector<const char*> enabledLayers;
	if( enableValidation && HasLayer( instanceLayers, "VK_LAYER_KHRONOS_validation" ) )
	{
		enabledLayers.push_back( "VK_LAYER_KHRONOS_validation" );
		state.validationEnabled = true;
	}

	std::vector<const char*> enabledExtensions;
	if( HasExtension( instanceExtensions, VK_KHR_SURFACE_EXTENSION_NAME ) )
	{
		enabledExtensions.push_back( VK_KHR_SURFACE_EXTENSION_NAME );
	}
	if( HasExtension( instanceExtensions, VK_KHR_WIN32_SURFACE_EXTENSION_NAME ) )
	{
		enabledExtensions.push_back( VK_KHR_WIN32_SURFACE_EXTENSION_NAME );
	}
	if( HasExtension( instanceExtensions, VK_EXT_DEBUG_UTILS_EXTENSION_NAME ) )
	{
		enabledExtensions.push_back( VK_EXT_DEBUG_UTILS_EXTENSION_NAME );
	}

	VkInstanceCreateInfo createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	createInfo.pApplicationInfo = &appInfo;
	createInfo.enabledLayerCount = static_cast<uint32_t>( enabledLayers.size() );
	createInfo.ppEnabledLayerNames = enabledLayers.data();
	createInfo.enabledExtensionCount = static_cast<uint32_t>( enabledExtensions.size() );
	createInfo.ppEnabledExtensionNames = enabledExtensions.data();

	VkResult result = vkCreateInstance( &createInfo, nullptr, &state.instance );
	if( result != VK_SUCCESS )
	{
		CCP_AL_LOGERR( "vkCreateInstance failed: %d", int( result ) );
		return E_FAIL;
	}

	if( state.validationEnabled )
	{
		CreateDebugMessenger();
	}

	return S_OK;
}

ALResult VulkanContext::CreateDebugMessenger()
{
	VkDebugUtilsMessengerCreateInfoEXT createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
	createInfo.messageSeverity =
		VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
		VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
		VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
	createInfo.messageType =
		VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
		VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
		VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
	createInfo.pfnUserCallback = DebugCallback;

	auto createFn = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
		vkGetInstanceProcAddr( state.instance, "vkCreateDebugUtilsMessengerEXT" ) );
	if( !createFn )
	{
		return E_FAIL;
	}

	VkResult result = createFn( state.instance, &createInfo, nullptr, &state.debugMessenger );
	return MapVkResult( result );
}

void VulkanContext::DestroyDebugMessenger()
{
	if( state.debugMessenger == VK_NULL_HANDLE )
	{
		return;
	}
	auto destroyFn = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
		vkGetInstanceProcAddr( state.instance, "vkDestroyDebugUtilsMessengerEXT" ) );
	if( destroyFn )
	{
		destroyFn( state.instance, state.debugMessenger, nullptr );
	}
	state.debugMessenger = VK_NULL_HANDLE;
}

ALResult VulkanContext::CreateSurface( Tr2WindowHandle window )
{
	VkWin32SurfaceCreateInfoKHR createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
	createInfo.hwnd = window;
	createInfo.hinstance = GetModuleHandle( nullptr );

	VkResult result = vkCreateWin32SurfaceKHR( state.instance, &createInfo, nullptr, &state.surface );
	if( result != VK_SUCCESS )
	{
		}
	return MapVkResult( result );
}

void VulkanContext::DestroySurface()
{
	if( state.surface != VK_NULL_HANDLE )
	{
		vkDestroySurfaceKHR( state.instance, state.surface, nullptr );
		state.surface = VK_NULL_HANDLE;
	}
}

ALResult VulkanContext::PickPhysicalDevice( uint32_t adapter, bool requireSurface )
{
	uint32_t deviceCount = 0;
	VK_CHECK( vkEnumeratePhysicalDevices( state.instance, &deviceCount, nullptr ) );
	if( deviceCount == 0 )
	{
		CCP_AL_LOGERR( "No Vulkan physical devices found" );
		return E_FAIL;
	}

	std::vector<VkPhysicalDevice> devices( deviceCount );
	VK_CHECK( vkEnumeratePhysicalDevices( state.instance, &deviceCount, devices.data() ) );

	if( adapter >= deviceCount )
	{
		CCP_AL_LOGERR( "Adapter index %u out of range (device count %u)", adapter, deviceCount );
		return E_INVALIDARG;
	}

	VkPhysicalDevice candidate = devices[adapter];
	VkPhysicalDeviceProperties candidateProperties = {};
	vkGetPhysicalDeviceProperties( candidate, &candidateProperties );

	uint32_t extensionCount = 0;
	VK_CHECK( vkEnumerateDeviceExtensionProperties( candidate, nullptr, &extensionCount, nullptr ) );
	std::vector<VkExtensionProperties> availableExtensions( extensionCount );
	VK_CHECK( vkEnumerateDeviceExtensionProperties( candidate, nullptr, &extensionCount, availableExtensions.data() ) );

	for( const char* required : REQUIRED_DEVICE_EXTENSIONS )
	{
		if( !HasExtension( availableExtensions, required ) )
		{
			CCP_AL_LOGERR( "Device %s rejected: missing required extension %s", candidateProperties.deviceName, required ); return E_FAIL;
		}
	}

	VkPhysicalDeviceFeatures candidateFeatures = {};
	vkGetPhysicalDeviceFeatures( candidate, &candidateFeatures );

	VkPhysicalDeviceVulkan13Features vulkan13 = {};
	vulkan13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
	VkPhysicalDeviceVulkan12Features vulkan12 = {};
	vulkan12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
	VkPhysicalDeviceFeatures2 features2 = {};
	features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
	features2.pNext = &vulkan13;
	vulkan13.pNext = &vulkan12;
	vkGetPhysicalDeviceFeatures2( candidate, &features2 );

	VkPhysicalDeviceProperties2 properties2 = {};
	properties2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
	vkGetPhysicalDeviceProperties2( candidate, &properties2 );

	if( !vulkan13.dynamicRendering || !vulkan13.synchronization2 || !vulkan12.timelineSemaphore )
	{
		CCP_AL_LOGERR( "Device %s rejected: missing Vulkan 1.3 features", candidateProperties.deviceName ); return E_FAIL;
	}

	uint32_t queueFamilyCount = 0;
	vkGetPhysicalDeviceQueueFamilyProperties( candidate, &queueFamilyCount, nullptr );
	std::vector<VkQueueFamilyProperties> queueFamilies( queueFamilyCount );
	vkGetPhysicalDeviceQueueFamilyProperties( candidate, &queueFamilyCount, queueFamilies.data() );

	uint32_t graphicsFamily = VK_QUEUE_FAMILY_IGNORED;
	uint32_t presentFamily = VK_QUEUE_FAMILY_IGNORED;
	for( uint32_t i = 0; i < queueFamilyCount; ++i )
	{
		if( queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT )
		{
			graphicsFamily = i;
		}
	}

	if( graphicsFamily == VK_QUEUE_FAMILY_IGNORED )
	{
		CCP_AL_LOGERR( "Device %s rejected: no graphics queue family", candidateProperties.deviceName );
		return E_FAIL;
	}

	if( requireSurface )
	{
		VkBool32 presentSupported = VK_FALSE;
		VK_CHECK( vkGetPhysicalDeviceSurfaceSupportKHR( candidate, graphicsFamily, state.surface, &presentSupported ) );
		if( presentSupported )
		{
			presentFamily = graphicsFamily;
		}
		else
		{
			for( uint32_t i = 0; i < queueFamilyCount; ++i )
			{
				VK_CHECK( vkGetPhysicalDeviceSurfaceSupportKHR( candidate, i, state.surface, &presentSupported ) );
				if( presentSupported )
				{
					presentFamily = i;
					break;
				}
			}
		}

		if( presentFamily == VK_QUEUE_FAMILY_IGNORED )
		{
			CCP_AL_LOGERR( "Device %s rejected: no present queue family", candidateProperties.deviceName );
			return E_FAIL;
		}
	}
	else
	{
		presentFamily = graphicsFamily;
	}

	state.physicalDevice = candidate;
	state.properties = candidateProperties;
	state.features = candidateFeatures;
	state.features2 = features2;
	state.properties2 = properties2;
	state.vulkan13Features = vulkan13;
	state.vulkan12Features = vulkan12;
	state.graphicsFamily = graphicsFamily;
	state.presentFamily = presentFamily;
	vkGetPhysicalDeviceMemoryProperties( candidate, &state.memoryProperties );

	CCP_AL_LOG( "Selected Vulkan device: %s (api %u.%u.%u, driver %u.%u.%u)",
		candidateProperties.deviceName,
		VK_VERSION_MAJOR( candidateProperties.apiVersion ),
		VK_VERSION_MINOR( candidateProperties.apiVersion ),
		VK_VERSION_PATCH( candidateProperties.apiVersion ),
		VK_VERSION_MAJOR( candidateProperties.driverVersion ),
		VK_VERSION_MINOR( candidateProperties.driverVersion ),
		VK_VERSION_PATCH( candidateProperties.driverVersion ) );

	return S_OK;
}

ALResult VulkanContext::CreateLogicalDevice()
{
	uint32_t extensionCount = 0;
	VK_CHECK( vkEnumerateDeviceExtensionProperties( state.physicalDevice, nullptr, &extensionCount, nullptr ) );
	std::vector<VkExtensionProperties> availableExtensions( extensionCount );
	VK_CHECK( vkEnumerateDeviceExtensionProperties( state.physicalDevice, nullptr, &extensionCount, availableExtensions.data() ) );

	std::vector<const char*> enabledExtensions;
	for( const char* required : REQUIRED_DEVICE_EXTENSIONS )
	{
		enabledExtensions.push_back( required );
	}
	for( const char* optional : OPTIONAL_DEVICE_EXTENSIONS )
	{
		if( HasExtension( availableExtensions, optional ) )
		{
			enabledExtensions.push_back( optional );
		}
	}
	state.deviceFaultEnabled = HasExtension( availableExtensions, VK_EXT_DEVICE_FAULT_EXTENSION_NAME );
	state.swapchainMaintenance1 = HasExtension( availableExtensions, VK_EXT_SWAPCHAIN_MAINTENANCE_1_EXTENSION_NAME );

	VkPhysicalDeviceVulkan13Features vulkan13 = {};
	vulkan13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
	vulkan13.dynamicRendering = VK_TRUE;
	vulkan13.synchronization2 = VK_TRUE;

	VkPhysicalDeviceVulkan12Features vulkan12 = {};
	vulkan12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
	vulkan12.hostQueryReset = VK_TRUE;
	vulkan12.timelineSemaphore = VK_TRUE;

	// Point sizes above 1.0f (starfield) require the largePoints feature.
	VkPhysicalDeviceFeatures2 features2 = {};
	features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
	features2.features.largePoints = state.features.largePoints;

	VkPhysicalDeviceFaultFeaturesEXT faultFeatures = {};
	faultFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FAULT_FEATURES_EXT;
	faultFeatures.deviceFault = VK_TRUE;
	faultFeatures.deviceFaultVendorBinary = VK_FALSE;

	// Chain: features2 -> vulkan12 -> vulkan13 -> fault (optional).
	void* featureChain = &features2;
	features2.pNext = &vulkan12;
	vulkan12.pNext = &vulkan13;
	if( state.deviceFaultEnabled )
	{
		vulkan13.pNext = &faultFeatures;
	}

	float queuePriority = 1.0f;
	std::array<uint32_t, 2> uniqueFamilies = { state.graphicsFamily, state.presentFamily };
	std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
	for( uint32_t family : uniqueFamilies )
	{
		bool alreadyAdded = false;
		for( const auto& existing : queueCreateInfos )
		{
			if( existing.queueFamilyIndex == family )
			{
				alreadyAdded = true;
				break;
			}
		}
		if( alreadyAdded )
		{
			continue;
		}
		VkDeviceQueueCreateInfo queueInfo = {};
		queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queueInfo.queueFamilyIndex = family;
		queueInfo.queueCount = 1;
		queueInfo.pQueuePriorities = &queuePriority;
		queueCreateInfos.push_back( queueInfo );
	}

	VkDeviceCreateInfo createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	createInfo.pNext = featureChain;
	createInfo.queueCreateInfoCount = static_cast<uint32_t>( queueCreateInfos.size() );
	createInfo.pQueueCreateInfos = queueCreateInfos.data();
	createInfo.enabledExtensionCount = static_cast<uint32_t>( enabledExtensions.size() );
	createInfo.ppEnabledExtensionNames = enabledExtensions.data();

	VkResult result = vkCreateDevice( state.physicalDevice, &createInfo, nullptr, &state.device );
	if( result != VK_SUCCESS )
	{
		CCP_AL_LOGERR( "vkCreateDevice failed: %d", int( result ) );
		return E_FAIL;
	}

	vkGetDeviceQueue( state.device, state.graphicsFamily, 0, &state.graphicsQueue );
	vkGetDeviceQueue( state.device, state.presentFamily, 0, &state.presentQueue );

	state.timestampPeriod = state.properties.limits.timestampPeriod;
	state.timestampValidBits = state.properties.limits.timestampComputeAndGraphics;

	return S_OK;
}

ALResult VulkanContext::CreateAllocator()
{
	VmaAllocatorCreateInfo createInfo = {};
	createInfo.physicalDevice = state.physicalDevice;
	createInfo.device = state.device;
	createInfo.instance = state.instance;
	createInfo.vulkanApiVersion = VULKAN_API_VERSION;

	VkResult result = vmaCreateAllocator( &createInfo, &state.allocator );
	return MapVkResult( result );
}

ALResult VulkanContext::CreateDevice( uint32_t adapter, Tr2WindowHandle window, const Tr2PresentParametersAL& params )
{
	state.window = window;
	state.presentParameters = params;
	state.windowless = ( window == nullptr );
	state.deviceLostAdapter = adapter;

	if( state.surface == VK_NULL_HANDLE && !state.windowless )
	{
		CR_RETURN_HR( CreateSurface( window ) );
	}

	CR_RETURN_HR( PickPhysicalDevice( adapter, !state.windowless ) );
	CR_RETURN_HR( CreateLogicalDevice() );
	CR_RETURN_HR( CreateAllocator() );
	CR_RETURN_HR( CreatePipelineCache() );
	CR_RETURN_HR( CreateTimestampQueryPool() );

	if( !state.windowless )
	{
		CR_RETURN_HR( CreateSwapchain() );
	}

	CR_RETURN_HR( CreateFrameContexts() );

	return S_OK;
}

ALResult VulkanContext::QuerySurfaceCapabilities(
	VkSurfaceCapabilitiesKHR& caps,
	std::vector<VkSurfaceFormatKHR>& formats,
	std::vector<VkPresentModeKHR>& modes ) const
{
	VK_CHECK( vkGetPhysicalDeviceSurfaceCapabilitiesKHR( state.physicalDevice, state.surface, &caps ) );

	uint32_t formatCount = 0;
	VK_CHECK( vkGetPhysicalDeviceSurfaceFormatsKHR( state.physicalDevice, state.surface, &formatCount, nullptr ) );
	formats.resize( formatCount );
	VK_CHECK( vkGetPhysicalDeviceSurfaceFormatsKHR( state.physicalDevice, state.surface, &formatCount, formats.data() ) );

	uint32_t modeCount = 0;
	VK_CHECK( vkGetPhysicalDeviceSurfacePresentModesKHR( state.physicalDevice, state.surface, &modeCount, nullptr ) );
	modes.resize( modeCount );
	VK_CHECK( vkGetPhysicalDeviceSurfacePresentModesKHR( state.physicalDevice, state.surface, &modeCount, modes.data() ) );

	return S_OK;
}

VkSurfaceFormatKHR VulkanContext::SelectSurfaceFormat( const std::vector<VkSurfaceFormatKHR>& formats ) const
{
	for( const auto& format : formats )
	{
		if( format.format == VK_FORMAT_B8G8R8A8_UNORM &&
			format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR )
		{
			return format;
		}
	}
	for( const auto& format : formats )
	{
		if( format.format == VK_FORMAT_B8G8R8A8_SRGB &&
			format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR )
		{
			return format;
		}
	}
	return formats.empty() ? VkSurfaceFormatKHR{} : formats[0];
}

VkPresentModeKHR VulkanContext::SelectPresentMode( const std::vector<VkPresentModeKHR>& modes ) const
{
	VkPresentModeKHR preferred = VK_PRESENT_MODE_FIFO_KHR;
	switch( state.presentParameters.presentInterval )
	{
	case Tr2RenderContextEnum::PRESENT_INTERVAL_IMMEDIATE:
		preferred = VK_PRESENT_MODE_IMMEDIATE_KHR;
		break;
	case Tr2RenderContextEnum::PRESENT_INTERVAL_ONE:
	default:
		preferred = VK_PRESENT_MODE_FIFO_KHR;
		break;
	}

	for( const auto& mode : modes )
	{
		if( mode == preferred )
		{
			return mode;
		}
	}
	return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D VulkanContext::SelectExtent( const VkSurfaceCapabilitiesKHR& caps ) const
{
	if( caps.currentExtent.width != std::numeric_limits<uint32_t>::max() )
	{
		return caps.currentExtent;
	}

	VkExtent2D extent = {
		std::clamp( state.presentParameters.mode.width, caps.minImageExtent.width, caps.maxImageExtent.width ),
		std::clamp( state.presentParameters.mode.height, caps.minImageExtent.height, caps.maxImageExtent.height ),
	};
	return extent;
}

ALResult VulkanContext::CreateSwapchainInternal( VkSwapchainKHR oldSwapchain )
{
	VkSurfaceCapabilitiesKHR caps = {};
	std::vector<VkSurfaceFormatKHR> formats;
	std::vector<VkPresentModeKHR> modes;
	CR_RETURN_HR( QuerySurfaceCapabilities( caps, formats, modes ) );
	state.surfaceFormat = SelectSurfaceFormat( formats );
	state.presentMode = SelectPresentMode( modes );
	VkExtent2D extent = SelectExtent( caps );

	// Minimized windows report a zero extent. Suspend the swapchain until
	// the window is restored; do not create zero-sized resources.
	if( extent.width == 0 || extent.height == 0 )
	{
		state.swapchain.suspended = true;
		state.swapchain.extent = extent;
		state.swapchain.imageCount = 0;
		state.swapchain.images.clear();
		state.swapchain.imageViews.clear();
		if( oldSwapchain != VK_NULL_HANDLE )
		{
			vkDestroySwapchainKHR( state.device, oldSwapchain, nullptr );
		}
		state.swapchain.swapchain = VK_NULL_HANDLE;
		CCP_AL_LOG( "Swapchain suspended (zero extent)" );
		return S_OK;
	}
	state.swapchain.suspended = false;
	uint32_t imageCount = caps.minImageCount + 1;
	if( caps.maxImageCount > 0 && imageCount > caps.maxImageCount )
	{
		imageCount = caps.maxImageCount;
	}

	VkSwapchainCreateInfoKHR createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	createInfo.surface = state.surface;
	createInfo.minImageCount = imageCount;
	createInfo.imageFormat = state.surfaceFormat.format;
	createInfo.imageColorSpace = state.surfaceFormat.colorSpace;
	createInfo.imageExtent = extent;
	createInfo.imageArrayLayers = 1;
	createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
	createInfo.preTransform = caps.currentTransform;
	createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	createInfo.presentMode = state.presentMode;
	createInfo.clipped = VK_TRUE;
	createInfo.oldSwapchain = oldSwapchain;

	if( state.graphicsFamily != state.presentFamily )
	{
		std::array<uint32_t, 2> families = { state.graphicsFamily, state.presentFamily };
		createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
		createInfo.queueFamilyIndexCount = 2;
		createInfo.pQueueFamilyIndices = families.data();
	}
	else
	{
		createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
	}

	VkResult result = vkCreateSwapchainKHR( state.device, &createInfo, nullptr, &state.swapchain.swapchain );
	if( result != VK_SUCCESS )
	{
		CCP_AL_LOGERR( "vkCreateSwapchainKHR failed: %d", int( result ) );
		return E_FAIL;
	}
	state.swapchain.format = state.surfaceFormat.format;
	state.swapchain.colorSpace = state.surfaceFormat.colorSpace;
	state.swapchain.extent = extent;
	state.swapchain.currentImageIndex = 0;

	VK_CHECK( vkGetSwapchainImagesKHR( state.device, state.swapchain.swapchain, &state.swapchain.imageCount, nullptr ) );
	state.swapchain.images.resize( state.swapchain.imageCount );
	VK_CHECK( vkGetSwapchainImagesKHR( state.device, state.swapchain.swapchain, &state.swapchain.imageCount, state.swapchain.images.data() ) );
	state.swapchain.imageViews.resize( state.swapchain.imageCount );
	for( uint32_t i = 0; i < state.swapchain.imageCount; ++i )
	{
		VkImageViewCreateInfo viewInfo = {};
		viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image = state.swapchain.images[i];
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format = state.swapchain.format;
		viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		viewInfo.subresourceRange.baseMipLevel = 0;
		viewInfo.subresourceRange.levelCount = 1;
		viewInfo.subresourceRange.baseArrayLayer = 0;
		viewInfo.subresourceRange.layerCount = 1;
		VK_CHECK( vkCreateImageView( state.device, &viewInfo, nullptr, &state.swapchain.imageViews[i] ) );
	}
	CR_RETURN_HR( CreateDepthResources() );
	CCP_AL_LOG( "Swapchain created: %ux%u, %u images, format %d, present mode %d",
		extent.width, extent.height, imageCount, int( state.swapchain.format ), int( state.presentMode ) );

	return S_OK;
}

ALResult VulkanContext::CreateSwapchain()
{
	return CreateSwapchainInternal( VK_NULL_HANDLE );
}

ALResult VulkanContext::RecreateSwapchain()
{
	WaitIdle();
	VkSwapchainKHR oldSwapchain = state.swapchain.swapchain;
	DestroyDepthResources();
	for( auto& view : state.swapchain.imageViews )
	{
		vkDestroyImageView( state.device, view, nullptr );
	}
	state.swapchain.imageViews.clear();
	state.swapchain.images.clear();

	ALResult result = CreateSwapchainInternal( oldSwapchain );
	if( FAILED( result ) )
	{
		if( state.swapchain.swapchain != oldSwapchain )
		{
			vkDestroySwapchainKHR( state.device, oldSwapchain, nullptr );
		}
		state.swapchain.swapchain = VK_NULL_HANDLE;
		return result;
	}

	// The suspended path inside CreateSwapchainInternal already destroyed
	// the old swapchain; only destroy it here when a new one was created.
	if( state.swapchain.swapchain != VK_NULL_HANDLE && state.swapchain.swapchain != oldSwapchain )
	{
		vkDestroySwapchainKHR( state.device, oldSwapchain, nullptr );
	}
	return S_OK;
}

void VulkanContext::DestroySwapchain()
{
	DestroyDepthResources();
	for( auto& view : state.swapchain.imageViews )
	{
		vkDestroyImageView( state.device, view, nullptr );
	}
	state.swapchain.imageViews.clear();
	state.swapchain.images.clear();
	if( state.swapchain.swapchain != VK_NULL_HANDLE )
	{
		vkDestroySwapchainKHR( state.device, state.swapchain.swapchain, nullptr );
		state.swapchain.swapchain = VK_NULL_HANDLE;
	}
}

ALResult VulkanContext::CreateDepthResources()
{
	state.swapchain.depthFormat = FindDepthFormat();

	VkImageCreateInfo imageInfo = {};
	imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageInfo.imageType = VK_IMAGE_TYPE_2D;
	imageInfo.format = state.swapchain.depthFormat;
	imageInfo.extent = { state.swapchain.extent.width, state.swapchain.extent.height, 1 };
	imageInfo.mipLevels = 1;
	imageInfo.arrayLayers = 1;
	imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	VmaAllocationCreateInfo allocInfo = {};
	allocInfo.usage = VMA_MEMORY_USAGE_GPU_ONLY;

	VkResult result = vmaCreateImage( state.allocator, &imageInfo, &allocInfo,
		&state.swapchain.depthImage, &state.swapchain.depthAllocation, nullptr );
	if( result != VK_SUCCESS )
	{
		CCP_AL_LOGERR( "vmaCreateImage (depth) failed: %d", int( result ) );
		return E_FAIL;
	}
	state.swapchain.depthLayout = VK_IMAGE_LAYOUT_UNDEFINED;

	VkImageViewCreateInfo viewInfo = {};
	viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewInfo.image = state.swapchain.depthImage;
	viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	viewInfo.format = state.swapchain.depthFormat;
	viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
	viewInfo.subresourceRange.baseMipLevel = 0;
	viewInfo.subresourceRange.levelCount = 1;
	viewInfo.subresourceRange.baseArrayLayer = 0;
	viewInfo.subresourceRange.layerCount = 1;
	VK_CHECK( vkCreateImageView( state.device, &viewInfo, nullptr, &state.swapchain.depthView ) );

	return S_OK;
}

void VulkanContext::DestroyDepthResources()
{
	if( state.swapchain.depthView != VK_NULL_HANDLE )
	{
		vkDestroyImageView( state.device, state.swapchain.depthView, nullptr );
		state.swapchain.depthView = VK_NULL_HANDLE;
	}
	if( state.swapchain.depthImage != VK_NULL_HANDLE )
	{
		vmaDestroyImage( state.allocator, state.swapchain.depthImage, state.swapchain.depthAllocation );
		state.swapchain.depthImage = VK_NULL_HANDLE;
		state.swapchain.depthAllocation = VK_NULL_HANDLE;
	}
}

ALResult VulkanContext::CreateFrameContexts()
{
	for( uint32_t i = 0; i < VULKAN_FRAME_CONTEXT_COUNT; ++i )
	{
		VulkanFrameContext& frame = state.frames[i];

		VkCommandPoolCreateInfo poolInfo = {};
		poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
		poolInfo.queueFamilyIndex = state.graphicsFamily;
		VK_CHECK( vkCreateCommandPool( state.device, &poolInfo, nullptr, &frame.commandPool ) );

		VkCommandBufferAllocateInfo allocInfo = {};
		allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocInfo.commandPool = frame.commandPool;
		allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocInfo.commandBufferCount = 1;
		VK_CHECK( vkAllocateCommandBuffers( state.device, &allocInfo, &frame.commandBuffer ) );

		VkDescriptorPoolSize poolSizes[] = {
			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 64 },
			{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 64 },
			{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 64 },
			{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 64 },
			{ VK_DESCRIPTOR_TYPE_SAMPLER, 64 },
		};
		VkDescriptorPoolCreateInfo descriptorPoolInfo = {};
		descriptorPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		descriptorPoolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
		descriptorPoolInfo.maxSets = 64;
		descriptorPoolInfo.poolSizeCount = static_cast<uint32_t>( std::size( poolSizes ) );
		descriptorPoolInfo.pPoolSizes = poolSizes;
		VK_CHECK( vkCreateDescriptorPool( state.device, &descriptorPoolInfo, nullptr, &frame.descriptorPool ) );

		VkSemaphoreCreateInfo semaphoreInfo = {};
		semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
		VK_CHECK( vkCreateSemaphore( state.device, &semaphoreInfo, nullptr, &frame.acquireSemaphore ) );
		VK_CHECK( vkCreateSemaphore( state.device, &semaphoreInfo, nullptr, &frame.renderFinishedSemaphore ) );

		VkSemaphoreTypeCreateInfo timelineInfo = {};
		timelineInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
		timelineInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
		timelineInfo.initialValue = 0;
		semaphoreInfo.pNext = &timelineInfo;
		VK_CHECK( vkCreateSemaphore( state.device, &semaphoreInfo, nullptr, &frame.timelineSemaphore ) );

		VkFenceCreateInfo fenceInfo = {};
		fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
		VK_CHECK( vkCreateFence( state.device, &fenceInfo, nullptr, &frame.inFlightFence ) );
	}

	return S_OK;
}

void VulkanContext::DestroyFrameContexts()
{
	for( uint32_t i = 0; i < VULKAN_FRAME_CONTEXT_COUNT; ++i )
	{
		VulkanFrameContext& frame = state.frames[i];
		if( frame.inFlightFence != VK_NULL_HANDLE )
		{
			vkDestroyFence( state.device, frame.inFlightFence, nullptr );
			frame.inFlightFence = VK_NULL_HANDLE;
		}
		if( frame.timelineSemaphore != VK_NULL_HANDLE )
		{
			vkDestroySemaphore( state.device, frame.timelineSemaphore, nullptr );
			frame.timelineSemaphore = VK_NULL_HANDLE;
		}
		if( frame.renderFinishedSemaphore != VK_NULL_HANDLE )
		{
			vkDestroySemaphore( state.device, frame.renderFinishedSemaphore, nullptr );
			frame.renderFinishedSemaphore = VK_NULL_HANDLE;
		}
		if( frame.acquireSemaphore != VK_NULL_HANDLE )
		{
			vkDestroySemaphore( state.device, frame.acquireSemaphore, nullptr );
			frame.acquireSemaphore = VK_NULL_HANDLE;
		}
		if( frame.descriptorPool != VK_NULL_HANDLE )
		{
			vkDestroyDescriptorPool( state.device, frame.descriptorPool, nullptr );
			frame.descriptorPool = VK_NULL_HANDLE;
		}
		if( frame.commandPool != VK_NULL_HANDLE )
		{
			vkDestroyCommandPool( state.device, frame.commandPool, nullptr );
			frame.commandPool = VK_NULL_HANDLE;
		}
	}
}

ALResult VulkanContext::CreateTimestampQueryPool()
{
	VkQueryPoolCreateInfo createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
	createInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
	createInfo.queryCount = VULKAN_FRAME_CONTEXT_COUNT * 2;

	VkResult result = vkCreateQueryPool( state.device, &createInfo, nullptr, &state.timestampQueryPool );
	return MapVkResult( result );
}

void VulkanContext::DestroyTimestampQueryPool()
{
	if( state.timestampQueryPool != VK_NULL_HANDLE )
	{
		vkDestroyQueryPool( state.device, state.timestampQueryPool, nullptr );
		state.timestampQueryPool = VK_NULL_HANDLE;
	}
}

ALResult VulkanContext::CreatePipelineCache()
{
	VkPipelineCacheCreateInfo createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;

	VkResult result = vkCreatePipelineCache( state.device, &createInfo, nullptr, &state.pipelineCache );
	return MapVkResult( result );
}

void VulkanContext::DestroyPipelineCache()
{
	if( state.pipelineCache != VK_NULL_HANDLE )
	{
		vkDestroyPipelineCache( state.device, state.pipelineCache, nullptr );
		state.pipelineCache = VK_NULL_HANDLE;
	}
}

void VulkanContext::Destroy()
{
	WaitIdle();
	DrainDeferred( std::numeric_limits<uint64_t>::max() );
	DestroyFrameContexts();
	DestroySwapchain();
	DestroyDevice();
	DestroyInstance();
}

void VulkanContext::DestroyDevice()
{
	if( state.device == VK_NULL_HANDLE )
	{
		return;
	}

	DestroyTimestampQueryPool();
	DestroyPipelineCache();

	if( state.allocator != VK_NULL_HANDLE )
	{
		vmaDestroyAllocator( state.allocator );
		state.allocator = VK_NULL_HANDLE;
	}

	vkDestroyDevice( state.device, nullptr );
	state.device = VK_NULL_HANDLE;
	state.physicalDevice = VK_NULL_HANDLE;
	state.graphicsQueue = VK_NULL_HANDLE;
	state.presentQueue = VK_NULL_HANDLE;
}

void VulkanContext::DestroyInstance()
{
	DestroyDebugMessenger();
	if( state.surface != VK_NULL_HANDLE )
	{
		vkDestroySurfaceKHR( state.instance, state.surface, nullptr );
		state.surface = VK_NULL_HANDLE;
	}
	if( state.instance != VK_NULL_HANDLE )
	{
		vkDestroyInstance( state.instance, nullptr );
		state.instance = VK_NULL_HANDLE;
	}
}

ALResult VulkanContext::BeginFrame()
{
	VulkanFrameContext& frame = state.frames[state.frameIndex];

	if( state.deviceLost )
	{
		CCP_AL_LOGERR( "BeginFrame called after VK_ERROR_DEVICE_LOST" );
		return E_DEVICELOST;
	}

	// A surface loss reported by a prior present is deferred here so the
	// presented image can be observed first; recreate the surface before
	// any surface-dependent call in this frame.
	if( !state.surfaceLossReason.empty() && state.swapchain.swapchain != VK_NULL_HANDLE )
	{
		CCP_AL_LOG( "Recreating surface deferred from present (%s)", state.surfaceLossReason.c_str() );
		CR_RETURN_HR( RecreateSurface() );
	}

	VK_CHECK( vkWaitForFences( state.device, 1, &frame.inFlightFence, VK_TRUE, std::numeric_limits<uint64_t>::max() ) );

	uint64_t completedValue = 0;
	VK_CHECK( vkGetSemaphoreCounterValue( state.device, frame.timelineSemaphore, &completedValue ) );
	DrainDeferred( completedValue );

	if( state.swapchain.suspended )
	{
		// Window is minimized; re-check the surface extent without a busy
		// loop. The caller retries on the next frame.
		VkSurfaceCapabilitiesKHR caps = {};
		VkResult capsResult = vkGetPhysicalDeviceSurfaceCapabilitiesKHR( state.physicalDevice, state.surface, &caps );
		if( capsResult == VK_ERROR_SURFACE_LOST_KHR )
		{
			state.surfaceLossReason = "surface caps returned VK_ERROR_SURFACE_LOST_KHR";
			CCP_AL_LOG( "Surface lost while suspended; recreating surface" );
			CR_RETURN_HR( RecreateSurface() );
			if( state.swapchain.suspended )
			{
				VK_CHECK( vkResetCommandPool( state.device, frame.commandPool, 0 ) );
				VkCommandBufferBeginInfo beginInfo = {};
				beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
				beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
				VK_CHECK( vkBeginCommandBuffer( frame.commandBuffer, &beginInfo ) );
				return S_OK;
			}
		}
		else if( capsResult == VK_SUCCESS && caps.currentExtent.width != 0 && caps.currentExtent.height != 0 )
		{
			CR_RETURN_HR( RecreateSwapchain() );
			if( state.swapchain.suspended )
			{
				// Still suspended (extent raced back to zero); keep the
				// command buffer recording and skip submission.
				VK_CHECK( vkResetCommandPool( state.device, frame.commandPool, 0 ) );
				VkCommandBufferBeginInfo beginInfo = {};
				beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
				beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
				VK_CHECK( vkBeginCommandBuffer( frame.commandBuffer, &beginInfo ) );
				return S_OK;
			}
		}
		else
		{
			// Keep the command buffer in a valid recording state so the
			// scene can record commands; they are never submitted while
			// suspended.
			VK_CHECK( vkResetCommandPool( state.device, frame.commandPool, 0 ) );
			VkCommandBufferBeginInfo beginInfo = {};
			beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
			beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
			VK_CHECK( vkBeginCommandBuffer( frame.commandBuffer, &beginInfo ) );
			return S_OK;
		}
	}

	VkResult acquireResult = vkAcquireNextImageKHR( state.device, state.swapchain.swapchain,
		std::numeric_limits<uint64_t>::max(), frame.acquireSemaphore, VK_NULL_HANDLE, &state.swapchain.currentImageIndex );

	if( acquireResult == VK_ERROR_OUT_OF_DATE_KHR || acquireResult == VK_SUBOPTIMAL_KHR )
	{
		CR_RETURN_HR( RecreateSwapchain() );
		if( state.swapchain.suspended )
		{
			return S_OK;
		}
		acquireResult = vkAcquireNextImageKHR( state.device, state.swapchain.swapchain,
			std::numeric_limits<uint64_t>::max(), frame.acquireSemaphore, VK_NULL_HANDLE, &state.swapchain.currentImageIndex );
	}
	if( acquireResult == VK_ERROR_SURFACE_LOST_KHR )
	{
		state.surfaceLossReason = "acquire returned VK_ERROR_SURFACE_LOST_KHR";
		CCP_AL_LOG( "Surface lost on acquire; recreating surface" );
		CR_RETURN_HR( RecreateSurface() );
		if( state.swapchain.suspended )
		{
			return S_OK;
		}
		acquireResult = vkAcquireNextImageKHR( state.device, state.swapchain.swapchain,
			std::numeric_limits<uint64_t>::max(), frame.acquireSemaphore, VK_NULL_HANDLE, &state.swapchain.currentImageIndex );
	}
	if( acquireResult == VK_ERROR_DEVICE_LOST )
	{
		state.deviceLost = true;
		CCP_AL_LOGERR( "vkAcquireNextImageKHR returned VK_ERROR_DEVICE_LOST" );
		GatherDeviceFaultData();
		return E_DEVICELOST;
	}
	if( acquireResult != VK_SUCCESS )
	{
		CCP_AL_LOGERR( "vkAcquireNextImageKHR failed: %d", int( acquireResult ) );
		return E_FAIL;
	}

	VK_CHECK( vkResetFences( state.device, 1, &frame.inFlightFence ) );
	VK_CHECK( vkResetCommandPool( state.device, frame.commandPool, 0 ) );

	VkCommandBufferBeginInfo beginInfo = {};
	beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	VK_CHECK( vkBeginCommandBuffer( frame.commandBuffer, &beginInfo ) );

	return S_OK;
}

ALResult VulkanContext::EndFrame()
{
	VulkanFrameContext& frame = state.frames[state.frameIndex];

	if( state.swapchain.suspended )
	{
		// Discard the recorded commands; nothing is submitted while
		// suspended.
		VK_CHECK( vkResetCommandPool( state.device, frame.commandPool, 0 ) );
		state.frameNumber++;
		return S_OK;
	}

	VK_CHECK( vkEndCommandBuffer( frame.commandBuffer ) );

	VkPipelineStageFlags2 waitStage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
	VkSemaphoreSubmitInfo waitSemaphore = {};
	waitSemaphore.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
	waitSemaphore.semaphore = frame.acquireSemaphore;
	waitSemaphore.stageMask = waitStage;

	VkSemaphoreSubmitInfo signalSemaphores[2] = {};
	signalSemaphores[0].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
	signalSemaphores[0].semaphore = frame.renderFinishedSemaphore;
	signalSemaphores[0].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

	signalSemaphores[1].sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
	signalSemaphores[1].semaphore = frame.timelineSemaphore;
	signalSemaphores[1].value = ++frame.timelineValue;
	signalSemaphores[1].stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

	VkCommandBufferSubmitInfo commandInfo = {};
	commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
	commandInfo.commandBuffer = frame.commandBuffer;

	VkSubmitInfo2 submitInfo = {};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
	submitInfo.waitSemaphoreInfoCount = 1;
	submitInfo.pWaitSemaphoreInfos = &waitSemaphore;
	submitInfo.commandBufferInfoCount = 1;
	submitInfo.pCommandBufferInfos = &commandInfo;
	submitInfo.signalSemaphoreInfoCount = 2;
	submitInfo.pSignalSemaphoreInfos = signalSemaphores;

	VkResult result = vkQueueSubmit2( state.graphicsQueue, 1, &submitInfo, frame.inFlightFence );
	if( result == VK_ERROR_DEVICE_LOST )
	{
		state.deviceLost = true;
		CCP_AL_LOGERR( "vkQueueSubmit2 returned VK_ERROR_DEVICE_LOST" );
		GatherDeviceFaultData();
		return E_DEVICELOST;
	}
	if( result != VK_SUCCESS )
	{
		CCP_AL_LOGERR( "vkQueueSubmit2 failed: %d", int( result ) );
		return E_FAIL;
	}

	state.frameNumber++;
	return S_OK;
}

ALResult VulkanContext::Present()
{
	if( state.swapchain.suspended )
	{
		state.frameIndex = ( state.frameIndex + 1 ) % VULKAN_FRAME_CONTEXT_COUNT;
		return S_OK;
	}

	VulkanFrameContext& frame = state.frames[state.frameIndex];

	VkPresentInfoKHR presentInfo = {};
	presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores = &frame.renderFinishedSemaphore;
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = &state.swapchain.swapchain;
	presentInfo.pImageIndices = &state.swapchain.currentImageIndex;

	VkResult result = vkQueuePresentKHR( state.presentQueue, &presentInfo );
	state.renderedFrameNumber = state.frameNumber;
	state.lastPresentedImageIndex = state.swapchain.currentImageIndex;
	if( result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR )
	{
		// Defer the swapchain recreation to the next acquire; recreating
		// here would destroy the just-presented image before a readback
		// can observe it.
		CCP_AL_LOG( "Present returned %d; swapchain recreation deferred to next acquire", int( result ) );
	}
	else if( result == VK_ERROR_SURFACE_LOST_KHR )
	{
		state.surfaceLossReason = "present returned VK_ERROR_SURFACE_LOST_KHR";
		CCP_AL_LOG( "Surface lost on present; recreation deferred to next acquire" );
	}
	else if( result == VK_ERROR_DEVICE_LOST )
	{
		state.deviceLost = true;
		CCP_AL_LOGERR( "vkQueuePresentKHR returned VK_ERROR_DEVICE_LOST" );
		GatherDeviceFaultData();
		return E_DEVICELOST;
	}
	else if( result != VK_SUCCESS )
	{
		CCP_AL_LOGERR( "vkQueuePresentKHR failed: %d", int( result ) );
		return E_FAIL;
	}

	state.frameIndex = ( state.frameIndex + 1 ) % VULKAN_FRAME_CONTEXT_COUNT;
	return S_OK;
}

void VulkanContext::GatherDeviceFaultData()
{
	if( !state.deviceFaultEnabled || state.device == VK_NULL_HANDLE )
	{
		return;
	}

	auto getFaultDataFn = reinterpret_cast<PFN_vkGetDeviceFaultInfoEXT>(
		vkGetDeviceProcAddr( state.device, "vkGetDeviceFaultInfoEXT" ) );
	if( getFaultDataFn == nullptr )
	{
		return;
	}

	VkDeviceFaultCountsEXT faultCounts = {};
	faultCounts.sType = VK_STRUCTURE_TYPE_DEVICE_FAULT_COUNTS_EXT;
	VkResult result = getFaultDataFn( state.device, &faultCounts, nullptr );
	if( result != VK_SUCCESS || ( faultCounts.addressInfoCount == 0 &&
		faultCounts.vendorInfoCount == 0 && faultCounts.vendorBinarySize == 0 ) )
	{
		CCP_AL_LOG( "vkGetDeviceFaultInfoEXT reported no fault records (0x%x)",
			unsigned( result ) );
		return;
	}

	std::vector<VkDeviceFaultAddressInfoKHR> addresses( faultCounts.addressInfoCount );
	std::vector<VkDeviceFaultVendorInfoKHR> vendors( faultCounts.vendorInfoCount );
	std::vector<uint8_t> vendorBinary( faultCounts.vendorBinarySize );

	VkDeviceFaultInfoEXT faultInfo = {};
	faultInfo.sType = VK_STRUCTURE_TYPE_DEVICE_FAULT_INFO_EXT;
	faultInfo.pAddressInfos = addresses.data();
	faultInfo.pVendorInfos = vendors.data();
	faultInfo.pVendorBinaryData = vendorBinary.empty() ? nullptr : vendorBinary.data();

	result = getFaultDataFn( state.device, &faultCounts, &faultInfo );
	if( result != VK_SUCCESS )
	{
		CCP_AL_LOGERR( "vkGetDeviceFaultInfoEXT failed: 0x%x", unsigned( result ) );
		return;
	}

	CCP_AL_LOG( "Device fault: description='%s' addressCount=%u vendorCount=%u",
		faultInfo.description,
		unsigned( faultCounts.addressInfoCount ),
		unsigned( faultCounts.vendorInfoCount ) );
	for( uint32_t i = 0; i < faultCounts.addressInfoCount; ++i )
	{
		CCP_AL_LOG( "  fault address %u: type=%d reported=%llu precision=%llu",
			i, int( addresses[i].addressType ),
			static_cast<unsigned long long>( addresses[i].reportedAddress ),
			static_cast<unsigned long long>( addresses[i].addressPrecision ) );
	}
	for( uint32_t i = 0; i < faultCounts.vendorInfoCount; ++i )
	{
		CCP_AL_LOG( "  vendor fault %u: %s, code=%u data=%llu",
			i,
			vendors[i].description,
			unsigned( vendors[i].vendorFaultCode ),
			static_cast<unsigned long long>( vendors[i].vendorFaultData ) );
	}
}

VkCommandBuffer VulkanContext::GetCurrentCommandBuffer() const
{
	return state.frames[state.frameIndex].commandBuffer;
}

void SetVulkanObjectName( VkDevice device, uint64_t objectHandle, VkObjectType objectType, const char* name )
{
	if( device == VK_NULL_HANDLE || objectHandle == 0 || name == nullptr )
	{
		return;
	}
	auto setFn = reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(
		vkGetDeviceProcAddr( device, "vkSetDebugUtilsObjectNameEXT" ) );
	if( setFn == nullptr )
	{
		return;
	}
	VkDebugUtilsObjectNameInfoEXT nameInfo = {};
	nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
	nameInfo.objectType = objectType;
	nameInfo.objectHandle = objectHandle;
	nameInfo.pObjectName = name;
	setFn( device, &nameInfo );
}

VkDescriptorPool VulkanContext::GetCurrentDescriptorPool() const
{
	return state.frames[state.frameIndex].descriptorPool;
}

uint64_t VulkanContext::GetCurrentTimelineValue() const
{
	return state.frames[state.frameIndex].timelineValue;
}

uint32_t VulkanContext::GetCurrentImageIndex() const
{
	return state.swapchain.currentImageIndex;
}

uint32_t VulkanContext::GetLastPresentedImageIndex() const
{
	return state.lastPresentedImageIndex;
}

void VulkanContext::Retire( const VulkanDeferredDestroy& entry )
{
	if( m_deferred.size() >= MAX_DEFERRED_ENTRIES )
	{
		WaitIdle();
		DrainDeferred( std::numeric_limits<uint64_t>::max() );
	}
	m_deferred.push_back( entry );
}

void VulkanContext::DrainDeferred( uint64_t completedValue )
{
	auto it = m_deferred.begin();
	while( it != m_deferred.end() )
	{
		if( it->timelineValue > completedValue )
		{
			++it;
			continue;
		}
		if( it->imageView != VK_NULL_HANDLE )
		{
			vkDestroyImageView( it->device, it->imageView, nullptr );
		}
		if( it->image != VK_NULL_HANDLE )
		{
			vmaDestroyImage( it->allocator, it->image, it->allocation );
		}
		if( it->buffer != VK_NULL_HANDLE )
		{
			vmaDestroyBuffer( it->allocator, it->buffer, it->allocation );
		}
		if( it->descriptorSetLayout != VK_NULL_HANDLE )
		{
			vkDestroyDescriptorSetLayout( it->device, it->descriptorSetLayout, nullptr );
		}
		if( it->pipelineLayout != VK_NULL_HANDLE )
		{
			vkDestroyPipelineLayout( it->device, it->pipelineLayout, nullptr );
		}
		if( it->pipeline != VK_NULL_HANDLE )
		{
			vkDestroyPipeline( it->device, it->pipeline, nullptr );
		}
		if( it->sampler != VK_NULL_HANDLE )
		{
			vkDestroySampler( it->device, it->sampler, nullptr );
		}
		if( it->queryPool != VK_NULL_HANDLE )
		{
			vkDestroyQueryPool( it->device, it->queryPool, nullptr );
		}
		if( it->swapchain != VK_NULL_HANDLE )
		{
			vkDestroySwapchainKHR( it->device, it->swapchain, nullptr );
		}
		if( it->semaphore != VK_NULL_HANDLE )
		{
			vkDestroySemaphore( it->device, it->semaphore, nullptr );
		}
		if( it->fence != VK_NULL_HANDLE )
		{
			vkDestroyFence( it->device, it->fence, nullptr );
		}
		if( it->commandPool != VK_NULL_HANDLE )
		{
			vkDestroyCommandPool( it->device, it->commandPool, nullptr );
		}
		it = m_deferred.erase( it );
	}
}

void VulkanContext::WaitIdle()
{
	if( state.device != VK_NULL_HANDLE )
	{
		vkDeviceWaitIdle( state.device );
	}
}

VkFormat VulkanContext::FindDepthFormat() const
{
	return FindSupportedFormat(
		{ VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT, VK_FORMAT_D24_UNORM_S8_UINT },
		VK_IMAGE_TILING_OPTIMAL,
		VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT );
}

VkFormat VulkanContext::FindSupportedFormat(
	const std::vector<VkFormat>& candidates,
	VkImageTiling tiling,
	VkFormatFeatureFlags features ) const
{
	for( VkFormat format : candidates )
	{
		VkFormatProperties props = {};
		vkGetPhysicalDeviceFormatProperties( state.physicalDevice, format, &props );
		if( tiling == VK_IMAGE_TILING_LINEAR && ( props.linearTilingFeatures & features ) == features )
		{
			return format;
		}
		if( tiling == VK_IMAGE_TILING_OPTIMAL && ( props.optimalTilingFeatures & features ) == features )
		{
			return format;
		}
	}
	CCP_AL_LOGERR( "No supported format found for requested features" );
	return VK_FORMAT_UNDEFINED;
}

uint32_t VulkanContext::FindMemoryType( uint32_t typeFilter, VkMemoryPropertyFlags properties ) const
{
	for( uint32_t i = 0; i < state.memoryProperties.memoryTypeCount; ++i )
	{
		if( ( typeFilter & ( 1u << i ) ) &&
			( state.memoryProperties.memoryTypes[i].propertyFlags & properties ) == properties )
		{
			return i;
		}
	}
	CCP_AL_LOGERR( "No memory type found for requested properties" );
	return VK_MAX_MEMORY_TYPES;
}

ALResult MapVkResult( VkResult result )
{
	switch( result )
	{
	case VK_SUCCESS:
	case VK_SUBOPTIMAL_KHR:
		return S_OK;
	case VK_ERROR_OUT_OF_HOST_MEMORY:
	case VK_ERROR_OUT_OF_DEVICE_MEMORY:
		return E_OUTOFMEMORY;
	case VK_ERROR_DEVICE_LOST:
		return E_DEVICELOST;
	case VK_ERROR_SURFACE_LOST_KHR:
		return E_INVALIDCALL;
	case VK_ERROR_OUT_OF_DATE_KHR:
		return E_INVALIDCALL;
	default:
		return E_FAIL;
	}
}

ALResult VulkanContext::RecreateSurface()
{
	if( state.windowless || state.window == nullptr )
	{
		return E_INVALIDCALL;
	}

	// The deferred-present reason is consumed here; even if the recreated
	// swapchain is immediately suspended (window minimized), the surface
	// itself is fresh and must not be recreated again next frame.
	state.surfaceLossReason.clear();

	WaitIdle();
	DestroySwapchain();
	DestroySurface();

	CR_RETURN_HR( CreateSurface( state.window ) );
	CR_RETURN_HR( PickPhysicalDevice( state.deviceLostAdapter, true ) );
	CR_RETURN_HR( RecreateSwapchain() );
	if( state.swapchain.suspended )
	{
		return S_OK;
	}

	CCP_AL_LOG( "Surface recreated after VK_ERROR_SURFACE_LOST_KHR" );
	return S_OK;
}

VkFormat ConvertPixelFormat( Tr2RenderContextEnum::PixelFormat format )
{
	using namespace Tr2RenderContextEnum;
	switch( format )
	{
	case PIXEL_FORMAT_R32G32B32A32_TYPELESS: return VK_FORMAT_R32G32B32A32_SFLOAT;
	case PIXEL_FORMAT_R32G32B32A32_FLOAT: return VK_FORMAT_R32G32B32A32_SFLOAT;
	case PIXEL_FORMAT_R32G32B32A32_UINT: return VK_FORMAT_R32G32B32A32_UINT;
	case PIXEL_FORMAT_R32G32B32A32_SINT: return VK_FORMAT_R32G32B32A32_SINT;
	case PIXEL_FORMAT_R32G32B32_TYPELESS: return VK_FORMAT_R32G32B32_SFLOAT;
	case PIXEL_FORMAT_R32G32B32_FLOAT: return VK_FORMAT_R32G32B32_SFLOAT;
	case PIXEL_FORMAT_R32G32B32_UINT: return VK_FORMAT_R32G32B32_UINT;
	case PIXEL_FORMAT_R32G32B32_SINT: return VK_FORMAT_R32G32B32_SINT;
	case PIXEL_FORMAT_R16G16B16A16_TYPELESS: return VK_FORMAT_R16G16B16A16_SFLOAT;
	case PIXEL_FORMAT_R16G16B16A16_FLOAT: return VK_FORMAT_R16G16B16A16_SFLOAT;
	case PIXEL_FORMAT_R16G16B16A16_UNORM: return VK_FORMAT_R16G16B16A16_UNORM;
	case PIXEL_FORMAT_R16G16B16A16_UINT: return VK_FORMAT_R16G16B16A16_UINT;
	case PIXEL_FORMAT_R16G16B16A16_SNORM: return VK_FORMAT_R16G16B16A16_SNORM;
	case PIXEL_FORMAT_R16G16B16A16_SINT: return VK_FORMAT_R16G16B16A16_SINT;
	case PIXEL_FORMAT_R32G32_TYPELESS: return VK_FORMAT_R32G32_SFLOAT;
	case PIXEL_FORMAT_R32G32_FLOAT: return VK_FORMAT_R32G32_SFLOAT;
	case PIXEL_FORMAT_R32G32_UINT: return VK_FORMAT_R32G32_UINT;
	case PIXEL_FORMAT_R32G32_SINT: return VK_FORMAT_R32G32_SINT;
	case PIXEL_FORMAT_R10G10B10A2_TYPELESS: return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
	case PIXEL_FORMAT_R10G10B10A2_UNORM: return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
	case PIXEL_FORMAT_R10G10B10A2_UINT: return VK_FORMAT_A2B10G10R10_UINT_PACK32;
	case PIXEL_FORMAT_R11G11B10_FLOAT: return VK_FORMAT_B10G11R11_UFLOAT_PACK32;
	case PIXEL_FORMAT_R8G8B8A8_TYPELESS: return VK_FORMAT_R8G8B8A8_UNORM;
	case PIXEL_FORMAT_R8G8B8A8_UNORM: return VK_FORMAT_R8G8B8A8_UNORM;
	case PIXEL_FORMAT_R8G8B8A8_UNORM_SRGB: return VK_FORMAT_R8G8B8A8_SRGB;
	case PIXEL_FORMAT_R8G8B8A8_UINT: return VK_FORMAT_R8G8B8A8_UINT;
	case PIXEL_FORMAT_R8G8B8A8_SNORM: return VK_FORMAT_R8G8B8A8_SNORM;
	case PIXEL_FORMAT_R8G8B8A8_SINT: return VK_FORMAT_R8G8B8A8_SINT;
	case PIXEL_FORMAT_R16G16_TYPELESS: return VK_FORMAT_R16G16_SFLOAT;
	case PIXEL_FORMAT_R16G16_FLOAT: return VK_FORMAT_R16G16_SFLOAT;
	case PIXEL_FORMAT_R16G16_UNORM: return VK_FORMAT_R16G16_UNORM;
	case PIXEL_FORMAT_R16G16_UINT: return VK_FORMAT_R16G16_UINT;
	case PIXEL_FORMAT_R16G16_SNORM: return VK_FORMAT_R16G16_SNORM;
	case PIXEL_FORMAT_R16G16_SINT: return VK_FORMAT_R16G16_SINT;
	case PIXEL_FORMAT_R32_TYPELESS: return VK_FORMAT_R32_SFLOAT;
	case PIXEL_FORMAT_D32_FLOAT: return VK_FORMAT_D32_SFLOAT;
	case PIXEL_FORMAT_R32_FLOAT: return VK_FORMAT_R32_SFLOAT;
	case PIXEL_FORMAT_R32_UINT: return VK_FORMAT_R32_UINT;
	case PIXEL_FORMAT_R32_SINT: return VK_FORMAT_R32_SINT;
	case PIXEL_FORMAT_R24G8_TYPELESS: return VK_FORMAT_D24_UNORM_S8_UINT;
	case PIXEL_FORMAT_D24_UNORM_S8_UINT: return VK_FORMAT_D24_UNORM_S8_UINT;
	case PIXEL_FORMAT_R8G8_TYPELESS: return VK_FORMAT_R8G8_UNORM;
	case PIXEL_FORMAT_R8G8_UNORM: return VK_FORMAT_R8G8_UNORM;
	case PIXEL_FORMAT_R8G8_UINT: return VK_FORMAT_R8G8_UINT;
	case PIXEL_FORMAT_R8G8_SNORM: return VK_FORMAT_R8G8_SNORM;
	case PIXEL_FORMAT_R8G8_SINT: return VK_FORMAT_R8G8_SINT;
	case PIXEL_FORMAT_R16_TYPELESS: return VK_FORMAT_R16_SFLOAT;
	case PIXEL_FORMAT_R16_FLOAT: return VK_FORMAT_R16_SFLOAT;
	case PIXEL_FORMAT_D16_UNORM: return VK_FORMAT_D16_UNORM;
	case PIXEL_FORMAT_R16_UNORM: return VK_FORMAT_R16_UNORM;
	case PIXEL_FORMAT_R16_UINT: return VK_FORMAT_R16_UINT;
	case PIXEL_FORMAT_R16_SNORM: return VK_FORMAT_R16_SNORM;
	case PIXEL_FORMAT_R16_SINT: return VK_FORMAT_R16_SINT;
	case PIXEL_FORMAT_R8_TYPELESS: return VK_FORMAT_R8_UNORM;
	case PIXEL_FORMAT_R8_UNORM: return VK_FORMAT_R8_UNORM;
	case PIXEL_FORMAT_R8_UINT: return VK_FORMAT_R8_UINT;
	case PIXEL_FORMAT_R8_SNORM: return VK_FORMAT_R8_SNORM;
	case PIXEL_FORMAT_R8_SINT: return VK_FORMAT_R8_SINT;
	case PIXEL_FORMAT_A8_UNORM: return VK_FORMAT_R8_UNORM;
	case PIXEL_FORMAT_BC1_TYPELESS: return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
	case PIXEL_FORMAT_BC1_UNORM: return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
	case PIXEL_FORMAT_BC1_UNORM_SRGB: return VK_FORMAT_BC1_RGBA_SRGB_BLOCK;
	case PIXEL_FORMAT_BC2_TYPELESS: return VK_FORMAT_BC2_UNORM_BLOCK;
	case PIXEL_FORMAT_BC2_UNORM: return VK_FORMAT_BC2_UNORM_BLOCK;
	case PIXEL_FORMAT_BC2_UNORM_SRGB: return VK_FORMAT_BC2_SRGB_BLOCK;
	case PIXEL_FORMAT_BC3_TYPELESS: return VK_FORMAT_BC3_UNORM_BLOCK;
	case PIXEL_FORMAT_BC3_UNORM: return VK_FORMAT_BC3_UNORM_BLOCK;
	case PIXEL_FORMAT_BC3_UNORM_SRGB: return VK_FORMAT_BC3_SRGB_BLOCK;
	case PIXEL_FORMAT_BC4_TYPELESS: return VK_FORMAT_BC4_UNORM_BLOCK;
	case PIXEL_FORMAT_BC4_UNORM: return VK_FORMAT_BC4_UNORM_BLOCK;
	case PIXEL_FORMAT_BC4_SNORM: return VK_FORMAT_BC4_SNORM_BLOCK;
	case PIXEL_FORMAT_BC5_TYPELESS: return VK_FORMAT_BC5_UNORM_BLOCK;
	case PIXEL_FORMAT_BC5_UNORM: return VK_FORMAT_BC5_UNORM_BLOCK;
	case PIXEL_FORMAT_BC5_SNORM: return VK_FORMAT_BC5_SNORM_BLOCK;
	case PIXEL_FORMAT_B5G6R5_UNORM: return VK_FORMAT_B5G6R5_UNORM_PACK16;
	case PIXEL_FORMAT_B5G5R5A1_UNORM: return VK_FORMAT_B5G5R5A1_UNORM_PACK16;
	case PIXEL_FORMAT_B8G8R8A8_UNORM: return VK_FORMAT_B8G8R8A8_UNORM;
	case PIXEL_FORMAT_B8G8R8X8_UNORM: return VK_FORMAT_B8G8R8A8_UNORM;
	case PIXEL_FORMAT_B8G8R8A8_TYPELESS: return VK_FORMAT_B8G8R8A8_UNORM;
	case PIXEL_FORMAT_B8G8R8A8_UNORM_SRGB: return VK_FORMAT_B8G8R8A8_SRGB;
	case PIXEL_FORMAT_B8G8R8X8_TYPELESS: return VK_FORMAT_B8G8R8A8_UNORM;
	case PIXEL_FORMAT_B8G8R8X8_UNORM_SRGB: return VK_FORMAT_B8G8R8A8_SRGB;
	case PIXEL_FORMAT_BC6H_TYPELESS: return VK_FORMAT_BC6H_UFLOAT_BLOCK;
	case PIXEL_FORMAT_BC6H_UF16: return VK_FORMAT_BC6H_UFLOAT_BLOCK;
	case PIXEL_FORMAT_BC6H_SF16: return VK_FORMAT_BC6H_SFLOAT_BLOCK;
	case PIXEL_FORMAT_BC7_TYPELESS: return VK_FORMAT_BC7_UNORM_BLOCK;
	case PIXEL_FORMAT_BC7_UNORM: return VK_FORMAT_BC7_UNORM_BLOCK;
	case PIXEL_FORMAT_BC7_UNORM_SRGB: return VK_FORMAT_BC7_SRGB_BLOCK;
	default:
		return VK_FORMAT_UNDEFINED;
	}
}

Tr2RenderContextEnum::PixelFormat ConvertVkFormat( VkFormat format )
{
	using namespace Tr2RenderContextEnum;
	switch( format )
	{
	case VK_FORMAT_R32G32B32A32_SFLOAT: return PIXEL_FORMAT_R32G32B32A32_FLOAT;
	case VK_FORMAT_R32G32B32A32_UINT: return PIXEL_FORMAT_R32G32B32A32_UINT;
	case VK_FORMAT_R32G32B32A32_SINT: return PIXEL_FORMAT_R32G32B32A32_SINT;
	case VK_FORMAT_R32G32B32_SFLOAT: return PIXEL_FORMAT_R32G32B32_FLOAT;
	case VK_FORMAT_R32G32B32_UINT: return PIXEL_FORMAT_R32G32B32_UINT;
	case VK_FORMAT_R32G32B32_SINT: return PIXEL_FORMAT_R32G32B32_SINT;
	case VK_FORMAT_R16G16B16A16_SFLOAT: return PIXEL_FORMAT_R16G16B16A16_FLOAT;
	case VK_FORMAT_R16G16B16A16_UNORM: return PIXEL_FORMAT_R16G16B16A16_UNORM;
	case VK_FORMAT_R16G16B16A16_UINT: return PIXEL_FORMAT_R16G16B16A16_UINT;
	case VK_FORMAT_R16G16B16A16_SNORM: return PIXEL_FORMAT_R16G16B16A16_SNORM;
	case VK_FORMAT_R16G16B16A16_SINT: return PIXEL_FORMAT_R16G16B16A16_SINT;
	case VK_FORMAT_R32G32_SFLOAT: return PIXEL_FORMAT_R32G32_FLOAT;
	case VK_FORMAT_R32G32_UINT: return PIXEL_FORMAT_R32G32_UINT;
	case VK_FORMAT_R32G32_SINT: return PIXEL_FORMAT_R32G32_SINT;
	case VK_FORMAT_A2B10G10R10_UNORM_PACK32: return PIXEL_FORMAT_R10G10B10A2_UNORM;
	case VK_FORMAT_A2B10G10R10_UINT_PACK32: return PIXEL_FORMAT_R10G10B10A2_UINT;
	case VK_FORMAT_B10G11R11_UFLOAT_PACK32: return PIXEL_FORMAT_R11G11B10_FLOAT;
	case VK_FORMAT_R8G8B8A8_UNORM: return PIXEL_FORMAT_R8G8B8A8_UNORM;
	case VK_FORMAT_R8G8B8A8_SRGB: return PIXEL_FORMAT_R8G8B8A8_UNORM_SRGB;
	case VK_FORMAT_R8G8B8A8_UINT: return PIXEL_FORMAT_R8G8B8A8_UINT;
	case VK_FORMAT_R8G8B8A8_SNORM: return PIXEL_FORMAT_R8G8B8A8_SNORM;
	case VK_FORMAT_R8G8B8A8_SINT: return PIXEL_FORMAT_R8G8B8A8_SINT;
	case VK_FORMAT_R16G16_SFLOAT: return PIXEL_FORMAT_R16G16_FLOAT;
	case VK_FORMAT_R16G16_UNORM: return PIXEL_FORMAT_R16G16_UNORM;
	case VK_FORMAT_R16G16_UINT: return PIXEL_FORMAT_R16G16_UINT;
	case VK_FORMAT_R16G16_SNORM: return PIXEL_FORMAT_R16G16_SNORM;
	case VK_FORMAT_R16G16_SINT: return PIXEL_FORMAT_R16G16_SINT;
	case VK_FORMAT_D32_SFLOAT: return PIXEL_FORMAT_D32_FLOAT;
	case VK_FORMAT_R32_SFLOAT: return PIXEL_FORMAT_R32_FLOAT;
	case VK_FORMAT_R32_UINT: return PIXEL_FORMAT_R32_UINT;
	case VK_FORMAT_R32_SINT: return PIXEL_FORMAT_R32_SINT;
	case VK_FORMAT_D24_UNORM_S8_UINT: return PIXEL_FORMAT_D24_UNORM_S8_UINT;
	case VK_FORMAT_R8G8_UNORM: return PIXEL_FORMAT_R8G8_UNORM;
	case VK_FORMAT_R8G8_UINT: return PIXEL_FORMAT_R8G8_UINT;
	case VK_FORMAT_R8G8_SNORM: return PIXEL_FORMAT_R8G8_SNORM;
	case VK_FORMAT_R8G8_SINT: return PIXEL_FORMAT_R8G8_SINT;
	case VK_FORMAT_R16_SFLOAT: return PIXEL_FORMAT_R16_FLOAT;
	case VK_FORMAT_D16_UNORM: return PIXEL_FORMAT_D16_UNORM;
	case VK_FORMAT_R16_UNORM: return PIXEL_FORMAT_R16_UNORM;
	case VK_FORMAT_R16_UINT: return PIXEL_FORMAT_R16_UINT;
	case VK_FORMAT_R16_SNORM: return PIXEL_FORMAT_R16_SNORM;
	case VK_FORMAT_R16_SINT: return PIXEL_FORMAT_R16_SINT;
	case VK_FORMAT_R8_UNORM: return PIXEL_FORMAT_R8_UNORM;
	case VK_FORMAT_R8_UINT: return PIXEL_FORMAT_R8_UINT;
	case VK_FORMAT_R8_SNORM: return PIXEL_FORMAT_R8_SNORM;
	case VK_FORMAT_R8_SINT: return PIXEL_FORMAT_R8_SINT;
	case VK_FORMAT_BC1_RGBA_UNORM_BLOCK: return PIXEL_FORMAT_BC1_UNORM;
	case VK_FORMAT_BC1_RGBA_SRGB_BLOCK: return PIXEL_FORMAT_BC1_UNORM_SRGB;
	case VK_FORMAT_BC2_UNORM_BLOCK: return PIXEL_FORMAT_BC2_UNORM;
	case VK_FORMAT_BC2_SRGB_BLOCK: return PIXEL_FORMAT_BC2_UNORM_SRGB;
	case VK_FORMAT_BC3_UNORM_BLOCK: return PIXEL_FORMAT_BC3_UNORM;
	case VK_FORMAT_BC3_SRGB_BLOCK: return PIXEL_FORMAT_BC3_UNORM_SRGB;
	case VK_FORMAT_BC4_UNORM_BLOCK: return PIXEL_FORMAT_BC4_UNORM;
	case VK_FORMAT_BC4_SNORM_BLOCK: return PIXEL_FORMAT_BC4_SNORM;
	case VK_FORMAT_BC5_UNORM_BLOCK: return PIXEL_FORMAT_BC5_UNORM;
	case VK_FORMAT_BC5_SNORM_BLOCK: return PIXEL_FORMAT_BC5_SNORM;
	case VK_FORMAT_B5G6R5_UNORM_PACK16: return PIXEL_FORMAT_B5G6R5_UNORM;
	case VK_FORMAT_B5G5R5A1_UNORM_PACK16: return PIXEL_FORMAT_B5G5R5A1_UNORM;
	case VK_FORMAT_B8G8R8A8_UNORM: return PIXEL_FORMAT_B8G8R8A8_UNORM;
	case VK_FORMAT_B8G8R8A8_SRGB: return PIXEL_FORMAT_B8G8R8A8_UNORM_SRGB;
	case VK_FORMAT_BC6H_UFLOAT_BLOCK: return PIXEL_FORMAT_BC6H_UF16;
	case VK_FORMAT_BC6H_SFLOAT_BLOCK: return PIXEL_FORMAT_BC6H_SF16;
	case VK_FORMAT_BC7_UNORM_BLOCK: return PIXEL_FORMAT_BC7_UNORM;
	case VK_FORMAT_BC7_SRGB_BLOCK: return PIXEL_FORMAT_BC7_UNORM_SRGB;
	default:
		return PIXEL_FORMAT_UNKNOWN;
	}
}

VkFormat ConvertDepthStencilFormat( Tr2RenderContextEnum::DepthStencilFormat format )
{
	switch( format )
	{
	case Tr2RenderContextEnum::DSFMT_D24S8:
	case Tr2RenderContextEnum::DSFMT_D24X8:
	case Tr2RenderContextEnum::DSFMT_D24FS8:
		return VK_FORMAT_D24_UNORM_S8_UINT;
	case Tr2RenderContextEnum::DSFMT_D32F:
	case Tr2RenderContextEnum::DSFMT_D32:
		return VK_FORMAT_D32_SFLOAT;
	case Tr2RenderContextEnum::DSFMT_D16:
	case Tr2RenderContextEnum::DSFMT_D16_LOCKABLE:
		return VK_FORMAT_D16_UNORM;
	default:
		return VK_FORMAT_UNDEFINED;
	}
}

VkPrimitiveTopology ConvertTopology( Tr2RenderContextEnum::Topology topology )
{
	switch( topology )
	{
	case Tr2RenderContextEnum::TOP_TRIANGLES: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	case Tr2RenderContextEnum::TOP_TRIANGLE_STRIP: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
	case Tr2RenderContextEnum::TOP_LINES: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
	case Tr2RenderContextEnum::TOP_LINE_STRIP: return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
	case Tr2RenderContextEnum::TOP_POINTS: return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
	default: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	}
}

VkCompareOp ConvertCompareFunc( Tr2RenderContextEnum::CompareFunc func )
{
	switch( func )
	{
	case Tr2RenderContextEnum::CMP_NEVER: return VK_COMPARE_OP_NEVER;
	case Tr2RenderContextEnum::CMP_LESS: return VK_COMPARE_OP_LESS;
	case Tr2RenderContextEnum::CMP_EQUAL: return VK_COMPARE_OP_EQUAL;
	case Tr2RenderContextEnum::CMP_LESSEQUAL: return VK_COMPARE_OP_LESS_OR_EQUAL;
	case Tr2RenderContextEnum::CMP_GREATER: return VK_COMPARE_OP_GREATER;
	case Tr2RenderContextEnum::CMP_NOTEQUAL: return VK_COMPARE_OP_NOT_EQUAL;
	case Tr2RenderContextEnum::CMP_GREATEREQUAL: return VK_COMPARE_OP_GREATER_OR_EQUAL;
	case Tr2RenderContextEnum::CMP_ALWAYS: return VK_COMPARE_OP_ALWAYS;
	default: return VK_COMPARE_OP_ALWAYS;
	}
}

VkBlendFactor ConvertBlendMode( Tr2RenderContextEnum::BlendMode mode )
{
	switch( mode )
	{
	case Tr2RenderContextEnum::BM_ZERO: return VK_BLEND_FACTOR_ZERO;
	case Tr2RenderContextEnum::BM_ONE: return VK_BLEND_FACTOR_ONE;
	case Tr2RenderContextEnum::BM_SRCCOLOR: return VK_BLEND_FACTOR_SRC_COLOR;
	case Tr2RenderContextEnum::BM_INVSRCCOLOR: return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
	case Tr2RenderContextEnum::BM_SRCALPHA: return VK_BLEND_FACTOR_SRC_ALPHA;
	case Tr2RenderContextEnum::BM_INVSRCALPHA: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
	case Tr2RenderContextEnum::BM_DESTALPHA: return VK_BLEND_FACTOR_DST_ALPHA;
	case Tr2RenderContextEnum::BM_INVDESTALPHA: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
	case Tr2RenderContextEnum::BM_DESTCOLOR: return VK_BLEND_FACTOR_DST_COLOR;
	case Tr2RenderContextEnum::BM_INVDESTCOLOR: return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
	case Tr2RenderContextEnum::BM_SRCALPHASAT: return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
	case Tr2RenderContextEnum::BM_BLENDFACTOR: return VK_BLEND_FACTOR_CONSTANT_COLOR;
	case Tr2RenderContextEnum::BM_INVBLENDFACTOR: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
	default: return VK_BLEND_FACTOR_ONE;
	}
}

VkBlendOp ConvertBlendOperation( Tr2RenderContextEnum::BlendOperation op )
{
	switch( op )
	{
	case Tr2RenderContextEnum::BO_ADD: return VK_BLEND_OP_ADD;
	case Tr2RenderContextEnum::BO_SUBTRACT: return VK_BLEND_OP_SUBTRACT;
	case Tr2RenderContextEnum::BO_REVSUBTRACT: return VK_BLEND_OP_REVERSE_SUBTRACT;
	case Tr2RenderContextEnum::BO_MIN: return VK_BLEND_OP_MIN;
	case Tr2RenderContextEnum::BO_MAX: return VK_BLEND_OP_MAX;
	default: return VK_BLEND_OP_ADD;
	}
}

VkStencilOp ConvertStencilOperation( Tr2RenderContextEnum::StencilOperation op )
{
	switch( op )
	{
	case Tr2RenderContextEnum::STENCILOP_KEEP: return VK_STENCIL_OP_KEEP;
	case Tr2RenderContextEnum::STENCILOP_ZERO: return VK_STENCIL_OP_ZERO;
	case Tr2RenderContextEnum::STENCILOP_REPLACE: return VK_STENCIL_OP_REPLACE;
	case Tr2RenderContextEnum::STENCILOP_INCRSAT: return VK_STENCIL_OP_INCREMENT_AND_CLAMP;
	case Tr2RenderContextEnum::STENCILOP_DECRSAT: return VK_STENCIL_OP_DECREMENT_AND_CLAMP;
	case Tr2RenderContextEnum::STENCILOP_INVERT: return VK_STENCIL_OP_INVERT;
	case Tr2RenderContextEnum::STENCILOP_INCR: return VK_STENCIL_OP_INCREMENT_AND_WRAP;
	case Tr2RenderContextEnum::STENCILOP_DECR: return VK_STENCIL_OP_DECREMENT_AND_WRAP;
	default: return VK_STENCIL_OP_KEEP;
	}
}

VkFilter ConvertTextureFilter( Tr2RenderContextEnum::TextureFilter filter )
{
	switch( filter & ~Tr2RenderContextEnum::TF_COMPARISON )
	{
	case Tr2RenderContextEnum::TF_POINT: return VK_FILTER_NEAREST;
	case Tr2RenderContextEnum::TF_LINEAR: return VK_FILTER_LINEAR;
	case Tr2RenderContextEnum::TF_ANISOTROPIC: return VK_FILTER_LINEAR;
	default: return VK_FILTER_LINEAR;
	}
}

VkSamplerAddressMode ConvertAddressMode( Tr2RenderContextEnum::TextureAddressMode mode )
{
	switch( mode )
	{
	case Tr2RenderContextEnum::TA_WRAP: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
	case Tr2RenderContextEnum::TA_MIRROR: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
	case Tr2RenderContextEnum::TA_CLAMP: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
	case Tr2RenderContextEnum::TA_BORDER: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
	case Tr2RenderContextEnum::TA_MIRROR_ONCE: return VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE;
	default: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
	}
}

VkFormat ConvertVertexDataType( Tr2VertexDefinition::DataType dataType )
{
	const unsigned size = Tr2VertexDefinition::GetDataTypeSizeInMembers( dataType );
	const bool normalized = ( dataType & Tr2VertexDefinition::DT_NORMALIZED_BIT ) != 0;
	const bool isUnsigned = ( dataType & Tr2VertexDefinition::DT_UNSIGNED_BIT ) != 0;

	switch( dataType & Tr2VertexDefinition::DT_TYPE_MASK )
	{
	case Tr2VertexDefinition::DT_INT8:
		if( size == 1 ) return isUnsigned ? ( normalized ? VK_FORMAT_R8_UNORM : VK_FORMAT_R8_UINT ) : ( normalized ? VK_FORMAT_R8_SNORM : VK_FORMAT_R8_SINT );
		if( size == 2 ) return isUnsigned ? ( normalized ? VK_FORMAT_R8G8_UNORM : VK_FORMAT_R8G8_UINT ) : ( normalized ? VK_FORMAT_R8G8_SNORM : VK_FORMAT_R8G8_SINT );
		if( size == 3 ) return isUnsigned ? ( normalized ? VK_FORMAT_R8G8B8_UNORM : VK_FORMAT_R8G8B8_UINT ) : ( normalized ? VK_FORMAT_R8G8B8_SNORM : VK_FORMAT_R8G8B8_SINT );
		return isUnsigned ? ( normalized ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_R8G8B8A8_UINT ) : ( normalized ? VK_FORMAT_R8G8B8A8_SNORM : VK_FORMAT_R8G8B8A8_SINT );
	case Tr2VertexDefinition::DT_INT16:
		if( size == 1 ) return isUnsigned ? ( normalized ? VK_FORMAT_R16_UNORM : VK_FORMAT_R16_UINT ) : ( normalized ? VK_FORMAT_R16_SNORM : VK_FORMAT_R16_SINT );
		if( size == 2 ) return isUnsigned ? ( normalized ? VK_FORMAT_R16G16_UNORM : VK_FORMAT_R16G16_UINT ) : ( normalized ? VK_FORMAT_R16G16_SNORM : VK_FORMAT_R16G16_SINT );
		if( size == 3 ) return isUnsigned ? ( normalized ? VK_FORMAT_R16G16B16_UNORM : VK_FORMAT_R16G16B16_UINT ) : ( normalized ? VK_FORMAT_R16G16B16_SNORM : VK_FORMAT_R16G16B16_SINT );
		return isUnsigned ? ( normalized ? VK_FORMAT_R16G16B16A16_UNORM : VK_FORMAT_R16G16B16A16_UINT ) : ( normalized ? VK_FORMAT_R16G16B16A16_SNORM : VK_FORMAT_R16G16B16A16_SINT );
	case Tr2VertexDefinition::DT_INT32:
		if( size == 1 ) return isUnsigned ? VK_FORMAT_R32_UINT : VK_FORMAT_R32_SINT;
		if( size == 2 ) return isUnsigned ? VK_FORMAT_R32G32_UINT : VK_FORMAT_R32G32_SINT;
		if( size == 3 ) return isUnsigned ? VK_FORMAT_R32G32B32_UINT : VK_FORMAT_R32G32B32_SINT;
		return isUnsigned ? VK_FORMAT_R32G32B32A32_UINT : VK_FORMAT_R32G32B32A32_SINT;
	case Tr2VertexDefinition::DT_FLOAT16:
		if( size == 1 ) return VK_FORMAT_R16_SFLOAT;
		if( size == 2 ) return VK_FORMAT_R16G16_SFLOAT;
		if( size == 3 ) return VK_FORMAT_R16G16B16_SFLOAT;
		return VK_FORMAT_R16G16B16A16_SFLOAT;
	case Tr2VertexDefinition::DT_FLOAT32:
		if( size == 1 ) return VK_FORMAT_R32_SFLOAT;
		if( size == 2 ) return VK_FORMAT_R32G32_SFLOAT;
		if( size == 3 ) return VK_FORMAT_R32G32B32_SFLOAT;
		return VK_FORMAT_R32G32B32A32_SFLOAT;
	default:
		return VK_FORMAT_UNDEFINED;
	}
}

VkShaderStageFlagBits ConvertShaderType( Tr2RenderContextEnum::ShaderType type )
{
	switch( type )
	{
	case Tr2RenderContextEnum::VERTEX_SHADER: return VK_SHADER_STAGE_VERTEX_BIT;
	case Tr2RenderContextEnum::PIXEL_SHADER: return VK_SHADER_STAGE_FRAGMENT_BIT;
	case Tr2RenderContextEnum::COMPUTE_SHADER: return VK_SHADER_STAGE_COMPUTE_BIT;
	case Tr2RenderContextEnum::GEOMETRY_SHADER: return VK_SHADER_STAGE_GEOMETRY_BIT;
	case Tr2RenderContextEnum::HULL_SHADER: return VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
	case Tr2RenderContextEnum::DOMAIN_SHADER: return VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
	default: return VK_SHADER_STAGE_ALL;
	}
}

Tr2RenderContextEnum::ShaderType ConvertVkShaderStage( VkShaderStageFlagBits stage )
{
	switch( stage )
	{
	case VK_SHADER_STAGE_VERTEX_BIT: return Tr2RenderContextEnum::VERTEX_SHADER;
	case VK_SHADER_STAGE_FRAGMENT_BIT: return Tr2RenderContextEnum::PIXEL_SHADER;
	case VK_SHADER_STAGE_COMPUTE_BIT: return Tr2RenderContextEnum::COMPUTE_SHADER;
	case VK_SHADER_STAGE_GEOMETRY_BIT: return Tr2RenderContextEnum::GEOMETRY_SHADER;
	case VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT: return Tr2RenderContextEnum::HULL_SHADER;
	case VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT: return Tr2RenderContextEnum::DOMAIN_SHADER;
	default: return Tr2RenderContextEnum::INVALID_SHADER;
	}
}

VkImageLayout ConvertGpuUsageToLayout( Tr2GpuUsage::Type usage )
{
	if( Tr2GpuUsage::HasFlag( usage, Tr2GpuUsage::RENDER_TARGET ) )
	{
		return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	}
	if( Tr2GpuUsage::HasFlag( usage, Tr2GpuUsage::DEPTH_STENCIL ) )
	{
		return VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	}
	if( Tr2GpuUsage::HasFlag( usage, Tr2GpuUsage::UNORDERED_ACCESS ) )
	{
		return VK_IMAGE_LAYOUT_GENERAL;
	}
	if( Tr2GpuUsage::HasFlag( usage, Tr2GpuUsage::COPY_DESTINATION ) )
	{
		return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	}
	return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}

VkImageUsageFlags ConvertGpuUsageToImageUsage( Tr2GpuUsage::Type usage )
{
	VkImageUsageFlags flags = 0;
	if( Tr2GpuUsage::HasFlag( usage, Tr2GpuUsage::RENDER_TARGET ) )
	{
		flags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	}
	if( Tr2GpuUsage::HasFlag( usage, Tr2GpuUsage::DEPTH_STENCIL ) )
	{
		flags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	}
	if( Tr2GpuUsage::HasFlag( usage, Tr2GpuUsage::SHADER_RESOURCE ) )
	{
		flags |= VK_IMAGE_USAGE_SAMPLED_BIT;
	}
	if( Tr2GpuUsage::HasFlag( usage, Tr2GpuUsage::UNORDERED_ACCESS ) )
	{
		flags |= VK_IMAGE_USAGE_STORAGE_BIT;
	}
	if( Tr2GpuUsage::HasFlag( usage, Tr2GpuUsage::COPY_DESTINATION ) )
	{
		flags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
	}
	if( flags == 0 )
	{
		flags = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
	}
	return flags;
}

VkBufferUsageFlags ConvertGpuUsageToBufferUsage( Tr2GpuUsage::Type usage )
{
	VkBufferUsageFlags flags = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	if( Tr2GpuUsage::HasFlag( usage, Tr2GpuUsage::VERTEX_BUFFER ) )
	{
		flags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
	}
	if( Tr2GpuUsage::HasFlag( usage, Tr2GpuUsage::INDEX_BUFFER ) )
	{
		flags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
	}
	if( Tr2GpuUsage::HasFlag( usage, Tr2GpuUsage::SHADER_RESOURCE ) )
	{
		flags |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	}
	if( Tr2GpuUsage::HasFlag( usage, Tr2GpuUsage::UNORDERED_ACCESS ) )
	{
		flags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	}
	if( Tr2GpuUsage::HasFlag( usage, Tr2GpuUsage::COPY_DESTINATION ) )
	{
		flags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	}
	if( Tr2GpuUsage::HasFlag( usage, Tr2GpuUsage::DRAW_INDIRECT_ARGS ) )
	{
		flags |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
	}
	if( flags == VK_BUFFER_USAGE_TRANSFER_DST_BIT )
	{
		flags = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	}
	return flags;
}

VkMemoryPropertyFlags ConvertCpuUsageToMemoryFlags( Tr2CpuUsage::Type usage )
{
	if( Tr2CpuUsage::HasFlag( usage, Tr2CpuUsage::READ ) )
	{
		return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
	}
	if( Tr2CpuUsage::HasFlag( usage, Tr2CpuUsage::WRITE ) )
	{
		return VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
	}
	return VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
}

VkImageAspectFlags GetImageAspectFlags( VkFormat format )
{
	if( IsDepthFormat( format ) )
	{
		return VK_IMAGE_ASPECT_DEPTH_BIT;
	}
	return VK_IMAGE_ASPECT_COLOR_BIT;
}

bool IsDepthFormat( VkFormat format )
{
	switch( format )
	{
	case VK_FORMAT_D16_UNORM:
	case VK_FORMAT_D16_UNORM_S8_UINT:
	case VK_FORMAT_D24_UNORM_S8_UINT:
	case VK_FORMAT_D32_SFLOAT:
	case VK_FORMAT_D32_SFLOAT_S8_UINT:
		return true;
	default:
		return false;
	}
}

}

#endif
