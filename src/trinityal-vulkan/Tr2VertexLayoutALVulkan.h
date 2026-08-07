// Copyright © 2026 Ithax contributors.

#pragma once

#if TRINITY_PLATFORM == TRINITY_VULKAN

#include "Tr2VertexLayoutAL.h"
#include "Tr2VulkanContext.h"

class Tr2ShaderProgramAL;

namespace TrinityALImpl
{
class Tr2VertexLayoutAL : public Tr2DeviceResourceAL<Tr2VertexLayoutAL>
{
public:
	Tr2VertexLayoutAL();
	~Tr2VertexLayoutAL();

	ALResult Create( const Tr2VertexDefinition& definition, Tr2PrimaryRenderContextAL& renderContext );
	void Destroy();

	bool IsValid() const;
	Tr2ALMemoryType GetMemoryClass() const;
	void Describe( Tr2DeviceResourceDescriptionAL& description ) const;
	ALResult SetName( const char* name );

	VkPipeline GetPipeline( const ::Tr2ShaderProgramAL& program, Tr2RenderContextEnum::Topology topology,
		VkFormat colorFormat, VkFormat depthFormat, VkPipelineCache cache );
	// Pipeline variant without a depth attachment (no depth test/write);
	// used for passes that render only to color.
	VkPipeline GetPipelineNoDepth( const ::Tr2ShaderProgramAL& program, Tr2RenderContextEnum::Topology topology,
		VkFormat colorFormat, VkPipelineCache cache );

private:
	Tr2VertexDefinition m_definition;
	VkDevice m_device = VK_NULL_HANDLE;
	std::vector<VkVertexInputBindingDescription> m_bindings;
	std::vector<VkVertexInputAttributeDescription> m_attributes;
	bool m_isValid = false;
	VkPipeline m_cachedPipeline = VK_NULL_HANDLE;
	const ::Tr2ShaderProgramAL* m_cachedProgram = nullptr;
	Tr2RenderContextEnum::Topology m_cachedTopology = Tr2RenderContextEnum::TOP_INVALID;
	VkFormat m_cachedColorFormat = VK_FORMAT_UNDEFINED;
	VkFormat m_cachedDepthFormat = VK_FORMAT_UNDEFINED;
	bool m_cachedUsesDepth = false;
	uint32_t m_cachedVertexShaderMask = 0;
};
}

#endif
