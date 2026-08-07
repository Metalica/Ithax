# ThreadBudget Runtime Evidence

Status: The Windows Carbon host p99 gate, Stage 3 native gate, and three named
Carbon-owner measurements are implemented. Carbon DB provider, real Trinity
renderer/shader, optional SDK owners, and Linux TSan remain open.
Date: 2026-08-06

## Probe Contract

`ithax-thread-budget-measurement` is a runtime-owned measurement probe. It:

- captures the process CPU set or process affinity mask;
- counts live threads belonging to the current process;
- records a baseline before creating a Taskflow executor;
- holds every requested Taskflow worker in an active phase and records the
  process high-water thread count;
- records the post-drain count after the executor is destroyed; and
- rejects a requested worker count when hard reservations, soft reservations,
  and required headroom leave no measured capacity.

The worker policy is derived from the process CPU set. The probe does not use
`std::thread::hardware_concurrency()` to select workers.

## Reproduction

Build the Debug target with the clean prefix, then run a bounded measurement:

```powershell
& ".\build-stage2\Debug\ithax-thread-budget-measurement.exe" `
  --workers 2 `
  --repetitions 3 `
  --hard-reserved 1 `
  --soft-reserved 0 `
  --headroom 1
```

The output is JSON Lines. Each `thread_budget_sample` record includes the
phase, owner, configured and observed counts, process CPU-set count, CPU-set
source, bounded CPU-set IDs, affinity mask, reservation class, and UTC
timestamp. The final `thread_budget_summary` reports available workers,
baseline threads, peak threads, and measured worker threads.

CTest runs a smaller one-repetition version as
`thread_budget_measurement_smoke` and a no-headroom rejection case as
`thread_budget_rejects_no_headroom`.

The bounded worker sweep is reproducible with:

```powershell
& ".\scripts\thread-budget.ps1" `
  -BuildRoot "build-stage2" `
  -OutputPath "artifacts\benchmarks\thread-budget.jsonl" `
  -Repetitions 3 `
  -Workers 1,2,4
```

## Measured Run

The local sweep on 2026-08-06 used the clean-prefix Debug build, MSVC
19.44.35228, process affinity mask `255`, and eight process-affine CPUs. The
baseline process had four threads and each drained phase returned to four.

| Configured workers | Available workers | Active peak | Measured worker delta |
|---:|---:|---:|---:|
| 1 | 6 | 5 | 1 |
| 2 | 6 | 6 | 2 |
| 4 | 6 | 8 | 4 |

The raw JSONL is `artifacts/benchmarks/thread-budget.jsonl`. These values are
local evidence for the probe and Taskflow phase, not a claim about disabled or
unmeasured Carbon and vendor pools.

## Real Carbon-owner Measurements

The following lanes run real installed Carbon workloads in the Windows Debug
build. They report live OS thread counts rather than inferred pool sizes.
`reservation` remains `unknown` until each owner is integrated into the
process-wide admission policy.

| Test | Real workload | Observed result |
|---|---|---|
| `carbon_io_owner_measurement` | Carbon Python IO extensions, Scheduler tasklets, loopback socket, SSL, and select; 8 repetitions | 4 baseline/ready threads, 7 peak threads |
| `carbon_blue_owner_measurement` | Embedded Python, Blue startup, one resource worker, cold/warm resource fixture | 4 stopped threads, 28 active process-thread high-water |
| `crashpad_owner_measurement` | Installed Crashpad handler, `StartHandler`, `DumpWithoutCrash`, pending report | 1 handler process, 8 to 9 handler threads |

The Blue count is a process-level high-water sample while the callback manager
is active; it is not a claim that all 28 threads belong to the resource queue.
The Crashpad count is scoped to the separate handler process. Blue uses
`resManThreadCount=1` and records `shutdown_mode=process-exit` because the
available native API does not provide a safe complete shutdown for this
one-shot measurement.

## Full Carbon Host Gate

`ithax-stage3-carbon-host-integration` is the full Windows host lane. It:

- initializes isolated embedded Python, `blue_debug`, and `bluepycore`;
- configures Blue with `resManThreadCount=1` and initializes resource paths;
- imports `_db_debug` as an extension-only Carbon DB probe;
- runs 1,000 ticks over 10,000 entities with one default Taskflow worker;
- records Tracy timing, thread-budget, and deadline evidence; and
- drains the supervisor, resource queues, callback manager, and Python owner
  before calling `BlueTerminate`.

The serial local Debug run completed all 1,000 ticks and passed the lifecycle,
state-integrity, and shutdown checks. The host now reuses the owner snapshot,
reuses the Taskflow graph, and validates journals in one pass. The measured
owned simulation slice uses one Taskflow worker and gates on p99; the final
CTest run reported p50 `6.646 ms`, p95 `7.243 ms`, and p99 `7.661 ms` against
a `16.667 ms` budget, with zero hard sample misses. Carbon `BlueOS::PumpOS`
remains executed on every tick, but its
documented pacing sleep/yield is outside the measured slice. The earlier
`18.855 ms` and `42.678 ms` results remain historical pre-optimization samples.
The measured process CPU set contained eight CPUs, with one hard reservation,
two soft reservations, one headroom slot, and four available Taskflow workers;
the host selects one by default based on the measured workload.

The host DB result is deliberately limited to `_db_debug` extension import. No
Carbon DB provider or provider worker pool is configured in this lane, so its
owner and reservation remain `unknown`. A separate opt-in provider runner is
available for an authorized SQL Server/OLE DB connection. The host uses
`PyOS->Shutdown(1)` during
preparation followed by `BlueTerminate`; a second level-two Python shutdown
after level one is not used because that API sequence faults in this build.

Run the host lane with:

```powershell
& ".\tools\cmake\bin\ctest.exe" `
  --test-dir "build-stage2" -C Debug `
  -R "^stage3_carbon_host_integration$" --output-on-failure
