cmake_minimum_required(VERSION 3.30)
project(ithax-trinityal-standalone LANGUAGES CXX)

get_filename_component(REPO_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(AL_ROOT "${REPO_ROOT}/external/trinity/trinityal")
set(ITHAX_VULKAN_DIR "${REPO_ROOT}/src/trinityal-vulkan")

# The upstream TrinityAL include macros resolve platform headers through
# ../vulkan/... relative to the trinityal root. Mirror the Ithax Vulkan
# backend into the upstream tree when it is not already present, matching
# the carbon-trinity portfile injection.
if(NOT EXISTS "${AL_ROOT}/vulkan")
    file(COPY "${ITHAX_VULKAN_DIR}/" DESTINATION "${AL_ROOT}/vulkan")
endif()

# The pinned upstream TrinityALForward.h predates the Vulkan backend and
# has no TRINITY_VULKAN platform branch, which fails the Vulkan target
# with "Missing TrinityAL platform description". Apply the Ithax Vulkan
# patch set (platform branch, readback invalidation, FSR1 Vulkan guards)
# in place; the patch is idempotent so a pre-patched tree is left
# untouched.
set(TRINITY_PATCH_FILE "${REPO_ROOT}/cmake/patches/trinityal-vulkan.patch")
set(TRINITY_PATCH_MARKER "${AL_ROOT}/include/TrinityALForward.h")
file(READ "${TRINITY_PATCH_MARKER}" TRINITY_PATCH_CONTENT)
if(NOT TRINITY_PATCH_CONTENT MATCHES "TRINITY_VULKAN")
    find_program(TRINITY_GIT_EXECUTABLE NAMES git git.cmd REQUIRED)
    execute_process(
        COMMAND "${TRINITY_GIT_EXECUTABLE}"
            -c core.autocrlf=false
            apply
            --directory=external/trinity
            --ignore-whitespace
            --whitespace=nowarn
            "${TRINITY_PATCH_FILE}"
        WORKING_DIRECTORY "${REPO_ROOT}"
        RESULT_VARIABLE TRINITY_PATCH_RESULT
        OUTPUT_VARIABLE TRINITY_PATCH_OUTPUT
        ERROR_VARIABLE TRINITY_PATCH_ERROR
    )
    if(NOT TRINITY_PATCH_RESULT EQUAL 0)
        message(FATAL_ERROR
            "Failed to apply the TrinityAL Vulkan patch set: "
            "${TRINITY_PATCH_RESULT}\n${TRINITY_PATCH_ERROR}")
    endif()
    file(READ "${TRINITY_PATCH_MARKER}" TRINITY_PATCH_CONTENT)
    if(NOT TRINITY_PATCH_CONTENT MATCHES "TRINITY_VULKAN")
        message(FATAL_ERROR
            "TrinityAL Vulkan patch completed without updating "
            "TrinityALForward.h")
    endif()
endif()

find_package(carbon-core CONFIG REQUIRED)
find_package(carbon-imageio CONFIG REQUIRED)
find_package(nvidia-aftermath CONFIG REQUIRED)
find_package(nvidia-streamline CONFIG REQUIRED)
find_package(nvidia-streamline-dlss-sr CONFIG REQUIRED)
find_package(nvidia-streamline-dlss-fg CONFIG REQUIRED)
find_package(nvidia-streamline-nis CONFIG REQUIRED)
find_package(amd-fidelityfx-fsr CONFIG REQUIRED)
find_package(CarbonMesh CONFIG REQUIRED)
find_package(Vulkan REQUIRED)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)

