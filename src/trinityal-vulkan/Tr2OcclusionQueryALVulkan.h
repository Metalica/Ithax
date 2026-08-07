// Copyright © 2026 Ithax contributors.

#pragma once

#if TRINITY_PLATFORM == TRINITY_VULKAN

#include "Tr2OcclusionQueryAL.h"
#include "Tr2VulkanContext.h"

namespace TrinityALImpl
{
class Tr2OcclusionQueryAL : public Tr2DeviceResourceAL<Tr2OcclusionQueryAL>
{
public:
	Tr2OcclusionQueryAL();
	~Tr2OcclusionQueryAL();

	ALResult Create( Tr2RenderContextAL& renderContext );
	bool IsValid() const;
	void Destroy();

	ALResult Begin( Tr2RenderContextAL& renderContext );
	ALResult End( Tr2RenderContextAL& renderContext );
	ALResult GetPixelCount( Tr2RenderContextAL& renderContext, uint32_t& count, ::Tr2OcclusionQueryAL::WaitMode waitMode );

	Tr2ALMemoryType GetMemoryClass() const
	{
		return AL_MEMORY_VIDEO;
	}
	void Describe( Tr2DeviceResourceDescriptionAL& description ) const;
	ALResult SetName( const char* name );

private:
	VkDevice m_device = VK_NULL_HANDLE;
	VkQueryPool m_queryPool = VK_NULL_HANDLE;
	bool m_isRunning = false;
	Tr2OcclusionQueryAL( const Tr2OcclusionQueryAL& ) = delete;
	Tr2OcclusionQueryAL& operator=( const Tr2OcclusionQueryAL& ) = delete;
};
}

#endif
