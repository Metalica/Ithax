// Copyright © 2026 Ithax contributors.

#include "StdAfx.h"
#include "Tr2TextureALVulkan.h"

#if TRINITY_PLATFORM == TRINITY_VULKAN

#include "ALLog.h"
#include "Tr2PrimaryRenderContextAL.h"
#include "Tr2RenderContextAL.h"

namespace TrinityALImpl
{

Tr2TextureAL::Tr2TextureAL()
{
}

Tr2TextureAL::~Tr2TextureAL()
{
	Destroy();
}

ALResult Tr2TextureAL::Create(
	const Tr2BitmapDimensions& desc,
	const Tr2MsaaDesc& msaa,
	Tr2GpuUsage::Type gpuUsage,
	Tr2CpuUsage::Type cpuUsage,
	Tr2SubresourceData* initialData,
	Tr2PrimaryRenderContextAL& renderContext )
{
	return CreateImage( desc, msaa, gpuUsage, cpuUsage, initialData, renderContext );
}

ALResult Tr2TextureAL::OpenShared( uintptr_t handle, Tr2GpuUsage::Type gpuUsage, Tr2PrimaryRenderContextAL& renderContext )
{
	(void)handle;
	(void)gpuUsage;
	(void)renderContext;
	return E_NOTIMPL;
}

ALResult Tr2TextureAL::CreateImage(
	const Tr2BitmapDimensions& desc,
	const Tr2MsaaDesc& msaa,
	Tr2GpuUsage::Type gpuUsage,
	Tr2CpuUsage::Type cpuUsage,
	Tr2SubresourceData* initialData,
	Tr2PrimaryRenderContextAL& renderContext )
{
	m_desc = desc;
	m_msaa = msaa;
	m_gpuUsage = gpuUsage;
	m_cpuUsage = cpuUsage;
	m_device = renderContext.GetVulkanContext().state.device;
	m_allocator = renderContext.GetVulkanContext().state.allocator;
	m_format = ConvertPixelFormat( desc.GetFormat() );
	m_layout = VK_IMAGE_LAYOUT_UNDEFINED;

	if( m_format == VK_FORMAT_UNDEFINED )
	{
		CCP_AL_LOGERR( "Unsupported pixel format %d", int( desc.GetFormat() ) );
		return E_INVALIDARG;
	}

	VkImageType imageType = VK_IMAGE_TYPE_2D;
	VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_2D;
	switch( desc.GetType() )
	{
	case Tr2RenderContextEnum::TEX_TYPE_1D:
		imageType = VK_IMAGE_TYPE_1D;
		viewType = VK_IMAGE_VIEW_TYPE_1D;
		break;
	case Tr2RenderContextEnum::TEX_TYPE_3D:
		imageType = VK_IMAGE_TYPE_3D;
		viewType = VK_IMAGE_VIEW_TYPE_3D;
		break;
	case Tr2RenderContextEnum::TEX_TYPE_CUBE:
		imageType = VK_IMAGE_TYPE_2D;
		viewType = VK_IMAGE_VIEW_TYPE_CUBE;
		break;
	default:
		break;
	}

	VkImageCreateInfo imageInfo = {};
	imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	imageInfo.imageType = imageType;
	imageInfo.format = m_format;
	imageInfo.extent = { desc.GetWidth(), desc.GetHeight(), desc.GetDepth() };
	imageInfo.mipLevels = desc.GetMipCount();
	imageInfo.arrayLayers = desc.GetArraySize();
	imageInfo.samples = static_cast<VkSampleCountFlagBits>( msaa.samples );
	imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	imageInfo.usage = ConvertGpuUsageToImageUsage( gpuUsage );
	imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	// UNORM formats with an SRGB variant need MUTABLE_FORMAT so the SRGB
	// view (created for COLOR_SPACE_SRGB sampling) is legal.
	switch( m_format )
	{
	case VK_FORMAT_R8G8B8A8_UNORM:
	case VK_FORMAT_B8G8R8A8_UNORM:
	case VK_FORMAT_BC1_RGBA_UNORM_BLOCK:
	case VK_FORMAT_BC2_UNORM_BLOCK:
	case VK_FORMAT_BC3_UNORM_BLOCK:
	case VK_FORMAT_BC7_UNORM_BLOCK:
		imageInfo.flags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
		break;
	default:
		break;
	}
	if( desc.GetType() == Tr2RenderContextEnum::TEX_TYPE_CUBE )
	{
		imageInfo.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
	}

	VmaAllocationCreateInfo allocInfo = {};
	allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
	allocInfo.requiredFlags = ConvertCpuUsageToMemoryFlags( cpuUsage );

	VkResult result = vmaCreateImage( m_allocator, &imageInfo, &allocInfo,
		&m_image, &m_allocation, nullptr );
	if( result != VK_SUCCESS )
	{
		CCP_AL_LOGERR( "vmaCreateImage failed: %d", int( result ) );
		return E_OUTOFMEMORY;
	}

	CR_RETURN_HR( CreateViews() );

	if( initialData != nullptr )
	{
		Tr2TextureSubresource region( 0 );
		ALResult updateResult = UpdateSubresource( region, initialData->m_sysMem,
			initialData->m_sysMemPitch, initialData->m_sysMemSlicePitch, renderContext );
		if( FAILED( updateResult ) )
		{
			Destroy();
			return updateResult;
		}
	}

	return S_OK;
}

ALResult Tr2TextureAL::CreateViews()
{
	VkImageViewCreateInfo viewInfo = {};
	viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
	viewInfo.image = m_image;
	viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
	viewInfo.format = m_format;
	viewInfo.subresourceRange.aspectMask = GetImageAspectFlags( m_format );
	viewInfo.subresourceRange.baseMipLevel = 0;
	viewInfo.subresourceRange.levelCount = m_desc.GetMipCount();
	viewInfo.subresourceRange.baseArrayLayer = 0;
	viewInfo.subresourceRange.layerCount = m_desc.GetArraySize();

	VkResult result = vkCreateImageView( m_device, &viewInfo, nullptr, &m_linearView );
	if( result != VK_SUCCESS )
	{
		return E_FAIL;
	}

	VkFormat srgbFormat = m_format;
	switch( m_format )
	{
	case VK_FORMAT_R8G8B8A8_UNORM: srgbFormat = VK_FORMAT_R8G8B8A8_SRGB; break;
	case VK_FORMAT_B8G8R8A8_UNORM: srgbFormat = VK_FORMAT_B8G8R8A8_SRGB; break;
	case VK_FORMAT_BC1_RGBA_UNORM_BLOCK: srgbFormat = VK_FORMAT_BC1_RGBA_SRGB_BLOCK; break;
	case VK_FORMAT_BC2_UNORM_BLOCK: srgbFormat = VK_FORMAT_BC2_SRGB_BLOCK; break;
	case VK_FORMAT_BC3_UNORM_BLOCK: srgbFormat = VK_FORMAT_BC3_SRGB_BLOCK; break;
	case VK_FORMAT_BC7_UNORM_BLOCK: srgbFormat = VK_FORMAT_BC7_SRGB_BLOCK; break;
	default: break;
	}

	if( srgbFormat != m_format )
	{
		viewInfo.format = srgbFormat;
		result = vkCreateImageView( m_device, &viewInfo, nullptr, &m_srgbView );
		if( result != VK_SUCCESS )
		{
			return E_FAIL;
		}
	}

	if( Tr2GpuUsage::HasFlag( m_gpuUsage, Tr2GpuUsage::UNORDERED_ACCESS ) )
	{
		viewInfo.format = m_format;
		viewInfo.subresourceRange.levelCount = 1;
		result = vkCreateImageView( m_device, &viewInfo, nullptr, &m_uavView );
		if( result != VK_SUCCESS )
		{
			return E_FAIL;
		}
	}

	return S_OK;
}

void Tr2TextureAL::DestroyViews()
{
	if( !m_ownsViews )
	{
		m_linearView = VK_NULL_HANDLE;
		m_srgbView = VK_NULL_HANDLE;
		m_uavView = VK_NULL_HANDLE;
		return;
	}
	if( m_uavView != VK_NULL_HANDLE )
	{
		vkDestroyImageView( m_device, m_uavView, nullptr );
		m_uavView = VK_NULL_HANDLE;
	}
	if( m_srgbView != VK_NULL_HANDLE )
	{
		vkDestroyImageView( m_device, m_srgbView, nullptr );
		m_srgbView = VK_NULL_HANDLE;
	}
	if( m_linearView != VK_NULL_HANDLE )
	{
		vkDestroyImageView( m_device, m_linearView, nullptr );
		m_linearView = VK_NULL_HANDLE;
	}
}

void Tr2TextureAL::Destroy()
{
	DestroyViews();
	if( m_readbackBuffer != VK_NULL_HANDLE )
	{
		if( m_readbackMapped != nullptr )
		{
			vmaUnmapMemory( m_allocator, m_readbackAllocation );
			m_readbackMapped = nullptr;
		}
		vmaDestroyBuffer( m_allocator, m_readbackBuffer, m_readbackAllocation );
		m_readbackBuffer = VK_NULL_HANDLE;
		m_readbackAllocation = VK_NULL_HANDLE;
	}
	if( m_image != VK_NULL_HANDLE && m_allocation != VK_NULL_HANDLE )
	{
		vmaDestroyImage( m_allocator, m_image, m_allocation );
	}
	m_image = VK_NULL_HANDLE;
	m_allocation = VK_NULL_HANDLE;
}

bool Tr2TextureAL::IsValid() const
{
	return m_image != VK_NULL_HANDLE;
}

Tr2ALMemoryType Tr2TextureAL::GetMemoryClass() const
{
	return AL_MEMORY_VIDEO;
}

const Tr2BitmapDimensions& Tr2TextureAL::GetDesc() const
{
	return m_desc;
}

const Tr2MsaaDesc& Tr2TextureAL::GetMsaaDesc() const
{
	return m_msaa;
}

Tr2GpuUsage::Type Tr2TextureAL::GetGpuUsage() const
{
	return m_gpuUsage;
}

Tr2CpuUsage::Type Tr2TextureAL::GetCpuUsage() const
{
	return m_cpuUsage;
}

ALResult Tr2TextureAL::MapForReading( const Tr2TextureSubresource& region, const void*& data, uint32_t& pitch, Tr2RenderContextAL& renderContext )
{
	return MapForReading( region, true, data, pitch, renderContext );
}

ALResult Tr2TextureAL::MapForReading( const Tr2TextureSubresource& region, bool synchronize, const void*& data, uint32_t& pitch, Tr2RenderContextAL& renderContext )
{
	(void)synchronize;
	if( m_image == VK_NULL_HANDLE )
	{
		return E_INVALIDCALL;
	}

	VulkanContext& context = renderContext.GetVulkanContext();

	Tr2TextureSubresource clampedRegion = region;
	clampedRegion.ClampToTexture( m_desc );

	uint32_t width = clampedRegion.GetWidth();
	uint32_t height = clampedRegion.GetHeight();
	uint32_t depth = clampedRegion.GetDepth();
	uint32_t bytesPerPixel = ImageIO::GetBytesPerPixel( m_desc.GetFormat() );
	if( bytesPerPixel == 0 )
	{
		return E_INVALIDARG;
	}
	pitch = width * bytesPerPixel;
	VkDeviceSize bufferSize = static_cast<VkDeviceSize>( pitch ) * height * depth;
	if( bufferSize == 0 )
	{
		return E_INVALIDARG;
	}

	VkBufferCreateInfo stagingInfo = {};
	stagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	stagingInfo.size = bufferSize;
	stagingInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	stagingInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	VmaAllocationCreateInfo stagingAllocInfo = {};
	stagingAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
	stagingAllocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;

	VkBuffer stagingBuffer = VK_NULL_HANDLE;
	VmaAllocation stagingAllocation = VK_NULL_HANDLE;
	VkResult result = vmaCreateBuffer( m_allocator, &stagingInfo, &stagingAllocInfo,
		&stagingBuffer, &stagingAllocation, nullptr );
	if( result != VK_SUCCESS )
	{
		VmaBudget budgets[VK_MAX_MEMORY_HEAPS] = {};
		vmaGetHeapBudgets( m_allocator, budgets );
		for( uint32_t i = 0; i < VK_MAX_MEMORY_HEAPS; ++i )
		{
			if( budgets[i].statistics.blockCount > 0 || budgets[i].usage > 0 )
			{
				std::fprintf( stderr, "[rb] heap %u: usage=%llu budget=%llu\n", i,
					(unsigned long long)budgets[i].usage, (unsigned long long)budgets[i].budget );
			}
		}
		std::fprintf( stderr, "[rb] vmaCreateBuffer failed: %d (size=%llu)\n", int( result ),
			(unsigned long long)bufferSize );
		return E_OUTOFMEMORY;
	}

	// Record the copy into the frame's own command buffer, ordered before
	// the present transition, so the image is still acquired and its
	// contents are valid. The caller must submit the frame (Present) and
	// wait for completion before reading the returned pointer.
	VkCommandBuffer commandBuffer = context.GetCurrentCommandBuffer();

	VkImageLayout oldLayout = m_layout;
	VkImageLayout newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;

	VkImageMemoryBarrier2 barrier = {};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
	barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
	barrier.srcAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
	barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
	barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
	barrier.oldLayout = oldLayout;
	barrier.newLayout = newLayout;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = m_image;
	barrier.subresourceRange = GetSubresourceRange();

	VkDependencyInfo dependencyInfo = {};
	dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
	dependencyInfo.imageMemoryBarrierCount = 1;
	dependencyInfo.pImageMemoryBarriers = &barrier;
	vkCmdPipelineBarrier2( commandBuffer, &dependencyInfo );

	VkBufferImageCopy copyRegion = {};
	copyRegion.bufferOffset = 0;
	copyRegion.bufferRowLength = 0;
	copyRegion.bufferImageHeight = 0;
	copyRegion.imageSubresource.aspectMask = GetImageAspectFlags( m_format );
	copyRegion.imageSubresource.mipLevel = clampedRegion.m_startMipLevel;
	copyRegion.imageSubresource.baseArrayLayer = clampedRegion.m_startFace;
	copyRegion.imageSubresource.layerCount = clampedRegion.GetFaceCount();
	copyRegion.imageOffset = { static_cast<int32_t>( clampedRegion.m_box.left ), static_cast<int32_t>( clampedRegion.m_box.top ), static_cast<int32_t>( clampedRegion.m_box.front ) };
	copyRegion.imageExtent = { width, height, depth };
	vkCmdCopyImageToBuffer( commandBuffer, m_image, newLayout, stagingBuffer, 1, &copyRegion );

	VkBufferMemoryBarrier2 bufferBarrier = {};
	bufferBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
	bufferBarrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
	bufferBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
	bufferBarrier.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
	bufferBarrier.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
	bufferBarrier.buffer = stagingBuffer;
	bufferBarrier.offset = 0;
	bufferBarrier.size = bufferSize;

	VkDependencyInfo bufferDependency = {};
	bufferDependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
	bufferDependency.bufferMemoryBarrierCount = 1;
	bufferDependency.pBufferMemoryBarriers = &bufferBarrier;
	vkCmdPipelineBarrier2( commandBuffer, &bufferDependency );

	m_layout = newLayout;

	void* mapped = nullptr;
	result = vmaMapMemory( m_allocator, stagingAllocation, &mapped );
	if( result != VK_SUCCESS )
	{
		vmaDestroyBuffer( m_allocator, stagingBuffer, stagingAllocation );
		return E_FAIL;
	}
	data = mapped;

	// Retain the staging buffer until UnmapForReading releases it.
	m_readbackBuffer = stagingBuffer;
	m_readbackAllocation = stagingAllocation;
	m_readbackMapped = mapped;
	m_readbackPitch = pitch;
	m_readbackHeight = height;

	return S_OK;
}

void Tr2TextureAL::UnmapForReading( Tr2RenderContextAL& renderContext )
{
	(void)renderContext;
	if( m_readbackBuffer != VK_NULL_HANDLE )
	{
		if( m_readbackMapped != nullptr )
		{
			vmaUnmapMemory( m_allocator, m_readbackAllocation );
			m_readbackMapped = nullptr;
		}
		vmaDestroyBuffer( m_allocator, m_readbackBuffer, m_readbackAllocation );
		m_readbackBuffer = VK_NULL_HANDLE;
		m_readbackAllocation = VK_NULL_HANDLE;
	}
}

ALResult Tr2TextureAL::InvalidateReadback( Tr2RenderContextAL& renderContext )
{
	(void)renderContext;
	if( m_readbackBuffer == VK_NULL_HANDLE || m_readbackMapped == nullptr )
	{
		return E_INVALIDCALL;
	}
	VkDeviceSize size = static_cast<VkDeviceSize>( m_readbackPitch ) * m_readbackHeight;
	vmaInvalidateAllocation( m_allocator, m_readbackAllocation, 0, size );
	return S_OK;
}

ALResult Tr2TextureAL::MapForWriting( const Tr2TextureSubresource& region, void*& data, uint32_t& pitch, Tr2RenderContextAL& renderContext )
{
	(void)region;
	(void)renderContext;
	data = nullptr;
	pitch = 0;
	return E_NOTIMPL;
}

void Tr2TextureAL::UnmapForWriting( Tr2RenderContextAL& renderContext )
{
	(void)renderContext;
}

ALResult Tr2TextureAL::UpdateSubresource( const Tr2TextureSubresource& region, const void* source, uint32_t pitch, uint32_t slicePitch, Tr2RenderContextAL& renderContext )
{
	if( source == nullptr )
	{
		return E_INVALIDARG;
	}

	VulkanContext& context = renderContext.GetVulkanContext();
	VkCommandBuffer commandBuffer = context.GetCurrentCommandBuffer();

	uint32_t width = region.GetWidth();
	uint32_t height = region.GetHeight();
	uint32_t depth = region.GetDepth();
	VkDeviceSize bufferSize = static_cast<VkDeviceSize>( slicePitch ) * depth;

	VkBufferCreateInfo stagingInfo = {};
	stagingInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	stagingInfo.size = bufferSize;
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
		VmaBudget budgets[VK_MAX_MEMORY_HEAPS] = {};
		vmaGetHeapBudgets( m_allocator, budgets );
		for( uint32_t i = 0; i < VK_MAX_MEMORY_HEAPS; ++i )
		{
			if( budgets[i].statistics.blockCount > 0 || budgets[i].usage > 0 )
			{
				std::fprintf( stderr, "[rb] heap %u: usage=%llu budget=%llu\n", i,
					(unsigned long long)budgets[i].usage,
					(unsigned long long)budgets[i].budget );
			}
		}
		std::fprintf( stderr, "[rb] vmaCreateBuffer failed: %d (size=%llu)\n", int( result ),
			(unsigned long long)bufferSize );
		return E_OUTOFMEMORY;
	}

