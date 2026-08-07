// Copyright © 2026 Ithax contributors.

#pragma once

#if TRINITY_PLATFORM == TRINITY_VULKAN

#include "Tr2ConstantBufferAL.h"
#include "Tr2VulkanContext.h"

namespace TrinityALImpl
{
class Tr2ConstantBufferAL : public Tr2DeviceResourceAL<Tr2ConstantBufferAL>
{
public:
	Tr2ConstantBufferAL();
	~Tr2ConstantBufferAL();

	ALResult Create( uint32_t size, Tr2ConstantUsageAL::Type usage, const void* initialData, Tr2RenderContextAL& renderContext );
	void Destroy();

	ALResult Lock( void** data, Tr2RenderContextAL& renderContext );
	ALResult Unlock( Tr2RenderContextAL& renderContext );

	bool IsValid() const;
	uint32_t GetSize() const;
	Tr2ALMemoryType GetMemoryClass() const;
	void Describe( Tr2DeviceResourceDescriptionAL& description ) const;
	ALResult SetName( const char* name );

	VkBuffer GetBuffer() const;
	VkDeviceSize GetSizeBytes() const;
	VkDescriptorBufferInfo GetDescriptorInfo() const;

private:
	Tr2ConstantBufferAL( const Tr2ConstantBufferAL& ) = delete;
	Tr2ConstantBufferAL& operator=( const Tr2ConstantBufferAL& ) = delete;

	uint32_t m_size = 0;
	Tr2ConstantUsageAL::Type m_usage = Tr2ConstantUsageAL::IMMUTABLE;
	VkBuffer m_buffer = VK_NULL_HANDLE;
	VmaAllocation m_allocation = VK_NULL_HANDLE;
	VkDevice m_device = VK_NULL_HANDLE;
	VmaAllocator m_allocator = VK_NULL_HANDLE;
	void* m_mappedData = nullptr;
	bool m_isMapped = false;

	friend class Tr2RenderContextAL;
};
}

#endif