```

The host emits `gate_status: "open"` when p99 exceeds the deadline. The
`missed_deadlines` field remains a hard-sample diagnostic and does not override
the p99 gate. Add `--fail-on-deadline` to the executable when a controlled
performance run should return a failing process status.

Run the real Carbon DB provider lane only when the connection string is
available through the environment. The connection string is never placed in
arguments or evidence:

```powershell
$env:ITHAX_CARBON_DB_CONNECTION_STRING = "<authorized-local-connection>"
& ".\scripts\stage3-carbon-db-provider.ps1" `
  -InstallRoot "vcpkg_installed-clean" `
  -Provider "ole-db" `
  -OutputPath "artifacts\benchmarks\carbon-db-provider.jsonl"
```

Without that environment variable the runner records `not_configured` for the
OLE DB lane. The local SQLite provider lane closes the `carbon-db-provider-workers`
gate instead: a pinned `sqlite3.exe` from `tools/sqlite` (downloaded from
sqlite.org, SHA3-256 `88b4659fe747896b853af10157316b4ade143553efb89c1c8ca7423a278dcc8b`,
SQLite 3.53.4) runs bounded concurrent worker batches against a shared WAL
database at worker counts 1, 2, and 4, with row-count verification, per-process
thread observation, and an engine probe through the pinned Python `_sqlite3`
module:

```powershell
& ".\scripts\stage3-carbon-db-provider.ps1" `
  -InstallRoot "vcpkg_installed-clean" `
  -Provider "sqlite" `
  -OutputPath "artifacts\benchmarks\carbon-db-provider.jsonl"
```

The runner picks `sqlite` automatically when the connection string is unset,
and `ole-db` when it is set. With the OLE DB path, the runner creates bounded
Carbon `db.NSession` pools, refreshes schema through the real OLE DB provider,
samples the process thread high-water, and records no connection details.

Re-run the three lanes with:

```powershell
& ".\tools\cmake\bin\ctest.exe" `
  --test-dir "build-stage2" -C Debug `
  -R "^(carbon_io_owner_measurement|carbon_blue_owner_measurement|crashpad_owner_measurement)$" `
  --output-on-failure
```

## Current Scope

The native probe measures the process and the Taskflow executor directly. The
named lanes above measure Carbon IO, Blue resource/callback startup, and the
Crashpad handler. The Stage 3 owner probe additionally measures oneTBB, Tracy,
and D3D11 device creation. It does not establish counts for Wwise, Carbon DB,
or vendor SDKs. Those owners remain `unknown` until their real workloads can
run under observation.

The Stage 3 owner probe separates real runtime work from boundary checks:
oneTBB and Tracy are real local workloads, D3D11/DXGI is a device-creation
probe, and the Trinity stub target proves only the no-device loading path.
The real Trinity renderer owner is measured by the separate
`stage3_renderer_owner_measurement` lane through the `TrinityAL_dx11`
provider, so the Wwise and vendor SDK gates stay open.

