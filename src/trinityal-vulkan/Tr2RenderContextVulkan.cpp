// Copyright © 2026 Ithax contributors.

#include "StdAfx.h"
#include "Tr2RenderContextVulkan.h"

#if TRINITY_PLATFORM == TRINITY_VULKAN

#include "ALLog.h"
#include "ITr2RenderContextEvents.h"
#include "Tr2AdapterStructures.h"
#include "Tr2CapsAL.h"
#include "Tr2SamplerStateAL.h"
#include "Tr2GpuTimerAL.h"
#include "Tr2BufferALVulkan.h"
#include "Tr2TextureALVulkan.h"
#include "Tr2SamplerStateALVulkan.h"
#include "Tr2ShaderALVulkan.h"
#include "Tr2ShaderProgramALVulkan.h"
#include "Tr2ResourceSetALVulkan.h"
#include "Tr2ConstantBufferALVulkan.h"
#include "Tr2VertexLayoutALVulkan.h"

#include <limits>

namespace
{

Tr2RenderContextAL*& GetPrimaryContextPointerStorage()
{
	static Tr2RenderContextAL* primaryRenderContext = nullptr;
	return primaryRenderContext;
}

}

Tr2RenderContextAL::Tr2RenderContextAL() :
	m_isValid( false ),
	m_viewport( 0, 0 ),
	m_frameNumber( 0 ),
	m_renderedFrameNumber( 0 ),
	m_indexBufferStride( 0 ),
	m_topology( Tr2RenderContextEnum::TOP_TRIANGLES ),
	m_renderStateDirty( true ),
	m_useReadOnlyDepth( false ),
	m_isSrgbRenderTarget( false ),
	m_renderingActive( false ),
	m_events( nullptr )
{
	memset( m_vertexBufferOffsets, 0, sizeof( m_vertexBufferOffsets ) );
	memset( m_vertexBufferStrides, 0, sizeof( m_vertexBufferStrides ) );
	memset( m_allRenderStates, 0, sizeof( m_allRenderStates ) );
	memset( m_constantBufferDirty, 0, sizeof( m_constantBufferDirty ) );
}

Tr2RenderContextAL::~Tr2RenderContextAL()
{
	Destroy();
}

void Tr2RenderContextAL::Destroy()
{
	if( GetPrimaryContextPointerStorage() == this )
	{
		GetPrimaryContextPointerStorage() = nullptr;
	}
	m_vulkan.WaitIdle();
	m_vulkan.Destroy();
	m_isValid = false;
}

void Tr2RenderContextAL::SetPrimaryRenderContext( Tr2RenderContextAL* renderContext )
{
	GetPrimaryContextPointerStorage() = renderContext;
}

Tr2RenderContextAL& Tr2RenderContextAL::GetPrimaryRenderContext()
{
	CCP_ASSERT( GetPrimaryContextPointerStorage() );
	return *GetPrimaryContextPointerStorage();
}

Tr2RenderContextAL* Tr2RenderContextAL::GetPrimaryRenderContextPointer()
{
	return GetPrimaryContextPointerStorage();
}

ALResult Tr2RenderContextAL::CreateDevice(
	uint32_t Adapter,
	Tr2WindowHandle hFocusWindow,
	const Tr2PresentParametersAL& presentationParameters )
{
	Destroy();

	m_vulkan.state.presentParameters = presentationParameters;

	ALResult result = m_vulkan.CreateInstance( true );
	if( FAILED( result ) )
	{
		return result;
	}

	result = m_vulkan.CreateDevice( Adapter, hFocusWindow, presentationParameters );
	if( FAILED( result ) )
	{
		return result;
	}

	m_isValid = true;
	SetPrimaryRenderContext( this );

	m_frameTimer.Create( *this );

	if( m_events )
	{
		m_events->OnContextCreated( *this );
	}

	CR_RETURN_HR( m_vulkan.BeginFrame() );
	CR_RETURN_HR( CreateBackBufferTexture() );

	return S_OK;
}

ALResult Tr2RenderContextAL::SetPresentParameters( unsigned adapter, const Tr2PresentParametersAL& presentationParameters )
{
	m_vulkan.state.presentParameters = presentationParameters;
	ALResult result = m_vulkan.RecreateSwapchain();
	if( FAILED( result ) )
	{
		return result;
	}
	// The old swapchain images were destroyed; detach the backbuffer so
	// the next Present()/BeginFrame() re-attaches the acquired image.
	DestroyBackBufferTexture();
	return S_OK;
}
const Tr2CapsAL& Tr2RenderContextAL::GetCaps() const
{
	return m_caps;
}

ALResult Tr2RenderContextAL::BeginScene()
{
	return S_OK;
}

ALResult Tr2RenderContextAL::EndScene()
{
	if( m_renderingActive )
	{
		ALResult result = EndRendering();
		if( FAILED( result ) )
		{
			return result;
		}
		m_renderingActive = false;
	}
	return S_OK;
}

ALResult Tr2RenderContextAL::Present()
{
	if( m_renderingActive )
	{
		ALResult result = EndRendering();
		if( FAILED( result ) )
		{
			return result;
		}
		m_renderingActive = false;
	}

	ALResult result = TransitionBackBuffer( VK_IMAGE_LAYOUT_PRESENT_SRC_KHR );
	if( FAILED( result ) )
	{
		return result;
	}

	result = m_vulkan.EndFrame();
	if( FAILED( result ) )
	{
		return result;
	}

	result = m_vulkan.Present();
	if( FAILED( result ) )
	{
		return result;
	}

	m_renderedFrameNumber = m_vulkan.state.renderedFrameNumber;

	result = m_vulkan.BeginFrame();
	if( FAILED( result ) )
	{
		return result;
	}

	return CreateBackBufferTexture();
}

bool Tr2RenderContextAL::IsValid()
{
	return m_isValid && m_vulkan.state.device != VK_NULL_HANDLE;
}

void Tr2RenderContextAL::ReleaseDeviceResources()
{
	m_vulkan.WaitIdle();
	DestroyBackBufferTexture();
	m_vulkan.DestroySwapchain();
}

ALResult Tr2RenderContextAL::SetStreamSource(
	uint32_t stream,
	const Tr2BufferAL& buffer,
	uint32_t offset,
	uint32_t stride ) throw()
{
	if( stream >= 4 )
	{
		return E_INVALIDARG;
	}
	m_vertexBuffers[stream] = buffer;
	m_vertexBufferOffsets[stream] = offset;
	m_vertexBufferStrides[stream] = stride;
	return S_OK;
}

ALResult Tr2RenderContextAL::SetIndices( const Tr2BufferAL& buffer ) throw()
{
	m_indexBuffer = buffer;
	m_indexBufferStride = 2;
	return S_OK;
}

ALResult Tr2RenderContextAL::SetIndices( const Tr2BufferAL& buffer, uint32_t stride ) throw()
{
	m_indexBuffer = buffer;
	m_indexBufferStride = stride;
	return S_OK;
}

