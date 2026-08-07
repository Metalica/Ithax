// Copyright © 2026 Ithax contributors.

#pragma once

#if TRINITY_PLATFORM == TRINITY_VULKAN

#include "Tr2ShaderProgramAL.h"
#include "Tr2ResourceSetAL.h"
#include "Tr2VulkanContext.h"

namespace TrinityALImpl
{
class Tr2ShaderProgramAL : public Tr2DeviceResourceAL<Tr2ShaderProgramAL>
{
public:
	Tr2ShaderProgramAL();
	~Tr2ShaderProgramAL();

	ALResult Create( ::Tr2ShaderAL* shaders, size_t count, Tr2PrimaryRenderContextAL& renderContext );
	void Destroy();

	bool IsValid() const;
	const Tr2RegisterMapAL& GetRegisterMap() const;

	Tr2ALMemoryType GetMemoryClass() const;

	void Describe( Tr2DeviceResourceDescriptionAL& description ) const;
	ALResult SetName( const char* name );

	VkPipelineLayout GetPipelineLayout() const;
	VkDescriptorSetLayout GetDescriptorSetLayout() const;
	VkShaderStageFlags GetStageMask() const;
	uint32_t GetShaderCount() const;
	VkShaderModule GetModule( uint32_t index ) const;
	VkShaderStageFlagBits GetStage( uint32_t index ) const;

private:
	Tr2RegisterMapAL m_registerMap;
	bool m_isValid = false;
	VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
	VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
	VkDevice m_device = VK_NULL_HANDLE;
	std::vector<VkShaderModule> m_modules;
	std::vector<VkShaderStageFlagBits> m_stages;
	VkShaderStageFlags m_stageMask = 0;

	friend class Tr2RenderContextAL;
	friend class Tr2ResourceSetAL;
};
}

#endif
