// Copyright © 2026 Ithax contributors.

#pragma once

#if TRINITY_PLATFORM == TRINITY_VULKAN

#include "Tr2FenceAL.h"
#include "Tr2VulkanContext.h"

namespace TrinityALImpl
{
class Tr2FenceAL : public Tr2DeviceResourceAL<Tr2FenceAL>
{
public:
	Tr2FenceAL();
	~Tr2FenceAL();

	ALResult Create( Tr2PrimaryRenderContextAL& renderContext );
	void Destroy();

	bool IsValid() const;

	ALResult PutFence( Tr2RenderContextAL& renderContext );
	ALResult IsReached( bool& isReached, Tr2RenderContextAL& renderContext );
	ALResult Wait( Tr2RenderContextAL& renderContext );

	Tr2ALMemoryType GetMemoryClass() const
	{
		return AL_MEMORY_VIDEO;
	}

	void Describe( Tr2DeviceResourceDescriptionAL& description ) const;
	ALResult SetName( const char* name );

private:
	VkDevice m_device = VK_NULL_HANDLE;
	VkSemaphore m_semaphore = VK_NULL_HANDLE;
	uint64_t m_signaledValue = 0;
	bool m_hasFence = false;
};
}

#endif
