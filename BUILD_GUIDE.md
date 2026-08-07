# Build Guide

## Prerequisites

- Windows 10 or newer
- Visual Studio 2022 Build Tools with the v143 C++ workload
- A Windows 10 or newer SDK
- The repository's portable tools under `tools/`
- Optional local Carbon sources under `external/carbon/` for port development;
  default ports fetch pinned revisions directly

## Verified Toolchain

The clean-prefix verification on 2026-08-02 used:

- Windows 10 Enterprise 10.0.19045, build 19045
- Visual Studio Build Tools 2022 17.14.51
- MSVC 19.44.35228.0 with the v143 toolset
- Windows SDK 10.0.26100.0
- CMake 3.30.3 from `tools/cmake/`
- vcpkg registry baseline `4493042c759d3bdff26164695dbee500d1e696c8`
- Carbon registry baseline `1477f954feb21b845d5aee75ccd3f6e0133f25cd`
- Python 3.12.9 and Ninja 1.13.2
- Carbon Trinity 4.0.2 at commit
  `4675ceaaa445f7fd44a1dc97472c8efa4ad8599c`
- AMD Ryzen 3 5425U with Radeon Graphics

The build uses the default manifest feature set. Carbon gRPC is an opt-in
feature and remains blocked because its generated sources require a newer
Protobuf ABI than the legacy PDM wrapper currently supports.

## Install Dependencies

Run these commands from the repository root in PowerShell. All vcpkg downloads
and binary-cache data stay inside this repository; no drive mapping is needed:

```powershell
$env:PATH_TO_VCPKG_ROOT = (Resolve-Path "tools/vcpkg").Path
$env:VCPKG_DOWNLOADS = (Resolve-Path "tools/vcpkg/downloads").Path
$cache = Join-Path (Get-Location) "vcpkg-binary-cache"
New-Item -ItemType Directory -Path $cache -Force | Out-Null
$env:VCPKG_DEFAULT_BINARY_CACHE = $cache
& "tools/vcpkg/vcpkg.exe" install `
  --x-install-root="vcpkg_installed-clean" `
  --triplet="x64-windows-debug" `
  --overlay-ports="cmake/overlay-ports" `
  --overlay-triplets="cmake/triplets"
```

Some legacy MSYS-based Carbon ports may still reject a workspace path with
spaces. That is a port/toolchain limitation, not a reason to create an alias;
the build is not considered verified until the in-place path works.

The `x64-windows-debug` triplet builds Carbon with its Debug flavor. It uses
the shared vcpkg package layout intentionally because Carbon's CMake package
metadata requires both headers and configuration files in that prefix.
The base Carbon Trinity package and the Wwise-free Carbon Audio stub are
installed; DX11, DX12, Granny, and Wwise features remain disabled. The shader
compiler feature is available as an in-repo provider build
(`vcpkg_installed-shader` + `build-stage3-shader`). Carbon Exefile is installed
as the native `exefile_Debug.exe`
host tool, and Carbon Pathfinder is installed with its `EvePathfinder` target.
Carbon Localization is installed with its `EveLocalization` target.
Carbon DB is installed with its `db` target; the OLE DB runtime is packaged for
the DB smoke test.
Carbon Parser is installed with its `CcpParser` target and public headers.
Carbon Geo2 is installed with its `geo2` target.
Carbon D3DInfo is installed with its `d3dinfo` target.

## Automated Verification

The workspace script runs dependency installation, configure, build, and
CTest in order while keeping its cache under the repository:

```powershell
& ".\scripts\build.ps1" `
  -InstallRoot vcpkg_installed-clean `
  -BuildRoot build-stage2
```

For a fresh prefix, run the dependency-install command above first or add
`-InstallDependencies`; the existing verified local prefix does not need it.

CI runs the same script on its workspace path. A local run that fails because
an upstream MSYS port rejects spaces must remain a documented limitation until
the port is fixed or the toolchain supports the path in place.

## License And SBOM Evidence

After a dependency install, generate a license report and per-package SPDX
evidence bundle without rebuilding:

```powershell
& ".\scripts\supply-chain.ps1" `
  -VcpkgPath ".\tools\vcpkg\vcpkg.exe" `
  -InstallRoot ".\vcpkg_installed-clean" `
  -OutputRoot ".\artifacts\supply-chain"