set(TRINITYAL_SOURCES
    "${AL_ROOT}/ALResult.cpp"
    "${AL_ROOT}/BcDecompress.cpp"
    "${AL_ROOT}/StdAfx.cpp"
    "${AL_ROOT}/Tr2DeviceResourceAL.cpp"
    "${AL_ROOT}/Tr2DrawUPHelper.cpp"
    "${AL_ROOT}/Tr2DriverUtilities.cpp"
    "${AL_ROOT}/Tr2HalHelperStructures.cpp"
    "${AL_ROOT}/Tr2LockGuard.cpp"
    "${AL_ROOT}/Tr2MemoryCounterAL.cpp"
    "${AL_ROOT}/Tr2RenderContextEnum.cpp"
    "${AL_ROOT}/Tr2VertexDefinition.cpp"
    "${AL_ROOT}/dx11/Tr2BufferALDx11.cpp"
    "${AL_ROOT}/dx11/Tr2CapsALDx11.cpp"
    "${AL_ROOT}/dx11/Tr2ConstantBufferALDx11.cpp"
    "${AL_ROOT}/dx11/Tr2FenceALDx11.cpp"
    "${AL_ROOT}/dx11/Tr2GpuTimerALDx11.cpp"
    "${AL_ROOT}/dx11/Tr2OcclusionQueryALDx11.cpp"
    "${AL_ROOT}/dx11/Tr2PipelineStatsQueryALDx11.cpp"
    "${AL_ROOT}/dx11/Tr2PrimaryRenderContextDx11.cpp"
    "${AL_ROOT}/dx11/Tr2RenderContextDx11.cpp"
    "${AL_ROOT}/dx11/Tr2RenderStateEmulationDx11.cpp"
    "${AL_ROOT}/dx11/Tr2ResourceSetALDx11.cpp"
    "${AL_ROOT}/dx11/Tr2SamplerStateALDx11.cpp"
    "${AL_ROOT}/dx11/Tr2ShaderALDx11.cpp"
    "${AL_ROOT}/dx11/Tr2ShaderProgramALDx11.cpp"
    "${AL_ROOT}/dx11/Tr2SwapChainALDx11.cpp"
    "${AL_ROOT}/dx11/Tr2TextureALDx11.cpp"
    "${AL_ROOT}/dx11/Tr2VertexLayoutALDx11.cpp"
    "${AL_ROOT}/dx11/Tr2VideoAdapterInfoALDx11.cpp"
    "${AL_ROOT}/dx11/upscaling/Tr2DlssUpscaling.cpp"
    "${AL_ROOT}/dx11/upscaling/Tr2UpscalingALDx11.cpp"
    "${AL_ROOT}/dx12/Tr2BufferALDx12.cpp"
    "${AL_ROOT}/dx12/Tr2ConstantBufferALDx12.cpp"
    "${AL_ROOT}/dx12/Tr2FenceALDx12.cpp"
    "${AL_ROOT}/dx12/Tr2GpuTimerALDx12.cpp"
    "${AL_ROOT}/dx12/Tr2OcclusionQueryALDx12.cpp"
    "${AL_ROOT}/dx12/Tr2PipelineStatsQueryALDx12.cpp"
    "${AL_ROOT}/dx12/Tr2PrimaryRenderContextDx12.cpp"
    "${AL_ROOT}/dx12/Tr2RenderContextDx12.cpp"
    "${AL_ROOT}/dx12/Tr2ResourceHelper.cpp"
    "${AL_ROOT}/dx12/Tr2ResourceSetALDx12.cpp"
    "${AL_ROOT}/dx12/Tr2RtBottomLevelAccelerationStructureALDx12.cpp"
    "${AL_ROOT}/dx12/Tr2RtPipelineStateALDx12.cpp"
    "${AL_ROOT}/dx12/Tr2RtShaderTableALDx12.cpp"
    "${AL_ROOT}/dx12/Tr2RtTopLevelAccelerationStructureALDx12.cpp"
    "${AL_ROOT}/dx12/Tr2SamplerStateALDx12.cpp"
    "${AL_ROOT}/dx12/Tr2ShaderALDx12.cpp"
    "${AL_ROOT}/dx12/Tr2ShaderProgramALDx12.cpp"
    "${AL_ROOT}/dx12/Tr2SwapChainALDx12.cpp"
    "${AL_ROOT}/dx12/Tr2TextureALDx12.cpp"
    "${AL_ROOT}/dx12/Tr2VertexLayoutALDx12.cpp"
    "${AL_ROOT}/dx12/Tr2VideoAdapterInfoALDx12.cpp"
    "${AL_ROOT}/dx12/Utilities.cpp"
    "${AL_ROOT}/dx12/upscaling/Tr2DlssUpscaling.cpp"
    "${AL_ROOT}/dx12/upscaling/Tr2Fsr3Upscaling.cpp"
    "${AL_ROOT}/dx12/upscaling/Tr2UpscalingALDx12.cpp"
    "${AL_ROOT}/dx12/upscaling/Tr2XessUpscaling.cpp"
    "${AL_ROOT}/dx12/util/DescriptorHeapViewDx12.cpp"
    "${AL_ROOT}/dx12/util/DescriptorStateCacheDx12.cpp"
    "${AL_ROOT}/dx12/util/FrameLocalDescriptorHeapAllocatorDx12.cpp"
    "${AL_ROOT}/dx12/util/FrameLocalUploadBufferAllocatorDx12.cpp"
    "${AL_ROOT}/dx12/util/GlobalDescriptorHeapAllocatorDx12.cpp"
    "${AL_ROOT}/dx12/util/GpuCrashTracker.cpp"
    "${AL_ROOT}/dx12/util/GpuMarkerBuffer.cpp"
    "${AL_ROOT}/dx12/util/PsoDescription.cpp"
    "${AL_ROOT}/src/Tr2BufferAL.cpp"
    "${AL_ROOT}/src/Tr2ConstantBufferAL.cpp"
    "${AL_ROOT}/src/Tr2FenceAL.cpp"
    "${AL_ROOT}/src/Tr2GpuTimerAl.cpp"
    "${AL_ROOT}/src/Tr2OcclusionQueryAL.cpp"
    "${AL_ROOT}/src/Tr2PipelineStatsQueryAL.cpp"
    "${AL_ROOT}/src/Tr2ResourceSetAL.cpp"
    "${AL_ROOT}/src/Tr2RtBottomLevelAccelerationStructureAL.cpp"
    "${AL_ROOT}/src/Tr2RtPipelineStateAL.cpp"
    "${AL_ROOT}/src/Tr2RtShaderTableAL.cpp"
    "${AL_ROOT}/src/Tr2RtTopLevelAccelerationStructureAL.cpp"
    "${AL_ROOT}/src/Tr2SamplerStateAL.cpp"
    "${AL_ROOT}/src/Tr2ShaderAL.cpp"
    "${AL_ROOT}/src/Tr2ShaderProgramAL.cpp"
    "${AL_ROOT}/src/Tr2StreamlineAL.cpp"
    "${AL_ROOT}/src/Tr2SwapChainAL.cpp"
    "${AL_ROOT}/src/Tr2TextureAL.cpp"
    "${AL_ROOT}/src/Tr2VertexLayoutAL.cpp"
    "${AL_ROOT}/src/upscaling/Tr2Fsr1Upscaling.cpp"
    "${AL_ROOT}/src/upscaling/Tr2UpscalingAL.cpp"
    "${AL_ROOT}/stub/Tr2BufferALStub.cpp"
    "${AL_ROOT}/stub/Tr2CapsALStub.cpp"
    "${AL_ROOT}/stub/Tr2ConstantBufferALStub.cpp"
    "${AL_ROOT}/stub/Tr2FenceALStub.cpp"
    "${AL_ROOT}/stub/Tr2GpuTimerALStub.cpp"
    "${AL_ROOT}/stub/Tr2OcclusionQueryALStub.cpp"
    "${AL_ROOT}/stub/Tr2PipelineStatsQueryALStub.cpp"
    "${AL_ROOT}/stub/Tr2RenderContextStub.cpp"
    "${AL_ROOT}/stub/Tr2ResourceSetALStub.cpp"
    "${AL_ROOT}/stub/Tr2SamplerStateALStub.cpp"
    "${AL_ROOT}/stub/Tr2ShaderALStub.cpp"
    "${AL_ROOT}/stub/Tr2ShaderProgramALStub.cpp"
    "${AL_ROOT}/stub/Tr2SwapChainALStub.cpp"
    "${AL_ROOT}/stub/Tr2TextureALStub.cpp"
    "${AL_ROOT}/stub/Tr2VertexLayoutALStub.cpp"
    "${AL_ROOT}/stub/Tr2VideoAdapterInfoALStub.cpp"
    "${AL_ROOT}/stub/upscaling/Tr2UpscalingALStub.cpp"
    "${AL_ROOT}/vulkan/Tr2BufferALVulkan.cpp"
    "${AL_ROOT}/vulkan/Tr2CapsALVulkan.cpp"
    "${AL_ROOT}/vulkan/Tr2ConstantBufferALVulkan.cpp"
    "${AL_ROOT}/vulkan/Tr2FenceALVulkan.cpp"
    "${AL_ROOT}/vulkan/Tr2GpuTimerALVulkan.cpp"
    "${AL_ROOT}/vulkan/Tr2OcclusionQueryALVulkan.cpp"
    "${AL_ROOT}/vulkan/Tr2PipelineStatsQueryALVulkan.cpp"
    "${AL_ROOT}/vulkan/Tr2RenderContextVulkan.cpp"
    "${AL_ROOT}/vulkan/Tr2RenderGraphALVulkan.cpp"
    "${AL_ROOT}/vulkan/Tr2ResourceSetALVulkan.cpp"
    "${AL_ROOT}/vulkan/Tr2SamplerStateALVulkan.cpp"
    "${AL_ROOT}/vulkan/Tr2ShaderALVulkan.cpp"
    "${AL_ROOT}/vulkan/Tr2ShaderProgramALVulkan.cpp"
    "${AL_ROOT}/vulkan/Tr2SwapChainALVulkan.cpp"
    "${AL_ROOT}/vulkan/Tr2TextureALVulkan.cpp"
    "${AL_ROOT}/vulkan/Tr2VertexLayoutALVulkan.cpp"
    "${AL_ROOT}/vulkan/Tr2VideoAdapterInfoALVulkan.cpp"
    "${AL_ROOT}/vulkan/Tr2VulkanContext.cpp"
    "${AL_ROOT}/vulkan/VmaImplementation.cpp"
)

