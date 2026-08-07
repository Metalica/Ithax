// Copyright © 2026 Ithax contributors.

#pragma once

#if TRINITY_PLATFORM == TRINITY_VULKAN

#include "Tr2ShaderAL.h"
#include "Tr2VulkanContext.h"

namespace TrinityALImpl
{
class Tr2ShaderAL : public Tr2DeviceResourceAL<Tr2ShaderAL>
{
public:
	Tr2ShaderAL();
	~Tr2ShaderAL();

	ALResult Create(
		Tr2RenderContextEnum::ShaderType type,
		const Tr2ShaderBytecodeAL& bytecode,
		const Tr2ShaderSignatureAL& signature,
		const char* shaderPath,
		Tr2PrimaryRenderContextAL& renderContext );

	void Destroy();

	bool IsValid() const;
	Tr2RenderContextEnum::ShaderType GetType() const;
	ALResult GetBytecode( Tr2ShaderBytecodeAL& bytecode ) const;
	const Tr2ShaderSignatureAL& GetSignature() const;

	Tr2ALMemoryType GetMemoryClass() const
	{
		return AL_MEMORY_MANAGED;
	}

	void SetNullShaderType( Tr2RenderContextEnum::ShaderType type );
	void Describe( Tr2DeviceResourceDescriptionAL& description ) const;
	ALResult SetName( const char* name );

	VkShaderModule GetModule() const;
	VkShaderStageFlagBits GetStage() const;

private:
	Tr2RenderContextEnum::ShaderType m_type = Tr2RenderContextEnum::INVALID_SHADER;
	CcpMallocBuffer m_bytecode;
	Tr2ShaderSignatureAL m_signature;
	VkShaderModule m_module = VK_NULL_HANDLE;
	VkDevice m_device = VK_NULL_HANDLE;
};
}

#endif
