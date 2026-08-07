// Copyright © 2026 Ithax contributors.

#include "StdAfx.h"
#include "Tr2PipelineStatsQueryALVulkan.h"

#if TRINITY_PLATFORM == TRINITY_VULKAN

#include "ALLog.h"
#include "Tr2PrimaryRenderContextAL.h"
#include "Tr2RenderContextAL.h"

namespace TrinityALImpl
{

namespace
{

constexpr uint32_t PIPELINE_STAT_QUERY_COUNT = 11;

struct StatEntry
{
	const char* label;
	const char* description;
	size_t offset;
};

const StatEntry STAT_ENTRIES[] = {
	{ "IAVertices", "Input assembler vertices", offsetof( Tr2PipelineStatsDataAL, inputAssemblyVertices ) },
	{ "IAPrimitives", "Input assembler primitives", offsetof( Tr2PipelineStatsDataAL, inputAssemblyPrimitives ) },
	{ "VSInvocations", "Vertex shader invocations", offsetof( Tr2PipelineStatsDataAL, vertexShaderInvocations ) },
	{ "GSInvocations", "Geometry shader invocations", offsetof( Tr2PipelineStatsDataAL, geometryShaderInvocations ) },
	{ "GSPrimitives", "Geometry shader primitives", offsetof( Tr2PipelineStatsDataAL, geometryShaderPrimitives ) },
	{ "CInvocations", "Clipping invocations", offsetof( Tr2PipelineStatsDataAL, clippingInvocations ) },
	{ "CPrimitives", "Clipping primitives", offsetof( Tr2PipelineStatsDataAL, clippingPrimitives ) },
	{ "PSInvocations", "Pixel shader invocations", offsetof( Tr2PipelineStatsDataAL, fragmentShaderInvocations ) },
	{ "HSInvocations", "Hull shader invocations", offsetof( Tr2PipelineStatsDataAL, tessControlShaderPatches ) },
	{ "DSInvocations", "Domain shader invocations", offsetof( Tr2PipelineStatsDataAL, tessEvaluationShaderInvocations ) },
	{ "CSInvocations", "Compute shader invocations", offsetof( Tr2PipelineStatsDataAL, computeShaderInvocations ) },
};

}

Tr2PipelineStatsQueryAL::Tr2PipelineStatsQueryAL()
{
}

Tr2PipelineStatsQueryAL::~Tr2PipelineStatsQueryAL()
{
	Destroy();
}

ALResult Tr2PipelineStatsQueryAL::Create( Tr2PrimaryRenderContextAL& renderContext )
{
	m_device = renderContext.GetVulkanContext().state.device;

	VkQueryPoolCreateInfo createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
	createInfo.queryType = VK_QUERY_TYPE_PIPELINE_STATISTICS;
	createInfo.queryCount = 1;
	createInfo.pipelineStatistics =
		VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_VERTICES_BIT |
		VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_PRIMITIVES_BIT |
		VK_QUERY_PIPELINE_STATISTIC_VERTEX_SHADER_INVOCATIONS_BIT |
		VK_QUERY_PIPELINE_STATISTIC_GEOMETRY_SHADER_INVOCATIONS_BIT |
		VK_QUERY_PIPELINE_STATISTIC_GEOMETRY_SHADER_PRIMITIVES_BIT |
		VK_QUERY_PIPELINE_STATISTIC_CLIPPING_INVOCATIONS_BIT |
		VK_QUERY_PIPELINE_STATISTIC_CLIPPING_PRIMITIVES_BIT |
		VK_QUERY_PIPELINE_STATISTIC_FRAGMENT_SHADER_INVOCATIONS_BIT |
		VK_QUERY_PIPELINE_STATISTIC_TESSELLATION_CONTROL_SHADER_PATCHES_BIT |
		VK_QUERY_PIPELINE_STATISTIC_TESSELLATION_EVALUATION_SHADER_INVOCATIONS_BIT |
		VK_QUERY_PIPELINE_STATISTIC_COMPUTE_SHADER_INVOCATIONS_BIT;

	VkResult result = vkCreateQueryPool( m_device, &createInfo, nullptr, &m_queryPool );
	if( result != VK_SUCCESS )
	{
		CCP_AL_LOGERR( "vkCreateQueryPool (pipeline stats) failed: %d", int( result ) );
		return E_FAIL;
	}
	return S_OK;
}

bool Tr2PipelineStatsQueryAL::IsValid() const
{
	return m_queryPool != VK_NULL_HANDLE;
}

void Tr2PipelineStatsQueryAL::Destroy()
{
	if( m_queryPool != VK_NULL_HANDLE )
	{
		vkDestroyQueryPool( m_device, m_queryPool, nullptr );
		m_queryPool = VK_NULL_HANDLE;
	}
	m_isRunning = false;
}

ALResult Tr2PipelineStatsQueryAL::Begin( Tr2RenderContextAL& renderContext )
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

ALResult Tr2PipelineStatsQueryAL::End( Tr2RenderContextAL& renderContext )
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

ALResult Tr2PipelineStatsQueryAL::GetStats( Tr2PipelineStatsDataAL& data, Tr2RenderContextAL& renderContext )
{
	(void)renderContext;
	if( m_queryPool == VK_NULL_HANDLE )
	{
		return E_INVALIDCALL;
	}
	VkResult result = vkGetQueryPoolResults( m_device, m_queryPool, 0, 1,
		sizeof( data ), &data, sizeof( uint64_t ), VK_QUERY_RESULT_64_BIT );
	if( result != VK_SUCCESS )
	{
		return E_FAIL;
	}
	return S_OK;
}

size_t Tr2PipelineStatsQueryAL::GetValueCount( const Tr2PipelineStatsDataAL& data )
{
	(void)data;
	return PIPELINE_STAT_QUERY_COUNT;
}

const char* Tr2PipelineStatsQueryAL::GetLabel( const Tr2PipelineStatsDataAL& data, size_t index )
{
	(void)data;
	if( index >= PIPELINE_STAT_QUERY_COUNT )
	{
		return "";
	}
	return STAT_ENTRIES[index].label;
}

const char* Tr2PipelineStatsQueryAL::GetDescription( const Tr2PipelineStatsDataAL& data, size_t index )
{
	(void)data;
	if( index >= PIPELINE_STAT_QUERY_COUNT )
	{
		return "";
	}
	return STAT_ENTRIES[index].description;
}

::Tr2PipelineStatsQueryAL::Value Tr2PipelineStatsQueryAL::GetValue( const Tr2PipelineStatsDataAL& data, size_t index )
{
	if( index >= PIPELINE_STAT_QUERY_COUNT )
	{
		return 0;
	}
	return *reinterpret_cast<const uint64_t*>(
		reinterpret_cast<const char*>( &data ) + STAT_ENTRIES[index].offset );
}

void Tr2PipelineStatsQueryAL::Describe( Tr2DeviceResourceDescriptionAL& description ) const
{
	description["type"] = "pipeline_stats_query";
}

ALResult Tr2PipelineStatsQueryAL::SetName( const char* name )
{
	SetVulkanObjectName( m_device, reinterpret_cast<uint64_t>( m_queryPool ),
		VK_OBJECT_TYPE_QUERY_POOL, name );
	return S_OK;
}

}

#endif
