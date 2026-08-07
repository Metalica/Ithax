// Copyright © 2026 Ithax contributors.

#include "StdAfx.h"
#include "Tr2CapsALVulkan.h"

#if TRINITY_PLATFORM == TRINITY_VULKAN

bool Tr2CapsAL::SupportsFloat16() const
{
	return true;
}

bool Tr2CapsAL::SupportsGpuBuffer() const
{
	return true;
}

bool Tr2CapsAL::SupportsStandaloneSwapChain() const
{
	return true;
}

bool Tr2CapsAL::SupportsVertexShaderTextures() const
{
	return true;
}

bool Tr2CapsAL::SupportsVariableRefreshRate() const
{
	return false;
}

bool Tr2CapsAL::SupportsRaytracing() const
{
	return false;
}

#endif