	void* mapped = nullptr;
	result = vmaMapMemory( m_allocator, stagingAllocation, &mapped );
	if( result == VK_SUCCESS )
	{
		const char* src = static_cast<const char*>( source );
		char* dst = static_cast<char*>( mapped );
		for( uint32_t z = 0; z < depth; ++z )
		{
			for( uint32_t y = 0; y < height; ++y )
			{
				memcpy( dst + z * slicePitch + y * pitch, src + z * slicePitch + y * pitch, pitch );
			}
		}
		vmaUnmapMemory( m_allocator, stagingAllocation );
	}

	VkImageLayout oldLayout = m_layout;
	VkImageLayout newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

	VkImageMemoryBarrier2 barrier = {};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
	barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
	barrier.srcAccessMask = VK_ACCESS_2_MEMORY_READ_BIT;
	barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
	barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
	barrier.oldLayout = oldLayout;
	barrier.newLayout = newLayout;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = m_image;
	barrier.subresourceRange = GetSubresourceRange();

	VkDependencyInfo dependencyInfo = {};
	dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
	dependencyInfo.imageMemoryBarrierCount = 1;
	dependencyInfo.pImageMemoryBarriers = &barrier;
	vkCmdPipelineBarrier2( commandBuffer, &dependencyInfo );

