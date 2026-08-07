// Copyright © 2026 Ithax contributors.

#include "StdAfx.h"
#include "Tr2GpuTimerALVulkan.h"

#if TRINITY_PLATFORM == TRINITY_VULKAN

#include "ALLog.h"
#include "Tr2PrimaryRenderContextAL.h"
#include "Tr2RenderContextAL.h"

namespace TrinityALImpl
{

Tr2GpuTimerAL::Tr2GpuTimerAL()
{
}

Tr2GpuTimerAL::~Tr2GpuTimerAL()
{
	Destroy();
}

ALResult Tr2GpuTimerAL::Create( Tr2PrimaryRenderContextAL& renderContext )
{
	m_device = renderContext.GetVulkanContext().state.device;

	VkQueryPoolCreateInfo createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
	createInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
	createInfo.queryCount = 2;

	VkResult result = vkCreateQueryPool( m_device, &createInfo, nullptr, &m_queryPool );
	if( result != VK_SUCCESS )
	{
		CCP_AL_LOGERR( "vkCreateQueryPool (timer) failed: %d", int( result ) );
		return E_FAIL;
	}
	return S_OK;
}

void Tr2GpuTimerAL::Destroy()
{
	if( m_queryPool != VK_NULL_HANDLE )
	{
		vkDestroyQueryPool( m_device, m_queryPool, nullptr );
		m_queryPool = VK_NULL_HANDLE;
	}
	m_isRunning = false;
}

bool Tr2GpuTimerAL::Begin( Tr2RenderContextAL& renderContext )
{
	if( m_queryPool == VK_NULL_HANDLE || m_isRunning )
	{
		return false;
	}
	VkCommandBuffer commandBuffer = renderContext.GetVulkanContext().GetCurrentCommandBuffer();
	vkCmdResetQueryPool( commandBuffer, m_queryPool, 0, 2 );
	vkCmdWriteTimestamp2( commandBuffer, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, m_queryPool, 0 );
	m_isRunning = true;
	return true;
}

void Tr2GpuTimerAL::End( Tr2RenderContextAL& renderContext )
{
	if( m_queryPool == VK_NULL_HANDLE || !m_isRunning )
	{
		return;
	}
	VkCommandBuffer commandBuffer = renderContext.GetVulkanContext().GetCurrentCommandBuffer();
	vkCmdWriteTimestamp2( commandBuffer, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, m_queryPool, 1 );
	m_isRunning = false;
}

float Tr2GpuTimerAL::GetTime( Tr2RenderContextAL& renderContext )
{
	(void)renderContext;
	if( m_queryPool == VK_NULL_HANDLE )
	{
		return 0.0f;
	}
	uint64_t timestamps[2] = { 0, 0 };
	VkResult result = vkGetQueryPoolResults( m_device, m_queryPool, 0, 2,
		sizeof( timestamps ), timestamps, sizeof( uint64_t ),
		VK_QUERY_RESULT_64_BIT );
	if( result != VK_SUCCESS )
	{
		return 0.0f;
	}
	float period = 1.0f;
	VulkanContext& context = renderContext.GetVulkanContext();
	if( context.state.device != VK_NULL_HANDLE )
	{
		period = context.state.timestampPeriod;
	}
	return static_cast<float>( timestamps[1] - timestamps[0] ) * period;
}

bool Tr2GpuTimerAL::IsValid() const
{
	return m_queryPool != VK_NULL_HANDLE;
}

void Tr2GpuTimerAL::Describe( Tr2DeviceResourceDescriptionAL& description ) const
{
	description["type"] = "gpu_timer";
}

ALResult Tr2GpuTimerAL::SetName( const char* name )
{
	SetVulkanObjectName( m_device, reinterpret_cast<uint64_t>( m_queryPool ),
		VK_OBJECT_TYPE_QUERY_POOL, name );
	return S_OK;
}

}

#endif
