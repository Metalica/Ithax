// Copyright © 2026 Ithax contributors.

#include "StdAfx.h"
#include "Tr2BufferALVulkan.h"

#if TRINITY_PLATFORM == TRINITY_VULKAN

#include "ALLog.h"
#include "Tr2PrimaryRenderContextAL.h"
#include "Tr2RenderContextAL.h"

namespace TrinityALImpl
{

Tr2BufferAL::Tr2BufferAL()
{
}

Tr2BufferAL::~Tr2BufferAL()
{
	Destroy();
}

ALResult Tr2BufferAL::Create(
	const Tr2BufferDescriptionAL& desc,
	const void* initialData,
	Tr2PrimaryRenderContextAL& renderContext )
{
	m_desc = desc;
	m_device = renderContext.GetVulkanContext().state.device;
	m_allocator = renderContext.GetVulkanContext().state.allocator;

	uint32_t stride = desc.stride;
	if( desc.format != Tr2RenderContextEnum::PIXEL_FORMAT_UNKNOWN )
	{
		stride = ImageIO::GetBytesPerPixel( desc.format );
	}
	m_sizeBytes = static_cast<VkDeviceSize>( stride ) * desc.count;
	m_usageFlags = ConvertGpuUsageToBufferUsage( desc.gpuUsage );
	m_memoryFlags = ConvertCpuUsageToMemoryFlags( desc.cpuUsage );

	VkBufferCreateInfo bufferInfo = {};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size = m_sizeBytes;
	bufferInfo.usage = m_usageFlags;
	bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VmaAllocationCreateInfo allocInfo = {};
	allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
	allocInfo.requiredFlags = m_memoryFlags;

	VkResult result = vmaCreateBuffer( m_allocator, &bufferInfo, &allocInfo,
		&m_buffer, &m_allocation, nullptr );
	if( result != VK_SUCCESS )
	{
		CCP_AL_LOGERR( "vmaCreateBuffer failed: %d", int( result ) );
		return E_OUTOFMEMORY;
	}

	if( initialData != nullptr )
	{
		ALResult updateResult = UpdateBuffer( 0, static_cast<uint32_t>( m_sizeBytes ), initialData, renderContext );
		if( FAILED( updateResult ) )
		{
			Destroy();
			return updateResult;
		}
	}

	return S_OK;
}

void Tr2BufferAL::Destroy()
{
	if( m_buffer != VK_NULL_HANDLE )
	{
		if( m_isMapped )
		{
			vmaUnmapMemory( m_allocator, m_allocation );
			m_isMapped = false;
			m_mappedData = nullptr;
		}
		vmaDestroyBuffer( m_allocator, m_buffer, m_allocation );
		m_buffer = VK_NULL_HANDLE;
		m_allocation = VK_NULL_HANDLE;
	}
}

bool Tr2BufferAL::IsValid() const
{
	return m_buffer != VK_NULL_HANDLE;
}

Tr2ALMemoryType Tr2BufferAL::GetMemoryClass() const
{
	return AL_MEMORY_VIDEO;
}

const Tr2BufferDescriptionAL& Tr2BufferAL::GetDesc() const
{
	return m_desc;
}

ALResult Tr2BufferAL::MapForReading( const void*& data, Tr2RenderContextAL& renderContext )
{
	return MapForReading( data, 0, static_cast<uint32_t>( m_sizeBytes ), renderContext );
}

ALResult Tr2BufferAL::MapForReading( const void*& data, uint32_t offset, uint32_t size, Tr2RenderContextAL& renderContext )
{
	(void)renderContext;
	if( offset + size > m_sizeBytes )
	{
		return E_INVALIDARG;
	}
	if( !m_isMapped )
	{
		VkResult result = vmaMapMemory( m_allocator, m_allocation, &m_mappedData );
		if( result != VK_SUCCESS )
		{
			return E_FAIL;
		}
		m_isMapped = true;
	}
	data = static_cast<const char*>( m_mappedData ) + offset;
	return S_OK;
}

void Tr2BufferAL::UnmapForReading( Tr2RenderContextAL& renderContext )
{
	(void)renderContext;
	if( m_isMapped )
	{
		vmaUnmapMemory( m_allocator, m_allocation );
		m_isMapped = false;
		m_mappedData = nullptr;
	}
}

ALResult Tr2BufferAL::MapForWriting( void*& data, Tr2RenderContextAL& renderContext )
{
	(void)renderContext;
	if( !m_isMapped )
	{
		VkResult result = vmaMapMemory( m_allocator, m_allocation, &m_mappedData );
		if( result != VK_SUCCESS )
		{
			return E_FAIL;
		}
		m_isMapped = true;
	}
	data = m_mappedData;
	return S_OK;
}

void Tr2BufferAL::UnmapForWriting( Tr2RenderContextAL& renderContext )
{
	(void)renderContext;
	if( m_isMapped )
	{
		vmaUnmapMemory( m_allocator, m_allocation );
		m_isMapped = false;
		m_mappedData = nullptr;
	}
}

ALResult Tr2BufferAL::UpdateBuffer( uint32_t offset, uint32_t size, const void* data, Tr2RenderContextAL& renderContext )
{
	if( data == nullptr || offset + size > m_sizeBytes )
	{
		return E_INVALIDARG;
	}

	if( m_memoryFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT )
	{
		void* mapped = nullptr;
		VkResult result = vmaMapMemory( m_allocator, m_allocation, &mapped );
		if( result != VK_SUCCESS )
		{
			return E_FAIL;
		}
		memcpy( static_cast<char*>( mapped ) + offset, data, size );
		vmaUnmapMemory( m_allocator, m_allocation );
		return S_OK;
	}

	VulkanContext& context = renderContext.GetVulkanContext();
	VkCommandBuffer commandBuffer = context.GetCurrentCommandBuffer();

	VkBufferCreateInfo stagingInfo = {};
	stagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	stagingInfo.size = size;
	stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	stagingInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VmaAllocationCreateInfo stagingAllocInfo = {};
	stagingAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
	stagingAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

	VkBuffer stagingBuffer = VK_NULL_HANDLE;
	VmaAllocation stagingAllocation = VK_NULL_HANDLE;
	VkResult result = vmaCreateBuffer( m_allocator, &stagingInfo, &stagingAllocInfo,
		&stagingBuffer, &stagingAllocation, nullptr );
	if( result != VK_SUCCESS )
	{
		return E_OUTOFMEMORY;
	}

	void* mapped = nullptr;
	result = vmaMapMemory( m_allocator, stagingAllocation, &mapped );
	if( result == VK_SUCCESS )
	{
		memcpy( mapped, data, size );
		vmaUnmapMemory( m_allocator, stagingAllocation );
	}

	VkBufferCopy copyRegion = {};
	copyRegion.srcOffset = 0;
	copyRegion.dstOffset = offset;
	copyRegion.size = size;
	vkCmdCopyBuffer( commandBuffer, stagingBuffer, m_buffer, 1, &copyRegion );

	VulkanDeferredDestroy deferred = {};
	deferred.device = m_device;
	deferred.allocator = m_allocator;
	deferred.timelineValue = context.GetCurrentTimelineValue() + 1;
	deferred.buffer = stagingBuffer;
	deferred.allocation = stagingAllocation;
	context.Retire( deferred );

	return S_OK;
}

void Tr2BufferAL::Describe( Tr2DeviceResourceDescriptionAL& description ) const
{
	description["type"] = "buffer";
	description["size"] = std::to_string( m_sizeBytes );
}

ALResult Tr2BufferAL::SetName( const char* name )
{
	SetVulkanObjectName( m_device, reinterpret_cast<uint64_t>( m_buffer ),
		VK_OBJECT_TYPE_BUFFER, name );
	return S_OK;
}

uint32_t Tr2BufferAL::GetSrvIndexInHeap() const
{
	return 0xffffffff;
}

uint32_t Tr2BufferAL::GetUavIndexInHeap() const
{
	return 0xffffffff;
}

VkBuffer Tr2BufferAL::GetBuffer() const
{
	return m_buffer;
}

VkDeviceSize Tr2BufferAL::GetSizeBytes() const
{
	return m_sizeBytes;
}

VkBufferUsageFlags Tr2BufferAL::GetUsageFlags() const
{
	return m_usageFlags;
}

VkDescriptorBufferInfo Tr2BufferAL::GetDescriptorInfo() const
{
	VkDescriptorBufferInfo info = {};
	info.buffer = m_buffer;
	info.offset = 0;
	info.range = m_sizeBytes;
	return info;
}

}

#endif