	VkBufferImageCopy copyRegion = {};
	copyRegion.bufferOffset = 0;
	copyRegion.bufferRowLength = 0;
	copyRegion.bufferImageHeight = 0;
	copyRegion.imageSubresource.aspectMask = GetImageAspectFlags( m_format );
	copyRegion.imageSubresource.mipLevel = region.m_startMipLevel;
	copyRegion.imageSubresource.baseArrayLayer = region.m_startFace;
	copyRegion.imageSubresource.layerCount = region.GetFaceCount();
	copyRegion.imageOffset = { static_cast<int32_t>( region.m_box.left ), static_cast<int32_t>( region.m_box.top ), static_cast<int32_t>( region.m_box.front ) };
	copyRegion.imageExtent = { width, height, depth };
	vkCmdCopyBufferToImage( commandBuffer, stagingBuffer, m_image, newLayout, 1, &copyRegion );

	VulkanDeferredDestroy deferred = {};
	deferred.device = m_device;
	deferred.allocator = m_allocator;
	deferred.timelineValue = context.GetCurrentTimelineValue() + 1;
	deferred.buffer = stagingBuffer;
	deferred.allocation = stagingAllocation;
	context.Retire( deferred );

	m_layout = newLayout;
	return S_OK;
}

ALResult Tr2TextureAL::CopySubresourceRegion( const Tr2TextureSubresource& destSubresource, Tr2TextureAL& source, const Tr2TextureSubresource& sourceSubresource, Tr2RenderContextAL& renderContext )
{
	VulkanContext& context = renderContext.GetVulkanContext();
	VkCommandBuffer commandBuffer = context.GetCurrentCommandBuffer();

	VkImageMemoryBarrier2 srcBarrier = {};
	srcBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
	srcBarrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
	srcBarrier.srcAccessMask = VK_ACCESS_2_MEMORY_READ_BIT;
	srcBarrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
	srcBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
	srcBarrier.oldLayout = source.m_layout;
	srcBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	srcBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	srcBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	srcBarrier.image = source.m_image;
	srcBarrier.subresourceRange = source.GetSubresourceRange();

	VkImageMemoryBarrier2 dstBarrier = {};
	dstBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
	dstBarrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
	dstBarrier.srcAccessMask = VK_ACCESS_2_MEMORY_READ_BIT;
	dstBarrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
	dstBarrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
	dstBarrier.oldLayout = m_layout;
	dstBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	dstBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	dstBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	dstBarrier.image = m_image;
	dstBarrier.subresourceRange = GetSubresourceRange();

	VkImageMemoryBarrier2 barriers[2] = { srcBarrier, dstBarrier };
	VkDependencyInfo dependencyInfo = {};
	dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
	dependencyInfo.imageMemoryBarrierCount = 2;
	dependencyInfo.pImageMemoryBarriers = barriers;
	vkCmdPipelineBarrier2( commandBuffer, &dependencyInfo );

	VkImageCopy copyRegion = {};
	copyRegion.srcSubresource.aspectMask = GetImageAspectFlags( source.m_format );
	copyRegion.srcSubresource.mipLevel = sourceSubresource.m_startMipLevel;
	copyRegion.srcSubresource.baseArrayLayer = sourceSubresource.m_startFace;
	copyRegion.srcSubresource.layerCount = sourceSubresource.GetFaceCount();
	copyRegion.srcOffset = { static_cast<int32_t>( sourceSubresource.m_box.left ), static_cast<int32_t>( sourceSubresource.m_box.top ), static_cast<int32_t>( sourceSubresource.m_box.front ) };
	copyRegion.dstSubresource.aspectMask = GetImageAspectFlags( m_format );
	copyRegion.dstSubresource.mipLevel = destSubresource.m_startMipLevel;
	copyRegion.dstSubresource.baseArrayLayer = destSubresource.m_startFace;
	copyRegion.dstSubresource.layerCount = destSubresource.GetFaceCount();
	copyRegion.dstOffset = { static_cast<int32_t>( destSubresource.m_box.left ), static_cast<int32_t>( destSubresource.m_box.top ), static_cast<int32_t>( destSubresource.m_box.front ) };
	copyRegion.extent = { destSubresource.GetWidth(), destSubresource.GetHeight(), destSubresource.GetDepth() };
	vkCmdCopyImage( commandBuffer, source.m_image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, m_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copyRegion );

	source.m_layout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	m_layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	return S_OK;
}

