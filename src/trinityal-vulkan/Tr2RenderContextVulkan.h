// Copyright © 2026 Ithax contributors.

#pragma once
#ifndef Tr2RenderContextVulkan_h_
#define Tr2RenderContextVulkan_h_

#if ( TRINITY_PLATFORM == TRINITY_VULKAN )

#include "Tr2RenderContextEnum.h"
#include "Tr2DrawUPHelper.h"
#include "Tr2CapsAL.h"
#include "Tr2ConstantBufferAL.h"
#include "Tr2ResourceSetAL.h"
#include "Tr2TextureAL.h"
#include "Tr2ShaderAL.h"
#include "Tr2ShaderProgramAL.h"
#include "Tr2VertexLayoutAL.h"
#include "Tr2RenderPassAL.h"
#include "Tr2RtTopLevelAccelerationStructureAL.h"
#include "Tr2GpuTimerAL.h"
#include "Tr2HalHelperStructures.h"
#include "upscaling/Tr2UpscalingAL.h"
#include "Tr2VulkanContext.h"
#include "Tr2RenderGraphALVulkan.h"

#include <TrackableContainer.h>

class Tr2ConstantBufferAL;
class Tr2VertexLayoutAL;
class Tr2ShaderAL;
class Tr2SamplerStateAL;
class Tr2TextureAL;
class Tr2ResourceSetAL;
class Tr2BufferAL;
class Tr2RtShaderTableAL;
class Tr2RtPipelineStateAL;
struct ITr2RenderContextEvents;
struct Tr2PresentParametersAL;

class Tr2BindlessResourcesAL
{
public:
	void Add( const Tr2TextureAL& )
	{
	}
	void Add( const Tr2BufferAL& )
	{
	}
	void Add( const Tr2BindlessResourcesAL& )
	{
	}
	void Clear()
	{
	}
};

class Tr2RenderContextAL
{
public:
	Tr2RenderContextAL();
	~Tr2RenderContextAL();
	void Destroy();

	static void SetPrimaryRenderContext( Tr2RenderContextAL* );
	static Tr2RenderContextAL& GetPrimaryRenderContext();
	static Tr2RenderContextAL* GetPrimaryRenderContextPointer();

	ALResult CreateDevice(
		uint32_t Adapter,
		Tr2WindowHandle hFocusWindow,
		const Tr2PresentParametersAL& presentationParameters );
	ALResult SetPresentParameters( unsigned adapter, const Tr2PresentParametersAL& presentationParameters );

	const Tr2CapsAL& GetCaps() const;

	ALResult BeginScene();
	ALResult EndScene();
	ALResult Present();

	bool IsValid();

	void ReleaseDeviceResources();

	ALResult SetStreamSource(
		uint32_t stream,
		const Tr2BufferAL& buffer,
		uint32_t offset,
		uint32_t stride ) throw();
	ALResult SetIndices( const Tr2BufferAL& buffer ) throw();
	ALResult SetIndices( const Tr2BufferAL& buffer, uint32_t stride ) throw();
	ALResult ClearUav( const Tr2BufferAL& buffer, const float values[4] ) throw();
	ALResult ClearUav( const Tr2BufferAL& buffer, const uint32_t values[4] ) throw();

	ALResult CopySubBuffer(
		Tr2BufferAL& dest,
		uint32_t destOffset,
		Tr2BufferAL& src,
		uint32_t offset,
		uint32_t length );

	ALResult SetTopology( long topology );
	ALResult SetShaderProgram( const Tr2ShaderProgramAL& shaderProgram );

	ALResult ClearUav( const Tr2TextureAL&, uint32_t, const float[4] ) throw()
	{
		return E_FAIL;
	}

	ALResult ClearUav( const Tr2TextureAL&, uint32_t, const uint32_t[4] ) throw()
	{
		return E_FAIL;
	}

	ALResult SetResourceSet( const Tr2ResourceSetAL& resourceSet );

	ALResult DrawIndexedPrimitive(
		uint32_t numVertices,
		uint32_t startIndex,
		uint32_t primitiveCount,
		uint32_t minimumIndex = 0 );

