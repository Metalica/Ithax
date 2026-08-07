// Copyright © 2026 Ithax contributors.

#include "StdAfx.h"
#include "Tr2ShaderALVulkan.h"

#if TRINITY_PLATFORM == TRINITY_VULKAN

#include "ALLog.h"
#include "Tr2PrimaryRenderContextAL.h"

namespace TrinityALImpl
{

Tr2ShaderAL::Tr2ShaderAL()
{
}

Tr2ShaderAL::~Tr2ShaderAL()
{
	Destroy();
}

ALResult Tr2ShaderAL::Create(
	Tr2RenderContextEnum::ShaderType type,
	const Tr2ShaderBytecodeAL& bytecode,
	const Tr2ShaderSignatureAL& signature,
	const char* shaderPath,
	Tr2PrimaryRenderContextAL& renderContext )
{
	(void)shaderPath;
	m_type = type;
	m_signature = signature;
	m_device = renderContext.GetVulkanContext().state.device;

	if( bytecode.bytecode == nullptr || bytecode.size == 0 )
	{
		return E_INVALIDARG;
	}

	m_bytecode.resize( "Tr2ShaderALVulkan::m_bytecode", bytecode.size );
	if( m_bytecode.empty() )
	{
		return E_OUTOFMEMORY;
	}
	memcpy( m_bytecode.get(), bytecode.bytecode, bytecode.size );

	VkShaderModuleCreateInfo createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	createInfo.codeSize = bytecode.size;
	createInfo.pCode = static_cast<const uint32_t*>( bytecode.bytecode );

	VkResult result = vkCreateShaderModule( m_device, &createInfo, nullptr, &m_module );
	if( result != VK_SUCCESS )
	{
		CCP_AL_LOGERR( "vkCreateShaderModule failed: %d", int( result ) );
		return E_FAIL;
	}
	return S_OK;
}

void Tr2ShaderAL::Destroy()
{
	if( m_module != VK_NULL_HANDLE )
	{
		vkDestroyShaderModule( m_device, m_module, nullptr );
		m_module = VK_NULL_HANDLE;
	}
	m_bytecode.clear();
}

bool Tr2ShaderAL::IsValid() const
{
	return m_module != VK_NULL_HANDLE;
}

Tr2RenderContextEnum::ShaderType Tr2ShaderAL::GetType() const
{
	return m_type;
}

ALResult Tr2ShaderAL::GetBytecode( Tr2ShaderBytecodeAL& bytecode ) const
{
	bytecode.bytecode = m_bytecode.get();
	bytecode.size = m_bytecode.size();
	return S_OK;
}

const Tr2ShaderSignatureAL& Tr2ShaderAL::GetSignature() const
{
	return m_signature;
}

void Tr2ShaderAL::SetNullShaderType( Tr2RenderContextEnum::ShaderType type )
{
	m_type = type;
}

void Tr2ShaderAL::Describe( Tr2DeviceResourceDescriptionAL& description ) const
{
	description["type"] = "shader";
	description["shader_type"] = std::to_string( int( m_type ) );
}

ALResult Tr2ShaderAL::SetName( const char* name )
{
	SetVulkanObjectName( m_device, reinterpret_cast<uint64_t>( m_module ),
		VK_OBJECT_TYPE_SHADER_MODULE, name );
	return S_OK;
}

VkShaderModule Tr2ShaderAL::GetModule() const
{
	return m_module;
}

VkShaderStageFlagBits Tr2ShaderAL::GetStage() const
{
	return ConvertShaderType( m_type );
}

}

#endif
