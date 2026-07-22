# EVE Client Clang/LLVM MinGW toolchain
# Portable — no Visual Studio required

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)

# Clang
set(CMAKE_C_COMPILER "C:\Users\Metal\Desktop\eve client\tools/llvm/bin/clang.exe")
set(CMAKE_CXX_COMPILER "C:\Users\Metal\Desktop\eve client\tools/llvm/bin/clang++.exe")

# LLD linker
set(CMAKE_LINKER "C:\Users\Metal\Desktop\eve client\tools/llvm/bin/ld.lld.exe")
set(CMAKE_C_COMPILER_WORKS 1)
set(CMAKE_CXX_COMPILER_WORKS 1)

# Target triple
set(CMAKE_C_FLAGS_INIT "-target x86_64-w64-windows-gnu")
set(CMAKE_CXX_FLAGS_INIT "-target x86_64-w64-windows-gnu")

# Use Ninja
set(CMAKE_MAKE_PROGRAM "C:\Users\Metal\Desktop\eve client\tools/ninja/ninja.exe")

# Find Python (for Blue module)
set(CMAKE_PYTHON_EXECUTABLE "C:\Users\Metal\Desktop\eve client\tools/python/python.exe")

# Find Git
set(CMAKE_GIT_EXECUTABLE "C:\Users\Metal\Desktop\eve client\tools/git/cmd/git.exe")

# Skip compiler sanity checks (we verified manually)
set(CMAKE_C_COMPILER_FORCED TRUE)
set(CMAKE_CXX_COMPILER_FORCED TRUE)