// Copyright © 2026 Ithax contributors.

#pragma once

#if TRINITY_PLATFORM == TRINITY_VULKAN

#include "Tr2ResourceSetAL.h"
#include "Tr2VulkanContext.h"

namespace TrinityALImpl
{
class Tr2ResourceSetAL : public Tr2DeviceResourceAL<Tr2ResourceSetAL>
{
public:
	Tr2ResourceSetAL();
	~Tr2ResourceSetAL();

	ALResult Create( const Tr2ResourceSetDescriptionAL& description, const ::Tr2ShaderProgramAL& program, Tr2PrimaryRenderContextAL& renderContext );
	bool IsValid() const;

	void Destroy();
	Tr2ALMemoryType GetMemoryClass() const;
	void Describe( Tr2DeviceResourceDescriptionAL& description ) const;
	ALResult SetName( const char* name );

	VkDescriptorSet GetDescriptorSet() const;
	VkDescriptorSetLayout GetLayout() const;

private:
	bool m_isValid = false;
	VkDescriptorSet m_descriptorSet = VK_NULL_HANDLE;
	VkDescriptorSetLayout m_layout = VK_NULL_HANDLE;
	VkDevice m_device = VK_NULL_HANDLE;
	VkDescriptorPool m_pool = VK_NULL_HANDLE;
};
}

#endif
