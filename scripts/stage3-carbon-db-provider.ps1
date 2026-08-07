[CmdletBinding()]
param(
    [string]$InstallRoot = "vcpkg_installed-clean",
    [string]$OutputPath = "artifacts\benchmarks\carbon-db-provider.jsonl",
    [string]$Provider = "auto",
    [switch]$FailIfUnavailable
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path.TrimEnd("\")
$installPath = if ([IO.Path]::IsPathRooted($InstallRoot)) {
    [IO.Path]::GetFullPath($InstallRoot)
} else {
    [IO.Path]::GetFullPath((Join-Path $repoRoot $InstallRoot))
}
$outputFile = if ([IO.Path]::IsPathRooted($OutputPath)) {
    [IO.Path]::GetFullPath($OutputPath)
} else {
    [IO.Path]::GetFullPath((Join-Path $repoRoot $OutputPath))
}
$outputDirectory = Split-Path -Parent $outputFile
$maxOutputBytes = 1MB
$maxRunSeconds = 30
$runDirectory = $null

if ($Provider -notin @("auto", "ole-db", "sqlite")) {
    throw "Provider must be one of: auto, ole-db, sqlite."
}

function Quote-ProcessArgument {
    param([Parameter(Mandatory)][string]$Value)

    return '"' + $Value.Replace('"', '\"') + '"'
}

function Read-BoundedText {
    param([Parameter(Mandatory)][string]$Path)

    $file = Get-Item -LiteralPath $Path
    if ($file.Length -gt $maxOutputBytes) {
        throw "Carbon DB provider output exceeded its bound."
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

function Write-ProviderNotConfigured {
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][string]$ProviderName,
        [Parameter(Mandatory)][string]$Reason
    )

    $record = [ordered]@{
        event = "stage3_carbon_db_provider_run"
        status = "not_configured"
        gate_status = "open"
        measurement_class = "real-provider"
        provider = $ProviderName
        connection_configured = $false
        reason = $Reason
    }
    $record | ConvertTo-Json -Compress | Set-Content `
        -LiteralPath $Path -Encoding UTF8
}

function Start-SqliteWorker {
    param(
        [Parameter(Mandatory)][string]$SqliteExe,
        [Parameter(Mandatory)][string]$Database,
        [Parameter(Mandatory)][string]$Sql,
        [Parameter(Mandatory)][string]$WorkingDirectory,
        [Parameter(Mandatory)][string]$StdoutPath,
        [Parameter(Mandatory)][string]$StderrPath
    )

    $arguments = @(
        (Quote-ProcessArgument $Database),
        (Quote-ProcessArgument $Sql)
    ) -join " "
    $process = Start-Process -FilePath $SqliteExe `
        -ArgumentList $arguments `
        -WorkingDirectory $WorkingDirectory `
        -RedirectStandardOutput $StdoutPath `
        -RedirectStandardError $StderrPath `
        -PassThru
    $null = $process.Handle
    return $process
}

function Invoke-SqliteBounded {
    param(
        [Parameter(Mandatory)][string]$SqliteExe,
        [Parameter(Mandatory)][string]$Database,
        [Parameter(Mandatory)][string]$Sql,
        [Parameter(Mandatory)][string]$WorkingDirectory,
        [Parameter(Mandatory)][string]$StdoutPath,
        [Parameter(Mandatory)][string]$StderrPath
    )

    $process = Start-SqliteWorker -SqliteExe $SqliteExe `
        -Database $Database -Sql $Sql -WorkingDirectory $WorkingDirectory `
        -StdoutPath $StdoutPath -StderrPath $StderrPath
    $peakThreads = 0
    $deadline = (Get-Date).AddSeconds($maxRunSeconds)
    while (-not $process.HasExited) {
        $observed = Get-ChildThreadCount $process.Id
        $peakThreads = [Math]::Max($peakThreads, $observed)
        if ((Get-Date) -ge $deadline) {
            $process.Kill()
            $process.WaitForExit()
            throw "SQLite provider worker exceeded its bounded runtime."
        }
        Start-Sleep -Milliseconds 5
    }
    $process.WaitForExit()
    $process.Refresh()
    return @{
        Process = $process
        PeakThreads = $peakThreads
    }
}

function Invoke-SqliteProviderLane {
    param(
        [Parameter(Mandatory)][string]$SqliteExe,
        [Parameter(Mandatory)][string]$PythonExe,
        [Parameter(Mandatory)][string]$OutputFile,
        [Parameter(Mandatory)][string]$RunRoot
    )

    $workerCounts = @(1, 2, 4)
    $rowsPerWorker = 2000
    $records = [System.Collections.Generic.List[string]]::new()
    $allPassed = $true

    $versionText = (& $SqliteExe --version 2>&1 | Select-Object -First 1)
    $versionMatch = [regex]::Match(
        [string]$versionText, "^(\d+\.\d+\.\d+)\s")
    $engineVersion = if ($versionMatch.Success) {
        $versionMatch.Groups[1].Value
    } else {
        "unknown"
    }

    $probeScript = Join-Path $RunRoot "sqlite_engine_probe.py"
    @'
import json
import sqlite3
print(json.dumps({
    "event": "sqlite_engine_probe",
    "provider": "pinned-python-sqlite3",
    "engine_version": sqlite3.sqlite_version,
    "threadsafe": sqlite3.threadsafety,
}))
'@ | Set-Content -LiteralPath $probeScript -Encoding UTF8
    $probeOutput = ""
    try {
        $probeOutput = (& $PythonExe $probeScript 2>&1) -join "`n"
    } catch {
        $probeOutput = ""
    }
    $embeddedVersion = "unavailable"
    foreach ($line in ($probeOutput -split "`r?`n")) {
        $text = [string]$line
        if ($text.Trim().StartsWith("{")) {
            try {
                $probeRecord = $text.Trim() | ConvertFrom-Json
                if ($null -ne $probeRecord.engine_version) {
                    $embeddedVersion = [string]$probeRecord.engine_version
                }
            } catch {
                $embeddedVersion = "unavailable"
            }
            break
        }
    }

    foreach ($workerCount in $workerCounts) {
        $batchRoot = Join-Path $RunRoot "workers-$workerCount"
        New-Item -ItemType Directory -Path $batchRoot | Out-Null
        $database = Join-Path $batchRoot "stage3.sqlite"
        $initSql = "PRAGMA journal_mode=WAL; PRAGMA busy_timeout=5000; " +
            "CREATE TABLE IF NOT EXISTS stage3_rows " +
            "(id INTEGER PRIMARY KEY, worker TEXT NOT NULL, " +
            "payload BLOB NOT NULL);"
        $init = Invoke-SqliteBounded -SqliteExe $SqliteExe -Database $database `
            -Sql $initSql -WorkingDirectory $batchRoot `
            -StdoutPath (Join-Path $batchRoot "init.out") `
            -StderrPath (Join-Path $batchRoot "init.err")
        if ($init.Process.ExitCode -ne 0) {
            throw "SQLite provider database initialization failed."
        }

        $peakBatchThreads = 0
        $workerProcesses = [System.Collections.Generic.List[object]]::new()
        for ($worker = 1; $worker -le $workerCount; $worker++) {
            $workerRoot = Join-Path $batchRoot "worker-$worker"
            New-Item -ItemType Directory -Path $workerRoot | Out-Null
            $workload = "PRAGMA busy_timeout=5000; " +
                "WITH RECURSIVE cnt(x) AS (SELECT 1 UNION ALL " +
                "SELECT x+1 FROM cnt WHERE x < $rowsPerWorker) " +
                "INSERT INTO stage3_rows (worker, payload) " +
                "SELECT 'w$worker', randomblob(256) FROM cnt;"
            $process = Start-SqliteWorker -SqliteExe $SqliteExe `
                -Database $database -Sql $workload `
                -WorkingDirectory $workerRoot `
                -StdoutPath (Join-Path $workerRoot "stdout.txt") `
                -StderrPath (Join-Path $workerRoot "stderr.txt")
            $workerProcesses.Add(@{
                Process = $process
                Root = $workerRoot
            })
        }

        $deadline = (Get-Date).AddSeconds($maxRunSeconds)
        while (@($workerProcesses | Where-Object {
                -not $_.Process.HasExited }).Count -gt 0) {
            foreach ($worker in $workerProcesses) {
                if ($worker.Process.HasExited) {
                    continue
                }
                $observed = Get-ChildThreadCount $worker.Process.Id
                $peakBatchThreads = [Math]::Max($peakBatchThreads, $observed)
            }
            if ((Get-Date) -ge $deadline) {
                foreach ($worker in $workerProcesses) {
                    if (-not $worker.Process.HasExited) {
                        $worker.Process.Kill()
                        $worker.Process.WaitForExit()
                    }
                }
                throw "SQLite provider batch exceeded its bounded runtime."
            }
            Start-Sleep -Milliseconds 5
        }

        $batchOk = $true
        foreach ($worker in $workerProcesses) {
            $worker.Process.Refresh()
            Read-BoundedText (Join-Path $worker.Root "stdout.txt") | Out-Null
            Read-BoundedText (Join-Path $worker.Root "stderr.txt") | Out-Null
            if ($worker.Process.ExitCode -ne 0) {
                $batchOk = $false
            }
        }

        $expected = $workerCount * $rowsPerWorker
        $actual = 0
        if ($batchOk) {
            $countResult = Invoke-SqliteBounded -SqliteExe $SqliteExe `
                -Database $database -Sql "SELECT COUNT(*) FROM stage3_rows;" `
                -WorkingDirectory $batchRoot `
                -StdoutPath (Join-Path $batchRoot "count.out") `
                -StderrPath (Join-Path $batchRoot "count.err")
            if ($countResult.Process.ExitCode -eq 0) {
                $countOutput = Read-BoundedText (Join-Path $batchRoot "count.out")
                $countMatch = [regex]::Match(
                    [string]$countOutput, "^\s*(\d+)")
                if ($countMatch.Success) {
                    $actual = [int]$countMatch.Groups[1].Value
                }
            }
            if ($actual -ne $expected) {
                $batchOk = $false
            }
        }

        $allPassed = $allPassed -and $batchOk
        $record = [ordered]@{
            event = "sqlite_provider_owner_summary"
            status = if ($batchOk) { "pass" } else { "fail" }
            gate_status = if ($batchOk) { "pass" } else { "open" }
            measurement_class = "real-provider"
            provider = "SQLite CLI"
            engine_version = $engineVersion
            configured_workers = $workerCount
            peak_batch_threads = $peakBatchThreads
            rows_written = $actual
            rows_expected = $expected
            workload = "shared-wal-concurrent-inserts"
        }
        $records.Add(($record | ConvertTo-Json -Compress))
    }

    $metadata = [ordered]@{
        event = "stage3_sqlite_provider_run"
        status = if ($allPassed) { "pass" } else { "open" }
        gate_status = if ($allPassed) { "pass" } else { "open" }
        measurement_class = "real-provider"
        provider = "SQLite CLI (in-repo tools/sqlite)"
        engine_version = $engineVersion
        embedded_engine_version = $embeddedVersion
        worker_counts = $workerCounts
        records = $records.Count
    }
    $lines = [System.Collections.Generic.List[string]]::new()
    $lines.Add(($metadata | ConvertTo-Json -Compress))
    foreach ($record in $records) {
        $lines.Add($record)
    }
    $lines | Set-Content -LiteralPath $OutputFile -Encoding UTF8
    Write-Output ("Wrote {0} SQLite provider records to {1}" -f `
        $lines.Count, $OutputFile)
    if ($FailIfUnavailable -and -not $allPassed) {
        throw "SQLite provider lane did not observe a passing worker batch."
    }
}

if (-not (Test-Path -LiteralPath $outputDirectory -PathType Container)) {
    $parentDirectory = Split-Path -Parent $outputDirectory
    if (-not (Test-Path -LiteralPath $parentDirectory -PathType Container)) {
        throw "Output parent directory was not found: $parentDirectory"
    }
    New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
}

$providerChoice = switch ($Provider) {
    "ole-db" { "ole-db" }
    "sqlite" { "sqlite" }
    default {
        if ([string]::IsNullOrWhiteSpace(
                $env:ITHAX_CARBON_DB_CONNECTION_STRING)) {
            "sqlite"
        } else {
            "ole-db"
        }
    }
}

if ($providerChoice -eq "ole-db") {
    if ([string]::IsNullOrWhiteSpace(
            $env:ITHAX_CARBON_DB_CONNECTION_STRING)) {
        Write-ProviderNotConfigured $outputFile "OLE DB via Carbon DB" `
            "ITHAX_CARBON_DB_CONNECTION_STRING is unset"
        Write-Output "Carbon DB provider evidence is open: connection is unset."
        if ($FailIfUnavailable) {
            throw "Carbon DB provider connection is not configured."
        }
        return
    }

    $exefilePath = Join-Path $installPath "tools\carbon-exefile\exefile_Debug.exe"
    $prefixBin = Join-Path $installPath "bin"
    $prefixLib = Join-Path $installPath "lib"
    $pythonRoot = Join-Path $installPath "tools\python3"
    $pythonStdlib = Join-Path $pythonRoot "Lib"
    $pythonDlls = Join-Path $pythonRoot "DLLs"
    $measurementScript = Join-Path $repoRoot "tests\db_owner_measurement.py"
    foreach ($requiredPath in @(
            $exefilePath,
            $prefixBin,
            $prefixLib,
            $pythonStdlib,
            $pythonDlls,
            $measurementScript
        )) {
        $pathType = if ([IO.Path]::HasExtension($requiredPath)) {
            "Leaf"
        } else {
            "Container"
        }
        if (-not (Test-Path -LiteralPath $requiredPath -PathType $pathType)) {
            throw "Carbon DB provider input was not found: $requiredPath"
        }
    }

    $oldPath = $env:PATH
    $oldPythonPath = $env:PYTHONPATH
    $oldPythonNoUserSite = $env:PYTHONNOUSERSITE
    try {
        $tempRoot = [IO.Path]::GetTempPath()
        $runDirectory = Join-Path $tempRoot "ithax-carbon-db-$PID"
        if (Test-Path -LiteralPath $runDirectory) {
            throw "Carbon DB provider run directory already exists."
        }
        New-Item -ItemType Directory -Path $runDirectory | Out-Null

        $stdoutPath = Join-Path $runDirectory "stdout.txt"
        $stderrPath = Join-Path $runDirectory "stderr.txt"
        $env:PATH = @(
            (Split-Path -Parent $exefilePath),
            $prefixBin,
            $prefixLib,
            $pythonRoot,
            $pythonDlls,
            $oldPath
        ) -join [IO.Path]::PathSeparator
        $env:PYTHONPATH = @(
            (Join-Path $repoRoot "tests"),
            $prefixBin,
            $prefixLib
        ) -join [IO.Path]::PathSeparator
        $env:PYTHONNOUSERSITE = "1"

        $libSearchPath = "$pythonStdlib;$prefixLib"
        $arguments = @(
            "/inherit",
            "/nocrashreportupload",
            "/buildflavor=debug",
            "/root=$(Quote-ProcessArgument $runDirectory)",
            "/bin=$(Quote-ProcessArgument $prefixBin)",
            "/lib=$(Quote-ProcessArgument $libSearchPath)",
            "/py",
            (Quote-ProcessArgument $measurementScript)
        ) -join " "
        $process = Start-Process -FilePath $exefilePath `
            -ArgumentList $arguments `
            -WorkingDirectory $runDirectory `
            -RedirectStandardOutput $stdoutPath `
            -RedirectStandardError $stderrPath `
            -Wait -PassThru

        $stdout = Read-BoundedText $stdoutPath
        Read-BoundedText $stderrPath | Out-Null
        if ($process.ExitCode -ne 0) {
            throw "Carbon DB provider measurement failed."
        }

        $records = [System.Collections.Generic.List[string]]::new()
        $summary = $null
        foreach ($line in ($stdout -split "`r?`n")) {
            $json = $line.Trim()
            if (-not $json.StartsWith("{")) {
                continue
            }
            try {
                $parsed = $json | ConvertFrom-Json
            } catch {
                throw "Carbon DB provider emitted invalid JSON evidence."
            }
            if ($parsed.event -like "carbon_db_owner_*") {
                $records.Add($json)
                if ($parsed.event -eq "carbon_db_owner_summary") {
                    $summary = $parsed
                }
            }
        }
        if ($null -eq $summary) {
            throw "Carbon DB provider emitted no summary evidence."
        }

        $metadata = [ordered]@{
            event = "stage3_carbon_db_provider_run"
            status = $summary.status
            gate_status = $summary.gate_status
            measurement_class = "real-provider"
            provider = "OLE DB via Carbon DB"
            connection_configured = $true
            records = $records.Count
        }
        $lines = [System.Collections.Generic.List[string]]::new()
        $lines.Add(($metadata | ConvertTo-Json -Compress))
        foreach ($record in $records) {
            $lines.Add($record)
        }
        $lines | Set-Content -LiteralPath $outputFile -Encoding UTF8
        Write-Output ("Wrote {0} Carbon DB provider records to {1}" -f `
            $lines.Count, $outputFile)
    } finally {
        $env:PATH = $oldPath
        $env:PYTHONPATH = $oldPythonPath
        $env:PYTHONNOUSERSITE = $oldPythonNoUserSite
        if ($null -ne $runDirectory -and
            (Test-Path -LiteralPath $runDirectory -PathType Container)) {
            Remove-Item -LiteralPath $runDirectory -Recurse -Force
        }
    }
    return
}

$sqliteExe = Join-Path $repoRoot "tools\sqlite\sqlite3.exe"
if (-not (Test-Path -LiteralPath $sqliteExe -PathType Leaf)) {
    Write-ProviderNotConfigured $outputFile "SQLite CLI" `
        "sqlite3.exe not found in tools/sqlite"
    Write-Output "SQLite provider evidence is open: sqlite3.exe is missing."
    if ($FailIfUnavailable) {
        throw "SQLite CLI provider is not installed."
    }
    return
}

$pythonRoot = Join-Path $installPath "tools\python3"
if (-not (Test-Path -LiteralPath (Join-Path $pythonRoot "python.exe") -PathType Leaf)) {
    $pythonRoot = Join-Path $installPath "x64-windows-debug\tools\python3"
}
$pythonExe = Join-Path $pythonRoot "python.exe"
if (-not (Test-Path -LiteralPath $pythonExe -PathType Leaf)) {
    $pythonExe = $null
}

$oldPath = $env:PATH
$oldPythonPath = $env:PYTHONPATH
$oldPythonNoUserSite = $env:PYTHONNOUSERSITE
try {
    $tempRoot = [IO.Path]::GetTempPath()
    $runDirectory = Join-Path $tempRoot "ithax-sqlite-db-$PID"
    if (Test-Path -LiteralPath $runDirectory) {
        throw "SQLite provider run directory already exists."
    }
    New-Item -ItemType Directory -Path $runDirectory | Out-Null

    if ($null -ne $pythonExe) {
        $env:PATH = @(
            (Split-Path -Parent $pythonExe),
            (Join-Path $pythonRoot "DLLs"),
            $oldPath
        ) -join [IO.Path]::PathSeparator
        $env:PYTHONNOUSERSITE = "1"
    }

    Invoke-SqliteProviderLane -SqliteExe $sqliteExe `
        -PythonExe $pythonExe -OutputFile $outputFile -RunRoot $runDirectory
} finally {
    $env:PATH = $oldPath
    $env:PYTHONPATH = $oldPythonPath
    $env:PYTHONNOUSERSITE = $oldPythonNoUserSite
    if ($null -ne $runDirectory -and
        (Test-Path -LiteralPath $runDirectory -PathType Container)) {
        Remove-Item -LiteralPath $runDirectory -Recurse -Force
    }
}