add_library(TrinityAL_dx11 STATIC ${TRINITYAL_SOURCES})

target_compile_definitions(TrinityAL_dx11 PUBLIC
    TRINITY_PLATFORM=TRINITY_DIRECTX11
)
target_compile_definitions(TrinityAL_dx11 PRIVATE
    TRINITY_AL_WITH_BLUE_EXPOSURE=0
    _WIN32_WINNT=0x0A00
    TRINITYDEV=0
)
target_compile_options(TrinityAL_dx11 PRIVATE
    "/FI${AL_ROOT}/StdAfx.h"
    /bigobj
)
target_compile_features(TrinityAL_dx11 PRIVATE cxx_std_17)
set_property(TARGET TrinityAL_dx11
    PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
target_compile_definitions(TrinityAL_dx11 PRIVATE _ITERATOR_DEBUG_LEVEL=0)
target_include_directories(TrinityAL_dx11 PUBLIC
    "${AL_ROOT}/include"
)
target_include_directories(TrinityAL_dx11 PRIVATE
    "${AL_ROOT}"
    "${AL_ROOT}/.."
)
target_link_libraries(TrinityAL_dx11 PUBLIC
    d3d11
    dxgi
    dxguid
    NVIDIA::Aftermath
    NVIDIA::Streamline
    NVIDIA::Streamline::dlss-sr
    NVIDIA::Streamline::dlss-fg
    NVIDIA::Streamline::nis
    AMD::FidelityFX::fsr
    legacy_stdio_definitions
    CcpCore
    CcpImageIO
)

add_library(TrinityAL_vulkan STATIC ${TRINITYAL_SOURCES})

target_compile_definitions(TrinityAL_vulkan PUBLIC
    TRINITY_PLATFORM=TRINITY_VULKAN
)
target_compile_definitions(TrinityAL_vulkan PRIVATE
    TRINITY_AL_WITH_BLUE_EXPOSURE=0
    _WIN32_WINNT=0x0A00
    TRINITYDEV=0
)
target_compile_options(TrinityAL_vulkan PRIVATE
    "/FI${AL_ROOT}/StdAfx.h"
    /bigobj
)
target_compile_features(TrinityAL_vulkan PRIVATE cxx_std_17)
set_property(TARGET TrinityAL_vulkan
    PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
target_compile_definitions(TrinityAL_vulkan PRIVATE _ITERATOR_DEBUG_LEVEL=0)
target_include_directories(TrinityAL_vulkan PUBLIC
    "${AL_ROOT}/include"
)
target_include_directories(TrinityAL_vulkan PRIVATE
    "${AL_ROOT}"
    "${AL_ROOT}/.."
)
target_link_libraries(TrinityAL_vulkan PUBLIC
    Vulkan::Vulkan
    GPUOpen::VulkanMemoryAllocator
    CcpCore
    CcpImageIO
)

# ---------------------------------------------------------------------------
# Stage 5 milestone scene: first TrinityAL Vulkan scene.
# ---------------------------------------------------------------------------
set(DXC_TOOL "${REPO_ROOT}/external/dxc/bin/x64/dxc.exe")
if(NOT EXISTS "${DXC_TOOL}")
    set(DXC_TOOL
        "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/tools/directx-dxc/dxc.exe")
endif()
if(NOT EXISTS "${DXC_TOOL}")
    message(FATAL_ERROR "dxc not found; install directx-dxc or populate external/dxc")
endif()

set(STAGE5_SHADER_DIR "${REPO_ROOT}/src/trinityal-vulkan/shaders")
set(STAGE5_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/stage5-generated")

add_executable(ithax-stage5-milestone-scene
    "${REPO_ROOT}/src/stage5_milestone_scene.cpp"
)

target_compile_features(ithax-stage5-milestone-scene PRIVATE cxx_std_17)
set_property(TARGET ithax-stage5-milestone-scene
    PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
target_compile_definitions(ithax-stage5-milestone-scene PRIVATE
    _ITERATOR_DEBUG_LEVEL=0
    TRINITY_PLATFORM=TRINITY_VULKAN
    TRINITY_AL_WITH_BLUE_EXPOSURE=0
    TRINITYDEV=0
)
target_compile_options(ithax-stage5-milestone-scene PRIVATE
    "/FI${AL_ROOT}/StdAfx.h"
)
target_include_directories(ithax-stage5-milestone-scene PRIVATE
    "${STAGE5_GENERATED_DIR}"
    "${AL_ROOT}/include"
    "${AL_ROOT}"
    "${AL_ROOT}/.."
)
target_link_libraries(ithax-stage5-milestone-scene PRIVATE
    TrinityAL_vulkan
    Vulkan::Vulkan
    GPUOpen::VulkanMemoryAllocator
    CcpCore
    CcpImageIO
    CarbonMesh
)

set(STAGE5_SHADERS
    PositionColor.vsh
    PositionColor.psh
    Textured.vsh
    Textured.psh
    Starfield.vsh
    Starfield.psh
)

set(Python3_EXECUTABLE
    "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/tools/python3/python.exe")
find_package(Python3 COMPONENTS Interpreter REQUIRED)

foreach(shaderSource ${STAGE5_SHADERS})
    string(REGEX REPLACE "\\.[^.]+$" "" shaderBase ${shaderSource})
    if(${shaderSource} MATCHES ".*vsh$")
        set(SHADER_TARGET "vs_6_0")
        set(SHADER_EXTRA_FLAGS "-fvk-invert-y")
        set(shaderSymbol "g_${shaderBase}_vs")
        set(shaderOutput "${STAGE5_GENERATED_DIR}/${shaderBase}_vs.h")
        set(shaderSpv "${STAGE5_GENERATED_DIR}/${shaderBase}_vs.spv")
    else()
        set(SHADER_TARGET "ps_6_0")
        set(SHADER_EXTRA_FLAGS "")
        set(shaderSymbol "g_${shaderBase}_ps")
        set(shaderOutput "${STAGE5_GENERATED_DIR}/${shaderBase}_ps.h")
        set(shaderSpv "${STAGE5_GENERATED_DIR}/${shaderBase}_ps.spv")
    endif()
    if(${shaderBase} MATCHES "^(Textured|Starfield)$")
        # The Vulkan backend descriptor layout fixes binding 0 = UBO,
        # 1 = SRV, 3 = samplers; shift HLSL t/s registers accordingly.
        set(SHADER_BIND_FLAGS "-fvk-t-shift" "1" "0" "-fvk-s-shift" "3" "0")
    else()
        set(SHADER_BIND_FLAGS "")
    endif()
    add_custom_command(
        OUTPUT ${shaderOutput}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${STAGE5_GENERATED_DIR}
        COMMAND ${DXC_TOOL} -spirv -T ${SHADER_TARGET} -E main
                -fvk-use-dx-layout ${SHADER_EXTRA_FLAGS}
                ${SHADER_BIND_FLAGS}
                ${STAGE5_SHADER_DIR}/${shaderSource}
                -Fo ${shaderSpv}
        COMMAND ${Python3_EXECUTABLE}
                "${REPO_ROOT}/scripts/bin2h.py"
                ${shaderSpv}
                ${shaderOutput}
                ${shaderSymbol}
        MAIN_DEPENDENCY ${STAGE5_SHADER_DIR}/${shaderSource}
    )
    target_sources(ithax-stage5-milestone-scene PRIVATE ${shaderOutput})
endforeach()

# ---------------------------------------------------------------------------
# Stage 5.7 render graph compilation tests (CPU-only, no Vulkan device).
# ---------------------------------------------------------------------------
add_executable(ithax-stage5-render-graph-test
    "${REPO_ROOT}/src/stage5_render_graph_test.cpp"
)

target_compile_features(ithax-stage5-render-graph-test PRIVATE cxx_std_17)
set_property(TARGET ithax-stage5-render-graph-test
    PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
target_compile_definitions(ithax-stage5-render-graph-test PRIVATE
    _ITERATOR_DEBUG_LEVEL=0
    TRINITY_PLATFORM=TRINITY_VULKAN
    TRINITY_AL_WITH_BLUE_EXPOSURE=0
    TRINITYDEV=0
)
target_compile_options(ithax-stage5-render-graph-test PRIVATE
    "/FI${AL_ROOT}/StdAfx.h"
)
target_include_directories(ithax-stage5-render-graph-test PRIVATE
    "${AL_ROOT}/include"
    "${AL_ROOT}"
    "${AL_ROOT}/.."
)
target_link_libraries(ithax-stage5-render-graph-test PRIVATE
    TrinityAL_vulkan
    Vulkan::Vulkan
    CcpCore
)

include(CTest)
if(BUILD_TESTING)
    add_test(
        NAME stage5_render_graph_compile
        COMMAND ithax-stage5-render-graph-test
    )
    set_tests_properties(stage5_render_graph_compile
        PROPERTIES
            LABELS "stage5;vulkan;render-graph"
            TIMEOUT 120
    )
endif()
