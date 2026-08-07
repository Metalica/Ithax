# EVE Client Clang/LLVM MinGW toolchain
# Portable — no Visual Studio required

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)

get_filename_component(ITHAX_REPO_ROOT
  "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(ITHAX_TOOL_ROOT "${ITHAX_REPO_ROOT}/tools" CACHE PATH
  "Root directory containing the portable build tools")

# Clang
set(CMAKE_C_COMPILER "${ITHAX_TOOL_ROOT}/llvm/bin/clang.exe")
set(CMAKE_CXX_COMPILER "${ITHAX_TOOL_ROOT}/llvm/bin/clang++.exe")

# LLD linker
set(CMAKE_LINKER "${ITHAX_TOOL_ROOT}/llvm/bin/ld.lld.exe")

# Target triple
set(CMAKE_C_FLAGS_INIT "-target x86_64-w64-windows-gnu")
set(CMAKE_CXX_FLAGS_INIT "-target x86_64-w64-windows-gnu")

# Use Ninja
set(CMAKE_MAKE_PROGRAM "${ITHAX_TOOL_ROOT}/ninja/ninja.exe")

# Find Python (for Blue module)
set(CMAKE_PYTHON_EXECUTABLE "${ITHAX_TOOL_ROOT}/python/python.exe")

# Find Git
set(CMAKE_GIT_EXECUTABLE "${ITHAX_TOOL_ROOT}/git/cmd/git.exe")
