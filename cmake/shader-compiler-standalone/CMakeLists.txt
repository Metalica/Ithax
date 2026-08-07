cmake_minimum_required(VERSION 3.30)
project(ithax-shadercompiler-standalone LANGUAGES CXX)

get_filename_component(REPO_ROOT
    "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(SC_ROOT "${REPO_ROOT}/external/trinity/shadercompiler")
set(DXC_ROOT "${REPO_ROOT}/external/dxc")
set(SC_GEN "${SC_ROOT}/GeneratedSources")

set(ShaderCompiler_VERSION_MAJOR 1)
set(ShaderCompiler_VERSION_MINOR 2)
set(ShaderCompiler_VERSION_PATCH 6)
set(ShaderCompiler_VERSION_TWEAK 0)

file(MAKE_DIRECTORY "${SC_GEN}")
configure_file(
    "${SC_ROOT}/ShaderCompilerConfig.h.in"
    "${SC_GEN}/ShaderCompilerConfig.h"
    @ONLY
)

set(SC_SOURCES    "${SC_ROOT}/ASTNode.cpp"
    "${SC_ROOT}/CachingIncludeHandler.cpp"
    "${SC_ROOT}/CompileMessageQueue.cpp"
    "${SC_ROOT}/DxReflection.cpp"
    "${SC_ROOT}/EffectCompilerDX11.cpp"
    "${SC_ROOT}/EffectCompilerDX12.cpp"
    "${SC_ROOT}/EffectCompilerMetal.cpp"
    "${SC_ROOT}/EffectData.cpp"
    "${SC_ROOT}/FXAnalyzer.cpp"
    "${SC_ROOT}/md5.cpp"
    "${SC_ROOT}/ModifiedTime.cpp"
    "${SC_ROOT}/OutputHLSL.cpp"
    "${SC_ROOT}/ParserState.cpp"
    "${SC_ROOT}/ParserUtils.cpp"
    "${SC_ROOT}/ShaderCompiler.cpp"
    "${SC_ROOT}/stdafx.cpp"
    "${SC_ROOT}/StringTable.cpp"
    "${SC_ROOT}/SymbolTable.cpp"
    "${SC_ROOT}/TextureFunctionConversionDX11.cpp"
    "${SC_ROOT}/Type.cpp"
    "${SC_GEN}/HLSLParser.c"
    "${SC_GEN}/Preprocessor.c"
    "${SC_GEN}/HLSLScanner.cpp"
)

add_executable(ShaderCompiler ${SC_SOURCES})

target_compile_definitions(ShaderCompiler PRIVATE
    CCP_TELEMETRY_ENABLED=0
    _WIN32_WINNT=0x0A00
)
target_compile_options(ShaderCompiler PRIVATE "/FI${SC_ROOT}/stdafx.h")
target_compile_features(ShaderCompiler PRIVATE cxx_std_17)
target_include_directories(ShaderCompiler PRIVATE
    "${SC_ROOT}"
    "${SC_GEN}"
    "${DXC_ROOT}/inc"
)

set_source_files_properties(
    "${SC_GEN}/HLSLParser.c"
    "${SC_GEN}/Preprocessor.c"
    PROPERTIES LANGUAGE CXX
)

target_link_libraries(ShaderCompiler PRIVATE
    d3dcompiler
    d3d11
    dxguid
    "${DXC_ROOT}/lib/x64/dxcompiler.lib"
    "${DXC_ROOT}/lib/x64/dxil.lib"
)
