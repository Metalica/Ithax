// Copyright © 2026 Ithax contributors.

#pragma once

#if TRINITY_PLATFORM == TRINITY_VULKAN

#include "Tr2PipelineStatsQueryAL.h"
#include "Tr2VulkanContext.h"

namespace TrinityALImpl
{
struct Tr2PipelineStatsDataAL
{
	uint64_t inputAssemblyVertices = 0;
	uint64_t inputAssemblyPrimitives = 0;
	uint64_t vertexShaderInvocations = 0;
	uint64_t geometryShaderInvocations = 0;
	uint64_t geometryShaderPrimitives = 0;
	uint64_t clippingInvocations = 0;
	uint64_t clippingPrimitives = 0;
	uint64_t fragmentShaderInvocations = 0;
	uint64_t tessControlShaderPatches = 0;
	uint64_t tessEvaluationShaderInvocations = 0;
	uint64_t computeShaderInvocations = 0;
};

class Tr2PipelineStatsQueryAL : public Tr2DeviceResourceAL<Tr2PipelineStatsQueryAL>
{
public:
	Tr2PipelineStatsQueryAL();
	~Tr2PipelineStatsQueryAL();

	ALResult Create( Tr2PrimaryRenderContextAL& renderContext );
	bool IsValid() const;
	void Destroy();

	ALResult Begin( Tr2RenderContextAL& renderContext );
	ALResult End( Tr2RenderContextAL& renderContext );

	ALResult GetStats( Tr2PipelineStatsDataAL& data, Tr2RenderContextAL& renderContext );

	static size_t GetValueCount( const Tr2PipelineStatsDataAL& data );
	static const char* GetLabel( const Tr2PipelineStatsDataAL& data, size_t index );
	static const char* GetDescription( const Tr2PipelineStatsDataAL& data, size_t index );
	static ::Tr2PipelineStatsQueryAL::Value GetValue( const Tr2PipelineStatsDataAL& data, size_t index );

	Tr2ALMemoryType GetMemoryClass() const
	{
		return AL_MEMORY_MANAGED;
	}
	void Describe( Tr2DeviceResourceDescriptionAL& description ) const;
	ALResult SetName( const char* name );

private:
	Tr2PipelineStatsQueryAL( const Tr2PipelineStatsQueryAL& ) = delete;
	Tr2PipelineStatsQueryAL& operator=( const Tr2PipelineStatsQueryAL& ) = delete;

	VkDevice m_device = VK_NULL_HANDLE;
	VkQueryPool m_queryPool = VK_NULL_HANDLE;
	bool m_isRunning = false;
};
}

#endif