ALResult Tr2RenderContextAL::ClearUav( const Tr2BufferAL& buffer, const float values[4] ) throw()
{
	(void)buffer;
	(void)values;
	return E_FAIL;
}

ALResult Tr2RenderContextAL::ClearUav( const Tr2BufferAL& buffer, const uint32_t values[4] ) throw()
{
	(void)buffer;
	(void)values;
	return E_FAIL;
}

ALResult Tr2RenderContextAL::CopySubBuffer(
	Tr2BufferAL& dest,
	uint32_t destOffset,
	Tr2BufferAL& src,
	uint32_t offset,
	uint32_t length )
{
	VkCommandBuffer commandBuffer = m_vulkan.GetCurrentCommandBuffer();
	VkBufferCopy copyRegion = {};
	copyRegion.srcOffset = offset;
	copyRegion.dstOffset = destOffset;
	copyRegion.size = length;
	vkCmdCopyBuffer( commandBuffer, src.TrinityALImpl_GetObject()->GetBuffer(),
		dest.TrinityALImpl_GetObject()->GetBuffer(), 1, &copyRegion );
	return S_OK;
}

ALResult Tr2RenderContextAL::SetTopology( long topology )
{
	m_topology = static_cast<Tr2RenderContextEnum::Topology>( topology );
	return S_OK;
}

ALResult Tr2RenderContextAL::SetShaderProgram( const Tr2ShaderProgramAL& shaderProgram )
{
	m_shaderProgram = shaderProgram;
	return S_OK;
}

ALResult Tr2RenderContextAL::SetResourceSet( const Tr2ResourceSetAL& resourceSet )
{
	m_resourceSet = resourceSet;
	return S_OK;
}

ALResult Tr2RenderContextAL::SetVertexLayout( const Tr2VertexLayoutAL& layout )
{
	m_vertexLayout = layout;
	return S_OK;
}

ALResult Tr2RenderContextAL::SetRenderState( Tr2RenderContextEnum::RenderState state, uint32_t value )
{
	if( state < Tr2RenderContextEnum::RS_MAX_STATE )
	{
		m_allRenderStates[state] = value;
		m_renderStateDirty = true;
	}
	return S_OK;
}

ALResult Tr2RenderContextAL::SetRenderStates( const uint32_t* stateValuePairs, uint32_t count )
{
	if( stateValuePairs == nullptr )
	{
		return E_INVALIDARG;
	}
	for( uint32_t i = 0; i + 1 < count * 2; i += 2 )
	{
		Tr2RenderContextEnum::RenderState state = static_cast<Tr2RenderContextEnum::RenderState>( stateValuePairs[i] );
		if( state < Tr2RenderContextEnum::RS_MAX_STATE )
		{
			m_allRenderStates[state] = stateValuePairs[i + 1];
		}
	}
	m_renderStateDirty = true;
	return S_OK;
}

ALResult Tr2RenderContextAL::SetConstants(
	const Tr2ConstantBufferAL& buffer,
	Tr2RenderContextEnum::ShaderType constantType,
	uint32_t registerIndex,
	uint32_t maxRegisterCount )
{
	(void)maxRegisterCount;
	if( constantType >= Tr2RenderContextEnum::SHADER_TYPE_COUNT || registerIndex >= 16 )
	{
		return E_INVALIDARG;
	}
	m_constantBuffers[constantType][registerIndex] = buffer;
	m_constantBufferDirty[constantType][registerIndex] = true;
	return S_OK;
}