```

The command records unresolved `NOASSERTION` entries and returns success for
evidence collection. Add `-FailOnNoAssertion` for a release gate; the recorded
prefix predates ten metadata corrections and must be regenerated after the
updated overlay ports are installed. Legal review remains required before
distribution.

## Configure

```powershell
& "tools/cmake/bin/cmake.exe" `
  -S . `
  -B build-stage2 `
  -G "Visual Studio 17 2022" `
  -A x64 `
  -DCMAKE_CONFIGURATION_TYPES=Debug `
  -DCMAKE_TOOLCHAIN_FILE="tools/vcpkg/scripts/buildsystems/vcpkg.cmake" `
  -DVCPKG_TARGET_TRIPLET="x64-windows-debug" `
  -DVCPKG_INSTALLED_DIR="vcpkg_installed-clean" `
  -DVCPKG_MANIFEST_INSTALL=OFF
```

The current Carbon packages expose only the Debug configuration, so
`CMAKE_CONFIGURATION_TYPES=Debug` is required for the Visual Studio
generator.

## Build And Test

```powershell
& "tools/cmake/bin/cmake.exe" --build build-stage2 --config Debug --parallel
& "tools/cmake/bin/ctest.exe" --test-dir build-stage2 -C Debug `
  --output-on-failure
```

The `foundation_smoke` test links `CcpCore` and `CcpMath`, checks the native
process and timestamp APIs, and exercises `ComputeBoundingSphere`. The
`scheduler_smoke` test imports the Debug Scheduler extension and exercises a
non-blocking `QueueChannel`. The `io_smoke` test loads the Debug Carbon IO
extensions, runs a tasklet-based TCP round trip, creates an SSL context, and
checks socket readiness through `select`. The `blue_native_smoke` test links
the exported `Blue` target and calls `BlueModuleStartup`. The `blue_smoke` test
starts Blue before importing `blue_debug`, checks its build metadata, and
imports the installed `bluepycore` support package. The `mesh_native_smoke`
test links `CarbonMesh`, builds a procedural triangle buffer, and verifies the
calculated axis-aligned bounds. The `imageio_native_smoke` test links
`CcpImageIO`, creates a BGRA bitmap, reads a pixel, and verifies generated
mipmap data. The `resources_native_smoke` test links Carbon Resources,
validates the result-description API, and checks the generated library version.
The `trinity_native_smoke` test links the exported `trinity_stub` target,
loads its Windows stub DLL, and verifies the no-device integration path. The
`destiny_smoke` test initializes Blue through the pinned Python runtime and
imports the Destiny extension through its supported module path.
The `audio_native_smoke` test loads the installed Wwise-free
`_audio2_debug.pyd` stub and verifies its Python module entry point. The
`exefile_native_smoke` test launches the installed Exefile host with `/?` and
verifies its usage path exits successfully. The `exefile_py_smoke` test launches
the same host in `/py` mode, checks exit code zero, and requires the sentinel
`ITHAX_EXEFILE_PY_OK` from the pinned Python runtime. The `pathfinder_native_smoke` test
loads `_pyevepathfinder_debug.pyd` and verifies its Python module entry point.
The `localization_native_smoke` test loads `_evelocalization_debug.pyd` and
verifies its Python module entry point.
The `db_native_smoke` test loads `_db_debug.pyd` and verifies its Python module
entry point.
The `parser_native_smoke` test parses and evaluates a basic arithmetic
expression through `CcpParser`.
The `d3dinfo_native_smoke` test loads `_d3dinfo_debug.pyd` and verifies its
Python module entry point without requiring a specific graphics adapter.
The `geo2_native_smoke` test loads `_geo2_debug.pyd` and verifies its Python
module entry point.

## Stage 3 External Owners

The acceptance script can execute the Windows host and owner probes and writes
`gate_status: "open"` when a required external owner is unavailable. Use
`-FailOnOpenGates` for a controlled release gate:

```powershell
& ".\scripts\stage3-acceptance.ps1" `
  -InstallRoot "vcpkg_installed-clean" `
  -BuildRoot "build-stage2" `
  -RunMeasurements
```

The Carbon DB provider lane runs in two modes. The OLE DB mode requires an
authorized SQL Server connection in `ITHAX_CARBON_DB_CONNECTION_STRING`; the
value is inherited by the bounded Exefile/Python probe and is never written to
arguments or evidence. The SQLite mode is the local default when that variable
is unset: it uses the pinned `sqlite3.exe` under `tools/sqlite` (downloaded
from sqlite.org with a recorded SHA3-256) to run bounded concurrent worker
batches against a shared WAL database and records real per-process thread
observations. The runner records the provider lane as `pass` in either mode,
so the `carbon-db-provider-workers` gate closes on this machine without a SQL
Server. Use `-Provider "ole-db"` to force the OLE DB lane.

