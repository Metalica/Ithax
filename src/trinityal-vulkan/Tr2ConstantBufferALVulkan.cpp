// Copyright © 2026 Ithax contributors.

#include "StdAfx.h"
#include "Tr2ConstantBufferALVulkan.h"

#if TRINITY_PLATFORM == TRINITY_VULKAN

#include "ALLog.h"
#include "Tr2RenderContextAL.h"

namespace TrinityALImpl
{

Tr2ConstantBufferAL::Tr2ConstantBufferAL()
{
}

Tr2ConstantBufferAL::~Tr2ConstantBufferAL()
{
	Destroy();
}

ALResult Tr2ConstantBufferAL::Create( uint32_t size, Tr2ConstantUsageAL::Type usage, const void* initialData, Tr2RenderContextAL& renderContext )
{
	m_size = size;
	m_usage = usage;
	m_device = renderContext.GetVulkanContext().state.device;
	m_allocator = renderContext.GetVulkanContext().state.allocator;

	VkBufferCreateInfo bufferInfo = {};
	bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	bufferInfo.size = size;
	bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
	bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VmaAllocationCreateInfo allocInfo = {};
	allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
	allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;

	VkResult result = vmaCreateBuffer( m_allocator, &bufferInfo, &allocInfo,
		&m_buffer, &m_allocation, nullptr );
	if( result != VK_SUCCESS )
	{
		CCP_AL_LOGERR( "vmaCreateBuffer (constant) failed: %d", int( result ) );
		return E_OUTOFMEMORY;
	}

	if( initialData != nullptr )
	{
		void* mapped = nullptr;
		result = vmaMapMemory( m_allocator, m_allocation, &mapped );
		if( result == VK_SUCCESS )
		{
			memcpy( mapped, initialData, size );
			vmaUnmapMemory( m_allocator, m_allocation );
		}
	}

	return S_OK;
}

void Tr2ConstantBufferAL::Destroy()
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

ALResult Tr2ConstantBufferAL::Lock( void** data, Tr2RenderContextAL& renderContext )
{
	(void)renderContext;
	if( data == nullptr )
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
	*data = m_mappedData;
	return S_OK;
}

ALResult Tr2ConstantBufferAL::Unlock( Tr2RenderContextAL& renderContext )
{
	(void)renderContext;
	if( m_isMapped )
	{
		// The allocation may be non-coherent host memory; flush so the GPU
		// observes the updated contents before the frame is submitted.
		vmaFlushAllocation( m_allocator, m_allocation, 0, VK_WHOLE_SIZE );
		vmaUnmapMemory( m_allocator, m_allocation );
		m_isMapped = false;
		m_mappedData = nullptr;
	}
	return S_OK;
}

bool Tr2ConstantBufferAL::IsValid() const
{
	return m_buffer != VK_NULL_HANDLE;
}

uint32_t Tr2ConstantBufferAL::GetSize() const
{
	return m_size;
}

Tr2ALMemoryType Tr2ConstantBufferAL::GetMemoryClass() const
{
	return AL_MEMORY_MANAGED;
}

void Tr2ConstantBufferAL::Describe( Tr2DeviceResourceDescriptionAL& description ) const
{
	description["type"] = "constant_buffer";
	description["size"] = std::to_string( m_size );
}

ALResult Tr2ConstantBufferAL::SetName( const char* name )
{
	(void)name;
	return S_OK;
}

VkBuffer Tr2ConstantBufferAL::GetBuffer() const
{
	return m_buffer;
}

VkDeviceSize Tr2ConstantBufferAL::GetSizeBytes() const
{
	return m_size;
}

VkDescriptorBufferInfo Tr2ConstantBufferAL::GetDescriptorInfo() const
{
	VkDescriptorBufferInfo info = {};
	info.buffer = m_buffer;
	info.offset = 0;
	info.range = m_size;
	return info;
}

}

#endif
