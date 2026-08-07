// Copyright © 2026 Ithax contributors.

#include "StdAfx.h"
#include "Tr2OcclusionQueryALVulkan.h"

#if TRINITY_PLATFORM == TRINITY_VULKAN

#include "ALLog.h"
#include "Tr2RenderContextAL.h"

namespace TrinityALImpl
{

Tr2OcclusionQueryAL::Tr2OcclusionQueryAL()
{
}

Tr2OcclusionQueryAL::~Tr2OcclusionQueryAL()
{
	Destroy();
}

ALResult Tr2OcclusionQueryAL::Create( Tr2RenderContextAL& renderContext )
{
	m_device = renderContext.GetVulkanContext().state.device;

	VkQueryPoolCreateInfo createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
	createInfo.queryType = VK_QUERY_TYPE_OCCLUSION;
	createInfo.queryCount = 1;

	VkResult result = vkCreateQueryPool( m_device, &createInfo, nullptr, &m_queryPool );
	if( result != VK_SUCCESS )
	{
		CCP_AL_LOGERR( "vkCreateQueryPool (occlusion) failed: %d", int( result ) );
		return E_FAIL;
	}
	return S_OK;
}

bool Tr2OcclusionQueryAL::IsValid() const
{
	return m_queryPool != VK_NULL_HANDLE;
}

void Tr2OcclusionQueryAL::Destroy()
{
	if( m_queryPool != VK_NULL_HANDLE )
	{
		vkDestroyQueryPool( m_device, m_queryPool, nullptr );
		m_queryPool = VK_NULL_HANDLE;
	}
	m_isRunning = false;
}

ALResult Tr2OcclusionQueryAL::Begin( Tr2RenderContextAL& renderContext )
{
	if( m_queryPool == VK_NULL_HANDLE || m_isRunning )
	{
		return E_INVALIDCALL;
	}
	VkCommandBuffer commandBuffer = renderContext.GetVulkanContext().GetCurrentCommandBuffer();
	vkCmdResetQueryPool( commandBuffer, m_queryPool, 0, 1 );
	vkCmdBeginQuery( commandBuffer, m_queryPool, 0, 0 );
	m_isRunning = true;
	return S_OK;
}

ALResult Tr2OcclusionQueryAL::End( Tr2RenderContextAL& renderContext )
{
	if( m_queryPool == VK_NULL_HANDLE || !m_isRunning )
	{
		return E_INVALIDCALL;
	}
	VkCommandBuffer commandBuffer = renderContext.GetVulkanContext().GetCurrentCommandBuffer();
	vkCmdEndQuery( commandBuffer, m_queryPool, 0 );
	m_isRunning = false;
	return S_OK;
}

ALResult Tr2OcclusionQueryAL::GetPixelCount( Tr2RenderContextAL& renderContext, uint32_t& count, ::Tr2OcclusionQueryAL::WaitMode waitMode )
{
	(void)renderContext;
	if( m_queryPool == VK_NULL_HANDLE )
	{
		return E_INVALIDCALL;
	}
	VkQueryResultFlags flags = VK_QUERY_RESULT_64_BIT;
	if( waitMode == ::Tr2OcclusionQueryAL::DO_NOT_WAIT )
	{
		flags |= VK_QUERY_RESULT_WITH_AVAILABILITY_BIT;
	}

	uint64_t results[2] = { 0, 0 };
	VkResult result = vkGetQueryPoolResults( m_device, m_queryPool, 0, 1,
		sizeof( results ), results, sizeof( uint64_t ), flags );
	if( result == VK_NOT_READY )
	{
		count = 0;
		return S_OK;
	}
	if( result != VK_SUCCESS )
	{
		return E_FAIL;
	}
	if( flags & VK_QUERY_RESULT_WITH_AVAILABILITY_BIT )
	{
		if( results[1] == 0 )
		{
			count = 0;
			return S_OK;
		}
		count = static_cast<uint32_t>( results[0] );
	}
	else
	{
		count = static_cast<uint32_t>( results[0] );
	}
	return S_OK;
}

void Tr2OcclusionQueryAL::Describe( Tr2DeviceResourceDescriptionAL& description ) const
{
	description["type"] = "occlusion_query";
}

ALResult Tr2OcclusionQueryAL::SetName( const char* name )
{
	SetVulkanObjectName( m_device, reinterpret_cast<uint64_t>( m_queryPool ),
		VK_OBJECT_TYPE_QUERY_POOL, name );
	return S_OK;
}

}

#endif
