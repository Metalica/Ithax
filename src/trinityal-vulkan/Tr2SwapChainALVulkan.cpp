// Copyright © 2026 Ithax contributors.

#include "StdAfx.h"
#include "Tr2SwapChainALVulkan.h"

#if TRINITY_PLATFORM == TRINITY_VULKAN

#include "ALLog.h"
#include "Tr2RenderContextAL.h"
#include "Tr2TextureALVulkan.h"

namespace TrinityALImpl
{

Tr2SwapChainAL::Tr2SwapChainAL()
{
}

Tr2SwapChainAL::~Tr2SwapChainAL()
{
	Destroy();
}

ALResult Tr2SwapChainAL::Create( Tr2WindowHandle windowHandle, Tr2RenderContextAL& renderContext )
{
	m_windowHandle = windowHandle;
	m_device = renderContext.GetVulkanContext().state.device;

	if( windowHandle == nullptr )
	{
		return E_INVALIDARG;
	}

	VulkanContext& context = renderContext.GetVulkanContext();

	VkWin32SurfaceCreateInfoKHR surfaceInfo = {};
	surfaceInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
	surfaceInfo.hwnd = windowHandle;
	surfaceInfo.hinstance = GetModuleHandle( nullptr );

	VkSurfaceKHR surface = VK_NULL_HANDLE;
	VkResult result = vkCreateWin32SurfaceKHR( context.state.instance, &surfaceInfo, nullptr, &surface );
	if( result != VK_SUCCESS )
	{
		CCP_AL_LOGERR( "vkCreateWin32SurfaceKHR (standalone) failed: %d", int( result ) );
		return E_FAIL;
	}

	VkSurfaceCapabilitiesKHR caps = {};
	std::vector<VkSurfaceFormatKHR> formats;
	std::vector<VkPresentModeKHR> modes;
	result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR( context.state.physicalDevice, surface, &caps );
	if( result != VK_SUCCESS )
	{
		vkDestroySurfaceKHR( context.state.instance, surface, nullptr );
		return E_FAIL;
	}

	uint32_t formatCount = 0;
	result = vkGetPhysicalDeviceSurfaceFormatsKHR( context.state.physicalDevice, surface, &formatCount, nullptr );
	if( result != VK_SUCCESS )
	{
		vkDestroySurfaceKHR( context.state.instance, surface, nullptr );
		return E_FAIL;
	}
	formats.resize( formatCount );
	result = vkGetPhysicalDeviceSurfaceFormatsKHR( context.state.physicalDevice, surface, &formatCount, formats.data() );
	if( result != VK_SUCCESS )
	{
		vkDestroySurfaceKHR( context.state.instance, surface, nullptr );
		return E_FAIL;
	}

	uint32_t modeCount = 0;
	result = vkGetPhysicalDeviceSurfacePresentModesKHR( context.state.physicalDevice, surface, &modeCount, nullptr );
	if( result != VK_SUCCESS )
	{
		vkDestroySurfaceKHR( context.state.instance, surface, nullptr );
		return E_FAIL;
	}
	modes.resize( modeCount );
	result = vkGetPhysicalDeviceSurfacePresentModesKHR( context.state.physicalDevice, surface, &modeCount, modes.data() );
	if( result != VK_SUCCESS )
	{
		vkDestroySurfaceKHR( context.state.instance, surface, nullptr );
		return E_FAIL;
	}

	VkSurfaceFormatKHR surfaceFormat = formats.empty() ? VkSurfaceFormatKHR{} : formats[0];
	for( const auto& format : formats )
	{
		if( format.format == VK_FORMAT_B8G8R8A8_UNORM &&
			format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR )
		{
			surfaceFormat = format;
			break;
		}
	}

	VkExtent2D extent = caps.currentExtent;
	if( extent.width == 0 || extent.height == 0 )
	{
		extent.width = 800;
		extent.height = 600;
	}

	uint32_t imageCount = caps.minImageCount + 1;
	if( caps.maxImageCount > 0 && imageCount > caps.maxImageCount )
	{
		imageCount = caps.maxImageCount;
	}

	VkSwapchainCreateInfoKHR swapchainInfo = {};
	swapchainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	swapchainInfo.surface = surface;
	swapchainInfo.minImageCount = imageCount;
	swapchainInfo.imageFormat = surfaceFormat.format;
	swapchainInfo.imageColorSpace = surfaceFormat.colorSpace;
	swapchainInfo.imageExtent = extent;
	swapchainInfo.imageArrayLayers = 1;
	swapchainInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	swapchainInfo.preTransform = caps.currentTransform;
	swapchainInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	swapchainInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
	swapchainInfo.clipped = VK_TRUE;
	swapchainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VkSwapchainKHR swapchain = VK_NULL_HANDLE;
	result = vkCreateSwapchainKHR( m_device, &swapchainInfo, nullptr, &swapchain );
	if( result != VK_SUCCESS )
	{
		vkDestroySurfaceKHR( context.state.instance, surface, nullptr );
		CCP_AL_LOGERR( "vkCreateSwapchainKHR (standalone) failed: %d", int( result ) );
		return E_FAIL;
	}

	uint32_t actualImageCount = 0;
	result = vkGetSwapchainImagesKHR( m_device, swapchain, &actualImageCount, nullptr );
	if( result != VK_SUCCESS )
	{
		vkDestroySwapchainKHR( m_device, swapchain, nullptr );
		vkDestroySurfaceKHR( context.state.instance, surface, nullptr );
		return E_FAIL;
	}
	std::vector<VkImage> images( actualImageCount );
	result = vkGetSwapchainImagesKHR( m_device, swapchain, &actualImageCount, images.data() );
	if( result != VK_SUCCESS )
	{
		vkDestroySwapchainKHR( m_device, swapchain, nullptr );
		vkDestroySurfaceKHR( context.state.instance, surface, nullptr );
		return E_FAIL;
	}

	Tr2BitmapDimensions dimensions(
		Tr2RenderContextEnum::TEX_TYPE_2D,
		ConvertVkFormat( surfaceFormat.format ),
		extent.width,
		extent.height,
		1,
		1 );

	Tr2TextureAL* backBuffer = new Tr2TextureAL();
	backBuffer->m_desc = dimensions;
	backBuffer->m_msaa = Tr2MsaaDesc( 1, 0 );
	backBuffer->m_gpuUsage = Tr2GpuUsage::RENDER_TARGET;
	backBuffer->m_cpuUsage = Tr2CpuUsage::NONE;
	backBuffer->m_device = m_device;
	backBuffer->m_allocator = context.state.allocator;
	backBuffer->m_format = surfaceFormat.format;
	backBuffer->m_layout = VK_IMAGE_LAYOUT_UNDEFINED;
	backBuffer->m_image = images[0];

	VkImageViewCreateInfo viewInfo = {};
	viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewInfo.image = images[0];
	viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	viewInfo.format = surfaceFormat.format;
	viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	viewInfo.subresourceRange.baseMipLevel = 0;
	viewInfo.subresourceRange.levelCount = 1;
	viewInfo.subresourceRange.baseArrayLayer = 0;
	viewInfo.subresourceRange.layerCount = 1;
	result = vkCreateImageView( m_device, &viewInfo, nullptr, &backBuffer->m_linearView );
	if( result != VK_SUCCESS )
	{
		delete backBuffer;
		vkDestroySwapchainKHR( m_device, swapchain, nullptr );
		vkDestroySurfaceKHR( context.state.instance, surface, nullptr );
		return E_FAIL;
	}

	m_backBuffer.m_texture = std::shared_ptr<TrinityALImpl::Tr2TextureAL>( backBuffer );

	VulkanDeferredDestroy deferred = {};
	deferred.device = m_device;
	deferred.allocator = context.state.allocator;
	deferred.timelineValue = context.GetCurrentTimelineValue();
	deferred.swapchain = swapchain;
	context.Retire( deferred );

	return S_OK;
}

void Tr2SwapChainAL::Destroy()
{
	m_backBuffer = ::Tr2TextureAL();
}

bool Tr2SwapChainAL::IsValid() const
{
	return m_backBuffer.IsValid();
}

ALResult Tr2SwapChainAL::Present( Tr2RenderContextAL& renderContext )
{
	(void)renderContext;
	return S_OK;
}

uint32_t Tr2SwapChainAL::GetWidth() const
{
	return m_backBuffer.GetWidth();
}

uint32_t Tr2SwapChainAL::GetHeight() const
{
	return m_backBuffer.GetHeight();
}

void Tr2SwapChainAL::Describe( Tr2DeviceResourceDescriptionAL& description ) const
{
	description["type"] = "swapchain";
}

ALResult Tr2SwapChainAL::SetName( const char* name )
{
	(void)name;
	return S_OK;
}

}

#endif
