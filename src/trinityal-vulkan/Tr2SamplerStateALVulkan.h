// Copyright © 2026 Ithax contributors.

#pragma once

#if TRINITY_PLATFORM == TRINITY_VULKAN

#include "Tr2SamplerStateAL.h"
#include "Tr2HalHelperStructures.h"
#include "Tr2VulkanContext.h"

namespace TrinityALImpl
{
class Tr2SamplerStateAL : public Tr2DeviceResourceAL<Tr2SamplerStateAL>
{
public:
	Tr2SamplerStateAL();
	~Tr2SamplerStateAL();

	ALResult Create( const Tr2SamplerDescription& description, Tr2RenderContextAL& renderContext );
	void Destroy();

	uint32_t GetIndexInHeap() const;

	bool IsValid() const;

	Tr2ALMemoryType GetMemoryClass() const
	{
		return AL_MEMORY_MANAGED;
	}

	void Describe( Tr2DeviceResourceDescriptionAL& description ) const;
	ALResult SetName( const char* name );

	VkSampler GetSampler() const;

private:
	VkSampler m_sampler = VK_NULL_HANDLE;
	VkDevice m_device = VK_NULL_HANDLE;
	Tr2SamplerDescription m_description;
};
}

#endif
