[CmdletBinding()]
param(
    [string]$OutputPath = "artifacts\benchmarks\stage4-fuzz.jsonl",
    [int]$Runs = 100000,
    [int]$MaxLen = 1024,
    [int]$TimeoutSeconds = 5
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path.TrimEnd("\")
$outputFile = if ([IO.Path]::IsPathRooted($OutputPath)) {
    [IO.Path]::GetFullPath($OutputPath)
} else {
    [IO.Path]::GetFullPath((Join-Path $repoRoot $OutputPath))
}
$outputDirectory = Split-Path -Parent $outputFile
$clangCl = Join-Path $repoRoot "tools\llvm\bin\clang-cl.exe"
$fuzzerLib = Join-Path $repoRoot `
    "tools\llvm\lib\clang\18\lib\windows\clang_rt.fuzzer-x86_64.lib"
$vcvars = "C:\Program Files (x86)\Microsoft Visual Studio\2022\" +
    "BuildTools\VC\Auxiliary\Build\vcvars64.bat"

foreach ($requiredPath in @($clangCl, $fuzzerLib, $vcvars)) {
    if (-not (Test-Path -LiteralPath $requiredPath)) {
        throw "Stage 4 fuzz input was not found: $requiredPath"
    }
}
if (-not (Test-Path -LiteralPath $outputDirectory -PathType Container)) {
    $parentDirectory = Split-Path -Parent $outputDirectory
    if (-not (Test-Path -LiteralPath $parentDirectory -PathType Container)) {
        throw "Output parent directory was not found: $parentDirectory"
    }
    New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
}

$tempRoot = [IO.Path]::GetTempPath()
$runDirectory = Join-Path $tempRoot "ithax-stage4-fuzz-$PID"
if (Test-Path -LiteralPath $runDirectory) {
    throw "Stage 4 fuzz run directory already exists."
}
New-Item -ItemType Directory -Path $runDirectory | Out-Null

try {
    $fuzzExe = Join-Path $runDirectory "stage4-fuzz.exe"
    $sourcePaths = @(
        (Join-Path $repoRoot "src\stage4_fuzz_target.cpp"),
        (Join-Path $repoRoot "src\network\marshal\marshal.cpp"),
        (Join-Path $repoRoot "src\network\marshal\string_table.cpp"),
        (Join-Path $repoRoot "src\network\packet.cpp")
    )
    $sources = ($sourcePaths | ForEach-Object {
        '"' + $_ + '"'
    }) -join " "
    $compileCommand = "`"$vcvars`" >nul 2>&1 && `"$clangCl`" /nologo " +
        "/std:c++20 /EHsc -fsanitize=fuzzer " +
        "/D_ALLOW_COMPILER_AND_STL_VERSION_MISMATCH " +
        "/I`"$repoRoot\src`" " +
        "/Fe:`"$fuzzExe`" $sources `"$fuzzerLib`" ws2_32.lib"
    $compileOutput = cmd /c $compileCommand 2>&1
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $fuzzExe)) {
        throw "Stage 4 fuzzer build failed: $($compileOutput -join '; ')"
    }

    $corpus = Join-Path $runDirectory "corpus"
    New-Item -ItemType Directory -Path $corpus | Out-Null
    $vectors = Get-Content `
        (Join-Path $repoRoot "tests\fixtures\stage4_golden_vectors.json") `
        -Raw | ConvertFrom-Json
    $seedIndex = 0
    foreach ($vector in $vectors) {
        $hex = [string]$vector.hex
        $bytes = [byte[]]::new($hex.Length / 2)
        for ($i = 0; $i -lt $bytes.Length; $i++) {
            $bytes[$i] = [Convert]::ToByte($hex.Substring($i * 2, 2), 16)
        }
        [IO.File]::WriteAllBytes(
            (Join-Path $corpus "seed-$seedIndex.bin"), $bytes)
        $seedIndex++
    }

    $startedAt = Get-Date
    $fuzzCommand = "`"$fuzzExe`" -runs=$Runs -max_len=$MaxLen " +
        "-timeout=$TimeoutSeconds `"$corpus`" 2>&1"
    $fuzzOutput = @(cmd /c $fuzzCommand)
    $exitCode = $LASTEXITCODE
    $elapsed = ((Get-Date) - $startedAt).TotalSeconds
    $summaryLine = [string]($fuzzOutput | Select-Object -Last 1)
    $crashes = @($fuzzOutput | Where-Object {
            $_ -match "ERROR: libFuzzer|SUMMARY: libFuzzer"
        }).Count
    $record = [ordered]@{
        event = "stage4_fuzz_run"
        status = if ($exitCode -eq 0 -and $crashes -eq 0) {
            "pass"
        } else {
            "fail"
        }
        gate_status = if ($exitCode -eq 0 -and $crashes -eq 0) {
            "pass"
        } else {
            "open"
        }
        runs = $Runs
        max_len = $MaxLen
        elapsed_seconds = [Math]::Round($elapsed, 3)
        crashes = $crashes
        summary = $summaryLine
        compiler = "clang-cl 18.1.8 (libFuzzer)"
        seed_vectors = $seedIndex
    }
    $record | ConvertTo-Json -Compress |
        Set-Content -LiteralPath $outputFile -Encoding UTF8
    Write-Output ("Stage 4 fuzz lane recorded: {0} runs, {1} crashes" -f `
        $Runs, $crashes)
    if ($exitCode -ne 0 -or $crashes -gt 0) {
        throw "Stage 4 fuzz lane failed."
    }
} finally {
    if (Test-Path -LiteralPath $runDirectory) {
        Remove-Item -LiteralPath $runDirectory -Recurse -Force `
            -ErrorAction SilentlyContinue
    }
}
