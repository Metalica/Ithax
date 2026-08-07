// Copyright © 2026 Ithax contributors.

#include "StdAfx.h"
#include "Tr2RenderGraphALVulkan.h"

#if TRINITY_PLATFORM == TRINITY_VULKAN

#include <algorithm>
#include <functional>
#include <queue>

namespace TrinityALImpl
{

namespace
{

constexpr uint32_t MAX_PASS_COUNT = 4096;
constexpr uint32_t MAX_RESOURCE_COUNT = 4096;

struct UsePair
{
	Tr2RenderGraphAL::PassId from;
	Tr2RenderGraphAL::PassId to;
};

struct ImageAccessInfo
{
	VkPipelineStageFlags2 stage;
	VkAccessFlags2 access;
	VkImageLayout layout;
};

struct BufferAccessInfo
{
	VkPipelineStageFlags2 stage;
	VkAccessFlags2 access;
};

ImageAccessInfo GetImageAccessInfo( Tr2RenderGraphAL::ImageAccess access )
{
	switch( access )
	{
	case Tr2RenderGraphAL::ImageAccess::COLOR_ATTACHMENT:
		return {
			VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
			VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
			VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
		};
	case Tr2RenderGraphAL::ImageAccess::DEPTH_ATTACHMENT:
		return {
			VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
				VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
			VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
				VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
			VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
		};
	case Tr2RenderGraphAL::ImageAccess::SHADER_READ:
		return {
			VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT |
				VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
			VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
			VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
		};
	case Tr2RenderGraphAL::ImageAccess::TRANSFER_WRITE:
		return {
			VK_PIPELINE_STAGE_2_TRANSFER_BIT,
			VK_ACCESS_2_TRANSFER_WRITE_BIT,
			VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		};
	case Tr2RenderGraphAL::ImageAccess::TRANSFER_READ:
		return {
			VK_PIPELINE_STAGE_2_TRANSFER_BIT,
			VK_ACCESS_2_TRANSFER_READ_BIT,
			VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
		};
	}
	return { 0, 0, VK_IMAGE_LAYOUT_UNDEFINED };
}

BufferAccessInfo GetBufferAccessInfo( Tr2RenderGraphAL::BufferAccess access )
{
	switch( access )
	{
	case Tr2RenderGraphAL::BufferAccess::VERTEX_READ:
		return {
			VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT,
			VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT,
		};
	case Tr2RenderGraphAL::BufferAccess::INDEX_READ:
		return {
			VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT,
			VK_ACCESS_2_INDEX_READ_BIT,
		};
	case Tr2RenderGraphAL::BufferAccess::SHADER_READ:
		return {
			VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT |
				VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
			VK_ACCESS_2_UNIFORM_READ_BIT |
				VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
		};
	case Tr2RenderGraphAL::BufferAccess::TRANSFER_WRITE:
		return {
			VK_PIPELINE_STAGE_2_TRANSFER_BIT,
			VK_ACCESS_2_TRANSFER_WRITE_BIT,
		};
	case Tr2RenderGraphAL::BufferAccess::TRANSFER_READ:
		return {
			VK_PIPELINE_STAGE_2_TRANSFER_BIT,
			VK_ACCESS_2_TRANSFER_READ_BIT,
		};
	}
	return { 0, 0 };
}

}

Tr2RenderGraphAL::Tr2RenderGraphAL()
{
}

Tr2RenderGraphAL::~Tr2RenderGraphAL()
{
}
Tr2RenderGraphAL::ResourceId Tr2RenderGraphAL::AddImage( const char* name, const ImageDesc& desc )
{
	if( m_resources.size() >= MAX_RESOURCE_COUNT )
	{
		return INVALID_RESOURCE;
	}
	Resource resource;
	resource.name = name != nullptr ? name : "";
	resource.isImage = true;
	resource.imageDesc = desc;
	m_resources.push_back( std::move( resource ) );
	return static_cast<ResourceId>( m_resources.size() - 1 );
}

Tr2RenderGraphAL::ResourceId Tr2RenderGraphAL::AddBuffer( const char* name, const BufferDesc& desc )
{
	if( m_resources.size() >= MAX_RESOURCE_COUNT )
	{
		return INVALID_RESOURCE;
	}
	Resource resource;
	resource.name = name != nullptr ? name : "";
	resource.isImage = false;
	resource.bufferDesc = desc;
	m_resources.push_back( std::move( resource ) );
	return static_cast<ResourceId>( m_resources.size() - 1 );
}

Tr2RenderGraphAL::PassId Tr2RenderGraphAL::AddPass( const char* name, Queue queue )
{
	if( m_passes.size() >= MAX_PASS_COUNT )
	{
		return INVALID_PASS;
	}
	Pass pass;
	pass.name = name != nullptr ? name : "";
	pass.queue = queue;
	m_passes.push_back( std::move( pass ) );
	return static_cast<PassId>( m_passes.size() - 1 );
}

ALResult Tr2RenderGraphAL::PassReadsImage( PassId pass, ResourceId resource, ImageAccess access )
{
	if( pass >= m_passes.size() || resource >= m_resources.size() ||
		!m_resources[resource].isImage )
	{
		return E_INVALIDARG;
	}
	m_resources[resource].imageUses.push_back( ImageUse{ pass, true, access } );
	return S_OK;
}

ALResult Tr2RenderGraphAL::PassWritesImage( PassId pass, ResourceId resource, ImageAccess access )
{
	if( pass >= m_passes.size() || resource >= m_resources.size() ||
		!m_resources[resource].isImage )
	{
		return E_INVALIDARG;
	}
	m_resources[resource].imageUses.push_back( ImageUse{ pass, false, access } );
	return S_OK;
}

ALResult Tr2RenderGraphAL::PassReadsBuffer( PassId pass, ResourceId resource, BufferAccess access )
{
	if( pass >= m_passes.size() || resource >= m_resources.size() ||
		m_resources[resource].isImage )
	{
		return E_INVALIDARG;
	}
	m_resources[resource].bufferUses.push_back( BufferUse{ pass, true, access } );
	return S_OK;
}

ALResult Tr2RenderGraphAL::PassWritesBuffer( PassId pass, ResourceId resource, BufferAccess access )
{
	if( pass >= m_passes.size() || resource >= m_resources.size() ||
		m_resources[resource].isImage )
	{
		return E_INVALIDARG;
	}
	m_resources[resource].bufferUses.push_back( BufferUse{ pass, false, access } );
	return S_OK;
}

void Tr2RenderGraphAL::MarkPassCullable( PassId pass )
{
	if( pass < m_passes.size() )
	{
		m_passes[pass].cullable = true;
	}
}

ALResult Tr2RenderGraphAL::SetAttachmentClear(
	PassId pass,
	ResourceId image,
	VkAttachmentLoadOp loadOp,
	const VkClearValue& clearValue )
{
	if( pass >= m_passes.size() || image >= m_resources.size() ||
		!m_resources[image].isImage )
	{
		return E_INVALIDARG;
	}
	for( ImageUse& use : m_resources[image].imageUses )
	{
		if( use.pass == pass && !use.isRead &&
			( use.access == ImageAccess::COLOR_ATTACHMENT ||
				use.access == ImageAccess::DEPTH_ATTACHMENT ) )
		{
			use.loadOp = loadOp;
			use.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
			use.clearValue = clearValue;
			return S_OK;
		}
	}
	return E_INVALIDCALL;
}

ALResult Tr2RenderGraphAL::MarkPresented( ResourceId image )
{
	if( image >= m_resources.size() || !m_resources[image].isImage )
	{
		return E_INVALIDARG;
	}
	m_resources[image].presented = true;
	return S_OK;
}

bool Tr2RenderGraphAL::HasReasonToSurvive(
	uint32_t pass,
	const std::vector<bool>& surviving ) const
{
	const Pass& declared = m_passes[pass];
	if( !declared.cullable )
	{
		return true;
	}

	for( const Resource& resource : m_resources )
	{
		if( resource.presented && resource.isImage )
		{
			for( const ImageUse& use : resource.imageUses )
			{
				if( !use.isRead && use.pass == pass )
				{
					return true;
				}
			}
		}
		for( const ImageUse& use : resource.imageUses )
		{
			if( use.pass != pass || use.isRead )
			{
				continue;
			}
			for( const ImageUse& reader : resource.imageUses )
			{
				if( reader.pass == pass || !reader.isRead || !surviving[reader.pass] )
				{
					continue;
				}
				return true;
			}
		}
		for( const BufferUse& use : resource.bufferUses )
		{
			if( use.pass != pass || use.isRead )
			{
				continue;
			}
			for( const BufferUse& reader : resource.bufferUses )
			{
				if( reader.pass == pass || !reader.isRead || !surviving[reader.pass] )
				{
					continue;
				}
				return true;
			}
		}
	}
	return false;
}

ALResult Tr2RenderGraphAL::Compile( CompileResult& out, std::string& errorMessage )
{
	out = CompileResult();

	if( m_passes.empty() )
	{
		errorMessage = "render graph has no passes";
		return E_INVALIDCALL;
	}
	if( m_resources.empty() )
	{
		errorMessage = "render graph has no resources";
		return E_INVALIDCALL;
	}

	// Validate declarations before any ordering work.
	for( uint32_t r = 0; r < m_resources.size(); ++r )
	{
		const Resource& resource = m_resources[r];
		if( resource.isImage )
		{
			if( resource.presented && resource.imageUses.empty() )
			{
				errorMessage = "presented image '" + resource.name +
					"' has no declared uses";
				return E_INVALIDCALL;
			}
			if( !resource.imageUses.empty() &&
				resource.imageDesc.initialLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
				resource.imageUses.front().isRead )
			{
				errorMessage = "image '" + resource.name +
					"' is read before its first write";
				return E_INVALIDCALL;
			}
		}
	}

	// Build ordering edges between consecutive uses of each resource.
	std::vector<UsePair> edges;
	edges.reserve( m_resources.size() );
	for( const Resource& resource : m_resources )
	{
		if( resource.isImage )
		{
			for( size_t u = 1; u < resource.imageUses.size(); ++u )
			{
				const ImageUse& previous = resource.imageUses[u - 1];
				const ImageUse& current = resource.imageUses[u];
				if( previous.isRead && current.isRead )
				{
					continue;
				}
				edges.push_back( UsePair{ previous.pass, current.pass } );
			}
		}
		else
		{
			for( size_t u = 1; u < resource.bufferUses.size(); ++u )
			{
				const BufferUse& previous = resource.bufferUses[u - 1];
				const BufferUse& current = resource.bufferUses[u];
				if( previous.isRead && current.isRead )
				{
					continue;
				}
				edges.push_back( UsePair{ previous.pass, current.pass } );
			}
		}
	}

	// Kahn's algorithm over pass indices.
	const uint32_t passCount = static_cast<uint32_t>( m_passes.size() );
	std::vector<uint32_t> inDegree( passCount, 0 );
	std::vector<std::vector<uint32_t>> outgoing( passCount );
	for( const UsePair& edge : edges )
	{
		if( edge.from == edge.to )
		{
			errorMessage = "pass '" + m_passes[edge.from].name +
				"' reads and writes the same resource";
			return E_INVALIDCALL;
		}
		inDegree[edge.to] += 1;
		outgoing[edge.from].push_back( edge.to );
	}

	std::priority_queue<uint32_t, std::vector<uint32_t>,
		std::greater<uint32_t>> ready;
	for( uint32_t p = 0; p < passCount; ++p )
	{
		if( inDegree[p] == 0 )
		{
			ready.push( p );
		}
	}

	std::vector<uint32_t> order;
	order.reserve( passCount );
	while( !ready.empty() )
	{
		const uint32_t pass = ready.top();
		ready.pop();
		order.push_back( pass );
		for( const uint32_t next : outgoing[pass] )
		{
			inDegree[next] -= 1;
			if( inDegree[next] == 0 )
			{
				ready.push( next );
			}
		}
	}
	if( order.size() != passCount )
	{
		errorMessage = "render graph contains a cycle";
		return E_INVALIDCALL;
	}

	// Dead-pass culling: iterate in reverse execution order, keeping a pass
	// when it is non-cullable, writes a presented image, or writes a
	// resource read by a surviving later pass.
	std::vector<bool> surviving( passCount, false );
	for( auto passIt = order.rbegin(); passIt != order.rend(); ++passIt )
	{
		const uint32_t pass = *passIt;
		if( HasReasonToSurvive( pass, surviving ) )
		{
			surviving[pass] = true;
		}
	}
	out.passes.resize( passCount );
	uint32_t orderIndex = 0;
	for( const uint32_t pass : order )
	{
		CompiledPass& compiled = out.passes[pass];
		compiled.passId = pass;
		compiled.name = m_passes[pass].name;
		compiled.queue = m_passes[pass].queue;
		compiled.culled = !surviving[pass];
		compiled.orderIndex = compiled.culled ? UINT32_MAX : orderIndex++;
	}

	// Resource lifetimes over surviving uses only.
	out.lifetimes.reserve( m_resources.size() );
	for( uint32_t r = 0; r < m_resources.size(); ++r )
	{
		ResourceLifetime lifetime;
		lifetime.resource = r;
		lifetime.isImage = m_resources[r].isImage;
		const Resource& resource = m_resources[r];
		if( resource.isImage )
		{
			for( const ImageUse& use : resource.imageUses )
			{
				if( surviving[use.pass] )
				{
					lifetime.used = true;
					if( lifetime.firstPass == INVALID_PASS )
					{
						lifetime.firstPass = use.pass;
					}
					lifetime.lastPass = use.pass;
				}
			}
		}
		else
		{
			for( const BufferUse& use : resource.bufferUses )
			{
				if( surviving[use.pass] )
				{
					lifetime.used = true;
					if( lifetime.firstPass == INVALID_PASS )
					{
						lifetime.firstPass = use.pass;
					}
					lifetime.lastPass = use.pass;
				}
			}
		}
		out.lifetimes.push_back( std::move( lifetime ) );
	}

	// Pass attachments: one entry per color/depth attachment write of a
	// surviving pass. Color attachment writes fill slots 0..3 in write
	// order; the depth attachment write fills slot 4. Transfer writes are
	// not rendering attachments and are skipped.
	out.passAttachments.reserve( m_resources.size() );
	for( uint32_t pass = 0; pass < passCount; ++pass )
	{
		if( !surviving[pass] )
		{
			continue;
		}
		uint32_t colorSlot = 0;
		for( uint32_t r = 0; r < m_resources.size(); ++r )
		{
			const Resource& resource = m_resources[r];
			if( !resource.isImage )
			{
				continue;
			}
			for( const ImageUse& use : resource.imageUses )
			{
				if( use.pass != pass || use.isRead )
				{
					continue;
				}
				PassAttachment attachment;
				attachment.pass = pass;
				attachment.image = r;
				attachment.loadOp = use.loadOp;
				attachment.storeOp = use.storeOp;
				attachment.clearValue = use.clearValue;
				if( use.access == ImageAccess::DEPTH_ATTACHMENT )
				{
					attachment.attachmentSlot = 4;
					attachment.isDepth = true;
					out.passAttachments.push_back( attachment );
				}
				else if( use.access == ImageAccess::COLOR_ATTACHMENT &&
					colorSlot < 4 )
				{
					attachment.attachmentSlot = colorSlot++;
					attachment.isDepth = false;
					out.passAttachments.push_back( attachment );
				}
			}
		}
	}

	// Image barriers: the first surviving use needs a transition that
	// establishes the actual runtime layout (the recorder supplies the
	// registered layout as oldLayout); later uses need a transition per
	// layout or stage/access change. A barrier is needed whenever the
	// previous use writes or the current use writes. The present
	// transition is last.
	for( uint32_t r = 0; r < m_resources.size(); ++r )
	{
		const Resource& resource = m_resources[r];
		if( !resource.isImage || resource.imageUses.empty() )
		{
			continue;
		}
		VkImageLayout currentLayout = resource.imageDesc.initialLayout;
		VkPipelineStageFlags2 previousStage = 0;
		VkAccessFlags2 previousAccess = 0;
		bool previousWasWrite = true;
		bool havePrevious = false;
		for( const ImageUse& use : resource.imageUses )
		{
			if( !surviving[use.pass] )
			{
				continue;
			}
			const ImageAccessInfo info = GetImageAccessInfo( use.access );
			const bool needsBarrier = havePrevious
				? ( info.layout != currentLayout || previousWasWrite || !use.isRead )
				: ( info.layout != currentLayout );
			if( needsBarrier )
			{
				ImageBarrier barrier;
				barrier.beforePass = use.pass;
				barrier.resource = r;
				barrier.srcStage = havePrevious ? previousStage
					: VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
				barrier.srcAccess = havePrevious ? previousAccess : 0;
				barrier.dstStage = info.stage;
				barrier.dstAccess = info.access;
				barrier.oldLayout = currentLayout;
				barrier.newLayout = info.layout;
				out.imageBarriers.push_back( barrier );
			}
			currentLayout = info.layout;
			previousStage = info.stage;
			previousAccess = info.access;
			previousWasWrite = !use.isRead;
			havePrevious = true;
		}
		if( resource.presented )
		{
			ImageBarrier barrier;
			barrier.beforePass = INVALID_PASS;
			barrier.resource = r;
			barrier.srcStage = previousStage;
			barrier.srcAccess = previousAccess;
			barrier.dstStage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
			barrier.dstAccess = VK_ACCESS_2_MEMORY_READ_BIT |
				VK_ACCESS_2_MEMORY_WRITE_BIT;
			barrier.oldLayout = currentLayout;
			barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
			out.imageBarriers.push_back( barrier );
		}
	}

	// Buffer barriers: stage/access changes between consecutive surviving
	// uses. The first use needs no barrier (persistent content). A barrier
	// is needed whenever either side writes (hazard), or the stage/access
	// changes between read-only uses.
	for( uint32_t r = 0; r < m_resources.size(); ++r )
	{
		const Resource& resource = m_resources[r];
		if( resource.isImage || resource.bufferUses.empty() )
		{
			continue;
		}
		VkPipelineStageFlags2 previousStage = 0;
		VkAccessFlags2 previousAccess = 0;
		bool previousWasWrite = true;
		bool havePrevious = false;
		for( const BufferUse& use : resource.bufferUses )
		{
			if( !surviving[use.pass] )
			{
				continue;
			}
			const BufferAccessInfo info = GetBufferAccessInfo( use.access );
			if( havePrevious &&
				( previousWasWrite || !use.isRead ||
					info.stage != previousStage || info.access != previousAccess ) )
			{
				BufferBarrier barrier;
				barrier.beforePass = use.pass;
				barrier.resource = r;
				barrier.srcStage = previousStage;
				barrier.srcAccess = previousAccess;
				barrier.dstStage = info.stage;
				barrier.dstAccess = info.access;
				out.bufferBarriers.push_back( barrier );
			}
			previousStage = info.stage;
			previousAccess = info.access;
			previousWasWrite = !use.isRead;
			havePrevious = true;
		}
	}

	errorMessage.clear();
	return S_OK;
}

}

#endif
