// Copyright © 2026 Ithax contributors.

#include "StdAfx.h"
#include "Tr2ShaderProgramALVulkan.h"

#if TRINITY_PLATFORM == TRINITY_VULKAN

#include "ALLog.h"
#include "Tr2PrimaryRenderContextAL.h"
#include "Tr2ShaderAL.h"
#include "Tr2ShaderALVulkan.h"

#include <array>

namespace TrinityALImpl
{

namespace
{

constexpr uint32_t BINDING_CONSTANT_BUFFER = 0;
constexpr uint32_t BINDING_SRV = 1;
constexpr uint32_t BINDING_UAV = 2;
constexpr uint32_t BINDING_SAMPLER = 3;

}

Tr2ShaderProgramAL::Tr2ShaderProgramAL()
{
}

Tr2ShaderProgramAL::~Tr2ShaderProgramAL()
{
	Destroy();
}

ALResult Tr2ShaderProgramAL::Create( ::Tr2ShaderAL* shaders, size_t count, Tr2PrimaryRenderContextAL& renderContext )
{
	if( shaders == nullptr || count == 0 )
	{
		return E_INVALIDARG;
	}

	m_device = renderContext.GetVulkanContext().state.device;
	m_registerMap = Tr2RegisterMapAL( static_cast<const ::Tr2ShaderAL*>( shaders ), count );

	std::vector<Tr2ShaderSignatureAL> signatures;
	signatures.reserve( count );
	for( size_t i = 0; i < count; ++i )
	{
		if( !shaders[i].IsValid() )
		{
			return E_INVALIDARG;
		}
		signatures.push_back( shaders[i].GetSignature() );
		m_modules.push_back( shaders[i].TrinityALImpl_GetObject()->GetModule() );
		m_stages.push_back( shaders[i].TrinityALImpl_GetObject()->GetStage() );
		m_stageMask |= m_stages.back();
	}

	std::array<VkDescriptorSetLayoutBinding, 4> bindings = {};
	bindings[BINDING_CONSTANT_BUFFER].binding = BINDING_CONSTANT_BUFFER;
	bindings[BINDING_CONSTANT_BUFFER].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	bindings[BINDING_CONSTANT_BUFFER].descriptorCount = 1;
	bindings[BINDING_CONSTANT_BUFFER].stageFlags = m_stageMask;

	bindings[BINDING_SRV].binding = BINDING_SRV;
	bindings[BINDING_SRV].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	bindings[BINDING_SRV].descriptorCount = Tr2RegisterMapAL::MAX_RESOURCES_IN_STAGE;
	bindings[BINDING_SRV].stageFlags = m_stageMask;

	bindings[BINDING_UAV].binding = BINDING_UAV;
	bindings[BINDING_UAV].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	bindings[BINDING_UAV].descriptorCount = Tr2RegisterMapAL::MAX_RESOURCES_IN_STAGE;
	bindings[BINDING_UAV].stageFlags = m_stageMask;

	bindings[BINDING_SAMPLER].binding = BINDING_SAMPLER;
	bindings[BINDING_SAMPLER].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
	bindings[BINDING_SAMPLER].descriptorCount = Tr2RegisterMapAL::MAX_RESOURCES_IN_STAGE;
	bindings[BINDING_SAMPLER].stageFlags = m_stageMask;

	VkDescriptorSetLayoutCreateInfo layoutInfo = {};
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.bindingCount = static_cast<uint32_t>( bindings.size() );
	layoutInfo.pBindings = bindings.data();

	VkResult result = vkCreateDescriptorSetLayout( m_device, &layoutInfo, nullptr, &m_descriptorSetLayout );
	if( result != VK_SUCCESS )
	{
		CCP_AL_LOGERR( "vkCreateDescriptorSetLayout failed: %d", int( result ) );
		return E_FAIL;
	}

	VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
	pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipelineLayoutInfo.setLayoutCount = 1;
	pipelineLayoutInfo.pSetLayouts = &m_descriptorSetLayout;

	result = vkCreatePipelineLayout( m_device, &pipelineLayoutInfo, nullptr, &m_pipelineLayout );
	if( result != VK_SUCCESS )
	{
		CCP_AL_LOGERR( "vkCreatePipelineLayout failed: %d", int( result ) );
		return E_FAIL;
	}

	m_isValid = true;
	return S_OK;
}

void Tr2ShaderProgramAL::Destroy()
{
	if( m_pipelineLayout != VK_NULL_HANDLE )
	{
		vkDestroyPipelineLayout( m_device, m_pipelineLayout, nullptr );
		m_pipelineLayout = VK_NULL_HANDLE;
	}
	if( m_descriptorSetLayout != VK_NULL_HANDLE )
	{
		vkDestroyDescriptorSetLayout( m_device, m_descriptorSetLayout, nullptr );
		m_descriptorSetLayout = VK_NULL_HANDLE;
	}
	m_modules.clear();
	m_stages.clear();
	m_stageMask = 0;
	m_isValid = false;
}

bool Tr2ShaderProgramAL::IsValid() const
{
	return m_isValid;
}

const Tr2RegisterMapAL& Tr2ShaderProgramAL::GetRegisterMap() const
{
	return m_registerMap;
}

Tr2ALMemoryType Tr2ShaderProgramAL::GetMemoryClass() const
{
	return AL_MEMORY_MANAGED;
}

void Tr2ShaderProgramAL::Describe( Tr2DeviceResourceDescriptionAL& description ) const
{
	description["type"] = "shader_program";
	description["shader_count"] = std::to_string( m_modules.size() );
}

ALResult Tr2ShaderProgramAL::SetName( const char* name )
{
	(void)name;
	return S_OK;
}

VkPipelineLayout Tr2ShaderProgramAL::GetPipelineLayout() const
{
	return m_pipelineLayout;
}

VkDescriptorSetLayout Tr2ShaderProgramAL::GetDescriptorSetLayout() const
{
	return m_descriptorSetLayout;
}

VkShaderStageFlags Tr2ShaderProgramAL::GetStageMask() const
{
	return m_stageMask;
}

uint32_t Tr2ShaderProgramAL::GetShaderCount() const
{
	return static_cast<uint32_t>( m_modules.size() );
}

VkShaderModule Tr2ShaderProgramAL::GetModule( uint32_t index ) const
{
	return m_modules[index];
}

VkShaderStageFlagBits Tr2ShaderProgramAL::GetStage( uint32_t index ) const
{
	return m_stages[index];
}

}

#endif
