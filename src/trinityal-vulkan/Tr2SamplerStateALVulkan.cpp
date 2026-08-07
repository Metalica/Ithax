// Copyright © 2026 Ithax contributors.

#include "StdAfx.h"
#include "Tr2SamplerStateALVulkan.h"

#if TRINITY_PLATFORM == TRINITY_VULKAN

#include "ALLog.h"
#include "Tr2RenderContextAL.h"

namespace TrinityALImpl
{

Tr2SamplerStateAL::Tr2SamplerStateAL()
{
}

Tr2SamplerStateAL::~Tr2SamplerStateAL()
{
	Destroy();
}

ALResult Tr2SamplerStateAL::Create( const Tr2SamplerDescription& description, Tr2RenderContextAL& renderContext )
{
	m_description = description;
	m_device = renderContext.GetVulkanContext().state.device;

	VkSamplerCreateInfo createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
	createInfo.magFilter = ConvertTextureFilter( description.m_magFilter );
	createInfo.minFilter = ConvertTextureFilter( description.m_minFilter );
	createInfo.mipmapMode = ( description.m_mipFilter == Tr2RenderContextEnum::TF_POINT )
		? VK_SAMPLER_MIPMAP_MODE_NEAREST
		: VK_SAMPLER_MIPMAP_MODE_LINEAR;
	createInfo.addressModeU = ConvertAddressMode( description.m_addressU );
	createInfo.addressModeV = ConvertAddressMode( description.m_addressV );
	createInfo.addressModeW = ConvertAddressMode( description.m_addressW );
	createInfo.mipLodBias = description.m_mipLODBias;
	createInfo.anisotropyEnable = ( description.m_maxAnisotropy > 1 ) ? VK_TRUE : VK_FALSE;
	createInfo.maxAnisotropy = static_cast<float>( std::max( description.m_maxAnisotropy, 1u ) );
	createInfo.compareEnable = description.m_isComparisonFilter ? VK_TRUE : VK_FALSE;
	createInfo.compareOp = ConvertCompareFunc( description.m_comparisonFunc );
	createInfo.minLod = description.m_minLOD;
	createInfo.maxLod = description.m_maxLOD;
	createInfo.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
	createInfo.unnormalizedCoordinates = VK_FALSE;

	VkResult result = vkCreateSampler( m_device, &createInfo, nullptr, &m_sampler );
	if( result != VK_SUCCESS )
	{
		CCP_AL_LOGERR( "vkCreateSampler failed: %d", int( result ) );
		return E_FAIL;
	}
	return S_OK;
}

void Tr2SamplerStateAL::Destroy()
{
	if( m_sampler != VK_NULL_HANDLE )
	{
		vkDestroySampler( m_device, m_sampler, nullptr );
		m_sampler = VK_NULL_HANDLE;
	}
}

uint32_t Tr2SamplerStateAL::GetIndexInHeap() const
{
	return 0xffffffff;
}

bool Tr2SamplerStateAL::IsValid() const
{
	return m_sampler != VK_NULL_HANDLE;
}

void Tr2SamplerStateAL::Describe( Tr2DeviceResourceDescriptionAL& description ) const
{
	description["type"] = "sampler";
}

ALResult Tr2SamplerStateAL::SetName( const char* name )
{
	(void)name;
	return S_OK;
}

VkSampler Tr2SamplerStateAL::GetSampler() const
{
	return m_sampler;
}

}

#endif
