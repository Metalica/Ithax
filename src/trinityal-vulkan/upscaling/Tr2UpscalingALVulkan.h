// Copyright © 2026 Ithax contributors.

#pragma once

#if TRINITY_PLATFORM == TRINITY_VULKAN

#include "upscaling/Tr2UpscalingAL.h"

namespace TrinityALImpl
{
static const std::vector<Tr2UpscalingAL::Technique> AVAILABLE_UPSCALING_TECHNIQUES = {};

inline Tr2UpscalingTechniqueAL* CreateUpscalingTechnique( Tr2UpscalingAL::Technique technique, Tr2UpscalingAL::Setting setting, bool frameGeneration, uint32_t adapter )
{
	(void)technique;
	(void)setting;
	(void)frameGeneration;
	(void)adapter;
	return nullptr;
}

}
#endif
