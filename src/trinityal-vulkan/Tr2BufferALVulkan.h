// Copyright © 2026 Ithax contributors.

#pragma once

#if TRINITY_PLATFORM == TRINITY_VULKAN

#include "Tr2BufferAL.h"
#include "Tr2VulkanContext.h"

namespace TrinityALImpl
{
class Tr2BufferAL : public Tr2DeviceResourceAL<Tr2BufferAL>
{
public:
	Tr2BufferAL();
	~Tr2BufferAL();

	ALResult Create(
		const Tr2BufferDescriptionAL& desc,
		const void* initialData,
		Tr2PrimaryRenderContextAL& renderContext );
	void Destroy();

	bool IsValid() const;
	Tr2ALMemoryType GetMemoryClass() const;
	const Tr2BufferDescriptionAL& GetDesc() const;

	ALResult MapForReading( const void*& data, Tr2RenderContextAL& renderContext );
	ALResult MapForReading( const void*& data, uint32_t offset, uint32_t size, Tr2RenderContextAL& renderContext );
	void UnmapForReading( Tr2RenderContextAL& renderContext );
	ALResult MapForWriting( void*& data, Tr2RenderContextAL& renderContext );
	void UnmapForWriting( Tr2RenderContextAL& renderContext );

	ALResult UpdateBuffer( uint32_t offset, uint32_t size, const void* data, Tr2RenderContextAL& renderContext );
	void Describe( Tr2DeviceResourceDescriptionAL& description ) const;
	ALResult SetName( const char* name );

	uint32_t GetSrvIndexInHeap() const;
	uint32_t GetUavIndexInHeap() const;

	VkBuffer GetBuffer() const;
	VkDeviceSize GetSizeBytes() const;
	VkBufferUsageFlags GetUsageFlags() const;
	VkDescriptorBufferInfo GetDescriptorInfo() const;

private:
	Tr2BufferDescriptionAL m_desc;
	VkBuffer m_buffer = VK_NULL_HANDLE;
	VmaAllocation m_allocation = VK_NULL_HANDLE;
	VkDeviceSize m_sizeBytes = 0;
	VkBufferUsageFlags m_usageFlags = 0;
	VkMemoryPropertyFlags m_memoryFlags = 0;
	void* m_mappedData = nullptr;
	bool m_isMapped = false;
	VkDevice m_device = VK_NULL_HANDLE;
	VmaAllocator m_allocator = VK_NULL_HANDLE;
	uint64_t m_retireTimeline = 0;

	friend class Tr2RenderContextAL;
	friend class Tr2PrimaryRenderContextAL;
	friend class Tr2ResourceSetAL;
};
}

#endif
