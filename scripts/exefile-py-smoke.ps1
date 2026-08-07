[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$ExefilePath,
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$Prefix
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$maxOutputBytes = 1MB
$sentinel = "ITHAX_EXEFILE_PY_OK"
$runDirectory = $null
$exitCode = 1
$failureMessage = $null

function Assert-FilePath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path,
        [Parameter(Mandatory = $true)]
        [string]$Description
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "$Description was not found: $Path"
    }
}

function Quote-ProcessArgument {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Value
    )

    return '"' + $Value.Replace('"', '\"') + '"'
}

function Read-BoundedText {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    $file = Get-Item -LiteralPath $Path
    if ($file.Length -gt $maxOutputBytes) {
        throw "Exefile output exceeded the bounded test limit."
    }
    return [System.IO.File]::ReadAllText($Path)
}

try {
    Assert-FilePath $ExefilePath "Exefile tool"

    $binDirectory = Join-Path $Prefix "bin"
    $libDirectory = Join-Path $Prefix "lib"
    $pythonDirectory = Join-Path $Prefix "tools\python3"
    $pythonStdlib = Join-Path $pythonDirectory "Lib"
    $pythonDllDirectory = Join-Path $pythonDirectory "DLLs"
    foreach ($directory in @(
            $binDirectory,
            $libDirectory,
            $pythonStdlib,
            $pythonDllDirectory
        )) {
        if (-not (Test-Path -LiteralPath $directory -PathType Container)) {
            throw "Required Exefile runtime directory was not found: $directory"
        }
    }

    $tempDirectory = [System.IO.Path]::GetTempPath()
    if (-not (Test-Path -LiteralPath $tempDirectory -PathType Container)) {
        throw "The system temporary directory is unavailable."
    }
    $runDirectory = Join-Path $tempDirectory "ithax-exefile-py-$PID"
    if (Test-Path -LiteralPath $runDirectory) {
        throw "The Exefile smoke directory already exists."
    }
    New-Item -ItemType Directory -Path $runDirectory | Out-Null

    $stdoutPath = Join-Path $runDirectory "stdout.txt"
    $stderrPath = Join-Path $runDirectory "stderr.txt"
    $pathEntries = @(
        (Split-Path -Parent $ExefilePath),
        $binDirectory,
        $libDirectory,
        $pythonDirectory,
        $pythonDllDirectory,
        $env:PATH
    )
    $env:PATH = $pathEntries -join [System.IO.Path]::PathSeparator

    $libSearchPath = "$pythonStdlib;$libDirectory"
    $pythonCode = "print('$sentinel')"
    $argumentList = @(
        "/inherit",
        "/nocrashreportupload",
        "/buildflavor=debug",
        "/root=$(Quote-ProcessArgument $runDirectory)",
        "/bin=$(Quote-ProcessArgument $binDirectory)",
        "/lib=$(Quote-ProcessArgument $libSearchPath)",
        "/py",
        "-c",
        (Quote-ProcessArgument $pythonCode)
    )
    $argumentText = $argumentList -join " "

    $process = Start-Process -FilePath $ExefilePath `
        -ArgumentList $argumentText `
        -WorkingDirectory $runDirectory `
        -RedirectStandardOutput $stdoutPath `
        -RedirectStandardError $stderrPath `
        -Wait -PassThru
    $childExitCode = $process.ExitCode
    $stdout = Read-BoundedText $stdoutPath
    $stderr = Read-BoundedText $stderrPath
    $sentinelFound = ($stdout -split "`r?`n") -contains $sentinel

    if ($childExitCode -ne 0) {
        throw "Exefile /py returned exit code $childExitCode."
    }
    if (-not $sentinelFound) {
        throw "Exefile /py output did not contain the expected sentinel."
    }

    [ordered]@{
        event = "exefile_py_smoke"
        status = "pass"
        exit_code = $childExitCode
        sentinel = $sentinelFound
        stdout_bytes = [System.Text.Encoding]::UTF8.GetByteCount($stdout)
        stderr_bytes = [System.Text.Encoding]::UTF8.GetByteCount($stderr)
    } | ConvertTo-Json -Compress
    $exitCode = 0
}
catch {
    $failureMessage = $_.Exception.Message
    [ordered]@{
        event = "exefile_py_smoke"
        status = "fail"
        error = $failureMessage
    } | ConvertTo-Json -Compress
}
finally {
    if ($null -ne $runDirectory -and
        (Test-Path -LiteralPath $runDirectory -PathType Container)) {
        Remove-Item -LiteralPath $runDirectory -Recurse -Force
    }
}

exit $exitCode
