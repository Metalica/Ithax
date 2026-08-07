[CmdletBinding()]
param(
    [string]$InstallRoot = "vcpkg_installed-clean",
    [string]$BuildRoot = "build-stage2",
    [string]$OutputPath = "artifacts\benchmarks\stage4-real-server.jsonl",
    [string]$ServerRoot = "",
    [string]$NodePath = "",
    [int]$Port = 26000,
    [int]$StartupTimeoutSeconds = 90
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path.TrimEnd("\")

function Convert-ToAbsolutePath {
    param([Parameter(Mandatory)][string]$Path)

    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $repoRoot $Path))
}

$installPath = Convert-ToAbsolutePath $InstallRoot
$buildPath = Convert-ToAbsolutePath $BuildRoot
$outputFile = Convert-ToAbsolutePath $OutputPath
$outputDirectory = Split-Path -Parent $outputFile

if ([string]::IsNullOrWhiteSpace($ServerRoot)) {
    $ServerRoot = "C:\Users\Metal\Desktop\12.4.1\server"
}
$serverRoot = [IO.Path]::GetFullPath($ServerRoot)
if ([string]::IsNullOrWhiteSpace($NodePath)) {
    $NodePath = (Get-Command node -ErrorAction SilentlyContinue).Source
}
if ([string]::IsNullOrWhiteSpace($NodePath)) {
    throw "Node.js was not found for the approved server."
}

$clientExe = Join-Path $buildPath "Debug\ithax-stage4-real-server-conformance.exe"
$serverIndex = Join-Path $serverRoot "index.js"
foreach ($requiredPath in @($clientExe, $serverIndex, $NodePath)) {
    if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
        throw "Stage 4 real-server input was not found: $requiredPath"
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
$runDirectory = Join-Path $tempRoot "ithax-stage4-real-$PID"
if (Test-Path -LiteralPath $runDirectory) {
    throw "Stage 4 real-server run directory already exists."
}
New-Item -ItemType Directory -Path $runDirectory | Out-Null

$serverOut = Join-Path $runDirectory "server.out"
$serverErr = Join-Path $runDirectory "server.err"
$clientOut = Join-Path $runDirectory "client.out"
$clientErr = Join-Path $runDirectory "client.err"

$serverProcess = $null
try {
    $env:EVEJS_SERVER_PORT = "$Port"
    $serverProcess = Start-Process -FilePath $NodePath `
        -ArgumentList ('"' + $serverIndex + '"') `
        -WorkingDirectory $serverRoot `
        -RedirectStandardOutput $serverOut `
        -RedirectStandardError $serverErr `
        -PassThru -NoNewWindow
    $null = $serverProcess.Handle

    $ready = $false
    $deadline = (Get-Date).AddSeconds($StartupTimeoutSeconds)
    while (-not $ready -and (Get-Date) -lt $deadline) {
        if ($serverProcess.HasExited) {
            throw "Approved server exited before becoming ready."
        }
        if (Test-Path -LiteralPath $serverOut) {
            $lines = Get-Content -LiteralPath $serverOut -ErrorAction SilentlyContinue
            if (@($lines | Where-Object {
                    $_ -match "EveJS Elysian is running"
                }).Count -gt 0) {
                $ready = $true
            }
        }
        if (-not $ready) {
            Start-Sleep -Milliseconds 500
        }
    }
    if (-not $ready) {
        throw "Approved server did not become ready in time."
    }

    $clientProcess = Start-Process -FilePath $clientExe `
        -ArgumentList @("127.0.0.1", "$Port") `
        -WorkingDirectory $repoRoot `
        -RedirectStandardOutput $clientOut `
        -RedirectStandardError $clientErr `
        -Wait -PassThru -NoNewWindow

    $clientText = if (Test-Path -LiteralPath $clientOut) {
        (Get-Content -LiteralPath $clientOut -Raw)
    } else {
        ""
    }
    if ($clientProcess.ExitCode -ne 0) {
        throw "Stage 4 real-server conformance failed: $clientText"
    }

    $records = [System.Collections.Generic.List[string]]::new()
    foreach ($line in @($clientText -split "`r?`n")) {
        $json = $line.Trim()
        if ($json.StartsWith("{")) {
            $records.Add($json)
        }
    }
    $summary = [ordered]@{
        event = "stage4_real_server_run"
        status = "pass"
        gate_status = "pass"
        measurement_class = "real-provider"
        provider = "approved server-side reference (AGPL, separate)"
        server_root = $serverRoot
        port = $Port
        records = $records.Count
    }
    $lines = [System.Collections.Generic.List[string]]::new()
    $lines.Add(($summary | ConvertTo-Json -Compress))
    foreach ($record in $records) {
        $lines.Add($record)
    }
    $lines | Set-Content -LiteralPath $outputFile -Encoding UTF8
    Write-Output ("Stage 4 real-server conformance recorded: {0} records" -f `
        $records.Count)
} finally {
    if ($null -ne $serverProcess -and -not $serverProcess.HasExited) {
        Stop-Process -Id $serverProcess.Id -Force -ErrorAction SilentlyContinue
        $serverProcess.WaitForExit()
    }
    Remove-Item Env:EVEJS_SERVER_PORT -ErrorAction SilentlyContinue
    if (Test-Path -LiteralPath $runDirectory) {
        Remove-Item -LiteralPath $runDirectory -Recurse -Force `
            -ErrorAction SilentlyContinue
    }
}
