// Copyright © 2026 Ithax contributors.

#include "StdAfx.h"
#include "Tr2ResourceSetALVulkan.h"

#if TRINITY_PLATFORM == TRINITY_VULKAN

#include "ALLog.h"
#include "Tr2PrimaryRenderContextAL.h"
#include "Tr2ShaderProgramAL.h"
#include "Tr2BufferALVulkan.h"
#include "Tr2TextureALVulkan.h"
#include "Tr2SamplerStateALVulkan.h"
#include "Tr2ShaderProgramALVulkan.h"

namespace TrinityALImpl
{

namespace
{

constexpr uint32_t BINDING_CONSTANT_BUFFER = 0;
constexpr uint32_t BINDING_SRV = 1;
constexpr uint32_t BINDING_UAV = 2;
constexpr uint32_t BINDING_SAMPLER = 3;

}

Tr2ResourceSetAL::Tr2ResourceSetAL()
{
}

Tr2ResourceSetAL::~Tr2ResourceSetAL()
{
	Destroy();
}

ALResult Tr2ResourceSetAL::Create( const Tr2ResourceSetDescriptionAL& description, const ::Tr2ShaderProgramAL& program, Tr2PrimaryRenderContextAL& renderContext )
{
	m_device = renderContext.GetVulkanContext().state.device;
	m_pool = renderContext.GetVulkanContext().GetCurrentDescriptorPool();
	m_layout = program.TrinityALImpl_GetObject()->GetDescriptorSetLayout();

	VkDescriptorSetAllocateInfo allocInfo = {};
	allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocInfo.descriptorPool = m_pool;
	allocInfo.descriptorSetCount = 1;
	allocInfo.pSetLayouts = &m_layout;

	VkResult result = vkAllocateDescriptorSets( m_device, &allocInfo, &m_descriptorSet );
	if( result != VK_SUCCESS )
	{
		CCP_AL_LOGERR( "vkAllocateDescriptorSets failed: %d", int( result ) );
		return E_FAIL;
	}

	std::vector<VkWriteDescriptorSet> writes;
	std::vector<VkDescriptorBufferInfo> bufferInfos;
	std::vector<VkDescriptorImageInfo> imageInfos;
	std::vector<VkDescriptorImageInfo> samplerInfos;
	// Pre-reserve so the pointers captured below stay stable across the
	// write collection loop.
	writes.reserve( 16 );
	bufferInfos.reserve( 8 );
	imageInfos.reserve( Tr2RegisterMapAL::MAX_RESOURCES_IN_STAGE + 8 );
	samplerInfos.reserve( Tr2RegisterMapAL::MAX_RESOURCES_IN_STAGE );

	const Tr2RegisterMapAL& registerMap = description.m_registerMap;

	for( uint32_t stage = 0; stage < Tr2RenderContextEnum::SHADER_TYPE_COUNT; ++stage )
	{
		for( uint32_t reg = 0; reg < Tr2RegisterMapAL::MAX_RESOURCES_IN_STAGE; ++reg )
		{
			uint8_t srvIndex = registerMap.srvs[stage][reg];
			if( srvIndex < registerMap.srvCount )
			{
				const auto& resource = description.m_srv[srvIndex];
				if( resource.type == Tr2ResourceSetDescriptionAL::Resource::BUFFER )
				{
					VkDescriptorBufferInfo info = resource.buffer.TrinityALImpl_GetObject()->GetDescriptorInfo();
					bufferInfos.push_back( info );
					VkWriteDescriptorSet write = {};
					write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
					write.dstSet = m_descriptorSet;
					write.dstBinding = BINDING_SRV;
					write.dstArrayElement = srvIndex;
					write.descriptorCount = 1;
					write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
					write.pBufferInfo = &bufferInfos.back();
					writes.push_back( write );
				}
				else if( resource.type == Tr2ResourceSetDescriptionAL::Resource::TEXTURE )
				{
					VkDescriptorImageInfo info = resource.texture.TrinityALImpl_GetObject()->GetDescriptorInfo( resource.colorSpace );
					// The descriptor is a combined image sampler; attach the
					// sampler that the description registered for this
					// register (binding 3) so the write is valid.
					uint8_t samplerIndex = registerMap.samplers[stage][reg];
					if( samplerIndex < registerMap.samplerCount &&
						description.m_samplers[samplerIndex].type ==
							Tr2ResourceSetDescriptionAL::Sampler::SAMPLER )
					{
						info.sampler = description.m_samplers[samplerIndex].sampler.TrinityALImpl_GetObject()->GetSampler();
					}
					// The layout binds a 32-wide array; a non-arrayed SPIR-V
					// variable on that binding makes validation require every
					// element to be initialized. Populate all 32 with the
					// same image so the shader access is always valid.
					const uint32_t elementCount = Tr2RegisterMapAL::MAX_RESOURCES_IN_STAGE;
					const size_t baseIndex = imageInfos.size();
					imageInfos.insert( imageInfos.end(), elementCount, info );
					VkWriteDescriptorSet write = {};
					write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
					write.dstSet = m_descriptorSet;
					write.dstBinding = BINDING_SRV;
					write.dstArrayElement = 0;
					write.descriptorCount = elementCount;
					write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
					write.pImageInfo = &imageInfos[baseIndex];
					writes.push_back( write );
				}
			}

			uint8_t uavIndex = registerMap.uavs[stage][reg];
			if( uavIndex < registerMap.uavCount )
			{
				const auto& resource = description.m_uav[uavIndex];
				if( resource.type == Tr2ResourceSetDescriptionAL::Resource::BUFFER )
				{
					VkDescriptorBufferInfo info = resource.buffer.TrinityALImpl_GetObject()->GetDescriptorInfo();
					bufferInfos.push_back( info );
					VkWriteDescriptorSet write = {};
					write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
					write.dstSet = m_descriptorSet;
					write.dstBinding = BINDING_UAV;
					write.dstArrayElement = uavIndex;
					write.descriptorCount = 1;
					write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
					write.pBufferInfo = &bufferInfos.back();
					writes.push_back( write );
				}
				else if( resource.type == Tr2ResourceSetDescriptionAL::Resource::TEXTURE )
				{
					VkDescriptorImageInfo info = {};
					info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
					info.imageView = resource.texture.TrinityALImpl_GetObject()->GetView( resource.mip );
					imageInfos.push_back( info );
					VkWriteDescriptorSet write = {};
					write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
					write.dstSet = m_descriptorSet;
					write.dstBinding = BINDING_UAV;
					write.dstArrayElement = uavIndex;
					write.descriptorCount = 1;
					write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
					write.pImageInfo = &imageInfos.back();
					writes.push_back( write );
				}
			}

			uint8_t samplerIndex = registerMap.samplers[stage][reg];
			if( samplerIndex < registerMap.samplerCount )
			{
				const auto& sampler = description.m_samplers[samplerIndex];
				if( sampler.type == Tr2ResourceSetDescriptionAL::Sampler::SAMPLER )
				{
					VkDescriptorImageInfo info = {};
					info.sampler = sampler.sampler.TrinityALImpl_GetObject()->GetSampler();
					// Same 32-wide population requirement as textures.
					const uint32_t elementCount = Tr2RegisterMapAL::MAX_RESOURCES_IN_STAGE;
					const size_t baseIndex = samplerInfos.size();
					samplerInfos.insert( samplerInfos.end(), elementCount, info );
					VkWriteDescriptorSet write = {};
					write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
					write.dstSet = m_descriptorSet;
					write.dstBinding = BINDING_SAMPLER;
					write.dstArrayElement = 0;
					write.descriptorCount = elementCount;
					write.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
					write.pImageInfo = &samplerInfos[baseIndex];
					writes.push_back( write );
				}
			}
		}
	}

	if( !writes.empty() )
	{
		vkUpdateDescriptorSets( m_device, static_cast<uint32_t>( writes.size() ), writes.data(), 0, nullptr );
	}

	m_isValid = true;
	return S_OK;
}

bool Tr2ResourceSetAL::IsValid() const
{
	return m_isValid;
}

void Tr2ResourceSetAL::Destroy()
{
	if( m_descriptorSet != VK_NULL_HANDLE && m_pool != VK_NULL_HANDLE )
	{
		vkFreeDescriptorSets( m_device, m_pool, 1, &m_descriptorSet );
		m_descriptorSet = VK_NULL_HANDLE;
	}
	m_isValid = false;
}

Tr2ALMemoryType Tr2ResourceSetAL::GetMemoryClass() const
{
	return AL_MEMORY_MANAGED;
}

void Tr2ResourceSetAL::Describe( Tr2DeviceResourceDescriptionAL& description ) const
{
	description["type"] = "resource_set";
}

ALResult Tr2ResourceSetAL::SetName( const char* name )
{
	(void)name;
	return S_OK;
}

VkDescriptorSet Tr2ResourceSetAL::GetDescriptorSet() const
{
	return m_descriptorSet;
}

VkDescriptorSetLayout Tr2ResourceSetAL::GetLayout() const
{
	return m_layout;
}

}

#endif