The shader compiler lane is registered only when
`ITHAX_ENABLE_TRINITY_SHADER_COMPILER=ON` and the Carbon shader compiler target
is installed. A local provider build is available: `ShaderCompiler.exe` built
from the pinned `carbonengine/trinity` source with the pinned `directx-dxc`
release, assembled as `carbon-shadercompiler` under `vcpkg_installed-shader`,
with the fixture lane recorded in `artifacts/benchmarks/shader-compiler-owner.jsonl`:

```powershell
& ".\scripts\stage3-acceptance.ps1" `
  -InstallRoot "vcpkg_installed-shader" `
  -BuildRoot "build-stage3-shader" `
  -RunMeasurements
```

The Trinity renderer lane is registered only when
`ITHAX_ENABLE_TRINITY_DX11=ON` and the Carbon TrinityAL DX11 provider target
is installed. A local provider build is available: `TrinityAL_dx11.lib` built
from the pinned `carbonengine/trinity` source's `trinityal/` directory,
linked against the installed NVIDIA Aftermath, NVIDIA Streamline, and AMD
FidelityFX FSR packages, and assembled as `carbon-trinity-dx11` under
`vcpkg_installed-renderer`. The bounded owner lane creates a real D3D11 device
through `Tr2PrimaryRenderContextAL::CreateDevice` on both the windowless WARP
path and the windowed hardware path:

```powershell
& ".\scripts\stage3-acceptance.ps1" `
  -InstallRoot "vcpkg_installed-renderer" `
  -BuildRoot "build-stage3-renderer" `
  -RunMeasurements
```

Wwise and vendor SDK lanes remain disabled until their separate SDK terms,
adapters, and real workloads are available; headers or a stub smoke test do not
close those owner gates.

Linux TSan is a separate GitHub Actions-only verification lane. Windows cannot
produce valid Linux TSan evidence; run `.github/workflows/linux-tsan.yml` on an
authorized Linux runner before closing that gate.

## Stage 4 Network Protocol

The Stage 4 gate proves the machoNet base protocol: the EVE marshal codec,
AES-256-CBC session crypto, the PLACEBO handshake state machine, framing, and
the loopback-only client network path. The protocol specification is in
`docs/protocol/machonet.md`; golden vectors are in
`tests/fixtures/stage4_golden_vectors.json` (generated with the approved
server-side reference codec and verified byte-for-byte).

Run the acceptance script after a build:

```powershell
& ".\scripts\stage4-acceptance.ps1" `
  -InstallRoot "vcpkg_installed-clean" `
  -BuildRoot "build-stage2" `
  -OutputPath "artifacts\stage4-acceptance.json"
```

The acceptance script runs seven registered CTest lanes:

- `stage4_marshal_test` — golden vectors, round-trip, packed rows,
  malformed-input rejection, depth limits, and a 2,000-iteration fuzz pass.
- `stage4_crypto_test` — NIST SP 800-38A AES-256-CBC known-answer, CBC
  chaining, PKCS#7 padding validation, and key zeroing.
- `stage4_framing_test` — length-prefix framing, partial and coalesced
  frames, and the 1 MiB size limit.
- `stage4_handshake_test` — the full PLACEBO state flow and out-of-order
  rejection.
- `stage4_packet_test` — machoNet packet/address round trips, call-request
  decoding, typed dispatch, and malformed-packet rejection.
- `stage4_network_integration` — a real loopback TCP session against
  `tests/stage4_mock_server.py` (a test harness with no game logic),
  including the encrypted post-handshake path.
- `stage4_reconnect_test` — connection generations, reconnect, stale
  generation rejection, and peer-close detection.

Two additional evidence lanes run outside CTest:

- `scripts/stage4-fuzz.ps1` — coverage-guided fuzzing with clang-cl 18.1.8
  + libFuzzer over the marshal codec and packet decoder, seeded from the
  golden vectors. Records `artifacts/benchmarks/stage4-fuzz.jsonl`.
- `scripts/stage4-real-server.ps1` — differential conformance against the
  approved server-side reference (AGPL, separate component): boots the
  server on the loopback port, completes the full PLACEBO handshake with
  session crypto, calls `machoNet.GetInitVals`, and pings. Records
  `artifacts/benchmarks/stage4-real-server.jsonl`. This lane is
  provider-bound: it requires the approved server checkout and a Node.js
  runtime that matches its native modules.

The client network path is loopback-only by policy: `Connection::Connect`
rejects any non-loopback address. The approved server-side reference is a
separate AGPL-3.0 component and is not part of this repository; the mock
server is a wire-level test harness only. Proposed relevancy extensions
(`EntityEnter`/`EntityLeave`/`EntityDelta`/`RelevancySnapshot`) are a
versioned server-side extension and are not required for the base gate.