	ALResult DrawPrimitive( uint32_t startVertex, uint32_t primitiveCount );

	ALResult DrawIndexedInstanced(
		uint32_t numVertices,
		uint32_t startIndex,
		uint32_t primitiveCount,
		uint32_t numInstances );

	ALResult DrawIndexedInstanced(
		uint32_t indexCountPerInstance,
		uint32_t instanceCount,
		uint32_t startIndexLocation,
		int32_t baseVertexLocation,
		uint32_t startInstanceLocation );
	ALResult DrawInstanced(
		uint32_t vertexCountPerInstance,
		uint32_t instanceCount,
		uint32_t startVertexLocation,
		uint32_t startInstanceLocation );

	ALResult DrawIndexedPrimitiveUP(
		uint32_t numVertices,
		uint32_t primitiveCount,
		const uint32_t* indexData,
		const void* vertexStreamZeroData,
		uint32_t vertexStreamZeroStride );

	ALResult DrawIndexedPrimitiveUP(
		uint32_t numVertices,
		uint32_t primitiveCount,
		const uint16_t* indexData,
		const void* vertexStreamZeroData,
		uint32_t vertexStreamZeroStride );

	ALResult DrawPrimitiveUP(
		uint32_t primitiveCount,
		const void* vertexStreamZeroData,
		uint32_t vertexStreamZeroStride );

	ALResult DrawIndexedInstancedIndirect( Tr2BufferAL&, uint32_t )
	{
		return E_FAIL;
	}

	ALResult DrawInstancedIndirect( Tr2BufferAL&, uint32_t )
	{
		return E_FAIL;
	}

	ALResult RunComputeShader( unsigned, unsigned, unsigned )
	{
		return E_FAIL;
	}
	ALResult RunComputeShaderIndirect( Tr2BufferAL&, unsigned )
	{
		return E_FAIL;
	}

	ALResult DispatchRays( Tr2RtPipelineStateAL& pipeline, Tr2RtShaderTableAL& shaderTable, const wchar_t* rayGenShader, uint32_t width, uint32_t height, uint32_t depth )
	{
		return E_FAIL;
	}

	ALResult SetVertexLayout( const Tr2VertexLayoutAL& layout );

	ALResult SetRenderState( Tr2RenderContextEnum::RenderState state, uint32_t value );
	ALResult SetRenderStates( const uint32_t* stateValuePairs, uint32_t count );

	ALResult SetConstants(
		const Tr2ConstantBufferAL& buffer,
		Tr2RenderContextEnum::ShaderType constantType,
		uint32_t registerIndex,
		uint32_t maxRegisterCount = 0 );

	ALResult Clear(
		uint32_t clearFlags,
		uint32_t color,
		float depth,
		uint32_t stencil = 0,
		uint32_t slot = 0 );

	ALResult SetDepthStencil( const Tr2TextureAL& depthStencil );
	void SetReadOnlyDepth( bool enable );
	bool GetReadOnlyDepth() const;
	ALResult SetRenderTarget( const Tr2TextureAL& renderTarget, uint32_t slot = 0, uint32_t slice = 0 );

	void RenderPassHint( const Tr2ColorAttachment& rt0, const Tr2DepthAttachment& depth );
	void RenderPassHint( const Tr2ColorAttachment& rt0, const Tr2ColorAttachment& rt1, const Tr2DepthAttachment& depth );

	ALResult SetViewport( const Tr2Viewport& viewport );
	ALResult GetViewport( Tr2Viewport& viewport );

	ALResult PushRenderTarget( uint32_t slot = 0 );
	ALResult PopRenderTarget( uint32_t slot = 0 );
	ALResult PushDepthStencil();
	ALResult PopDepthStencil();
	ALResult GetRenderTargetSize(
		uint32_t& width,
		uint32_t& height,
		uint32_t slot = 0 );

	long GetTotalVideoMemory();

	Tr2RenderContextEnum::PixelFormat GetBackBufferFormat() const;

