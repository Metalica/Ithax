// Copyright © 2026 Ithax contributors.

#include "StdAfx.h"
#include "Tr2FenceALVulkan.h"

#if TRINITY_PLATFORM == TRINITY_VULKAN

#include "ALLog.h"
#include "Tr2PrimaryRenderContextAL.h"
#include "Tr2RenderContextAL.h"

namespace TrinityALImpl
{

Tr2FenceAL::Tr2FenceAL()
{
}

Tr2FenceAL::~Tr2FenceAL()
{
	Destroy();
}

ALResult Tr2FenceAL::Create( Tr2PrimaryRenderContextAL& renderContext )
{
	m_device = renderContext.GetVulkanContext().state.device;

	VkSemaphoreTypeCreateInfo timelineInfo = {};
	timelineInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
	timelineInfo.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
	timelineInfo.initialValue = 0;

	VkSemaphoreCreateInfo createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	createInfo.pNext = &timelineInfo;

	VkResult result = vkCreateSemaphore( m_device, &createInfo, nullptr, &m_semaphore );
	if( result != VK_SUCCESS )
	{
		CCP_AL_LOGERR( "vkCreateSemaphore (fence) failed: %d", int( result ) );
		return E_FAIL;
	}
	return S_OK;
}

void Tr2FenceAL::Destroy()
{
	if( m_semaphore != VK_NULL_HANDLE )
	{
		vkDestroySemaphore( m_device, m_semaphore, nullptr );
		m_semaphore = VK_NULL_HANDLE;
	}
	m_hasFence = false;
}

bool Tr2FenceAL::IsValid() const
{
	return m_semaphore != VK_NULL_HANDLE;
}

ALResult Tr2FenceAL::PutFence( Tr2RenderContextAL& renderContext )
{
	VulkanContext& context = renderContext.GetVulkanContext();
	VulkanFrameContext& frame = context.state.frames[context.state.frameIndex];

	VkSemaphoreSubmitInfo timelineSignal = {};
	timelineSignal.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
	timelineSignal.semaphore = m_semaphore;
	timelineSignal.value = ++m_signaledValue;
	timelineSignal.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

	VkSubmitInfo2 submitInfo = {};
	submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
	submitInfo.signalSemaphoreInfoCount = 1;
	submitInfo.pSignalSemaphoreInfos = &timelineSignal;

	VkResult result = vkQueueSubmit2( context.state.graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE );
	if( result != VK_SUCCESS )
	{
		CCP_AL_LOGERR( "vkQueueSubmit2 (fence) failed: %d", int( result ) );
		return E_FAIL;
	}
	m_hasFence = true;
	return S_OK;
}

ALResult Tr2FenceAL::IsReached( bool& isReached, Tr2RenderContextAL& renderContext )
{
	(void)renderContext;
	if( !m_hasFence )
	{
		isReached = true;
		return S_OK;
	}
	uint64_t value = 0;
	VkResult result = vkGetSemaphoreCounterValue( m_device, m_semaphore, &value );
	if( result != VK_SUCCESS )
	{
		return E_FAIL;
	}
	isReached = ( value >= m_signaledValue );
	return S_OK;
}

ALResult Tr2FenceAL::Wait( Tr2RenderContextAL& renderContext )
{
	(void)renderContext;
	if( !m_hasFence )
	{
		return S_OK;
	}
	VkSemaphoreWaitInfo waitInfo = {};
	waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
	waitInfo.semaphoreCount = 1;
	waitInfo.pSemaphores = &m_semaphore;
	waitInfo.pValues = &m_signaledValue;

	VkResult result = vkWaitSemaphores( m_device, &waitInfo, std::numeric_limits<uint64_t>::max() );
	if( result != VK_SUCCESS )
	{
		return E_FAIL;
	}
	return S_OK;
}

void Tr2FenceAL::Describe( Tr2DeviceResourceDescriptionAL& description ) const
{
	description["type"] = "fence";
}

ALResult Tr2FenceAL::SetName( const char* name )
{
	SetVulkanObjectName( m_device, reinterpret_cast<uint64_t>( m_semaphore ),
		VK_OBJECT_TYPE_SEMAPHORE, name );
	return S_OK;
}

}

#endif
