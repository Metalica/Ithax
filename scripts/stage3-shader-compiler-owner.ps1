[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$ShaderCompilerPath,
    [string]$FixturePath = "tests\fixtures\stage3_owner.fx",
    [string]$OutputPath =
        "artifacts\benchmarks\shader-compiler-owner.jsonl",
    [object[]]$Workers = @(1, 2, 4),
    [switch]$FailOnUnobserved
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path.TrimEnd("\")
$shaderPath = if ([IO.Path]::IsPathRooted($ShaderCompilerPath)) {
    [IO.Path]::GetFullPath($ShaderCompilerPath)
} else {
    [IO.Path]::GetFullPath((Join-Path $repoRoot $ShaderCompilerPath))
}
$fixture = if ([IO.Path]::IsPathRooted($FixturePath)) {
    [IO.Path]::GetFullPath($FixturePath)
} else {
    [IO.Path]::GetFullPath((Join-Path $repoRoot $FixturePath))
}
$outputFile = if ([IO.Path]::IsPathRooted($OutputPath)) {
    [IO.Path]::GetFullPath($OutputPath)
} else {
    [IO.Path]::GetFullPath((Join-Path $repoRoot $OutputPath))
}
$outputDirectory = Split-Path -Parent $outputFile
$maxOutputBytes = 1MB
$maxRunSeconds = 30

function Quote-ProcessArgument {
    param([Parameter(Mandatory)][string]$Value)

    return '"' + $Value.Replace('"', '\"') + '"'
}

function Read-BoundedText {
    param([Parameter(Mandatory)][string]$Path)

    $file = Get-Item -LiteralPath $Path
    if ($file.Length -gt $maxOutputBytes) {
        throw "Shader compiler output exceeded its bound."
    }
    return [IO.File]::ReadAllText($Path)
}

function Get-ChildThreadCount {
    param([Parameter(Mandatory)][int]$ProcessId)

    try {
        return (Get-Process -Id $ProcessId -ErrorAction Stop).Threads.Count
    } catch {
        $ignoredError = $_.Exception.Message
        return 0
    }
}

if (-not (Test-Path -LiteralPath $shaderPath -PathType Leaf)) {
    throw "Shader compiler was not found: $shaderPath"
}
if (-not (Test-Path -LiteralPath $fixture -PathType Leaf)) {
    throw "Shader compiler fixture was not found: $fixture"
}
if (-not (Test-Path -LiteralPath $outputDirectory -PathType Container)) {
    $parentDirectory = Split-Path -Parent $outputDirectory
    if (-not (Test-Path -LiteralPath $parentDirectory -PathType Container)) {
        throw "Output parent directory was not found: $parentDirectory"
    }
    New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
}

$workerValues = @($Workers | ForEach-Object {
    [string]$_ -split "," | ForEach-Object { $_.Trim() }
} | Where-Object { $_ -ne "" } | ForEach-Object {
    [int]$_
} | Sort-Object -Unique)
if ($workerValues.Count -eq 0) {
    throw "At least one shader compiler worker count is required."
}
foreach ($worker in $workerValues) {
    if ($worker -lt 1 -or $worker -gt 16) {
        throw "Shader compiler workers must be between 1 and 16."
    }
}

$runRoot = Join-Path ([IO.Path]::GetTempPath()) "ithax-shader-owner-$PID"
if (Test-Path -LiteralPath $runRoot) {
    throw "Shader compiler run directory already exists."
}
New-Item -ItemType Directory -Path $runRoot | Out-Null
$records = [System.Collections.Generic.List[string]]::new()
$allObserved = $true
try {
    foreach ($worker in $workerValues) {
        $workerRoot = Join-Path $runRoot "workers-$worker"
        New-Item -ItemType Directory -Path $workerRoot | Out-Null
        $output = Join-Path $workerRoot "stage3_owner.bin"
        $stdout = Join-Path $workerRoot "stdout.txt"
        $stderr = Join-Path $workerRoot "stderr.txt"
        $arguments = @(
            "/no_warnings",
            "/threads",
            $worker,
            (Quote-ProcessArgument $fixture),
            (Quote-ProcessArgument $output)
        ) -join " "
        $process = Start-Process -FilePath $shaderPath `
            -ArgumentList $arguments `
            -WorkingDirectory (Split-Path -Parent $shaderPath) `
            -RedirectStandardOutput $stdout `
            -RedirectStandardError $stderr `
            -PassThru
        $null = $process.Handle
        $peakThreads = 0
        $deadline = (Get-Date).AddSeconds($maxRunSeconds)
        while (-not $process.HasExited) {
            $observed = Get-ChildThreadCount $process.Id
            $peakThreads = [Math]::Max($peakThreads, $observed)
            if ((Get-Date) -ge $deadline) {
                $process.Kill()
                $process.WaitForExit()
                throw "Shader compiler exceeded its bounded runtime."
            }
            Start-Sleep -Milliseconds 5
        }
        $process.WaitForExit()
        $process.Refresh()
        Read-BoundedText $stdout | Out-Null
        Read-BoundedText $stderr | Out-Null
        if ($process.ExitCode -ne 0) {
            throw "Shader compiler failed for worker count $worker."
        }
        if (-not (Test-Path -LiteralPath $output -PathType Leaf)) {
            throw "Shader compiler did not produce an output artifact."
        }
        $outputBytes = (Get-Item -LiteralPath $output).Length
        if ($outputBytes -eq 0) {
            throw "Shader compiler produced an empty output artifact."
        }
        $observed = $peakThreads -gt 0
        $allObserved = $allObserved -and $observed
        $record = [ordered]@{
            event = "shader_compiler_owner_summary"
            status = if ($observed) { "pass" } else { "unobserved" }
            gate_status = if ($observed) { "pass" } else { "open" }
            measurement_class = "real-provider"
            provider = "Carbon ShaderCompiler"
            configured_workers = $worker
            peak_observed_threads = $peakThreads
            output_bytes = $outputBytes
            workload = "bounded-stage3-fixture"
        }
        $records.Add(($record | ConvertTo-Json -Compress))
    }
} finally {
    if (Test-Path -LiteralPath $runRoot -PathType Container) {
        Remove-Item -LiteralPath $runRoot -Recurse -Force
    }
}

$metadata = [ordered]@{
    event = "stage3_shader_compiler_owner_run"
    status = if ($allObserved) { "pass" } else { "open" }
    gate_status = if ($allObserved) { "pass" } else { "open" }
    measurement_class = "real-provider"
    provider = "Carbon ShaderCompiler"
    worker_counts = $workerValues
    records = $records.Count
}
$lines = [System.Collections.Generic.List[string]]::new()
$lines.Add(($metadata | ConvertTo-Json -Compress))
foreach ($record in $records) {
    $lines.Add($record)
}
$lines | Set-Content -LiteralPath $outputFile -Encoding UTF8
Write-Output ("Wrote {0} shader compiler records to {1}" -f `
    $lines.Count, $outputFile)
if ($FailOnUnobserved -and -not $allObserved) {
    throw "Shader compiler completed without an observable worker sample."
}
