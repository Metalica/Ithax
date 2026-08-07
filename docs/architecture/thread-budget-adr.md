# ADR-0002: Process-Wide Thread Budget

Status: Accepted for Stage 2; runtime measurement gates remain open.
Date: 2026-08-06

## Context

Adding a Taskflow executor without accounting for existing Carbon, library,
driver, and SDK workers can oversubscribe the process. A machine-wide logical
CPU count is not a process budget and does not reveal actual worker creation.

## Verified Inventory

- Carbon core creates one native thread per explicit `CcpCreateThread` call;
  it does not provide a shared pool. See `external/carbon/core/CcpThread.cpp`.
- Blue resource loading uses an explicit `resManThreadCount`, otherwise its
  default is `clamp((logicalCpuCount - 1) * 8, 4, 24)`. The callback manager
  caps its worker count at 32. See `external/carbon/blue/src/ResourceLoading.cpp`
  and `CallbackMan.cpp`.
- Blue resource, async, network, texture-compression, and Trinity background
  work share `BeCallbackMan`. The main queue is polled by its owner rather than
  being an additional worker pool. See `external/carbon/blue/src/BlueResMan.cpp`.
- Trinity uses oneTBB `global_control` as a parallelism cap. It is not a
  guaranteed operating-system thread count. See `external/carbon/trinity/`
  `trinity/TriDevice.cpp`.
- Carbon DB submits work to libuv's global pool. The local libuv source shows a
  default of four workers and a `UV_THREADPOOL_SIZE` maximum of 1024. See
  `external/carbon/db/TaskletBlockingIO.cpp` and the pinned libuv source.
- Carbon DB also uses Windows `QueueUserWorkItem`, whose worker count is owned
  by Windows. See `external/carbon/db/SessionPool.cpp`.
- The shader compiler creates worker and message threads separately. See
  `external/carbon/trinity/shadercompiler/WorkQueue.h`.
- Optional gRPC, video, Wwise, Crashpad, Tracy, graphics drivers, and vendor
  SDKs may create additional workers. The default root build disables gRPC;
  the remaining counts require runtime measurement.

## Decision

Create one runtime-owned `ThreadBudget` before adding Stage 3 workers. The
budget is a policy and measurement object, not a claim that all external
threads can be controlled.

Reserve owners in this order:

1. Main/platform/Python owner.
2. Renderer and presentation owner required by Trinity.
3. I/O completion owner required by the selected socket implementation.
4. Audio owner when the active audio SDK requires affinity.
5. Existing Carbon, oneTBB, libuv, OS, driver, Crashpad, Tracy, and SDK pools.
6. The explicitly sized Taskflow executor from the remaining process budget.

The executor worker count must be derived from the process CPU set, measured
reservations, and a headroom policy. It must not use
`std::thread::hardware_concurrency()` as its only input.

## Required Runtime Model

Each inventory record must include:

- owner name and subsystem;
- creation mechanism and whether it is controllable;
- configured, observed, and peak counts;
- CPU-set or affinity constraints;
- lifecycle start and stop owner;
- workload phase in which it is active;
- measurement source and timestamp;
- whether its reservation is hard, soft, or unknown.

The budget must reject a Stage 3 configuration that has no measured headroom.
Shutdown must stop new work, drain owned queues, and report unjoined workers.

The initial runtime probe is recorded in
`docs/architecture/thread-budget-evidence.md`. It measures process CPU sets,
process thread counts, and a real Taskflow active/drained phase. It does not
substitute a synthetic Taskflow phase for the owner-specific Carbon and SDK
workloads listed below.

## Measurement Gates

- Capture process CPU sets rather than only host CPU totals.
- Measure Blue workers during cold and warm resource loading.
- Measure oneTBB activity during rendering and throttling.
- Record `UV_THREADPOOL_SIZE` and libuv high-water usage during DB and network
  work.
- Measure shader compilation, audio initialization, Crashpad, Tracy, driver,
  and optional SDK activity separately.
- Repeat measurements at supported worker counts and retain raw metadata with
  the build commit, OS, compiler, and workload.

## Rejected Alternatives

- One permanent thread for every subsystem: it increases context switching and
  ignores existing pools.
- A process-global Taskflow singleton: it hides ownership and makes lifecycle
  and tests non-deterministic.
- Global adoption of one lock-free queue: channel topology, ordering, and
  backpressure requirements differ by owner pair.
