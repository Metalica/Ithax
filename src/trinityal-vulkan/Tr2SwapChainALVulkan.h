// Copyright © 2026 Ithax contributors.

#pragma once

#if TRINITY_PLATFORM == TRINITY_VULKAN

#include "Tr2SwapChainAL.h"
#include "Tr2TextureAL.h"
#include "Tr2VulkanContext.h"

namespace TrinityALImpl
{
class Tr2SwapChainAL : public Tr2DeviceResourceAL<Tr2SwapChainAL>
{
public:
	Tr2SwapChainAL();
	~Tr2SwapChainAL();

	ALResult Create( Tr2WindowHandle windowHandle, Tr2RenderContextAL& renderContext );
	void Destroy();

	bool IsValid() const;

	ALResult Present( Tr2RenderContextAL& renderContext );

	uint32_t GetWidth() const;
	uint32_t GetHeight() const;

	Tr2ALMemoryType GetMemoryClass() const
	{
		return AL_MEMORY_VIDEO;
	}
	void Describe( Tr2DeviceResourceDescriptionAL& description ) const;
	ALResult SetName( const char* name );

	::Tr2TextureAL m_backBuffer;

private:
	Tr2WindowHandle m_windowHandle = nullptr;
	VkDevice m_device = VK_NULL_HANDLE;
};
}

#endif