ALResult Tr2TextureAL::GenerateMipMaps( Tr2RenderContextAL& renderContext )
{
	(void)renderContext;
	return E_NOTIMPL;
}

ALResult Tr2TextureAL::Resolve( Tr2TextureAL& destination, Tr2RenderContextAL& renderContext )
{
	(void)destination;
	(void)renderContext;
	return E_NOTIMPL;
}

uintptr_t Tr2TextureAL::GetSharedHandle() const
{
	return 0;
}

void Tr2TextureAL::Describe( Tr2DeviceResourceDescriptionAL& description ) const
{
	description["type"] = "texture";
	description["format"] = std::to_string( int( m_desc.GetFormat() ) );
	description["width"] = std::to_string( m_desc.GetWidth() );
	description["height"] = std::to_string( m_desc.GetHeight() );
}

ALResult Tr2TextureAL::SetName( const char* name )
{
	m_name = name ? name : "";
	return S_OK;
}

const char* Tr2TextureAL::GetName() const
{
	return m_name.c_str();
}

uint32_t Tr2TextureAL::GetSrvIndexInHeap( Tr2RenderContextEnum::ColorSpace colorSpace ) const
{
	(void)colorSpace;
	return 0xffffffff;
}

uint32_t Tr2TextureAL::GetUavIndexInHeap( uint32_t mip ) const
{
	(void)mip;
	return 0xffffffff;
}

