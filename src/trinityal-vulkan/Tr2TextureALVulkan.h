// Copyright © 2026 Ithax contributors.

#pragma once

#if TRINITY_PLATFORM == TRINITY_VULKAN

#include "Tr2TextureAL.h"
#include "Tr2HalHelperStructures.h"
#include "Tr2VulkanContext.h"

namespace TrinityALImpl
{
class Tr2TextureAL : public Tr2DeviceResourceAL<Tr2TextureAL>
{
public:
	Tr2TextureAL();
	~Tr2TextureAL();

	ALResult Create( const Tr2BitmapDimensions& desc, const Tr2MsaaDesc& msaa, Tr2GpuUsage::Type gpuUsage, Tr2CpuUsage::Type cpuUsage, Tr2SubresourceData* initialData, Tr2PrimaryRenderContextAL& renderContext );
	ALResult OpenShared( uintptr_t handle, Tr2GpuUsage::Type gpuUsage, Tr2PrimaryRenderContextAL& renderContext );
	void Destroy();

	bool IsValid() const;
	Tr2ALMemoryType GetMemoryClass() const;
	const Tr2BitmapDimensions& GetDesc() const;
	const Tr2MsaaDesc& GetMsaaDesc() const;
	Tr2GpuUsage::Type GetGpuUsage() const;
	Tr2CpuUsage::Type GetCpuUsage() const;

	ALResult MapForReading( const Tr2TextureSubresource& region, const void*& data, uint32_t& pitch, Tr2RenderContextAL& renderContext );
	ALResult MapForReading( const Tr2TextureSubresource& region, bool synchronize, const void*& data, uint32_t& pitch, Tr2RenderContextAL& renderContext );
	void UnmapForReading( Tr2RenderContextAL& renderContext );
	ALResult InvalidateReadback( Tr2RenderContextAL& renderContext );
	ALResult MapForWriting( const Tr2TextureSubresource& region, void*& data, uint32_t& pitch, Tr2RenderContextAL& renderContext );
	void UnmapForWriting( Tr2RenderContextAL& renderContext );

	ALResult UpdateSubresource( const Tr2TextureSubresource& region, const void* source, uint32_t pitch, uint32_t slicePitch, Tr2RenderContextAL& renderContext );
	ALResult CopySubresourceRegion( const Tr2TextureSubresource& destSubresource, Tr2TextureAL& source, const Tr2TextureSubresource& sourceSubresource, Tr2RenderContextAL& renderContext );
	ALResult GenerateMipMaps( Tr2RenderContextAL& renderContext );
	ALResult Resolve( Tr2TextureAL& destination, Tr2RenderContextAL& renderContext );
	uintptr_t GetSharedHandle() const;
	void Describe( Tr2DeviceResourceDescriptionAL& description ) const;
	ALResult SetName( const char* name );
	const char* GetName() const;

	uint32_t GetSrvIndexInHeap( Tr2RenderContextEnum::ColorSpace colorSpace = Tr2RenderContextEnum::COLOR_SPACE_LINEAR ) const;
	uint32_t GetUavIndexInHeap( uint32_t mip ) const;

	VkImage GetImage() const;
	VkImageView GetView( Tr2RenderContextEnum::ColorSpace colorSpace ) const;
	VkImageView GetView( uint32_t mip ) const;
	VkFormat GetVkFormat() const;
	VkImageLayout GetLayout() const;
	void SetLayout( VkImageLayout layout );
	VkDescriptorImageInfo GetDescriptorInfo( Tr2RenderContextEnum::ColorSpace colorSpace ) const;
	VkImageSubresourceRange GetSubresourceRange() const;
	void AttachSwapchainImage( VkImage image, VkImageView view, VkFormat format, uint32_t width, uint32_t height, VkImageLayout layout );
	void SetDevice( VkDevice device, VmaAllocator allocator );

private:
	ALResult CreateImage( const Tr2BitmapDimensions& desc, const Tr2MsaaDesc& msaa, Tr2GpuUsage::Type gpuUsage, Tr2CpuUsage::Type cpuUsage, Tr2SubresourceData* initialData, Tr2PrimaryRenderContextAL& renderContext );
	ALResult CreateViews();
	void DestroyViews();

	Tr2BitmapDimensions m_desc;
	Tr2MsaaDesc m_msaa;
	Tr2GpuUsage::Type m_gpuUsage = Tr2GpuUsage::NONE;
	Tr2CpuUsage::Type m_cpuUsage = Tr2CpuUsage::NONE;
	VkImage m_image = VK_NULL_HANDLE;
	VmaAllocation m_allocation = VK_NULL_HANDLE;
	VkFormat m_format = VK_FORMAT_UNDEFINED;
	VkImageLayout m_layout = VK_IMAGE_LAYOUT_UNDEFINED;
	VkImageView m_linearView = VK_NULL_HANDLE;
	VkImageView m_srgbView = VK_NULL_HANDLE;
	VkImageView m_uavView = VK_NULL_HANDLE;
	VkDevice m_device = VK_NULL_HANDLE;
	VmaAllocator m_allocator = VK_NULL_HANDLE;
	bool m_ownsViews = true;
	std::string m_name;
	uint64_t m_retireTimeline = 0;
	VkBuffer m_readbackBuffer = VK_NULL_HANDLE;
	VmaAllocation m_readbackAllocation = VK_NULL_HANDLE;
	void* m_readbackMapped = nullptr;
	uint32_t m_readbackPitch = 0;
	uint32_t m_readbackHeight = 0;

	friend class Tr2RenderContextAL;
	friend class Tr2PrimaryRenderContextAL;
	friend class Tr2ResourceSetAL;
	friend class Tr2SwapChainAL;
};
}

#endif