ALResult Tr2RenderContextAL::Clear(
	uint32_t clearFlags,
	uint32_t color,
	float depth,
	uint32_t stencil,
	uint32_t slot )
{
	(void)stencil;
	if( m_vulkan.state.swapchain.suspended )
	{
		return S_OK;
	}

	VkCommandBuffer commandBuffer = m_vulkan.GetCurrentCommandBuffer();

	Tr2TextureAL& target = ( slot == 0 && m_boundRenderTarget[0].texture.IsValid() == false )
		? m_defaultBackBuffer
		: m_boundRenderTarget[slot].texture;

	if( !target.IsValid() )
	{
		return E_INVALIDCALL;
	}

	TrinityALImpl::Tr2TextureAL* texture = target.TrinityALImpl_GetObject();

	if( clearFlags & Tr2RenderContextEnum::CLEARFLAGS_TARGET )
	{
		VkImageSubresourceRange subresourceRange = texture->GetSubresourceRange();

		VkImageMemoryBarrier2 barrier = {};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
		barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
		barrier.srcAccessMask = VK_ACCESS_2_MEMORY_READ_BIT;
		barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
		barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
		barrier.oldLayout = texture->GetLayout();
		barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = texture->GetImage();
		barrier.subresourceRange = subresourceRange;

		VkDependencyInfo dependencyInfo = {};
		dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
		dependencyInfo.imageMemoryBarrierCount = 1;
		dependencyInfo.pImageMemoryBarriers = &barrier;
		vkCmdPipelineBarrier2( commandBuffer, &dependencyInfo );

		VkClearColorValue clearColor = {};
		clearColor.float32[0] = static_cast<float>( ( color >> 16 ) & 0xff ) / 255.0f;
		clearColor.float32[1] = static_cast<float>( ( color >> 8 ) & 0xff ) / 255.0f;
		clearColor.float32[2] = static_cast<float>( color & 0xff ) / 255.0f;
		clearColor.float32[3] = static_cast<float>( ( color >> 24 ) & 0xff ) / 255.0f;
		vkCmdClearColorImage( commandBuffer, texture->GetImage(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			&clearColor, 1, &subresourceRange );

		texture->SetLayout( VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL );
	}

	if( clearFlags & Tr2RenderContextEnum::CLEARFLAGS_ZBUFFER )
	{
		TrinityALImpl::Tr2TextureAL* depthTexture = m_boundDepthStencil.IsValid()
			? m_boundDepthStencil.TrinityALImpl_GetObject()
			: nullptr;
		VkImage depthImage = depthTexture ? depthTexture->GetImage() : m_vulkan.state.swapchain.depthImage;
		VkImageLayout depthLayout = depthTexture ? depthTexture->GetLayout() : m_vulkan.state.swapchain.depthLayout;

		VkImageMemoryBarrier2 barrier = {};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
		barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
		barrier.srcAccessMask = VK_ACCESS_2_MEMORY_READ_BIT;
		barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
		barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
		barrier.oldLayout = depthLayout;
		barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = depthImage;
		barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		barrier.subresourceRange.baseMipLevel = 0;
		barrier.subresourceRange.levelCount = 1;
		barrier.subresourceRange.baseArrayLayer = 0;
		barrier.subresourceRange.layerCount = 1;

		VkDependencyInfo dependencyInfo = {};
		dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
		dependencyInfo.imageMemoryBarrierCount = 1;
		dependencyInfo.pImageMemoryBarriers = &barrier;
		vkCmdPipelineBarrier2( commandBuffer, &dependencyInfo );

		VkClearDepthStencilValue clearDepth = {};
		clearDepth.depth = depth;
		vkCmdClearDepthStencilImage( commandBuffer, depthImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
			&clearDepth, 1, &barrier.subresourceRange );

		if( depthTexture )
		{
			depthTexture->SetLayout( VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL );
		}
		else
		{
			m_vulkan.state.swapchain.depthLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		}
	}

	return S_OK;
}

ALResult Tr2RenderContextAL::SetDepthStencil( const Tr2TextureAL& depthStencil )
{
	m_boundDepthStencil = depthStencil;
	return S_OK;
}

void Tr2RenderContextAL::SetReadOnlyDepth( bool enable )
{
	m_useReadOnlyDepth = enable;
}

bool Tr2RenderContextAL::GetReadOnlyDepth() const
{
	return m_useReadOnlyDepth;
}

ALResult Tr2RenderContextAL::SetRenderTarget( const Tr2TextureAL& renderTarget, uint32_t slot, uint32_t slice )
{
	if( slot >= MAX_RENDER_TARGET )
	{
		return E_INVALIDARG;
	}
	m_boundRenderTarget[slot].texture = renderTarget;
	m_boundRenderTarget[slot].slice = slice;
	return S_OK;
}

void Tr2RenderContextAL::RenderPassHint( const Tr2ColorAttachment& rt0, const Tr2DepthAttachment& depth )
{
	(void)rt0;
	(void)depth;
}

void Tr2RenderContextAL::RenderPassHint( const Tr2ColorAttachment& rt0, const Tr2ColorAttachment& rt1, const Tr2DepthAttachment& depth )
{
	(void)rt0;
	(void)rt1;
	(void)depth;
}

ALResult Tr2RenderContextAL::SetViewport( const Tr2Viewport& viewport )
{
	m_viewport = viewport;
	VkCommandBuffer commandBuffer = m_vulkan.GetCurrentCommandBuffer();
	VkViewport vkViewport = {};
	vkViewport.x = viewport.m_x;
	vkViewport.y = viewport.m_y;
	vkViewport.width = viewport.m_width;
	vkViewport.height = viewport.m_height;
	vkViewport.minDepth = viewport.m_minZ;
	vkViewport.maxDepth = viewport.m_maxZ;
	vkCmdSetViewport( commandBuffer, 0, 1, &vkViewport );
	return S_OK;
}

ALResult Tr2RenderContextAL::GetViewport( Tr2Viewport& viewport )
{
	viewport = m_viewport;
	return S_OK;
}

ALResult Tr2RenderContextAL::PushRenderTarget( uint32_t slot )
{
	if( slot >= MAX_RENDER_TARGET )
	{
		return E_INVALIDARG;
	}
	m_stackRT[slot].push( m_boundRenderTarget[slot].texture );
	return S_OK;
}

ALResult Tr2RenderContextAL::PopRenderTarget( uint32_t slot )
{
	if( slot >= MAX_RENDER_TARGET || m_stackRT[slot].empty() )
	{
		return E_INVALIDCALL;
	}
	m_boundRenderTarget[slot].texture = m_stackRT[slot].top();
	m_stackRT[slot].pop();
	return S_OK;
}

ALResult Tr2RenderContextAL::PushDepthStencil()
{
	m_stackDS.push( m_boundDepthStencil );
	return S_OK;
}

ALResult Tr2RenderContextAL::PopDepthStencil()
{
	if( m_stackDS.empty() )
	{
		return E_INVALIDCALL;
	}
	m_boundDepthStencil = m_stackDS.top();
	m_stackDS.pop();
	return S_OK;
}

ALResult Tr2RenderContextAL::GetRenderTargetSize(
	uint32_t& width,
	uint32_t& height,
	uint32_t slot )
{
	Tr2TextureAL& target = ( slot == 0 && !m_boundRenderTarget[0].texture.IsValid() )
		? m_defaultBackBuffer
		: m_boundRenderTarget[slot].texture;
	if( !target.IsValid() )
	{
		return E_INVALIDCALL;
	}
	width = target.GetWidth();
	height = target.GetHeight();
	return S_OK;
}

long Tr2RenderContextAL::GetTotalVideoMemory()
{
	return static_cast<long>( m_vulkan.state.memoryProperties.memoryHeaps[0].size / ( 1024 * 1024 ) );
}

Tr2RenderContextEnum::PixelFormat Tr2RenderContextAL::GetBackBufferFormat() const
{
	return TrinityALImpl::ConvertVkFormat( m_vulkan.state.swapchain.format );
}

ALResult Tr2RenderContextAL::DrawIndexedPrimitive(
	uint32_t numVertices,
	uint32_t startIndex,
	uint32_t primitiveCount,
	uint32_t minimumIndex )
{
	(void)numVertices;
	(void)minimumIndex;
	VkCommandBuffer commandBuffer = m_vulkan.GetCurrentCommandBuffer();

	if( !m_renderingActive )
	{
		ALResult result = BeginRendering();
		if( FAILED( result ) )
		{
			return result;
		}
		m_renderingActive = true;
	}

	ALResult result = BindFrameResources();
	if( FAILED( result ) )
	{
		return result;
	}

	uint32_t indexCount = primitiveCount * 3;
	if( m_topology == Tr2RenderContextEnum::TOP_TRIANGLE_STRIP )
	{
		indexCount = primitiveCount + 2;
	}
	else if( m_topology == Tr2RenderContextEnum::TOP_LINES )
	{
		indexCount = primitiveCount * 2;
	}
	else if( m_topology == Tr2RenderContextEnum::TOP_LINE_STRIP )
	{
		indexCount = primitiveCount + 1;
	}
	else if( m_topology == Tr2RenderContextEnum::TOP_POINTS )
	{
		indexCount = primitiveCount;
	}

	VkIndexType indexType = ( m_indexBufferStride == 4 ) ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16;
	vkCmdBindIndexBuffer( commandBuffer, m_indexBuffer.TrinityALImpl_GetObject()->GetBuffer(), 0, indexType );
	vkCmdDrawIndexed( commandBuffer, indexCount, 1, startIndex, 0, 0 );
	return S_OK;
}

ALResult Tr2RenderContextAL::DrawPrimitive( uint32_t startVertex, uint32_t primitiveCount )
{
	VkCommandBuffer commandBuffer = m_vulkan.GetCurrentCommandBuffer();

	if( !m_renderingActive )
	{
		ALResult result = BeginRendering();
		if( FAILED( result ) )
		{
			return result;
		}
		m_renderingActive = true;
	}

	ALResult result = BindFrameResources();
	if( FAILED( result ) )
	{
		return result;
	}

	uint32_t vertexCount = primitiveCount * 3;
	if( m_topology == Tr2RenderContextEnum::TOP_TRIANGLE_STRIP )
	{
		vertexCount = primitiveCount + 2;
	}
	else if( m_topology == Tr2RenderContextEnum::TOP_LINES )
	{
		vertexCount = primitiveCount * 2;
	}
	else if( m_topology == Tr2RenderContextEnum::TOP_LINE_STRIP )
	{
		vertexCount = primitiveCount + 1;
	}
	else if( m_topology == Tr2RenderContextEnum::TOP_POINTS )
	{
		vertexCount = primitiveCount;
	}

	vkCmdDraw( commandBuffer, vertexCount, 1, startVertex, 0 );
	return S_OK;
}

ALResult Tr2RenderContextAL::DrawIndexedInstanced(
	uint32_t numVertices,
	uint32_t startIndex,
	uint32_t primitiveCount,
	uint32_t numInstances )
{
	(void)numVertices;
	VkCommandBuffer commandBuffer = m_vulkan.GetCurrentCommandBuffer();

	if( !m_renderingActive )
	{
		ALResult result = BeginRendering();
		if( FAILED( result ) )
		{
			return result;
		}
		m_renderingActive = true;
	}

	ALResult result = BindFrameResources();
	if( FAILED( result ) )
	{
		return result;
	}

	uint32_t indexCount = primitiveCount * 3;
	if( m_topology == Tr2RenderContextEnum::TOP_TRIANGLE_STRIP )
	{
		indexCount = primitiveCount + 2;
	}

	VkIndexType indexType = ( m_indexBufferStride == 4 ) ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16;
	vkCmdBindIndexBuffer( commandBuffer, m_indexBuffer.TrinityALImpl_GetObject()->GetBuffer(), 0, indexType );
	vkCmdDrawIndexed( commandBuffer, indexCount, numInstances, startIndex, 0, 0 );
	return S_OK;
}

ALResult Tr2RenderContextAL::DrawIndexedInstanced(
	uint32_t indexCountPerInstance,
	uint32_t instanceCount,
	uint32_t startIndexLocation,
	int32_t baseVertexLocation,
	uint32_t startInstanceLocation )
{
	VkCommandBuffer commandBuffer = m_vulkan.GetCurrentCommandBuffer();

	if( !m_renderingActive )
	{
		ALResult result = BeginRendering();
		if( FAILED( result ) )
		{
			return result;
		}
		m_renderingActive = true;
	}

	ALResult result = BindFrameResources();
	if( FAILED( result ) )
	{
		return result;
	}

	VkIndexType indexType = ( m_indexBufferStride == 4 ) ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16;
	vkCmdBindIndexBuffer( commandBuffer, m_indexBuffer.TrinityALImpl_GetObject()->GetBuffer(), 0, indexType );
	vkCmdDrawIndexed( commandBuffer, indexCountPerInstance, instanceCount, startIndexLocation, baseVertexLocation, startInstanceLocation );
	return S_OK;
}

ALResult Tr2RenderContextAL::DrawInstanced(
	uint32_t vertexCountPerInstance,
	uint32_t instanceCount,
	uint32_t startVertexLocation,
	uint32_t startInstanceLocation )
{
	VkCommandBuffer commandBuffer = m_vulkan.GetCurrentCommandBuffer();

	if( !m_renderingActive )
	{
		ALResult result = BeginRendering();
		if( FAILED( result ) )
		{
			return result;
		}
		m_renderingActive = true;
	}

	ALResult result = BindFrameResources();
	if( FAILED( result ) )
	{
		return result;
	}

	vkCmdDraw( commandBuffer, vertexCountPerInstance, instanceCount, startVertexLocation, startInstanceLocation );
	return S_OK;
}

ALResult Tr2RenderContextAL::DrawIndexedPrimitiveUP(
	uint32_t numVertices,
	uint32_t primitiveCount,
	const uint32_t* indexData,
	const void* vertexStreamZeroData,
	uint32_t vertexStreamZeroStride )
{
	return m_drawUP.DrawIndexedPrimitiveUP( m_topology, numVertices, primitiveCount, indexData,
		vertexStreamZeroData, vertexStreamZeroStride, *this, *this );
}

ALResult Tr2RenderContextAL::DrawIndexedPrimitiveUP(
	uint32_t numVertices,
	uint32_t primitiveCount,
	const uint16_t* indexData,
	const void* vertexStreamZeroData,
	uint32_t vertexStreamZeroStride )
{
	return m_drawUP.DrawIndexedPrimitiveUP( m_topology, numVertices, primitiveCount, indexData,
		vertexStreamZeroData, vertexStreamZeroStride, *this, *this );
}

ALResult Tr2RenderContextAL::DrawPrimitiveUP(
	uint32_t primitiveCount,
	const void* vertexStreamZeroData,
	uint32_t vertexStreamZeroStride )
{
	return m_drawUP.DrawPrimitiveUP( m_topology, primitiveCount, vertexStreamZeroData,
		vertexStreamZeroStride, *this, *this );
}

void Tr2RenderContextAL::AddGpuMarker( const char* marker )
{
	PushGpuMarker( marker );
}

void Tr2RenderContextAL::PushGpuMarker( const char* marker )
{
	VkCommandBuffer commandBuffer = m_vulkan.GetCurrentCommandBuffer();
	auto beginFn = reinterpret_cast<PFN_vkCmdBeginDebugUtilsLabelEXT>(
		vkGetDeviceProcAddr( m_vulkan.state.device, "vkCmdBeginDebugUtilsLabelEXT" ) );
	if( beginFn )
	{
		VkDebugUtilsLabelEXT label = {};
		label.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
		label.pLabelName = marker;
		beginFn( commandBuffer, &label );
	}
}

void Tr2RenderContextAL::PopGpuMarker()
{
	VkCommandBuffer commandBuffer = m_vulkan.GetCurrentCommandBuffer();
	auto endFn = reinterpret_cast<PFN_vkCmdEndDebugUtilsLabelEXT>(
		vkGetDeviceProcAddr( m_vulkan.state.device, "vkCmdEndDebugUtilsLabelEXT" ) );
	if( endFn )
	{
		endFn( commandBuffer );
	}
}

ALResult Tr2RenderContextAL::GetGpuStateMarker( Tr2RenderContextEnum::RenderContextStatus& status, std::string& marker ) const
{
	(void)marker;
	status = Tr2RenderContextEnum::CONTEXT_STATUS_EXECUTING;
	return S_OK;
}

ALResult Tr2RenderContextAL::GetGpuPageFaultResource(
	Tr2RenderContextEnum::PixelFormat& format,
	uint64_t& size,
	uint32_t& width,
	uint32_t& height,
	uint32_t& depth,
	uint32_t& mips ) const
{
	(void)format;
	(void)size;
	(void)width;
	(void)height;
	(void)depth;
	(void)mips;
	return E_FAIL;
}

ALResult Tr2RenderContextAL::UseResources( Tr2UseResourceDestination dest, Tr2GpuUsage::Type usage, const Tr2BindlessResourcesAL& resources )
{
	(void)dest;
	(void)usage;
	(void)resources;
	return S_OK;
}

ALResult Tr2RenderContextAL::UseAccelerationStructure( Tr2RtTopLevelAccelerationStructureAL tlas )
{
	(void)tlas;
	return E_FAIL;
}

bool Tr2RenderContextAL::SupportsBindlessTextures() const
{
	return false;
}

uint64_t Tr2RenderContextAL::GetRecordingFrameNumber() const
{
	return m_vulkan.state.frameNumber;
}

uint64_t Tr2RenderContextAL::GetRenderedFrameNumber() const
{
	return m_renderedFrameNumber;
}

Tr2UpscalingAL::Result Tr2RenderContextAL::EnableUpscaling( Tr2UpscalingAL::Technique tech, Tr2UpscalingAL::Setting setting, bool framegeneration, uint32_t adapter )
{
	(void)tech;
	(void)setting;
	(void)framegeneration;
	(void)adapter;
	return Tr2UpscalingAL::Result::TECHNIQUE_NOT_SUPPORTED;
}

Tr2UpscalingContextAL* Tr2RenderContextAL::GetUpscalingContext( uint32_t upscalingContextID )
{
	(void)upscalingContextID;
	return nullptr;
}

Tr2UpscalingContextAL* Tr2RenderContextAL::CreateUpscalingContext( Tr2UpscalingAL::UpscalingContextParams params, uint32_t existingContext )
{
	(void)params;
	(void)existingContext;
	return nullptr;
}

void Tr2RenderContextAL::DeleteUpscalingContext( uint32_t contextID )
{
	(void)contextID;
}

Tr2UpscalingAL::UpscalingInfo Tr2RenderContextAL::GetUpscalingInfo( uint32_t upscalingContextID )
{
	(void)upscalingContextID;
	return Tr2UpscalingAL::UpscalingInfo();
}

std::vector<std::tuple<Tr2UpscalingAL::Technique, uint32_t, bool>> Tr2RenderContextAL::GetSupportedUpscalingTechniques( uint32_t adapter )
{
	(void)adapter;
	return {};
}

void Tr2RenderContextAL::GetUpscalingSetup( Tr2UpscalingAL::Technique& technique, Tr2UpscalingAL::Setting& setting, bool& framegeneration, bool& temporal )
{
	technique = Tr2UpscalingAL::NONE;
	setting = Tr2UpscalingAL::NATIVE;
	framegeneration = false;
	temporal = false;
}

void Tr2RenderContextAL::MarkFrameEvent( Tr2RenderContextEnum::FrameEvent frameEvent )
{
	(void)frameEvent;
}

TrinityALImpl::VulkanContext& Tr2RenderContextAL::GetVulkanContext()
{
	return m_vulkan;
}

ALResult Tr2RenderContextAL::ApplyRenderState()
{
	(void)m_renderStateDirty;
	m_renderStateDirty = false;
	return S_OK;
}

ALResult Tr2RenderContextAL::BeginRendering()
{
	if( m_vulkan.state.swapchain.suspended )
	{
		return E_FAIL;
	}
	if( m_graphFrameActive )
	{
		// The render graph pass already began dynamic rendering.
		return S_OK;
	}

	VkCommandBuffer commandBuffer = m_vulkan.GetCurrentCommandBuffer();

	TrinityALImpl::Tr2TextureAL* texture = m_defaultBackBuffer.TrinityALImpl_GetObject();
	if( texture == nullptr || !texture->IsValid() )
	{
		return E_INVALIDCALL;
	}

	VkImageLayout oldLayout = texture->GetLayout();
	if( oldLayout != VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL )
	{
		VkImageMemoryBarrier2 barrier = {};
		barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
		barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
		barrier.srcAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
		barrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
		barrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
		barrier.oldLayout = oldLayout;
		barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = texture->GetImage();
		barrier.subresourceRange = texture->GetSubresourceRange();

		VkDependencyInfo dependencyInfo = {};
		dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
		dependencyInfo.imageMemoryBarrierCount = 1;
		dependencyInfo.pImageMemoryBarriers = &barrier;
		vkCmdPipelineBarrier2( commandBuffer, &dependencyInfo );

		texture->SetLayout( VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL );
	}

	VkRenderingAttachmentInfo colorAttachment = {};
	colorAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	colorAttachment.imageView = texture->GetView( Tr2RenderContextEnum::COLOR_SPACE_LINEAR );
	colorAttachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

	VkRenderingAttachmentInfo depthAttachment = {};
	depthAttachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
	depthAttachment.imageView = m_vulkan.state.swapchain.depthView;
	depthAttachment.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

	if( m_vulkan.state.swapchain.depthLayout != VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL )
	{
		VkImageMemoryBarrier2 depthBarrier = {};
		depthBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
		depthBarrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
		depthBarrier.srcAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
		depthBarrier.dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
		depthBarrier.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
		depthBarrier.oldLayout = m_vulkan.state.swapchain.depthLayout;
		depthBarrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
		depthBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		depthBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		depthBarrier.image = m_vulkan.state.swapchain.depthImage;
		depthBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		depthBarrier.subresourceRange.baseMipLevel = 0;
		depthBarrier.subresourceRange.levelCount = 1;
		depthBarrier.subresourceRange.baseArrayLayer = 0;
		depthBarrier.subresourceRange.layerCount = 1;

		VkDependencyInfo depthDependency = {};
		depthDependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
		depthDependency.imageMemoryBarrierCount = 1;
		depthDependency.pImageMemoryBarriers = &depthBarrier;
		vkCmdPipelineBarrier2( commandBuffer, &depthDependency );

		m_vulkan.state.swapchain.depthLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	}

	VkRenderingInfo renderingInfo = {};
	renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
	renderingInfo.renderArea = { { 0, 0 }, { m_vulkan.state.swapchain.extent.width, m_vulkan.state.swapchain.extent.height } };
	renderingInfo.layerCount = 1;
	renderingInfo.colorAttachmentCount = 1;
	renderingInfo.pColorAttachments = &colorAttachment;
	renderingInfo.pDepthAttachment = &depthAttachment;

	vkCmdBeginRendering( commandBuffer, &renderingInfo );

	VkViewport viewport = {};
	viewport.x = m_viewport.m_x;
	viewport.y = m_viewport.m_y;
	viewport.width = m_viewport.m_width;
	viewport.height = m_viewport.m_height;
	viewport.minDepth = m_viewport.m_minZ;
	viewport.maxDepth = m_viewport.m_maxZ;
	vkCmdSetViewport( commandBuffer, 0, 1, &viewport );

	VkRect2D scissor = {};
	scissor.offset = { 0, 0 };
	scissor.extent = { m_vulkan.state.swapchain.extent.width, m_vulkan.state.swapchain.extent.height };
	vkCmdSetScissor( commandBuffer, 0, 1, &scissor );

	return S_OK;
}

ALResult Tr2RenderContextAL::EndRendering()
{
	if( m_graphFrameActive )
	{
		// The render graph pass ends at EndGraphPass().
		return S_OK;
	}
	VkCommandBuffer commandBuffer = m_vulkan.GetCurrentCommandBuffer();
	vkCmdEndRendering( commandBuffer );
	return S_OK;
}

ALResult Tr2RenderContextAL::BindFrameResources()
{
	VkCommandBuffer commandBuffer = m_vulkan.GetCurrentCommandBuffer();

	if( !m_shaderProgram.IsValid() )
	{
		return E_INVALIDCALL;
	}

	TrinityALImpl::Tr2ShaderProgramAL* program = m_shaderProgram.TrinityALImpl_GetObject();

	VkPipelineLayout pipelineLayout = program->GetPipelineLayout();
	VkDescriptorSet descriptorSet = m_resourceSet.IsValid()
		? m_resourceSet.m_resourceSet->GetDescriptorSet()
		: VK_NULL_HANDLE;

	// Per-frame constants are written through the host-visible constant
	// buffer memory (Lock/Unlock), so the descriptor set is never updated
	// while bound. The set's UBO descriptor is written once at setup via
	// SetSetConstantBuffer.
	if( descriptorSet != VK_NULL_HANDLE )
	{
		vkCmdBindDescriptorSets( commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
			pipelineLayout, 0, 1, &descriptorSet, 0, nullptr );
	}

	TrinityALImpl::Tr2VertexLayoutAL* layout = m_vertexLayout.m_layout.get();
	if( layout && layout->IsValid() )
	{
		bool passUsesDepth = true;
		if( m_graphFrameActive && m_graphActivePass != TrinityALImpl::Tr2RenderGraphAL::INVALID_PASS )
		{
			passUsesDepth = false;
			for( const auto& attachment : m_graph.passAttachments )
			{
				if( attachment.pass == m_graphActivePass && attachment.isDepth )
				{
					passUsesDepth = true;
					break;
				}
			}
		}
		VkPipeline pipeline = passUsesDepth
			? layout->GetPipeline( m_shaderProgram, m_topology, m_vulkan.state.swapchain.format,
				m_vulkan.state.swapchain.depthFormat, m_vulkan.state.pipelineCache )
			: layout->GetPipelineNoDepth( m_shaderProgram, m_topology, m_vulkan.state.swapchain.format,
				m_vulkan.state.pipelineCache );
		if( pipeline == VK_NULL_HANDLE )
		{
			return E_FAIL;
		}
		vkCmdBindPipeline( commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline );
	}

	for( uint32_t i = 0; i < 4; ++i )
	{
		if( m_vertexBuffers[i].IsValid() )
		{
			VkBuffer buffer = m_vertexBuffers[i].TrinityALImpl_GetObject()->GetBuffer();
			VkDeviceSize offset = m_vertexBufferOffsets[i];
			vkCmdBindVertexBuffers( commandBuffer, i, 1, &buffer, &offset );
		}
	}

	return S_OK;
}

ALResult Tr2RenderContextAL::TransitionBackBuffer( VkImageLayout newLayout )
{
	if( m_vulkan.state.swapchain.suspended )
	{
		return S_OK;
	}

	TrinityALImpl::Tr2TextureAL* texture = m_defaultBackBuffer.TrinityALImpl_GetObject();
	if( texture == nullptr || !texture->IsValid() )
	{
		return E_INVALIDCALL;
	}

	VkImageLayout oldLayout = texture->GetLayout();
	if( oldLayout == newLayout )
	{
		return S_OK;
	}

	VkCommandBuffer commandBuffer = m_vulkan.GetCurrentCommandBuffer();

	VkImageMemoryBarrier2 barrier = {};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
	barrier.srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
	barrier.srcAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
	barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
	barrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT;
	barrier.oldLayout = oldLayout;
	barrier.newLayout = newLayout;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = texture->GetImage();
	barrier.subresourceRange = texture->GetSubresourceRange();

	VkDependencyInfo dependencyInfo = {};
	dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
	dependencyInfo.imageMemoryBarrierCount = 1;
	dependencyInfo.pImageMemoryBarriers = &barrier;
	vkCmdPipelineBarrier2( commandBuffer, &dependencyInfo );

	texture->SetLayout( newLayout );
	return S_OK;
}

ALResult Tr2RenderContextAL::CreateBackBufferTexture()
{
	if( m_vulkan.state.swapchain.suspended )
	{
		return S_OK;
	}

	TrinityALImpl::Tr2TextureAL* texture = m_defaultBackBuffer.TrinityALImpl_GetObject();
	if( texture == nullptr || !texture->IsValid() )
	{
		// The wrapper starts with the shared null texture; replace it with
		// a real impl so the null texture is never mutated (other wrappers
		// such as m_boundDepthStencil share it).
		texture = new TrinityALImpl::Tr2TextureAL();
		m_defaultBackBuffer.m_texture = std::shared_ptr<TrinityALImpl::Tr2TextureAL>( texture );
	}

	texture->SetDevice( m_vulkan.state.device, m_vulkan.state.allocator );

	uint32_t imageIndex = m_vulkan.GetCurrentImageIndex();
	VkImage image = m_vulkan.state.swapchain.images[imageIndex];
	VkImageView view = m_vulkan.state.swapchain.imageViews[imageIndex];
	VkFormat format = m_vulkan.state.swapchain.format;
	uint32_t width = m_vulkan.state.swapchain.extent.width;
	uint32_t height = m_vulkan.state.swapchain.extent.height;

	texture->AttachSwapchainImage( image, view, format, width, height, VK_IMAGE_LAYOUT_UNDEFINED );
	return S_OK;
}

ALResult Tr2RenderContextAL::AttachLastPresentedImage()
{
	if( m_vulkan.state.swapchain.suspended )
	{
		return E_FAIL;
	}

	TrinityALImpl::Tr2TextureAL* texture = m_defaultBackBuffer.TrinityALImpl_GetObject();
	if( texture == nullptr )
	{
		return E_FAIL;
	}

	texture->SetDevice( m_vulkan.state.device, m_vulkan.state.allocator );

	uint32_t imageIndex = m_vulkan.GetLastPresentedImageIndex();
	VkImage image = m_vulkan.state.swapchain.images[imageIndex];
	VkImageView view = m_vulkan.state.swapchain.imageViews[imageIndex];
	VkFormat format = m_vulkan.state.swapchain.format;
	uint32_t width = m_vulkan.state.swapchain.extent.width;
	uint32_t height = m_vulkan.state.swapchain.extent.height;

	// Preserve the tracked layout: the readback is recorded before the
	// present transition, so the image is still in its render layout.
	VkImageLayout layout = texture->GetLayout();
	texture->AttachSwapchainImage( image, view, format, width, height, layout );
	return S_OK;
}

ALResult Tr2RenderContextAL::WaitForFrameCompletion()
{
	if( m_vulkan.state.device == VK_NULL_HANDLE )
	{
		return E_INVALIDCALL;
	}
	VkResult result = vkDeviceWaitIdle( m_vulkan.state.device );
	return TrinityALImpl::MapVkResult( result );
}

ALResult Tr2RenderContextAL::SetSetConstantBuffer( Tr2ResourceSetAL& set, const Tr2ConstantBufferAL& buffer )
{
	VkDescriptorSet descriptorSet = set.m_resourceSet->GetDescriptorSet();
	if( descriptorSet == VK_NULL_HANDLE )
	{
		return E_INVALIDCALL;
	}
	VkDescriptorBufferInfo bufferInfo = buffer.TrinityALImpl_GetObject()->GetDescriptorInfo();
	VkWriteDescriptorSet write = {};
	write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
	write.dstSet = descriptorSet;
	write.dstBinding = 0;
	write.dstArrayElement = 0;
	write.descriptorCount = 1;
	write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	write.pBufferInfo = &bufferInfo;
	vkUpdateDescriptorSets( m_vulkan.state.device, 1, &write, 0, nullptr );
	return S_OK;
}

ALResult Tr2RenderContextAL::EndGraphFrame()
{
	if( !m_graphFrameActive )
	{
		return E_INVALIDCALL;
	}
	if( m_graphActivePass != TrinityALImpl::Tr2RenderGraphAL::INVALID_PASS )
	{
		ALResult result = EndGraphPass();
		if( FAILED( result ) )
		{
			return result;
		}
	}

	// The present transition (beforePass == INVALID_PASS) is intentionally
	// NOT recorded here: the caller may record a readback copy after this
	// point, and Present() emits the final transition to PRESENT_SRC.
	m_graphFrameActive = false;
	m_graphActivePass = TrinityALImpl::Tr2RenderGraphAL::INVALID_PASS;
	m_renderingActive = false;
	return S_OK;
}

ALResult Tr2RenderContextAL::ReattachBackBuffer()
{
	if( m_vulkan.state.swapchain.suspended )
	{
		return S_OK;
	}
	return CreateBackBufferTexture();
}

ALResult Tr2RenderContextAL::SetGraphResult( const TrinityALImpl::Tr2RenderGraphAL::CompileResult& result )
{
	m_graph = result;
	return S_OK;
}

Tr2TextureAL Tr2RenderContextAL::GetDepthTexture()
{
	TrinityALImpl::Tr2TextureAL* texture = m_depthTexture.TrinityALImpl_GetObject();
	if( texture == nullptr || !texture->IsValid() ||
		m_vulkan.state.swapchain.depthImage == VK_NULL_HANDLE ||
		texture->GetImage() != m_vulkan.state.swapchain.depthImage )
	{
		// Re-attach whenever the wrapper is invalid or the swapchain depth
		// image was recreated (resize).
		AttachDepthTexture();
	}
	return m_depthTexture;
}

ALResult Tr2RenderContextAL::AttachDepthTexture()
{
	if( m_vulkan.state.swapchain.depthImage == VK_NULL_HANDLE )
	{
		return E_INVALIDCALL;
	}
	TrinityALImpl::Tr2TextureAL* texture = m_depthTexture.TrinityALImpl_GetObject();
	if( texture == nullptr || !texture->IsValid() ||
		texture->GetImage() != m_vulkan.state.swapchain.depthImage )
	{
		texture = new TrinityALImpl::Tr2TextureAL();
		m_depthTexture.m_texture = std::shared_ptr<TrinityALImpl::Tr2TextureAL>( texture );
	}
	texture->SetDevice( m_vulkan.state.device, m_vulkan.state.allocator );
	texture->AttachSwapchainImage(
		m_vulkan.state.swapchain.depthImage,
		m_vulkan.state.swapchain.depthView,
		m_vulkan.state.swapchain.depthFormat,
		m_vulkan.state.swapchain.extent.width,
		m_vulkan.state.swapchain.extent.height,
		m_vulkan.state.swapchain.depthLayout );
	return S_OK;
}

ALResult Tr2RenderContextAL::BeginGraphFrame()
{
	m_graphFrameActive = false;
	m_graph = TrinityALImpl::Tr2RenderGraphAL::CompileResult();
	m_graphTextures.clear();
	m_graphColorSpaces.clear();
	m_graphBuffers.clear();
	m_graphResourceIsImage.clear();
	m_graphLayouts.clear();
	m_graphActivePass = TrinityALImpl::Tr2RenderGraphAL::INVALID_PASS;

	m_graphFrameActive = true;
	return S_OK;
}

ALResult Tr2RenderContextAL::RegisterGraphTexture(
	TrinityALImpl::Tr2RenderGraphAL::ResourceId resource,
	const Tr2TextureAL& texture,
	Tr2RenderContextEnum::ColorSpace colorSpace )
{
	if( !m_graphFrameActive )
	{
		return E_INVALIDCALL;
	}
	if( m_graphTextures.size() <= resource )
	{
		m_graphTextures.resize( resource + 1 );
		m_graphColorSpaces.resize( resource + 1 );
		m_graphBuffers.resize( resource + 1 );
		m_graphResourceIsImage.resize( resource + 1, false );
		m_graphLayouts.resize( resource + 1, VK_IMAGE_LAYOUT_UNDEFINED );
	}
	m_graphTextures[resource] = texture;
	m_graphColorSpaces[resource] = colorSpace;
	m_graphResourceIsImage[resource] = true;
	m_graphLayouts[resource] = texture.TrinityALImpl_GetObject()->GetLayout();
	return S_OK;
}

ALResult Tr2RenderContextAL::RegisterGraphBuffer(
	TrinityALImpl::Tr2RenderGraphAL::ResourceId resource,
	const Tr2BufferAL& buffer )
{
	if( !m_graphFrameActive )
	{
		return E_INVALIDCALL;
	}
	if( m_graphBuffers.size() <= resource )
	{
		m_graphTextures.resize( resource + 1 );
		m_graphColorSpaces.resize( resource + 1 );
		m_graphBuffers.resize( resource + 1 );
		m_graphResourceIsImage.resize( resource + 1, false );
		m_graphLayouts.resize( resource + 1, VK_IMAGE_LAYOUT_UNDEFINED );
	}
	m_graphBuffers[resource] = buffer;
	m_graphResourceIsImage[resource] = false;
	return S_OK;
}

ALResult Tr2RenderContextAL::BeginGraphPass( TrinityALImpl::Tr2RenderGraphAL::PassId pass )
{
	if( !m_graphFrameActive )
	{
		std::fprintf( stderr, "[graph] BeginGraphPass(%u) failed: frame not active\n", unsigned( pass ) );
		return E_INVALIDCALL;
	}
	if( m_graphActivePass != TrinityALImpl::Tr2RenderGraphAL::INVALID_PASS )
	{
		std::fprintf( stderr, "[graph] BeginGraphPass(%u) failed: pass %u already active\n",
			unsigned( pass ), unsigned( m_graphActivePass ) );
		return E_INVALIDCALL;
	}

	VkCommandBuffer commandBuffer = m_vulkan.GetCurrentCommandBuffer();

	// Emit all barriers for this pass in resource order.
	for( size_t i = 0; i < m_graph.imageBarriers.size(); ++i )
	{
		const auto& barrier = m_graph.imageBarriers[i];
		if( barrier.beforePass != pass )
		{
			continue;
		}
		TrinityALImpl::Tr2TextureAL* texture = m_graphTextures[barrier.resource].TrinityALImpl_GetObject();
		if( texture == nullptr || !texture->IsValid() )
		{
			std::fprintf( stderr, "[graph] BeginGraphPass(%u) failed: image barrier resource %u invalid\n",
				unsigned( pass ), unsigned( barrier.resource ) );
			return E_INVALIDCALL;
		}
		VkImageMemoryBarrier2 vkBarrier = {};
		vkBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
		vkBarrier.srcStageMask = barrier.srcStage;
		vkBarrier.srcAccessMask = barrier.srcAccess;
		vkBarrier.dstStageMask = barrier.dstStage;
		vkBarrier.dstAccessMask = barrier.dstAccess;
		vkBarrier.oldLayout = barrier.oldLayout;
		vkBarrier.newLayout = barrier.newLayout;
		vkBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		vkBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		vkBarrier.image = texture->GetImage();
		vkBarrier.subresourceRange = texture->GetSubresourceRange();

		VkDependencyInfo dependencyInfo = {};
		dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
		dependencyInfo.imageMemoryBarrierCount = 1;
		dependencyInfo.pImageMemoryBarriers = &vkBarrier;
		vkCmdPipelineBarrier2( commandBuffer, &dependencyInfo );

		m_graphLayouts[barrier.resource] = barrier.newLayout;
		texture->SetLayout( barrier.newLayout );
	}

	for( size_t i = 0; i < m_graph.bufferBarriers.size(); ++i )
	{
		const auto& barrier = m_graph.bufferBarriers[i];
		if( barrier.beforePass != pass )
		{
			continue;
		}
		TrinityALImpl::Tr2BufferAL* buffer = m_graphBuffers[barrier.resource].TrinityALImpl_GetObject();
		if( buffer == nullptr || !buffer->IsValid() )
		{
			return E_INVALIDCALL;
		}
		VkBufferMemoryBarrier2 vkBarrier = {};
		vkBarrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
		vkBarrier.srcStageMask = barrier.srcStage;
		vkBarrier.srcAccessMask = barrier.srcAccess;
		vkBarrier.dstStageMask = barrier.dstStage;
		vkBarrier.dstAccessMask = barrier.dstAccess;
		vkBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		vkBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		vkBarrier.buffer = buffer->GetBuffer();
		vkBarrier.offset = 0;
		vkBarrier.size = VK_WHOLE_SIZE;

		VkDependencyInfo dependencyInfo = {};
		dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
		dependencyInfo.bufferMemoryBarrierCount = 1;
		dependencyInfo.pBufferMemoryBarriers = &vkBarrier;
		vkCmdPipelineBarrier2( commandBuffer, &dependencyInfo );
	}

	// Begin dynamic rendering for this pass's attachments. Passes without
	// any attachments (e.g. transfer-only uploads) skip rendering.
	std::vector<VkRenderingAttachmentInfo> colors;
	VkRenderingAttachmentInfo depth = {};
	VkRenderingInfo renderingInfo = {};
	renderingInfo.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
	renderingInfo.renderArea.offset = { 0, 0 };
	renderingInfo.renderArea.extent = { m_vulkan.state.swapchain.extent.width,
		m_vulkan.state.swapchain.extent.height };
	renderingInfo.layerCount = 1;

	bool hasAttachment = false;
	for( const auto& attachment : m_graph.passAttachments )
	{
		if( attachment.pass != pass )
		{
			continue;
		}
		hasAttachment = true;
		TrinityALImpl::Tr2TextureAL* texture =
			m_graphTextures[attachment.image].TrinityALImpl_GetObject();
		if( texture == nullptr || !texture->IsValid() )
		{
			return E_INVALIDCALL;
		}
		if( attachment.isDepth )
		{
			depth.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
			depth.imageView = texture->GetView( Tr2RenderContextEnum::COLOR_SPACE_LINEAR );
			depth.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
			depth.loadOp = attachment.loadOp;
			depth.storeOp = attachment.storeOp;
			depth.clearValue = attachment.clearValue;
			renderingInfo.pDepthAttachment = &depth;
		}
		else
		{
			VkRenderingAttachmentInfo color = {};
			color.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
			color.imageView = texture->GetView( m_graphColorSpaces[attachment.image] );
			color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
			color.loadOp = attachment.loadOp;
			color.storeOp = attachment.storeOp;
			color.clearValue = attachment.clearValue;
			colors.push_back( color );
		}
	}

	m_graphPassHasRendering = hasAttachment;
	if( hasAttachment )
	{
		renderingInfo.colorAttachmentCount = static_cast<uint32_t>( colors.size() );
		renderingInfo.pColorAttachments = colors.data();
		vkCmdBeginRendering( commandBuffer, &renderingInfo );

		// Scissor is dynamic state; bind the full swapchain extent. The
		// viewport is set by the pass body through SetViewport().
		VkRect2D scissor = {};
		scissor.offset = { 0, 0 };
		scissor.extent = { m_vulkan.state.swapchain.extent.width,
			m_vulkan.state.swapchain.extent.height };
		vkCmdSetScissor( commandBuffer, 0, 1, &scissor );
	}

	m_graphActivePass = pass;
	return S_OK;
}

ALResult Tr2RenderContextAL::EndGraphPass()
{
	if( m_graphActivePass == TrinityALImpl::Tr2RenderGraphAL::INVALID_PASS )
	{
		return E_INVALIDCALL;
	}
	if( m_graphPassHasRendering )
	{
		VkCommandBuffer commandBuffer = m_vulkan.GetCurrentCommandBuffer();
		vkCmdEndRendering( commandBuffer );
	}
	m_graphActivePass = TrinityALImpl::Tr2RenderGraphAL::INVALID_PASS;
	m_graphPassHasRendering = false;
	return S_OK;
}

void Tr2RenderContextAL::DestroyBackBufferTexture()
{
	m_defaultBackBuffer = Tr2TextureAL();
}

#endif