VkImage Tr2TextureAL::GetImage() const
{
	return m_image;
}

VkImageView Tr2TextureAL::GetView( Tr2RenderContextEnum::ColorSpace colorSpace ) const
{
	if( colorSpace == Tr2RenderContextEnum::COLOR_SPACE_SRGB && m_srgbView != VK_NULL_HANDLE )
	{
		return m_srgbView;
	}
	return m_linearView;
}

VkImageView Tr2TextureAL::GetView( uint32_t mip ) const
{
	(void)mip;
	return m_uavView;
}

VkFormat Tr2TextureAL::GetVkFormat() const
{
	return m_format;
}

VkImageLayout Tr2TextureAL::GetLayout() const
{
	return m_layout;
}

void Tr2TextureAL::SetLayout( VkImageLayout layout )
{
	m_layout = layout;
}

VkDescriptorImageInfo Tr2TextureAL::GetDescriptorInfo( Tr2RenderContextEnum::ColorSpace colorSpace ) const
{
	VkDescriptorImageInfo info = {};
	info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	info.imageView = GetView( colorSpace );
	return info;
}

VkImageSubresourceRange Tr2TextureAL::GetSubresourceRange() const
{
	VkImageSubresourceRange range = {};
	range.aspectMask = GetImageAspectFlags( m_format );
	range.baseMipLevel = 0;
	range.levelCount = m_desc.GetMipCount();
	range.baseArrayLayer = 0;
	range.layerCount = m_desc.GetArraySize();
	return range;
}

