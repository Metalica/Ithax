# Ithax

Ithax is a multicore client foundation built around the open Carbon engine
modules. The repository implements the bounded native Stage 3 foundation,
the Stage 4 network conformance path, and the Stage 5 TrinityAL Vulkan
backend while measuring external Carbon and platform owners explicitly.
Server-side implementations, proprietary content, and external runtime
dependencies are outside this repository's distribution scope.

See [`ABOUT.md`](ABOUT.md) for an overview of the project, its milestones,
and its scope boundaries.

## Current Status

- Stage 0: gap analysis complete.
- Stage 2 architecture ADRs and the Python/ThreadBudget/Vulkan decisions are
  recorded under `docs/architecture/`.
- Stage 3 native slice is active: runtime-owned Taskflow jobs, owner-thread
  EnTT snapshots and deterministic journals, bounded SPSC frame packets with
  generation-checked frame slots, fixed-capacity worker scratch arenas, a
  lifecycle supervisor with typed failure propagation, and a separate native
  1,000-tick integration gate.
- The full Windows Carbon host lane initializes embedded Python and Blue,
  imports the Carbon DB extension, completes 1,000 ticks over 10,000 entities,
  and shuts down through the explicit Carbon drain path. Its owned simulation
  slice now passes the p99 16.667 ms budget with one measured Taskflow worker;
  Carbon's pacing wait remains outside that timing scope and hard sample misses
  remain recorded as diagnostics.
- Stage 4 network conformance passes: the machoNet framing, EVE marshal
  codec, six-step PLACEBO session handshake, session crypto, connection
  generations, and reconnect/stale-completion behavior are verified against
  golden vectors and a deterministic mock; the coverage-guided fuzz lane
  completes without crash or unbounded work.
- Stage 5 TrinityAL Vulkan backend is active: a real Vulkan 1.3 device path
  with VMA allocation, synchronization2 dynamic rendering, timeline
  semaphores, a compiled render graph, and an offline HLSL-to-SPIR-V
  toolchain validated with `spirv-val`.
- The Stage 5 milestone scene clears, draws a triangle and a Carbon Mesh
  procedural ship, orbits the camera, survives resize and minimize/restore,
  verifies a deterministic readback, recreates a lost surface, and records
  named-hardware p50/p95/p99 frame-time evidence with zero validation
  errors.
- Stage 5 device-loss handling is typed: `VK_ERROR_DEVICE_LOST` stops
  submission, surfaces `E_DEVICELOST`, and gathers `VK_EXT_device_fault`
  diagnostics when available.
- Stage 1.3: Carbon Core, Carbon Math, Carbon Scheduler, Carbon IO, Carbon
  Blue, Carbon Mesh, Carbon ImageIO, Carbon Resources, Carbon Trinity,
  Carbon Destiny, the Carbon Audio stub, Carbon Exefile, Carbon Pathfinder,
  Carbon Localization, Carbon DB, Carbon Parser, Carbon Geo2, and Carbon D3DInfo
  build successfully.
- Foundation smoke executable and CTest coverage are active.
- Scheduler Python extension smoke coverage is active.
- Carbon IO Python extension smoke coverage is active.
- Real Carbon IO owner measurement is active: the bounded loopback, SSL, and
  select workload peaks at 7 process threads from a 4-thread baseline.
- Carbon Blue native and Python smoke coverage is active.
- Real Carbon Blue owner measurement is active: one configured resource worker
  is exercised through cold and warm resource loads; process high-water is 28
  threads while the callback manager is active.
- Carbon Mesh native smoke coverage is active.
- Carbon ImageIO native smoke coverage is active.
- Carbon Resources native smoke coverage is active.
- Carbon Trinity stub native smoke coverage is active.
- Carbon Destiny Python extension smoke coverage is active.
- Carbon Audio stub native smoke coverage is active.
- Carbon Exefile native smoke coverage is active.
- Real Crashpad owner measurement is active: the installed handler is started,
  receives a non-crashing dump request, and reaches a pending report.
- Carbon Pathfinder native smoke coverage is active.
- Carbon Localization native smoke coverage is active.
- Carbon DB native smoke coverage is active.
- Carbon Parser native smoke coverage is active.
- Carbon Geo2 native smoke coverage is active.
- Carbon D3DInfo native smoke coverage is active.
- Full CTest suite passes: 31/31 tests, including the Carbon host lane, three
  real Carbon-owner lanes, Stage 3 native integration, Exefile `/py`, and
  ThreadBudget acceptance lanes.
- Stage 3 acceptance executes the Windows host and owner probes and records an
  explicit gate status; Wwise, shader compiler, vendor SDK, and Carbon DB
  provider workers remain provider-bound or unavailable in the default build.
- An opt-in Carbon DB provider runner and shader-compiler owner runner are
  available when an authorized database connection or optional package exists.
- Stage 2 benchmark and ThreadBudget evidence remain recorded separately from
  the remaining external-owner measurements.
- Synthetic external-owner validation records process-thread deltas without
  claiming measurements for unintegrated Carbon or vendor pools.
- The Stage 2 raw benchmark records are in
  `artifacts/benchmarks/stage2.jsonl` and `stage2-grain.jsonl`.
- The Stage 2 ThreadBudget raw records are in
  `artifacts/benchmarks/thread-budget.jsonl`.
- The TrinityAL public-interface inventory is in
  `docs/architecture/trinityal-interface-matrix.md`.
- The Stage 1 clean-prefix verification remains reproducible; the Stage 2
  benchmark uses a separate local prefix with Taskflow and EnTT installed.
- Carbon gRPC is opt-in and blocked by generated-code compatibility with the
  legacy PDM Protobuf ABI.

The Audio package is currently a Wwise-free stub because the proprietary Wwise
SDK is not redistributed by this project. Full Wwise integration remains a
conditional follow-up. Carbon gRPC remains a separately gated experiment.

## Build

The supported local build uses the Visual Studio 2022 generator and the
portable tools in `tools/`. See [`BUILD_GUIDE.md`](BUILD_GUIDE.md) for the
dependency and verification commands.

## Layout

- `src/` contains Ithax-owned native code, including the TrinityAL Vulkan
  backend under `src/trinityal-vulkan/`.
- `cmake/` contains vcpkg overlays, triplets, and toolchains, plus the
  standalone TrinityAL Vulkan build under `cmake/trinityal-standalone/`.
- `scripts/` contains reproducible dependency, build, and test automation.
- `external/carbon/` contains local Carbon engine sources; the pinned
  TrinityAL checkout used by the Stage 5 standalone build is fetched into
  `external/trinity/` by the CI pipeline.
- `docs/` contains architecture and gap-analysis documents.

## License

Original Ithax work is dual-licensed:

- The MIT License in `LICENSE` applies to the general project codebase.
- The PolyForm Noncommercial License 1.0.0 in `LICENSE-POLYFORM` applies to
  the Ithax multicore foundation (Stage 3 and Stage 10 work) and the Ithax
  TrinityAL Vulkan backend (Stage 5 work), including the sources under
  `src/trinityal-vulkan/`. Commercial use of that work requires a separate
  written license from the copyright holder.

Carbon modules and other dependencies retain their own licenses, copyright
notices, and required attribution terms.