	static const uint32_t SHADER_TYPE_MASK =
		( 1 << Tr2RenderContextEnum::VERTEX_SHADER ) |
		( 1 << Tr2RenderContextEnum::PIXEL_SHADER ) |
		( 1 << Tr2RenderContextEnum::COMPUTE_SHADER ) |
		( 1 << Tr2RenderContextEnum::GEOMETRY_SHADER ) |
		( 1 << Tr2RenderContextEnum::HULL_SHADER ) |
		( 1 << Tr2RenderContextEnum::DOMAIN_SHADER );

	size_t GetStackSizeRT( uint32_t = 0 ) const
	{
		return 0;
	}
	size_t GetStackSizeDS() const
	{
		return 0;
	}

	Tr2CapsAL m_caps;

	ITr2RenderContextEvents* m_events;
	Tr2TextureAL& GetDefaultBackBuffer()
	{
		return m_defaultBackBuffer;
	}

	void AddGpuMarker( const char* marker );
	void PushGpuMarker( const char* marker );
	void PopGpuMarker();
	ALResult GetGpuStateMarker( Tr2RenderContextEnum::RenderContextStatus& status, std::string& marker ) const;
	ALResult GetGpuPageFaultResource(
		Tr2RenderContextEnum::PixelFormat& format,
		uint64_t& size,
		uint32_t& width,
		uint32_t& height,
		uint32_t& depth,
		uint32_t& mips ) const;

	ALResult UseResources( Tr2UseResourceDestination dest, Tr2GpuUsage::Type usage, const Tr2BindlessResourcesAL& resources );
	ALResult UseAccelerationStructure( Tr2RtTopLevelAccelerationStructureAL tlas );

	bool SupportsBindlessTextures() const;

	uint64_t GetRecordingFrameNumber() const;
	uint64_t GetRenderedFrameNumber() const;

	Tr2UpscalingAL::Result EnableUpscaling( Tr2UpscalingAL::Technique tech, Tr2UpscalingAL::Setting setting, bool framegeneration, uint32_t adapter );
	Tr2UpscalingContextAL* GetUpscalingContext( uint32_t upscalingContextID );
	Tr2UpscalingContextAL* CreateUpscalingContext( Tr2UpscalingAL::UpscalingContextParams params, uint32_t existingContext = Tr2UpscalingAL::INVALID_CONTEXT_ID );
	void DeleteUpscalingContext( uint32_t contextID );
	Tr2UpscalingAL::UpscalingInfo GetUpscalingInfo( uint32_t upscalingContextID );
	std::vector<std::tuple<Tr2UpscalingAL::Technique, uint32_t, bool>> GetSupportedUpscalingTechniques( uint32_t adapter );
	void GetUpscalingSetup( Tr2UpscalingAL::Technique& technique, Tr2UpscalingAL::Setting& setting, bool& framegeneration, bool& temporal );
	void MarkFrameEvent( Tr2RenderContextEnum::FrameEvent frameEvent );

	TrinityALImpl::VulkanContext& GetVulkanContext();
	ALResult AttachLastPresentedImage();
	ALResult WaitForFrameCompletion();

	// Writes the UBO descriptor (binding 0) of a resource set once, before
	// the set is ever bound. Per-frame data flows through the constant
	// buffer's host-visible memory (Lock/Unlock), never through descriptor
	// updates while the set is bound.
	ALResult SetSetConstantBuffer( Tr2ResourceSetAL& set, const Tr2ConstantBufferAL& buffer );

	// Re-attaches the default backbuffer wrapper to the current swapchain
	// image. Needed after SetPresentParameters() detaches it.
	ALResult ReattachBackBuffer();

