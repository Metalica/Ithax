[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$ClientPath,
    [Parameter(Mandatory)][string]$PythonPath,
    [string]$MockServerPath = "tests\stage4_mock_server.py",
    [int]$Port = 26001,
    [int]$StartupTimeoutSeconds = 15
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path.TrimEnd("\")
$clientExe = if ([IO.Path]::IsPathRooted($ClientPath)) {
    [IO.Path]::GetFullPath($ClientPath)
} else {
    [IO.Path]::GetFullPath((Join-Path $repoRoot $ClientPath))
}
$pythonExe = if ([IO.Path]::IsPathRooted($PythonPath)) {
    [IO.Path]::GetFullPath($PythonPath)
} else {
    [IO.Path]::GetFullPath((Join-Path $repoRoot $PythonPath))
}
$mockScript = if ([IO.Path]::IsPathRooted($MockServerPath)) {
    [IO.Path]::GetFullPath($MockServerPath)
} else {
    [IO.Path]::GetFullPath((Join-Path $repoRoot $MockServerPath))
}

foreach ($requiredPath in @($clientExe, $pythonExe, $mockScript)) {
    if (-not (Test-Path -LiteralPath $requiredPath -PathType Leaf)) {
        throw "Stage 4 network integration input was not found: $requiredPath"
    }
}

$tempRoot = [IO.Path]::GetTempPath()
$runDirectory = Join-Path $tempRoot "ithax-stage4-net-$PID"
if (Test-Path -LiteralPath $runDirectory) {
    throw "Stage 4 network run directory already exists."
}
New-Item -ItemType Directory -Path $runDirectory | Out-Null

$mockOut = Join-Path $runDirectory "mock.out"
$mockErr = Join-Path $runDirectory "mock.err"
$clientOut = Join-Path $runDirectory "client.out"
$clientErr = Join-Path $runDirectory "client.err"

$mockProcess = $null
try {
    $mockArgs = '"' + $mockScript + '" --port ' + $Port
    $mockProcess = Start-Process -FilePath $pythonExe `
        -ArgumentList $mockArgs `
        -WorkingDirectory $runDirectory `
        -RedirectStandardOutput $mockOut `
        -RedirectStandardError $mockErr `
        -PassThru -NoNewWindow
    $null = $mockProcess.Handle

    $ready = $false
    $deadline = (Get-Date).AddSeconds($StartupTimeoutSeconds)
    while (-not $ready -and (Get-Date) -lt $deadline) {
        if ($mockProcess.HasExited) {
            throw "Stage 4 mock server exited before becoming ready."
        }
        if (Test-Path -LiteralPath $mockOut) {
            $lines = Get-Content -LiteralPath $mockOut -ErrorAction SilentlyContinue
            if (@($lines | Where-Object {
                    $_ -match '"event":\s*"stage4_mock_ready"'
                }).Count -gt 0) {
                $ready = $true
            }
        }
        if (-not $ready) {
            Start-Sleep -Milliseconds 200
        }
    }
    if (-not $ready) {
        throw "Stage 4 mock server did not become ready in time."
    }

    $clientProcess = Start-Process -FilePath $clientExe `
        -ArgumentList @("127.0.0.1", "$Port") `
        -WorkingDirectory $repoRoot `
        -RedirectStandardOutput $clientOut `
        -RedirectStandardError $clientErr `
        -Wait -PassThru -NoNewWindow

    if ($clientProcess.ExitCode -ne 0) {
        $clientText = if (Test-Path -LiteralPath $clientOut) {
            (Get-Content -LiteralPath $clientOut -Raw)
        } else {
            ""
        }
        throw "Stage 4 network integration failed with exit code " +
            "$($clientProcess.ExitCode): $clientText"
    }
    if (Test-Path -LiteralPath $clientOut) {
        foreach ($line in @(Get-Content -LiteralPath $clientOut)) {
            if (-not [string]::IsNullOrWhiteSpace($line)) {
                Write-Output $line
            }
        }
    }
    Write-Output "Stage 4 network integration passed."
} finally {
    if ($null -ne $mockProcess -and -not $mockProcess.HasExited) {
        Stop-Process -Id $mockProcess.Id -Force -ErrorAction SilentlyContinue
        $mockProcess.WaitForExit()
    }
    if (Test-Path -LiteralPath $runDirectory) {
        Remove-Item -LiteralPath $runDirectory -Recurse -Force `
            -ErrorAction SilentlyContinue
    }
}
