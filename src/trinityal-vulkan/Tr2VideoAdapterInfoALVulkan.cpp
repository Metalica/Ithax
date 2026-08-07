// Copyright © 2026 Ithax contributors.

#include "StdAfx.h"
#include "Tr2VideoAdapterInfoALVulkan.h"

#if TRINITY_PLATFORM == TRINITY_VULKAN

#include "ALLog.h"
#include "Tr2AdapterStructures.h"
#include "Tr2VulkanContext.h"

namespace
{

struct AdapterRecord
{
	VkPhysicalDevice device;
	VkPhysicalDeviceProperties properties;
};

std::vector<AdapterRecord>& GetAdapters()
{
	static std::vector<AdapterRecord> s_adapters;
	return s_adapters;
}

ALResult RefreshAdapters()
{
	auto& adapters = GetAdapters();
	adapters.clear();

	VkInstance instance = VK_NULL_HANDLE;
	VkApplicationInfo appInfo = {};
	appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	appInfo.pApplicationName = "Ithax";
	appInfo.applicationVersion = VK_MAKE_VERSION( 0, 1, 0 );
	appInfo.pEngineName = "TrinityAL";
	appInfo.engineVersion = VK_MAKE_VERSION( 4, 0, 2 );
	appInfo.apiVersion = VK_API_VERSION_1_3;

	VkInstanceCreateInfo createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	createInfo.pApplicationInfo = &appInfo;

	VkResult result = vkCreateInstance( &createInfo, nullptr, &instance );
	if( result != VK_SUCCESS )
	{
		return E_FAIL;
	}

	uint32_t deviceCount = 0;
	result = vkEnumeratePhysicalDevices( instance, &deviceCount, nullptr );
	if( result != VK_SUCCESS )
	{
		vkDestroyInstance( instance, nullptr );
		return E_FAIL;
	}

	std::vector<VkPhysicalDevice> devices( deviceCount );
	result = vkEnumeratePhysicalDevices( instance, &deviceCount, devices.data() );
	if( result != VK_SUCCESS )
	{
		vkDestroyInstance( instance, nullptr );
		return E_FAIL;
	}

	adapters.reserve( deviceCount );
	for( VkPhysicalDevice device : devices )
	{
		AdapterRecord record = {};
		record.device = device;
		vkGetPhysicalDeviceProperties( device, &record.properties );
		adapters.push_back( record );
	}

	vkDestroyInstance( instance, nullptr );
	return S_OK;
}

}

ALResult Tr2VideoAdapterInfo::GetAdapterCount( unsigned& count )
{
	ALResult result = RefreshAdapters();
	if( FAILED( result ) )
	{
		count = 0;
		return result;
	}
	count = static_cast<unsigned>( GetAdapters().size() );
	return S_OK;
}

ALResult Tr2VideoAdapterInfo::GetAdapterInfo( unsigned adapterIndex, Tr2AdapterInfo& info )
{
	auto& adapters = GetAdapters();
	if( adapterIndex >= adapters.size() )
	{
		return E_INVALIDARG;
	}

	const auto& record = adapters[adapterIndex];
	info.driver = "Vulkan";
	info.description = std::wstring( record.properties.deviceName, record.properties.deviceName + strlen( record.properties.deviceName ) );
	info.deviceName = record.properties.deviceName;
	info.driverVersion = record.properties.driverVersion;
	info.vendorID = record.properties.vendorID;
	info.deviceID = record.properties.deviceID;
	info.subSystemID = 0;
	info.revision = record.properties.apiVersion;
	info.deviceIdentifier.data1 = record.properties.vendorID;
	info.deviceIdentifier.data2 = static_cast<uint16_t>( record.properties.deviceID & 0xffff );
	info.deviceIdentifier.data3 = static_cast<uint16_t>( ( record.properties.deviceID >> 16 ) & 0xffff );
	memset( info.deviceIdentifier.data4, 0, sizeof( info.deviceIdentifier.data4 ) );
	memset( info.luid, 0, sizeof( info.luid ) );
	return S_OK;
}

ALResult Tr2VideoAdapterInfo::GetAdapterMonitor( unsigned adapterIndex, void*& monitor )
{
	(void)adapterIndex;
	monitor = nullptr;
	return S_OK;
}

ALResult Tr2VideoAdapterInfo::GetAdapterDisplayMode( unsigned adapterIndex, Tr2DisplayModeInfo& mode )
{
	(void)adapterIndex;
	mode.width = 0;
	mode.height = 0;
	mode.refreshRateNumerator = 0;
	mode.refreshRateDenominator = 0;
	mode.format = Tr2RenderContextEnum::PIXEL_FORMAT_B8G8R8A8_UNORM;
	mode.scanlineOrdering = Tr2RenderContextEnum::SCANLINE_ORDER_PROGRESSIVE;
	mode.scaling = Tr2RenderContextEnum::DISPLAY_SCALING_UNSPECIFIED;
	return S_OK;
}

ALResult Tr2VideoAdapterInfo::GetAdapterModeCount( unsigned adapterIndex, Tr2RenderContextEnum::PixelFormat backBufferFormat, unsigned& count )
{
	(void)adapterIndex;
	(void)backBufferFormat;
	count = 0;
	return S_OK;
}

ALResult Tr2VideoAdapterInfo::GetAdapterMode( unsigned adapterIndex, Tr2RenderContextEnum::PixelFormat backBufferFormat, unsigned modeIndex, Tr2DisplayModeInfo& mode )
{
	(void)adapterIndex;
	(void)backBufferFormat;
	(void)modeIndex;
	(void)mode;
	return E_INVALIDARG;
}

bool Tr2VideoAdapterInfo::SupportsBackBufferFormat( unsigned adapterIndex, Tr2RenderContextEnum::PixelFormat backBufferFormat )
{
	(void)adapterIndex;
	VkFormat format = TrinityALImpl::ConvertPixelFormat( backBufferFormat );
	if( format == VK_FORMAT_UNDEFINED )
	{
		return false;
	}
	return true;
}

bool Tr2VideoAdapterInfo::SupportsRenderTargetFormat( unsigned adapterIndex, Tr2RenderContextEnum::PixelFormat format )
{
	(void)adapterIndex;
	VkFormat vkFormat = TrinityALImpl::ConvertPixelFormat( format );
	if( vkFormat == VK_FORMAT_UNDEFINED )
	{
		return false;
	}
	return true;
}

ALResult Tr2VideoAdapterInfo::GetAdapterMaxTextureWidth( unsigned adapterIndex, unsigned& maxWidth )
{
	auto& adapters = GetAdapters();
	if( adapterIndex >= adapters.size() )
	{
		return E_INVALIDARG;
	}
	maxWidth = adapters[adapterIndex].properties.limits.maxImageDimension2D;
	return S_OK;
}

bool Tr2VideoAdapterInfo::AreAdaptersDifferent( unsigned adapter1, unsigned adapter2 )
{
	return adapter1 != adapter2;
}

ALResult Tr2VideoAdapterInfo::RefreshData()
{
	return RefreshAdapters();
}


#endif
