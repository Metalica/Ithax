// Copyright © 2026 Ithax contributors.

#pragma once

#if TRINITY_PLATFORM == TRINITY_VULKAN

#include "ALResult.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>
#include <vector>

namespace TrinityALImpl
{

// Compiles a frame's declared pass/resource usage into execution order,
// dead-pass culling, resource lifetimes, image layouts, and synchronization2
// barriers. The graph is independent from Taskflow and from Vulkan command
// emission; Compile() is a pure CPU transformation over declarations.
class Tr2RenderGraphAL
{
public:
	enum class Queue : uint8_t
	{
		GRAPHICS,
	};

	enum class ImageAccess : uint8_t
	{
		COLOR_ATTACHMENT,
		DEPTH_ATTACHMENT,
		SHADER_READ,
		TRANSFER_WRITE,
		TRANSFER_READ,
	};

	enum class BufferAccess : uint8_t
	{
		VERTEX_READ,
		INDEX_READ,
		SHADER_READ,
		TRANSFER_WRITE,
		TRANSFER_READ,
	};

	struct ImageDesc
	{
		uint32_t width = 0;
		uint32_t height = 0;
		VkFormat format = VK_FORMAT_UNDEFINED;
		VkImageLayout initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		bool isDepth = false;
	};

	struct BufferDesc
	{
		uint32_t sizeBytes = 0;
	};

	using ResourceId = uint32_t;
	using PassId = uint32_t;

	static constexpr ResourceId INVALID_RESOURCE = UINT32_MAX;
	static constexpr PassId INVALID_PASS = UINT32_MAX;

	// One image layout transition to be recorded before beforePass. A
	// beforePass of INVALID_PASS means "after the last pass, before
	// present".
	struct ImageBarrier
	{
		PassId beforePass = INVALID_PASS;
		ResourceId resource = INVALID_RESOURCE;
		VkPipelineStageFlags2 srcStage = 0;
		VkAccessFlags2 srcAccess = 0;
		VkPipelineStageFlags2 dstStage = 0;
		VkAccessFlags2 dstAccess = 0;
		VkImageLayout oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		VkImageLayout newLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	};

	// One buffer access transition to be recorded before beforePass.
	struct BufferBarrier
	{
		PassId beforePass = INVALID_PASS;
		ResourceId resource = INVALID_RESOURCE;
		VkPipelineStageFlags2 srcStage = 0;
		VkAccessFlags2 srcAccess = 0;
		VkPipelineStageFlags2 dstStage = 0;
		VkAccessFlags2 dstAccess = 0;
	};

	struct ResourceLifetime
	{
		ResourceId resource = INVALID_RESOURCE;
		bool isImage = false;
		bool used = false;
		// Execution order indices of the first and last surviving uses.
		PassId firstPass = INVALID_PASS;
		PassId lastPass = INVALID_PASS;
	};

	// One color or depth attachment of a compiled pass, indexed by the
	// dynamic-rendering attachment slot it fills (0..3 colors, 4 = depth).
	struct PassAttachment
	{
		PassId pass = INVALID_PASS;
		ResourceId image = INVALID_RESOURCE;
		uint32_t attachmentSlot = 0;
		bool isDepth = false;
		VkAttachmentLoadOp loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
		VkAttachmentStoreOp storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		VkClearValue clearValue = {};
	};

	struct CompiledPass
	{
		PassId passId = INVALID_PASS;
		std::string name;
		Queue queue = Queue::GRAPHICS;
		bool culled = false;
		// Position in execution order; UINT32_MAX when culled.
		uint32_t orderIndex = 0;
	};

	struct CompileResult
	{
		std::vector<CompiledPass> passes;
		std::vector<ImageBarrier> imageBarriers;
		std::vector<BufferBarrier> bufferBarriers;
		std::vector<ResourceLifetime> lifetimes;
		std::vector<PassAttachment> passAttachments;
	};

	Tr2RenderGraphAL();
	~Tr2RenderGraphAL();

	Tr2RenderGraphAL( const Tr2RenderGraphAL& ) = delete;
	Tr2RenderGraphAL& operator=( const Tr2RenderGraphAL& ) = delete;

	ResourceId AddImage( const char* name, const ImageDesc& desc );
	ResourceId AddBuffer( const char* name, const BufferDesc& desc );
	PassId AddPass( const char* name, Queue queue );

	ALResult PassReadsImage( PassId pass, ResourceId resource, ImageAccess access );
	ALResult PassWritesImage( PassId pass, ResourceId resource, ImageAccess access );
	ALResult PassReadsBuffer( PassId pass, ResourceId resource, BufferAccess access );
	ALResult PassWritesBuffer( PassId pass, ResourceId resource, BufferAccess access );

	// Allows dead-pass culling: the pass may be dropped when none of its
	// written resources are consumed by a surviving pass or by present.
	void MarkPassCullable( PassId pass );

	// Sets the load/store behavior of an attachment write. The clear value
	// is used when loadOp is CLEAR. Depth attachments always store.
	ALResult SetAttachmentClear(
		PassId pass,
		ResourceId image,
		VkAttachmentLoadOp loadOp,
		const VkClearValue& clearValue );

	// Marks an image as the presented swapchain image; the compiler emits a
	// final transition to VK_IMAGE_LAYOUT_PRESENT_SRC_KHR and the writing
	// pass is never culled.
	ALResult MarkPresented( ResourceId image );

	// Compiles the declared graph. Fails with a non-empty errorMessage on
	// cycles and invalid declarations.
	ALResult Compile( CompileResult& out, std::string& errorMessage );

private:
	struct ImageUse
	{
		PassId pass;
		bool isRead;
		ImageAccess access;
		VkAttachmentLoadOp loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
		VkAttachmentStoreOp storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		VkClearValue clearValue = {};
	};

	struct BufferUse
	{
		PassId pass;
		bool isRead;
		BufferAccess access;
	};

	struct Resource
	{
		std::string name;
		bool isImage = false;
		ImageDesc imageDesc;
		BufferDesc bufferDesc;
		bool presented = false;
		std::vector<ImageUse> imageUses;
		std::vector<BufferUse> bufferUses;
	};

	struct Pass
	{
		std::string name;
		Queue queue = Queue::GRAPHICS;
		bool cullable = false;
	};

	bool HasReasonToSurvive( uint32_t pass, const std::vector<bool>& surviving ) const;

	std::vector<Resource> m_resources;
	std::vector<Pass> m_passes;
};

}

#endif
