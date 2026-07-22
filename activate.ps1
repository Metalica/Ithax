# Activate EVE Client development environment
# Run: . .\activate.ps1  (note the dot-space, this sets env vars in current shell)

$env:EVE_CLIENT_ROOT = Split-Path -Parent $MyInvocation.MyCommand.Path
$tools = Join-Path $env:EVE_CLIENT_ROOT "tools"

# Tool paths
$env:PATH = "$tools\cmake\bin;$tools\python;$tools\node;$tools\git\cmd;$tools\ninja;$tools\vcpkg;$tools\llvm\bin;$env:PATH"

# Clang as default C/C++ compiler for CMake
$env:CC = "$tools\llvm\bin\clang.exe"
$env:CXX = "$tools\llvm\bin\clang++.exe"

# vcpkg
$env:VCPKG_ROOT = "$tools\vcpkg"
$env:VCPKG_DEFAULT_TRIPLET = "x64-windows"
$env:PATH_TO_VCPKG_ROOT = "$tools\vcpkg"

# EVE Online official client path (for asset extraction, protocol RE)
$env:EVE_OFFLINE_CLIENT = "C:\EVE Online - 3396210"

Write-Host "=== EVE Client Dev Environment ==="
Write-Host "Project root: $env:EVE_CLIENT_ROOT"
Write-Host "EVE client:   $env:EVE_OFFLINE_CLIENT"
Write-Host ""
Write-Host "Tools:"
Write-Host "  CMake:   $(cmake --version | Select-Object -First 1)"
Write-Host "  Python:  $(python --version)"
Write-Host "  Node:    $(node --version)"
Write-Host "  Git:     $(git --version)"
Write-Host "  Ninja:   $(ninja --version)"
Write-Host "  Clang:   $(clang --version | Select-Object -First 1)"