The Carbon shader compiler lane is closed locally: `ShaderCompiler.exe` is
built from the pinned `carbonengine/trinity` source (pinned commit
`4675ceaaa445f7fd44a1dc97472c8efa4ad8599c`) with the pinned `directx-dxc`
release `v1.9.2602.24`, then assembled into an in-repo `carbon-shadercompiler`
provider package under `vcpkg_installed-shader`. The bounded fixture lane
compiles `tests/fixtures/stage3_owner.fx` at worker counts 1, 2, and 4 and
records real per-run thread observations. When the optional Carbon shader
compiler is installed, CMake registers the bounded fixture lane automatically:

```powershell
& ".\scripts\stage3-acceptance.ps1" `
  -InstallRoot "vcpkg_installed-shader" `
  -BuildRoot "build-stage3-shader" `
  -RunMeasurements
```

The Trinity renderer lane is closed locally: `TrinityAL_dx11.lib` is built
from the same pinned trinity commit's `trinityal/` sources with
`TRINITY_PLATFORM=TRINITY_DIRECTX11`, linked against the installed
NVIDIA Aftermath, NVIDIA Streamline (DLSS-SR, DLSS-FG, NIS), and AMD FSR
provider packages, and assembled as the in-repo `carbon-trinity-dx11` package
under `vcpkg_installed-renderer`. The bounded owner lane creates a real D3D11
device through `Tr2PrimaryRenderContextAL::CreateDevice` on both the
windowless WARP path and the windowed hardware path, creates and flushes
textures through the HAL, captures thread-budget snapshots before/active/
drained, and tears the device down. When `ITHAX_ENABLE_TRINITY_DX11=ON`,
CMake registers the lane automatically:

```powershell
& ".\scripts\stage3-acceptance.ps1" `
  -InstallRoot "vcpkg_installed-renderer" `
  -BuildRoot "build-stage3-renderer" `
  -RunMeasurements
```

The Wwise and 3Dconnexion lanes remain provider-bound. They require each
vendor SDK and a reviewed adapter/workload; the default Wwise-free audio stub
and an installed header alone are not owner measurements.

## External-Owner Validation

`ithax-stage3-external-owner-measurement` starts one bounded synthetic owner at
a time and captures the live process-thread count before, during, and after the
owner phase. It validates that an active external thread is observable, that
the owner can be released and joined, and that the evidence retains an
explicit `unknown` reservation class. The runner is:

```powershell
& ".\scripts\stage3-external-owner.ps1" `
  -BuildRoot "build-stage2" `
  -OutputPath "artifacts\benchmarks\stage3-external-owner.jsonl" `
  -Repetitions 3 `
  -Owners 2
```

The owner names are synthetic validation labels. They must not be read as
measurements of any named Carbon or vendor owner. The real Carbon-owner lanes
above are separate tests.

The next evidence run must retain raw JSONL together with the build commit,
OS, compiler, CPU-set source, workload phase, and configured external pool
values. A passing probe is evidence that the budget mechanism works; it is not
evidence that the entire Carbon process has no unmeasured workers.

## Source Evidence

- `src/thread_budget.h` defines the policy, snapshot, and headroom contract.
- `src/thread_budget.cpp` implements bounded Windows process-thread and
  process-CPU-set measurement, with a Linux affinity path for core testing.
- `src/thread_budget_measurement.cpp` exercises a real Taskflow executor.
- `src/carbon_blue_owner_measurement.cpp` measures Blue resource loading.
- `tests/io_owner_measurement.py` measures the installed Carbon IO workload.
- `src/crashpad_owner_measurement.cpp` measures the Crashpad handler process.
- `src/stage3_carbon_host_integration.cpp` runs the full Carbon host lane.
- `src/stage3_tsan.cpp` is the standalone Linux TSan stress harness.
- `tests/db_owner_measurement.py` exercises the real Carbon DB provider when
  configured.
- `scripts/stage3-carbon-db-provider.ps1` bounds and records that provider
  lane without exposing its connection string.
- `scripts/stage3-shader-compiler-owner.ps1` measures the optional real shader
  compiler process when its target is enabled; locally verified against the
  in-repo provider build at worker counts 1, 2, and 4.
- `scripts/stage3-acceptance.ps1` executes selected lanes and records open
  gates instead of treating registration as measurement.
- `scripts/reproducibility.ps1` records configuration and dependency input
  provenance.
- `docs/architecture/thread-budget-adr.md` defines reservation order and the
  remaining owner-specific gates.