	// Render graph recording (5.8): the scene declares its per-frame
	// resource usage through TrinityALImpl::Tr2RenderGraphAL, compiles it,
	// then drives the frame command buffer through the compiled barrier and
	// attachment lists. The recorder is explicit: the backend never infers
	// graph structure from draw calls.
	ALResult BeginGraphFrame();
	ALResult SetGraphResult( const TrinityALImpl::Tr2RenderGraphAL::CompileResult& result );
	ALResult RegisterGraphTexture(
		TrinityALImpl::Tr2RenderGraphAL::ResourceId resource,
		const Tr2TextureAL& texture,
		Tr2RenderContextEnum::ColorSpace colorSpace );
	ALResult RegisterGraphBuffer(
		TrinityALImpl::Tr2RenderGraphAL::ResourceId resource,
		const Tr2BufferAL& buffer );
	ALResult BeginGraphPass( TrinityALImpl::Tr2RenderGraphAL::PassId pass );
	ALResult EndGraphPass();
	ALResult EndGraphFrame();
	Tr2TextureAL GetDepthTexture();
	ALResult AttachDepthTexture();

public:
	TrinityALImpl::Tr2SamplerStateALFactory m_samplerStateFactory;

private:
	enum
	{
		MAX_RENDER_TARGET = 8
	};
	struct BoundRT
	{
		Tr2TextureAL texture;
		uint32_t slice;
	};
	BoundRT m_boundRenderTarget[MAX_RENDER_TARGET];
	Tr2TextureAL m_boundDepthStencil;
	bool m_isValid;
	Tr2TextureAL m_defaultBackBuffer;
	Tr2Viewport m_viewport;
	TrackableStdStack<Tr2TextureAL> m_stackRT[MAX_RENDER_TARGET];
	TrackableStdStack<Tr2TextureAL> m_stackDS;
	uint64_t m_frameNumber;
	uint64_t m_renderedFrameNumber;

	Tr2BufferAL m_vertexBuffers[4];
	uint32_t m_vertexBufferOffsets[4];
	uint32_t m_vertexBufferStrides[4];
	Tr2BufferAL m_indexBuffer;
	uint32_t m_indexBufferStride;
	Tr2RenderContextEnum::Topology m_topology;
	Tr2VertexLayoutAL m_vertexLayout;
	Tr2ShaderProgramAL m_shaderProgram;
	Tr2ResourceSetAL m_resourceSet;
	Tr2ConstantBufferAL m_constantBuffers[Tr2RenderContextEnum::SHADER_TYPE_COUNT][16];
	bool m_constantBufferDirty[Tr2RenderContextEnum::SHADER_TYPE_COUNT][16];
	uint32_t m_allRenderStates[Tr2RenderContextEnum::RS_MAX_STATE];
	bool m_renderStateDirty;
	bool m_useReadOnlyDepth;
	bool m_isSrgbRenderTarget;
	bool m_renderingActive;
	TrinityALImpl::Tr2DrawUPHelper m_drawUP;
	TrinityALImpl::VulkanContext m_vulkan;
	Tr2GpuTimerAL m_frameTimer;

	// Render graph state (5.8): the compiled result for the frame being
	// recorded, registered resource handles, the active pass id, and the
	// per-resource tracked layouts as the graph advances.
	TrinityALImpl::Tr2RenderGraphAL::CompileResult m_graph;
	std::vector<Tr2TextureAL> m_graphTextures;
	std::vector<Tr2RenderContextEnum::ColorSpace> m_graphColorSpaces;
	std::vector<Tr2BufferAL> m_graphBuffers;
	std::vector<bool> m_graphResourceIsImage;
	std::vector<VkImageLayout> m_graphLayouts;
	TrinityALImpl::Tr2RenderGraphAL::PassId m_graphActivePass;
	bool m_graphFrameActive;
	bool m_graphPassHasRendering;

	Tr2TextureAL m_depthTexture;
	VkImage m_depthTextureImage = VK_NULL_HANDLE;

	ALResult ApplyRenderState();
	ALResult BindFrameResources();
	ALResult BeginRendering();
	ALResult EndRendering();
	ALResult TransitionBackBuffer( VkImageLayout newLayout );
	ALResult CreateBackBufferTexture();
	void DestroyBackBufferTexture();

	Tr2RenderContextAL( const Tr2RenderContextAL& ) = delete;
	Tr2RenderContextAL& operator=( const Tr2RenderContextAL& ) = delete;
};

#endif

#endif