void Tr2TextureAL::AttachSwapchainImage( VkImage image, VkImageView view, VkFormat format, uint32_t width, uint32_t height, VkImageLayout layout )
{
	if( m_ownsViews )
	{
		if( m_uavView != VK_NULL_HANDLE )
		{
			vkDestroyImageView( m_device, m_uavView, nullptr );
		}
		if( m_srgbView != VK_NULL_HANDLE )
		{
			vkDestroyImageView( m_device, m_srgbView, nullptr );
		}
		if( m_linearView != VK_NULL_HANDLE )
		{
			vkDestroyImageView( m_device, m_linearView, nullptr );
		}
		if( m_image != VK_NULL_HANDLE && m_allocation != VK_NULL_HANDLE )
		{
			vmaDestroyImage( m_allocator, m_image, m_allocation );
		}
	}
	m_image = image;
	m_allocation = VK_NULL_HANDLE;
	m_format = format;
	m_layout = layout;
	m_linearView = view;
	m_srgbView = VK_NULL_HANDLE;
	m_uavView = VK_NULL_HANDLE;
	m_ownsViews = false;
	m_desc = Tr2BitmapDimensions(
		Tr2RenderContextEnum::TEX_TYPE_2D,
		ConvertVkFormat( format ),
		width,
		height,
		1,
		1 );
	m_msaa = Tr2MsaaDesc( 1, 0 );
	m_gpuUsage = Tr2GpuUsage::RENDER_TARGET;
	m_cpuUsage = Tr2CpuUsage::NONE;
}

void Tr2TextureAL::SetDevice( VkDevice device, VmaAllocator allocator )
{
	m_device = device;
	m_allocator = allocator;
}

}

#endif
