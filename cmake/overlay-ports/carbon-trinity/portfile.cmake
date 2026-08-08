vcpkg_from_git(
  OUT_SOURCE_PATH SOURCE_PATH
  URL https://github.com/carbonengine/trinity.git
  REF 4675ceaaa445f7fd44a1dc97472c8efa4ad8599c
)

# Setup the features
vcpkg_check_features(OUT_FEATURE_OPTIONS FEATURE_OPTIONS
    FEATURES
        shader-compiler         BUILD_SHADER_COMPILER
        dx11                    BUILD_DX11
        dx12                    BUILD_DX12
        metal                   BUILD_METAL
        vulkan                  BUILD_VULKAN
        with-granny             WITH_GRANNY
)

# Inject the Ithax Vulkan TrinityAL backend into the upstream source tree.
# The upstream Carbon Trinity 4.0.2 has no Vulkan backend; the Ithax backend
# lives in the repository at src/trinityal-vulkan.
set(ITHAX_VULKAN_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../src/trinityal-vulkan")
if(EXISTS "${ITHAX_VULKAN_SOURCE_DIR}")
    file(COPY "${ITHAX_VULKAN_SOURCE_DIR}/" DESTINATION "${SOURCE_PATH}/trinityal/vulkan")
endif()

# The pinned upstream TrinityALForward.h predates the Vulkan backend and
# has no TRINITY_VULKAN platform branch, which fails the Vulkan target
# with "Missing TrinityAL platform description". Apply the Ithax Vulkan
# patch set (platform branch, readback invalidation, FSR1 Vulkan guards)
# in place; the patch is idempotent so a pre-patched tree is left
# untouched.
set(TRINITY_PATCH_FILE "${CMAKE_CURRENT_LIST_DIR}/../../patches/trinityal-vulkan.patch")
set(TRINITY_PATCH_MARKER "${SOURCE_PATH}/trinityal/include/TrinityALForward.h")
file(READ "${TRINITY_PATCH_MARKER}" TRINITY_PATCH_CONTENT)
if(NOT TRINITY_PATCH_CONTENT MATCHES "TRINITY_VULKAN")
    # A Windows checkout converts the LF patch to CRLF, which git apply
    # rejects against the LF git-cloned sources. Normalize to LF first.
    file(READ "${TRINITY_PATCH_FILE}" TRINITY_PATCH_TEXT)
    string(REPLACE "\r\n" "\n" TRINITY_PATCH_TEXT "${TRINITY_PATCH_TEXT}")
    set(TRINITY_PATCH_NORMALIZED
        "${CURRENT_BUILDTREES_DIR}/trinityal-vulkan.patch")
    file(WRITE "${TRINITY_PATCH_NORMALIZED}" "${TRINITY_PATCH_TEXT}")
    vcpkg_execute_required_process(
        COMMAND git apply --whitespace=nowarn "${TRINITY_PATCH_NORMALIZED}"
        WORKING_DIRECTORY "${SOURCE_PATH}"
        LOGNAME trinityal-vulkan-patch
    )
endif()

# Add the Vulkan sources to the shared TrinityAL source list.
vcpkg_replace_string(
  "${SOURCE_PATH}/trinityal/CMakeLists.txt"
  "    stub/upscaling/Tr2UpscalingALStub.cpp\n    stub/upscaling/Tr2UpscalingALStub.h\n)"
  "    stub/upscaling/Tr2UpscalingALStub.cpp\n    stub/upscaling/Tr2UpscalingALStub.h\n    vulkan/Tr2BufferALVulkan.cpp\n    vulkan/Tr2BufferALVulkan.h\n    vulkan/Tr2CapsALVulkan.cpp\n    vulkan/Tr2CapsALVulkan.h\n    vulkan/Tr2ConstantBufferALVulkan.cpp\n    vulkan/Tr2ConstantBufferALVulkan.h\n    vulkan/Tr2FenceALVulkan.cpp\n    vulkan/Tr2FenceALVulkan.h\n    vulkan/Tr2GpuTimerALVulkan.cpp\n    vulkan/Tr2GpuTimerALVulkan.h\n    vulkan/Tr2OcclusionQueryALVulkan.cpp\n    vulkan/Tr2OcclusionQueryALVulkan.h\n    vulkan/Tr2PipelineStatsQueryALVulkan.cpp\n    vulkan/Tr2PipelineStatsQueryALVulkan.h\n    vulkan/Tr2PrimaryRenderContextVulkan.h\n    vulkan/Tr2RenderContextVulkan.cpp\n    vulkan/Tr2RenderContextVulkan.h\n    vulkan/Tr2RenderGraphALVulkan.cpp\n    vulkan/Tr2RenderGraphALVulkan.h\n    vulkan/Tr2ResourceSetALVulkan.cpp\n    vulkan/Tr2ResourceSetALVulkan.h\n    vulkan/Tr2SamplerStateALVulkan.cpp\n    vulkan/Tr2SamplerStateALVulkan.h\n    vulkan/Tr2ShaderALVulkan.cpp\n    vulkan/Tr2ShaderALVulkan.h\n    vulkan/Tr2ShaderProgramALVulkan.cpp\n    vulkan/Tr2ShaderProgramALVulkan.h\n    vulkan/Tr2SwapChainALVulkan.cpp\n    vulkan/Tr2SwapChainALVulkan.h\n    vulkan/Tr2TextureALVulkan.cpp\n    vulkan/Tr2TextureALVulkan.h\n    vulkan/Tr2VertexLayoutALVulkan.cpp\n    vulkan/Tr2VertexLayoutALVulkan.h\n    vulkan/Tr2VideoAdapterInfoALVulkan.cpp\n    vulkan/Tr2VideoAdapterInfoALVulkan.h\n    vulkan/Tr2VulkanContext.cpp\n    vulkan/Tr2VulkanContext.h\n    vulkan/VmaImplementation.cpp\n    vulkan/upscaling/Tr2UpscalingALVulkan.h\n)"
)

# Add the TrinityAL_vulkan target after the dx12 target.
vcpkg_replace_string(
  "${SOURCE_PATH}/trinityal/CMakeLists.txt"
  "        set_shared_trinityal_properties(dx12)\n    endif()"
  "        set_shared_trinityal_properties(dx12)\n    endif()\n\n    if (BUILD_VULKAN)\n\n        find_package(VulkanHeaders CONFIG REQUIRED)\n        find_package(VulkanMemoryAllocator CONFIG REQUIRED)\n\n        ccp_add_library(TrinityAL_vulkan STATIC)\n        target_compile_definitions(TrinityAL_vulkan PUBLIC TRINITY_PLATFORM=TRINITY_VULKAN)\n        target_link_libraries(TrinityAL_vulkan PUBLIC Vulkan::VulkanHeaders GPUOpen::VulkanMemoryAllocator)\n        set_shared_trinityal_properties(vulkan)\n    endif()"
)

# Add the BUILD_VULKAN option to the top-level CMakeLists.
vcpkg_replace_string(
  "${SOURCE_PATH}/CMakeLists.txt"
  "        option(BUILD_DX12 \"Build dx12.\" OFF)"
  "        option(BUILD_DX12 \"Build dx12.\" OFF)\n        option(BUILD_VULKAN \"Build vulkan.\" OFF)"
)

# Add the trinity_vulkan shared target after the dx12 target.
vcpkg_replace_string(
  "${SOURCE_PATH}/trinity/CMakeLists.txt"
  "        set_shared_trinity_properties(${TRINITY_DX12_TARGET_NAME} carbon-trinity-dx12)\n    endif()"
  "        set_shared_trinity_properties(${TRINITY_DX12_TARGET_NAME} carbon-trinity-dx12)\n    endif()\n\n    if (BUILD_VULKAN)\n\n        set(TRINITY_VULKAN_TARGET_NAME trinity_vulkan)\n\n        ccp_add_library(${TRINITY_VULKAN_TARGET_NAME} SHARED)\n        target_compile_definitions(${TRINITY_VULKAN_TARGET_NAME}\n            PRIVATE\n                TRINITY_PLATFORM=TRINITY_VULKAN\n                TRINITYNAME=_trinity_vulkan\n        )\n        target_link_libraries(${TRINITY_VULKAN_TARGET_NAME} PRIVATE TrinityAL_vulkan)\n        set_shared_trinity_properties(${TRINITY_VULKAN_TARGET_NAME} carbon-trinity-vulkan)\n    endif()"
)

vcpkg_replace_string(
  "${SOURCE_PATH}/trinity/CMakeLists.txt"
  "RUNTIME DESTINATION bin"
  "ARCHIVE DESTINATION lib\nRUNTIME DESTINATION bin"
)

vcpkg_cmake_configure(
  SOURCE_PATH ${SOURCE_PATH}
  OPTIONS
  ${FEATURE_OPTIONS}
  -DBUILD_TESTING=OFF
  -DVCPKG_USE_HOST_TOOLS=ON
  -DVCPKG_HOST_TRIPLET=${HOST_TRIPLET}
  -DCMAKE_BUILD_TYPE=${CARBON_BUILD_TYPE}
)

vcpkg_cmake_install()

vcpkg_cmake_config_fixup()
vcpkg_install_copyright(
  FILE_LIST
    "${SOURCE_PATH}/LICENSE.md"
    "${SOURCE_PATH}/NOTICE.md"
)
vcpkg_copy_pdbs()
ccp_externalize_apple_debuginfo()
