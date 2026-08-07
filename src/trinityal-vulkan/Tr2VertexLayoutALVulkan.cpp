// Copyright © 2026 Ithax contributors.

#include "StdAfx.h"
#include "Tr2VertexLayoutALVulkan.h"

#if TRINITY_PLATFORM == TRINITY_VULKAN

#include "ALLog.h"
#include "Tr2VertexDefinition.h"
#include "Tr2PrimaryRenderContextAL.h"
#include "Tr2ShaderProgramALVulkan.h"

#include <array>
#include <map>

namespace TrinityALImpl
{

namespace
{

const char* GetSemanticName( Tr2VertexDefinition::UsageCode usage )
{
	switch( usage )
	{
	case Tr2VertexDefinition::POSITION: return "POSITION";
	case Tr2VertexDefinition::COLOR: return "COLOR";
	case Tr2VertexDefinition::NORMAL: return "NORMAL";
	case Tr2VertexDefinition::TANGENT: return "TANGENT";
	case Tr2VertexDefinition::BITANGENT: return "BINORMAL";
	case Tr2VertexDefinition::TEXCOORD: return "TEXCOORD";
	case Tr2VertexDefinition::BLENDINDICES: return "BLENDINDICES";
	case Tr2VertexDefinition::BLENDWEIGHTS: return "BLENDWEIGHT";
	default: return "UNKNOWN";
	}
}

}

Tr2VertexLayoutAL::Tr2VertexLayoutAL()
{
}

Tr2VertexLayoutAL::~Tr2VertexLayoutAL()
{
	Destroy();
}

ALResult Tr2VertexLayoutAL::Create( const Tr2VertexDefinition& definition, Tr2PrimaryRenderContextAL& renderContext )
{
	m_definition = definition;
	m_device = renderContext.GetVulkanContext().state.device;

	std::map<uint32_t, uint32_t> streamMaxOffset;
	for( const auto& item : definition.m_items )
	{
		streamMaxOffset[item.m_stream] = std::max( streamMaxOffset[item.m_stream],
			item.m_offset + Tr2VertexDefinition::GetDataTypeSizeInBytes( item.m_dataType ) );
	}

	for( const auto& entry : streamMaxOffset )
	{
		VkVertexInputBindingDescription binding = {};
		binding.binding = entry.first;
		binding.stride = entry.second;
		binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
		m_bindings.push_back( binding );
	}

	uint32_t location = 0;
	for( const auto& item : definition.m_items )
	{
		VkVertexInputAttributeDescription attribute = {};
		attribute.location = location++;
		attribute.binding = item.m_stream;
		attribute.format = ConvertVertexDataType( item.m_dataType );
		attribute.offset = item.m_offset;
		if( attribute.format == VK_FORMAT_UNDEFINED )
		{
			CCP_AL_LOGERR( "Unsupported vertex data type %u", unsigned( item.m_dataType ) );
			return E_INVALIDARG;
		}
		m_attributes.push_back( attribute );
	}

	m_isValid = true;
	return S_OK;
}

void Tr2VertexLayoutAL::Destroy()
{
	if( m_cachedPipeline != VK_NULL_HANDLE )
	{
		vkDestroyPipeline( m_device, m_cachedPipeline, nullptr );
		m_cachedPipeline = VK_NULL_HANDLE;
	}
	m_cachedProgram = nullptr;
	m_bindings.clear();
	m_attributes.clear();
	m_isValid = false;
}

bool Tr2VertexLayoutAL::IsValid() const
{
	return m_isValid;
}

Tr2ALMemoryType Tr2VertexLayoutAL::GetMemoryClass() const
{
	return AL_MEMORY_MANAGED;
}

void Tr2VertexLayoutAL::Describe( Tr2DeviceResourceDescriptionAL& description ) const
{
	description["type"] = "vertex_layout";
	description["attribute_count"] = std::to_string( m_attributes.size() );
}

ALResult Tr2VertexLayoutAL::SetName( const char* name )
{
	(void)name;
	return S_OK;
}

VkPipeline Tr2VertexLayoutAL::GetPipeline( const ::Tr2ShaderProgramAL& program, Tr2RenderContextEnum::Topology topology,
	VkFormat colorFormat, VkFormat depthFormat, VkPipelineCache cache )
{
	if( m_cachedPipeline != VK_NULL_HANDLE &&
		m_cachedProgram == &program &&
		m_cachedTopology == topology &&
		m_cachedColorFormat == colorFormat &&
		m_cachedDepthFormat == depthFormat &&
		m_cachedUsesDepth == ( depthFormat != VK_FORMAT_UNDEFINED ) )
	{
		return m_cachedPipeline;
	}

	if( m_cachedPipeline != VK_NULL_HANDLE )
	{
		vkDestroyPipeline( m_device, m_cachedPipeline, nullptr );
		m_cachedPipeline = VK_NULL_HANDLE;
	}

	const bool usesDepth = ( depthFormat != VK_FORMAT_UNDEFINED );

	TrinityALImpl::Tr2ShaderProgramAL* programImpl = program.TrinityALImpl_GetObject();
	VkPipelineLayout pipelineLayout = programImpl->GetPipelineLayout();
	VkShaderStageFlags stageMask = programImpl->GetStageMask();

	std::array<VkPipelineShaderStageCreateInfo, 6> stages = {};
	uint32_t stageCount = 0;
	for( uint32_t i = 0; i < programImpl->GetShaderCount(); ++i )
	{
		VkPipelineShaderStageCreateInfo stage = {};
		stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stage.stage = programImpl->GetStage( i );
		stage.module = programImpl->GetModule( i );
		stage.pName = "main";
		stages[stageCount++] = stage;
	}

	VkPipelineVertexInputStateCreateInfo vertexInput = {};
	vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertexInput.vertexBindingDescriptionCount = static_cast<uint32_t>( m_bindings.size() );
	vertexInput.pVertexBindingDescriptions = m_bindings.data();
	vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>( m_attributes.size() );
	vertexInput.pVertexAttributeDescriptions = m_attributes.data();

	VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
	inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	inputAssembly.topology = ConvertTopology( topology );
	inputAssembly.primitiveRestartEnable = VK_FALSE;

	VkPipelineViewportStateCreateInfo viewportState = {};
	viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewportState.viewportCount = 1;
	viewportState.scissorCount = 1;

	VkPipelineRasterizationStateCreateInfo rasterizer = {};
	rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterizer.depthClampEnable = VK_FALSE;
	rasterizer.rasterizerDiscardEnable = VK_FALSE;
	rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
	rasterizer.lineWidth = 1.0f;
	rasterizer.cullMode = VK_CULL_MODE_NONE;
	rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	rasterizer.depthBiasEnable = VK_FALSE;

	VkPipelineMultisampleStateCreateInfo multisampling = {};
	multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisampling.sampleShadingEnable = VK_FALSE;
	multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

	VkPipelineColorBlendAttachmentState blendAttachment = {};
	blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
		VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	blendAttachment.blendEnable = VK_FALSE;

	VkPipelineColorBlendStateCreateInfo colorBlending = {};
	colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	colorBlending.attachmentCount = 1;
	colorBlending.pAttachments = &blendAttachment;

	VkPipelineDepthStencilStateCreateInfo depthStencil = {};
	depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
	depthStencil.depthTestEnable = usesDepth ? VK_TRUE : VK_FALSE;
	depthStencil.depthWriteEnable = usesDepth ? VK_TRUE : VK_FALSE;
	depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
	depthStencil.stencilTestEnable = VK_FALSE;

	VkDynamicState dynamicStates[] = {
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR,
	};
	VkPipelineDynamicStateCreateInfo dynamicState = {};
	dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamicState.dynamicStateCount = 2;
	dynamicState.pDynamicStates = dynamicStates;

	VkPipelineRenderingCreateInfo renderingInfo = {};
	renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
	renderingInfo.colorAttachmentCount = 1;
	renderingInfo.pColorAttachmentFormats = &colorFormat;
	renderingInfo.depthAttachmentFormat = depthFormat;

	VkGraphicsPipelineCreateInfo pipelineInfo = {};
	pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	pipelineInfo.pNext = &renderingInfo;
	pipelineInfo.stageCount = stageCount;
	pipelineInfo.pStages = stages.data();
	pipelineInfo.pVertexInputState = &vertexInput;
	pipelineInfo.pInputAssemblyState = &inputAssembly;
	pipelineInfo.pViewportState = &viewportState;
	pipelineInfo.pRasterizationState = &rasterizer;
	pipelineInfo.pMultisampleState = &multisampling;
	pipelineInfo.pDepthStencilState = &depthStencil;
	pipelineInfo.pColorBlendState = &colorBlending;
	pipelineInfo.pDynamicState = &dynamicState;
	pipelineInfo.layout = pipelineLayout;

	VkPipeline pipeline = VK_NULL_HANDLE;
	VkResult result = vkCreateGraphicsPipelines( m_device, cache, 1, &pipelineInfo, nullptr, &pipeline );
	if( result != VK_SUCCESS )
	{
		CCP_AL_LOGERR( "vkCreateGraphicsPipelines failed: %d", int( result ) );
		return VK_NULL_HANDLE;
	}

	m_cachedPipeline = pipeline;
	m_cachedProgram = &program;
	m_cachedTopology = topology;
	m_cachedColorFormat = colorFormat;
	m_cachedDepthFormat = depthFormat;
	m_cachedUsesDepth = usesDepth;
	return pipeline;
}

VkPipeline Tr2VertexLayoutAL::GetPipelineNoDepth( const ::Tr2ShaderProgramAL& program,
	Tr2RenderContextEnum::Topology topology, VkFormat colorFormat, VkPipelineCache cache )
{
	return GetPipeline( program, topology, colorFormat, VK_FORMAT_UNDEFINED, cache );
}

}

#endif
