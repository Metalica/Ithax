# Ithax Open Source Client — Draft Development Plan

> INTERNAL PLANNING DOCUMENT — not part of the external proof pack.

## Connecting Carbon Engine To An External Server-Side Boundary

Audit date: 2026-07-29.

This revision cross-references the checked-out Carbon sources, the local gap
analysis, the Vulkan specification and guide, and mature open-source engine
patterns. Statements are treated as one of the following:

- **Verified:** demonstrated by source, protocol code, or a reproducible test.
- **Decision:** an architecture choice that still needs an implementation gate.
- **Hypothesis:** a proposed target that must not be reported as measured fact.

### Current Verified Baseline

- The authoritative Ithax renderer baseline is Carbon Trinity 4.0.2 at commit
  `4675ceaaa445f7fd44a1dc97472c8efa4ad8599c`, pinned by the overlay port.
- `external/carbon/trinity` is a separate v5.0.0 comparison checkout. It is not
  the implementation baseline unless an upgrade ADR changes the pin.
- Top-level CMake intends to link `trinity_stub`, but Carbon Blue, Trinity, and
  Destiny are currently marked purged in `vcpkg_installed/vcpkg/status`.
- `build-vs/Testing/Temporary/LastTest.log` records a historical 18/18 pass.
  It does not prove that the current manifests, installed tree, or newly added
  targets configure, build, and pass now.
- Stage 1 remains open: no checked-in `.github/workflows/` or `scripts/`
  implementation currently satisfies its CI/reproducibility gate.
- Stage 0 is reopened until its external integration profile, license
  conclusion, backend status, and notice-packaging findings match this audit.
- The external integration profile is explicitly localhost-only and not
  hardened for LAN or public Internet deployment.

---

## Scope & Landscape

### What's Open Source (Carbon Engine — github.com/carbonengine, 33 repos)

| Repo | Purpose | License |
|------|---------|---------|
| **trinity** | Rendering engine with TrinityAL D3D11, D3D12, Metal, and stub backends; shader compiler has D3D11, D3D12, and Metal targets. **There is no Vulkan backend today.** | MIT + third-party notices |
| **destiny** | Game world simulation, physics, pathfinding | MIT |
| **core** | Low-level cross-platform OS abstractions | MIT |
| **scheduler** | Greenlet coroutine scheduler (Stackless-compatible) | MIT |
| **blue** | Python/C++ interop, game loop, resource loading | MIT |
| **exefile** | Executable host process, Crashpad, platform init | MIT |
| **io** | Async networking (modified Python socket/ssl) | PSF-2.0 |
| **mesh** | 3D mesh, skeleton, animation serialization | MIT |
| **audio** | Wwise audio wrapper, sound prioritization | MIT |
| **resources** | Resource packaging, delivery pipeline | MIT |
| **math** | Vectors, matrices, quaternions | MIT |
| **pathfinder** | Route finding over EVE map data | MIT |
| **imageio** | Bitmap image I/O (multiple formats) | MIT |
| **localization** | Localization framework | MIT |
| **vcpkg-registry** | Dependency registry for all Carbon modules | MIT |
| — + 18 more | Audio tools, db, parser, gRPC, Prometheus, and others; see the per-repository compliance matrix | Mixed permissive licenses and optional SDK terms |

### What's NOT Open Source (Critical Gaps)

| Missing Component | Impact | Mitigation Strategy |
|-------------------|--------|---------------------|
| **CarbonUI source distribution** | CarbonUI is not in the open Carbon repos. The audited client contains a proprietary Python `carbonui/` package, not a missing C++ Perforce library. | Extract from a user-owned client for local interoperability, subject to legal review; build only the native Trinity-backed host and compatibility bindings. Keep a clean-room replacement as fallback. |
| **EVE Python game scripts** — game logic | Game scripts are not open source. The audited client contains Python 2.7 `.pyj` bytecode in `code.ccp`. | Extract locally for interoperability where lawful, or clean-room reimplement against the documented server protocol. Never redistribute extracted scripts. |
| **Game assets** — models, textures, audio, effects | The audited build has a 47.2 GB shared-cache installation. These assets are proprietary. | Users must provide their own supported client installation for local extraction; never redistribute output. |
| **Optional proprietary SDKs** | Wwise, Granny, 3Dconnexion, and renderer vendor SDKs have separate terms. The modern audited builds have no build-blocking Perforce source dependency. | Keep optional features disabled until each SDK and notice requirement is reviewed; do not create speculative stubs. |
| **Quasar/gRPC layer** (partially open) | Newer networking/protobuf event pipeline | Partially in `grpc` repo; rest needs RE |
| **Launcher & auth proxy** | The official launcher handles authentication, patching, SSO | Must build a lightweight launcher for the approved server-side interface |
| **Client economy/market UI** | Server-side market handlers exist in the audited external integration; client behavior and wire contracts still need conformance work. | Keep pricing, validation, and order authority server-side; implement a versioned client view model and UI. |

### External Server-Side Boundary

The external integration is a separately governed server-side component. It is
not part of this repository or its distribution artifacts. Any client-facing
protocol work must use a pinned, documented interface and remain behind a
separate approval and conformance gate.

The client network path uses a length-prefixed custom marshal format and a
versioned session handshake. It is **not Protobuf**. Protobuf is used only by
separate gRPC/Quasar paths.

---

## Core Design Decision: Measured Multicore from Day One

Carbon's Python/tasklet orchestration and primary Trinity render context are
main-thread-centric. Carbon is not wholly single-threaded: Blue already uses
background resource work and Trinity uses oneTBB for CPU-side parallel work.
Ithax preserves useful native concurrency while adding explicit ownership,
bounded task parallelism, and reproducible measurement.

### Architectural Principles

| Principle | Why |
|-----------|-----|
| **One primary bounded executor** | Use one reusable Taskflow executor with an explicit worker count. The process-wide budget also inventories and constrains existing oneTBB, Blue, OS thread-pool, driver, and SDK workers; sizing Taskflow alone is insufficient. |
| **Affinity owners only when required** | Keep dedicated owners only for platform/Python, renderer submission and presentation, I/O completion, and audio when an API requires affinity. Physics and asset work start as jobs or serial task lanes. |
| **Explicit data ownership** | Main/Python owns ECS structural mutation. Workers read stable storage and write private journals or disjoint declared ranges. Journals merge at deterministic phase boundaries. |
| **EnTT on its actual model** | EnTT uses sparse-set component pools and optional owning groups, not archetype tables. Parallel access is allowed only for declared non-conflicting component ranges. |
| **Bounded channels with backpressure** | Select SPSC, MPSC, MPMC, mutex, semaphore, or `atomic::wait` by channel topology. Lock-free is not a goal by itself; capacity, ordering, overflow, wake-up, and shutdown semantics are mandatory. |
| **Python has one owner** | Initial CPython/Stackless compatibility stays on the main owner. Workers return native owned data through a callback queue and never call the Python C API directly. |
| **CPU and GPU graphs are distinct** | Taskflow schedules CPU jobs. A render graph declares GPU resource use, ordering, barriers, lifetimes, and queue ownership. One cannot replace the other. |

### Target Multicore Architecture

```text
Main/platform/Python owner
  input -> network drain -> Python -> ECS structural commit
       |                         |
       |                         +-> Python callback queue
       v
One explicitly sized Taskflow executor
  physics lane | culling | animation | particles | asset decode
       |
       +-> per-worker deterministic journals and render chunks
       |
       v
Immutable, bounded frame packet
       |
Renderer coordinator ---------------------- I/O completion owner
  TrinityAL Vulkan backend                   TCP framing + EVE marshal
  render graph                               AES session state
  queue submit/present                       bounded completion channel
       |
       v
GPU queues selected from measured capabilities
```

The renderer coordinator may initially be the main thread because Trinity's
current primary context is main-thread-oriented. Moving it to a dedicated
thread is a separate, tested migration. A dedicated physics, asset, upload, or
audio thread is added only when API affinity or profiling justifies it.

Candidate libraries are Taskflow for CPU task graphs and EnTT for sparse-set
ECS storage. Queue implementations are selected per channel; adopting
`moodycamel::ConcurrentQueue` globally is explicitly not an architecture
decision.

The server connection is machoNet over TCP using custom EVE marshal and the
audited AES session handshake.
---

## Playable Milestones (User-Visible Progression)

Each stage gate must produce a user-visible milestone so progress is never invisible for more than one stage:

| Stage | Milestone | What You See |
|-------|-----------|-------------|
| **0** | Gap analysis document | Docs, not executables. But the catalog of what exists/missing is the foundation. |
| **1** | Audited Carbon baseline builds, local conformance runs | Console and tests prove each enabled package and the fixture configuration work. Stub backends are reported as stubs, not full implementations. |
| **2** | Architecture docs + reproducible job benchmark | Benchmark output records commit, CPU, worker count, build flags, workload, warm-up, repetitions, and p50/p95/p99 latency. |
| **3** | Multicore smoke test executable | Explicitly sized workers run, ECS ownership checks pass, and exercised Linux tests produce no TSan reports. No window yet. |
| **4** | Client passes the approved session contract | Console and conformance evidence prove the marshal codec and session handshake. Proposed relevancy messages are a separate server-extension gate. |
| **5** | First TrinityAL Vulkan scene | A Vulkan-backed TrinityAL window clears, renders a triangle and procedural ship, survives resize/minimize, and reports no validation or synchronization-validation errors. |
| **6** | Required Python bootstrap imports | The pinned login/bootstrap import corpus passes with typed, documented stubs; unsupported modules fail explicitly. |
| **7** | Login screen renders | User-local CarbonUI scripts render the login flow through typed native bindings and the Vulkan UI pass. Mouse, keyboard, text, DPI, and focus work. |
| **8** | Real EVE ship renders | Real ship model from official client assets displays in space with correct textures. |
| **9** | Audio plays | Ship engine sound, button clicks. Sound follows camera. |
| **10** | Full multicore game loop with server-driven ECS | Two clients see each other move. Thread budget, ownership, fixed-step simulation, bounded channels, and bulk scatter updates pass deterministic and overload tests. |
| **11** | Full login → in-space flow | User can type password, pick character, and appear in space. |
| **12** | Full solar system renders with bracket mode | Planets, belts, gates, stations, brackets, and HUD render within a documented frame-time budget on reference hardware. Brackets batch by compatible texture, font, clip, and pipeline state. |
| **13** | All major features work | Ship control, cargo view (server-side), market, chat, scanning. Playable game. |
| **14** | Localization, mods, dev console | Multi-language support, plugin system, `~` console with Python REPL. |
| **15** | Tests green, benchmarks tracked | CI dashboard shows all tests passing, performance graphs over time. |
| **16** | Alpha release shipped | Installable client. Setup wizard. Documentation site live. Community active. |

**Rule: Each stage MUST produce its milestone before the next stage begins. If a stage's milestone cannot be reached, that stage is blocked, not "done enough."**

---

## Architecture Comparison: Official Carbon vs Ithax vs Complete Vision

Before proceeding, it's critical to understand what we're replacing and why.

### Official Carbon Engine (CCP Open Source — What Exists Now)
- **Main-thread-centric orchestration.** Python tasklets and Trinity's primary
  render context are centered on the main thread.
- **Existing concurrency.** Blue uses background resource-loading queues and
  Trinity uses oneTBB for CPU-side parallel work. This concurrency must be
  preserved and audited rather than described as nonexistent.
- **No verified universal state model.** Python dictionaries exist, but the
  plan must map actual authoritative state before claiming that every system
  is monolithic.
- **CarbonUI distribution.** It is absent from the open repositories but is
  present as proprietary Python in the audited client package.
- **No measured scale ceiling.** A ship-count limit cannot be inferred from
  the threading model without a controlled workload and hardware profile.
- **Mixed licenses.** Most Carbon repositories are MIT, Carbon IO is PSF-2.0,
  and spatial-audio-clustering is Apache-2.0. Third-party notices also apply.

### Ithax Multicore (Draft Plan Baseline)
- **One process-wide worker budget.** Taskflow gets an explicit worker count
  after reserving capacity for true affinity owners.
- **EnTT sparse-set ECS.** The main owner performs structural mutation;
  workers process declared non-conflicting ranges and emit journals.
- **Few permanent threads.** Main/Python, renderer coordination, and I/O are
  owners. Physics, assets, and uploads begin as work on the shared executor.
- **Bounded communication.** Each channel defines topology, ordering,
  capacity, overflow, wake-up, ownership, and shutdown behavior.
- **Measured scale only.** Ship and entity limits remain hypotheses until the
  benchmark protocol in Stage 15 produces reproducible results.

### Complete Vision (Ithax + L2 Battle Scaling + Grid System)
- **All corrected Ithax multicore architecture** (bounded executor, explicit
  ownership, bounded channels, CPU task graph, and GPU render graph).
- **Server-authoritative containers** (the server-side implementation owns all
  inventory/container state; client = thin view layer).
- **L2-inspired LOD pyramid:** LOD0 (full mesh, <30km) → LOD1 (simplified, 30-100km) → LOD2 (billboard, 100-500km) → bracket-only (>500km or >500 hostiles).
- **Proposed tiered relevancy:** update rates and distance bands are tunable
  hypotheses that require protocol and gameplay validation.
- **Proposed spatial partition:** a grid or adaptive tree can reduce candidate
  recipients, but complexity depends on density, interest rules, and boundary
  traffic. It is not automatically O(n×k) with bounded `k`.
- **Bracket batching:** compatible brackets are batched, but CPU generation,
  uploads, vertices, glyphs, clipping, and fragment cost still scale with
  visible count.
- **Local simulation scope:** only entities requiring prediction or local
  collision should enter client simulation; the server remains authoritative.
- **No unbounded scale claim.** 40K/100K frame rates and packet ceilings are
  removed until measured on named hardware and a real server workload.

### Authoritative-Server Constraint

The client cannot process entities the server does not disclose, but client
cost still scales with disclosed entities, update rate, effects, UI, and local
work. Server time dilation does not guarantee flat client frame time. The
design must apply explicit client budgets and degrade measured quality safely.

---

## Proposed Grid-Based Spatial Architecture

This section is a **server-side design hypothesis**, not a description of the
current external wire protocol. The audited gap analysis identifies the
relevancy messages below as new extensions that must be designed, implemented,
versioned, and load-tested. A naïve all-to-all broadcaster is O(n²), but the
correct spatial structure and thresholds must be selected from observed
movement and density distributions.

### The Problem
```
Illustrative worst case: 40,000 ships with naïve all-to-all broadcast:
  Updates per tick: 40,000 × 39,999 = 1,599,960,000 broadcasts
  At 10Hz:           15,999,600,000 updates/sec
  Result:            unacceptable candidate-recipient work
```

### 3D Grid Design
```
Initial hypothesis: 3D cubic cells with a tunable side length.
Candidate viewport: neighboring cells centered on the player.
Actual dimensions: selected by replay-driven benchmarks and gameplay rules.
```

### Cell State Machine
| State | Condition | Behavior |
|-------|-----------|----------|
| EMPTY | 0 relevant entities | No entity work; retain required timers/events |
| ACTIVE | Below measured budget | Normal simulation and relevancy policy |
| HEAVY | Budget pressure | Reduce optional update detail by policy |
| CRITICAL | Deadline missed | Apply measured time-dilation/degradation policy |

### Broadcast Rules
| Distance | Frequency | Content |
|----------|-----------|---------|
| Near | Protocol-defined, measured | Required gameplay state |
| Mid | Lower measured rate | Position, velocity, and required events |
| Far | Lowest safe measured rate | Coarse state or brackets |
| Irrelevant | Event-driven only | No periodic state; never hide forced gameplay data |

### Proposed Relevancy Lifecycle

The names below are placeholders until the external protocol extension is
specified. They must not be confused with verified existing machoNet messages.

- **EntityEnter:** Server sends a complete baseline; the client creates the
  entity at its ECS commit phase.
- **EntityLeave:** Client starts an optional visual fade, then removes the
  mapping and entity at a safe structural phase.
- **EntityDelta:** Server sends sequenced updates against a known baseline.
- **RelevancySnapshot:** Server replaces a generation after world entry,
  reconnect, warp, overflow recovery, or detected desynchronization.
- **Forced relevance:** Gameplay-critical entities and events bypass spatial
  filtering according to audited server rules, not a hard-coded client count.

### Server Worker Pool — Multicore Grid Processing
Candidate grid partitions may be assigned to Node.js workers or processes only
after auditing the existing server-side worker path and measuring serialization,
transfer, and ownership costs:
```
Worker A: cells 0-199   (10,000 ships)
Worker B: cells 200-399 (10,000 ships)
Worker C: cells 400-599 (10,000 ships, dense zone → Tidi)
Worker D: cells 600-799 (10,000 ships)
```
Cross-partition communication needs an explicit owner, sequence, backpressure,
failure, and migration protocol. Shared ring buffers are one option, not a
requirement. Per-cell time dilation is safe only if cross-cell simulation and
event ordering remain correct.

### Dynamic Octree Subdivision (for 100K+ clustering)
An adaptive octree is one candidate when a measured cell budget is exceeded.
The following values are starting hypotheses, not production limits:

| Level | Cell Size (per side) | Triggers At | Max Ships Per Sub-Cell |
|-------|-----------|-------------|------------------------|
| 0 | 100km | Base grid | 500 (then splits) |
| 1 | 50km | Level-0 cell exceeds 500 ships | 500 (then splits) |
| 2 | 25km | Level-1 cell exceeds 500 ships | 500 (then splits) |
| 3 | 12.5km | Level-2 cell exceeds 500 ships | Terminal — no further split |

The 22,000-ship example reaches 64 cells of about 344 ships only under a
uniform distribution. Real fleets may remain clustered in one leaf, so the
64× reduction is a best-case illustration rather than a guarantee.
Cells **merge** when ship count drops below threshold (prevents permanent fragmentation).

### Client Grid-Aware Architecture
- **ECS entity count is relevancy-bounded.** The bound is determined by the
  server policy and forced-relevance rules; 24,000 is only a stress-test case.
- **LOD is client-owned presentation policy.** Use projected size, distance,
  importance, and measured budgets; do not expose server partition cells as a
  required rendering tier.
- **Bracket tiers are client presentation policy.** Select full, compact, and
  icon-only forms from gameplay importance, projected size, distance, filters,
  and measured budgets rather than server cell coordinates.
- **Relevancy transitions handled safely.** Entity enter/leave uses negotiated
  generation semantics; any visual fade is configurable and separate from
  authoritative mapping lifetime.
- **Bulk update path required.** Decode typed deltas, resolve server IDs and
  generations, then scatter or copy genuinely contiguous runs. Interleaved
  IDs and values cannot be `memcpy`-ed directly into EnTT pools.

### Broadcast Math — Model to Validate
```
Without partition:    40,000² = 1,600,000,000 candidate pairs
Example partition:   40,000 × 1,800 average candidates = 72M
Illustrative ratio:  22× fewer candidate pairs

At 100K with subdivision:
  Uniform-leaf example: 344 × 344 × 64 = about 7.5M candidate pairs
  Unknowns: clustering, forced relevance, events, bytes, serialization,
            boundary duplication, worker transfer cost, and client budget
```

No packet-rate or FPS conclusion follows from candidate-pair counts. The
prototype must measure encoded bytes, p50/p95/p99 latency, queue depth, missed
ticks, and client frame time on named hardware.

### Grid Integration Points in Plan
| Component | Stage | Impact |
|-----------|-------|--------|
| Proposed relevancy messages | Stage 4 — Network Protocol | Versioned extension after base conformance |
| Partition-policy changes | Stage 4 — Network Protocol | Prefer server-internal details; expose only client-required semantics |
| ECS lifecycle tied to server relevance | Stage 10.3 — World State | Generation-checked enter/leave/delta/snapshot |
| I/O receives relevancy-filtered packets | Stage 10.5 — I/O | Still requires byte, count, and age backpressure |
| Worker culling bounded by snapshot size | Stage 10.6 — Worker Jobs | Benchmark configured stress cases |
| Grid subdivision manager (server) | Stage 10.9 — Grid Subdivision Manager | Split/merge/heal state machine |
| Bracket mode policy | Stage 12.4 — Brackets | Batch by compatible state; benchmark visible counts |
| Client LOD policy | Stage 12.2 — Ship Rendering | Projected size, importance, distance, and measured budgets |
| Grid partition edge-case tests | Stage 15 — Testing | Warp crossing, split oscillation, desync |

---

## STAGE 0 — Gap Analysis & Complete Catalog

### Objective
Create an exhaustive inventory of what exists, what's missing, and what must be built.

### Sub-stage 0.1: Repo Audit
- Enumerate all 33 organization repositories; clone the 32 code repositories
  and record the `.github` organization-metadata repository separately
- For each: determine build status, dependency list, build success/failure
- Document any Perforce-gated dependencies
- Locate where CarbonUI actually lives (search all repos for UI-related headers)
- Document the `vcpkg-registry` dependency graph (produce a full DAG)

### Sub-stage 0.2: Protocol Survey
- Read the approved server-side source code thoroughly, especially its
  authoritative service directory
- Document every RPC call and message type the server handles
- Map the start-up sequence: auth → char select → world entry
- Verify the documented AES-256-CBC PLACEBO session handshake against the
  pinned server-side build

### Sub-stage 0.3: Asset Inventory
- Download the official EVE client (free-to-play from eveonline.com)
- Catalog the asset directory structure
- Verify the pinned build's ResFiles index/store, `.black`, `.gr2`, DDS/PNG,
  `.wem`/`.bnk`, and static-data formats; do not assume legacy `.stuff`/`.red`
- Determine which assets are essential for a minimal client

### Sub-stage 0.4: Licensing Review
- Produce a license compliance matrix for all dependencies
- Original Ithax work is standardized under MIT in `LICENSE` and `vcpkg.json`;
  obtain legal review of client/server separation, server-side modifications,
  extracted proprietary content, and third-party notices
- Make every overlay install upstream license/NOTICE files; remove or justify
  any skipped vcpkg copyright checks before distribution
- Document all trademark considerations (EVE Online is a registered trademark)

### Gate / Deliverable
A document (`docs/gap-analysis.md`) that lists every component with status: **OPEN** / **CLOSED** / **NEEDS-RE** / **MUST-BUILD**.

---

## STAGE 1 — Environment & Build System

### Objective
Get a reproducible build of every enabled Carbon component while reporting
optional, proprietary, and stub features explicitly.

### Sub-stage 1.1: Toolchain Setup
- Use the verified Visual Studio 2022 v143 baseline for the current native
  stack; treat VS 2026/v145 as an optional Carbon Audio requirement
- Install CMake 3.30+
- Install vcpkg with Windows triplets
- Use Python 3.12 for build tooling; separately decide how to provide the
  audited Python 2.7/Stackless runtime semantics required by client scripts
- Install Doxygen 1.12.0+ (for docs builds)
- Install Git with LFS support

### Sub-stage 1.2: vcpkg Registry Configuration
- Clone carbonengine/vcpkg-registry
- Configure `vcpkg-configuration.json` to use both Microsoft baseline AND carbon registry
- Set `PATH_TO_VCPKG_ROOT` environment variable
- Build only the dependency closure of enabled features; keep proprietary
  optional SDK features disabled until reviewed
- Document any failures or missing dependencies

### Sub-stage 1.3: Build Core Module Stack
Generate and verify the dependency DAG from pinned vcpkg manifests instead of
maintaining an assumed hand-written order. The audited broad phases are:

1. Carbon-independent/foundation ports such as `core`, `math`, `parser`,
   `pdm`, `resources`, and `trinityaudioapi`, plus their external dependencies
2. `blueexposure`, `exefile`, `imageio`, `scheduler`, `geo2`, `mesh`,
   `pdm-proto-wrapper`, and `grpc`
3. `pathfinder`, `imagetools`, and `io`
4. `blue`
5. `destiny`, `audio`, `localization`, and other Blue-dependent modules
6. `trinity`, then `videoplayer`

Record feature-gated exceptions. Audio, Granny, 3Dconnexion, and renderer
vendor integrations must not silently enter the default build.

### Sub-stage 1.4: Loopback Conformance Environment
- Legal boundary: no unapproved server-side implementation or third-party
  server project is used or bundled. The gate checks the client network path
  only.
- Run the loopback conformance fixture
  (`tests/conformance_stub.py --serve`) and capture its startup log
- Probe it from a second process
  (`tests/conformance_stub.py --probe`) and capture the response
- Record both outputs as gate evidence (proof-pack item 5); the
  fixture is a test harness with zero game logic, not a game implementation

### Sub-stage 1.5: CI Pipeline
- Set up GitHub Actions (or similar) for:
  - Automated builds of the supported open/default Carbon feature matrix
  - Loopback conformance fixture smoke test
  - Trivy/dependency scanning
- Create reproducible build scripts in `/scripts/`

### Gate / Deliverable
- A fully reproducible build of the supported enabled Carbon module set
- Loopback conformance fixture running locally with a reachable
  client network path (startup log + probe response)
- CI pipeline green on `main`
- A `BUILD_GUIDE.md` with exact tool versions and procedures

---

## STAGE 2 — Deep Architecture Study

### Objective
Understand every Carbon module's internals well enough to modify them.

### Sub-stage 2.1: Core + Scheduler Deep Dive
- Study `core`: threading primitives, memory allocators, file system abstraction, clock/timing
- Study `scheduler`: tasklet lifecycle, channel semantics, scheduling guarantees
- Document how the main loop works (Blue's game loop)
- Understand how greenlets yield control

### Sub-stage 2.2: Blue & Python Binding Layer
- Study how Blue embeds CPython
- Document the class exposure macros (`PyExpose`, etc.)
- Study the resource loading chain (how Python requests a resource and it arrives)
- Document the `PyExtension` module pattern
- Understand how C++ exception handling maps to Python
- Decide and pin the Python/Stackless ABI strategy before Stage 3 Blue
  integration; record interpreter version, allocator/runtime boundaries,
  bytecode policy, extension ABI, and test corpus

### Sub-stage 2.3: Trinity Rendering Deep Dive
- Study `trinityal/` — the hardware abstraction layer
- Document shader compilation pipeline (shadercompiler + Yacc grammar)
- Understand the scene graph structure
- Study the effect system (how ships/effects are rendered)
- Document the draw call submission pattern
- Understand the camera and viewport management
- Inventory every TrinityAL interface required by a new Vulkan backend
- Inventory primary-context globals and every main-thread render-context use
- Map HLSL compiler outputs, reflection metadata, and cache formats to a
  deliberate SPIR-V toolchain

### Sub-stage 2.4: Destiny Simulation Deep Dive
- Study the physics model (ship movement, collision detection)
- Understand the pathfinding system
- Document the simulation tick structure
- Record concrete optional/proprietary interfaces; do not assume Perforce
  stubs are required

### Sub-stage 2.5: Resource Pipeline
- Study `resources` CLI tool
- Understand `.black` through Blue serialization and verify whether any
  supported build actually requires legacy `.red`
- Study the `red-to-black-converter` tool
- Document the resource manifest format

### Sub-stage 2.6: IO & Networking Layer
- Study the machoNet EVE-marshal packet format and PLACEBO session handshake
- Understand how `_carbon_socket` wraps standard Python sockets
- Document the tasklet-blocking pattern
- Study Carbon IO SSL/TLS for HTTPS endpoints separately from machoNet's
  audited non-TLS game socket and AES session layer

### Sub-stage 2.7: Parallel Architecture Research
- Study open-source job systems:
  - **Taskflow** — modern C++ task graph parallelism, header-only
  - **Intel TBB** — oneAPI threading building blocks
  - **EnkiTS** — lightweight task scheduler used in games
- Study ECS implementations:
  - **EnTT** — header-only sparse-set storage with optional owning groups
  - **Flecs** — ECS with relations, systems, pipeline
  - Provisional decision: benchmark EnTT (MIT) against Flecs if archetype
    tables, relationships, or built-in staging are requirements
- Study bounded channel implementations and guarantees:
  - **moodycamel::ConcurrentQueue** — lock-free MPMC; not wait-free,
    linearizable, or sequentially consistent across producers
  - **Dmitry Vyukov's SPSC queue** — bounded lock-free
- Define every channel's producers, consumers, capacity, ordering,
  backpressure, wake-up, allocation, ownership, and shutdown behavior
- Produce a process-wide thread budget:
  - Discover processors available to this process, not just machine totals
  - Inventory oneTBB, Blue resource workers, Windows thread-pool I/O, graphics
    driver threads, and SDK callback pools; constrain, integrate, or reserve
    headroom for each controllable pool
  - Reserve true affinity owners before sizing one Taskflow executor
  - Benchmark worker counts; do not default blindly to
    `std::thread::hardware_concurrency()`
- Study rendering thread patterns:
  - Naughty Dog's "A Parallel Renderer" (GDC)
  - UE5's rendergraph / RDG design
  - Trinity's `trinityal` abstraction — can we push it onto a dedicated thread?
- Keep the CPU Taskflow DAG separate from the GPU render graph
- Define ECS read/write declarations, structural ownership, deterministic
  journal merge order, snapshots, and frame-slot lifetime
- Define runtime lifecycle and failure supervision:
  `Starting → Running → StopRequested → Draining → Stopped/Failed`
- Produce a parallel architecture recommendation document

### Sub-stage 2.8: Approved Server-Side Internals
- Study the approved server-side network handler
- Document all message types and their handlers
- Understand the database schema
- Study the authentication flow end-to-end
- Document the character creation/login flow
- Study the market server integration

### Gate / Deliverable
- Architecture documentation for each Carbon module in `/docs/architecture/`
- Parallel architecture recommendation document with selected libraries
- Job system benchmark (schedule 10,000 tasks, measure throughput)
- ECS vs monolithic state performance comparison
- Complete call graph of the startup sequence
- Documented understanding of the Python→C++→Server data flow
- A pinned Python runtime/ABI decision accepted before Blue integration

---

## STAGE 3 — Multicore Foundation and Optional Adapters

### Objective
Create a minimal buildable client that incorporates audited Carbon modules,
the bounded task/ECS foundation, and only concrete optional adapters.

### Current Native Slice — 2026-08-06

- Runtime-owned Taskflow execution is validated through `ThreadBudget`; the
  executor is not a process-global singleton.
- The owner-thread EnTT world publishes immutable snapshots and merges bounded,
  deterministic per-worker journals.
- The bounded SPSC frame channel validates producer/consumer ownership,
  backpressure, ordering, and close wake-up.
- `RuntimeSupervisor` now owns pending task handles, exposes the lifecycle
  state machine, propagates the first typed failure source, and provides a
  cooperative stop token. External channel and owner-thread lifetimes remain
  explicit.
- `FrameSlotPool` now owns bounded byte payloads with move-only leases,
  generation-checked tokens, explicit state transitions, and close wake-up.
  `FramePacketChannel` carries optional slot descriptors without owning the
  payload storage; shutdown drains or explicitly discards queued descriptors
  before the slot pool is destroyed.
- `ScratchAllocator` now provides a fixed-capacity, owner-thread bump arena with
  validated alignment, typed overflow errors, and explicit reset invalidation.
- The native 1,000-tick integration gate now exercises randomized worker-count
  order, per-worker scratch use, dedicated frame owners, and real owner failure
  supervision.
- A synthetic external-owner measurement lane validates process-thread
  observation and lifecycle metadata without claiming Carbon or SDK coverage.
- Real owner lanes now exercise Carbon IO, Blue resource loading, and the
  Crashpad handler. Their process/thread observations are recorded in
  `docs/architecture/thread-budget-evidence.md`.
- The Windows Carbon host lane now exercises embedded Python/Blue, Tracy,
  `_db_debug` extension loading, 1,000 ticks, and explicit drain/shutdown.
- The host gates its owned simulation slice on the p99 Debug deadline, records
  hard sample misses separately, and the Stage 3 acceptance script executes
  probes before recording measured versus provider-bound lanes.
- Opt-in Carbon DB provider and shader-compiler runners now exist; they require
  an authorized SQL Server/OLE DB connection or the separately enabled package.
- The remaining Stage 3 gates are Carbon DB provider workers, real Trinity
  renderer/shader workloads, Wwise and vendor SDK workloads, and Linux TSan.

### Sub-stage 3.1: Monorepo Setup
- Create the `eve-client/` monorepo structure
- Decide on submodule vs subtree vs separate repo strategy for Carbon dependencies
- Set up `vcpkg.json` for the project
- Create top-level `CMakeLists.txt` that includes all needed Carbon modules
- Add Taskflow and the selected ECS to vcpkg; add a queue library only for a
  channel whose measured requirements match its documented guarantees

### Sub-stage 3.2: Job System — THE FOUNDATION ★
- Establish this before adding new parallel systems; the already integrated
  Carbon build baseline remains the reference.
- Integrate **Taskflow** as the job system core:
  - One reusable executor with work stealing and an explicit worker count
  - DAG task graph (tasks with dependencies)
  - `for_each`/`for_each_index` and `reduce`/`transform_reduce` for data
    parallelism
  - Async task submission from any thread
- Build a thin wrapping layer in `src/jobs/`:
  - Runtime-owned executor service; no process-global singleton
  - Typed task handles/futures with consumed error results
  - `ParallelFor` — Ithax wrapper over Taskflow `for_each`/`for_each_index`
    for ECS component ranges
  - `JobProfiler` — Tracy integration for task visualization
- Define cancellation as cooperative and non-preemptive
- Keep each submitted graph and every captured snapshot, iterator, journal, and
  result alive and unmodified through future completion, or move graph
  ownership to the executor
- Do not block on a nested Taskflow future from a worker of the same executor;
  use graph dependencies or Taskflow cooperative execution
- Reject submissions after stop is requested and define bounded teardown order
- Write comprehensive tests:
  - Schedule 100,000 tasks, verify all complete
  - Task with dependency chain (A→B→C→D)
  - `ParallelFor` over 10M elements, compare to single-threaded execution
  - Inject task exceptions, cancellation, startup failure, and shutdown races
  - Benchmark 1/2/4/8/16 workers where supported
  - Measure task-size crossover, latency percentiles, context switches,
    queue contention, and cache effects
- For deterministic simulation reductions, use fixed partitions and a fixed
  merge order rather than relying on scheduler-dependent floating-point order
- Record measurements before setting task-overhead or utilization budgets

### Sub-stage 3.3: ECS Integration ★
- Integrate **EnTT** as the entity component system:
  - Entity creation/destruction (using registry)
  - Sparse-set component pools and optional owning groups
  - `view()` and `group()` for parallel iteration
  - `ctx()` only for clearly owned/immutable world context, not a shared-state
    escape hatch
- Build a wrapping layer in `src/ecs/`:
  - `EcsWorld` — owns registry, manages entity lifecycle
  - Typed Python binding adapters; defer generic runtime component registration
    until a concrete schema requires it
  - `ServerEntityMap` — connection/server ID to versioned local entity mapping
  - `EcsCommandBuffer` — per-worker journals merged by deterministic task and
    local sequence order
- Keep creation, destruction, add/remove, sorting, and pool preparation on the
  ECS owner
- Require each system to declare component reads and writes; reject conflicting
  parallel graph nodes
- Allow worker writes only to disjoint partitions or private output storage
- Define initial component types (as C++ structs):
  - `TransformComponent` — position and rotation
  - `VelocityComponent` — linear and angular velocity
  - `ShipComponent` — stable ship-specific metadata
  - `RenderComponent` — mesh ID, material ID, LOD level
  - `PhysicsComponent` — mass, collision shape, forces
  - `ModuleComponent` — active modules, capacitor draw
  - `TargetComponent` — target ID, lock status, range
  - `ServerIdentityComponent` — server ID and connection/baseline generation
- Keep inventory/container/item UI snapshots in their feature model unless a
  measured world-system use justifies a lightweight ECS handle
- Write tests:
  - Create 10,000 entities, iterate all components, measure throughput
  - Parallel `view` iteration across configured worker counts
  - Entity component add/remove stress test
  - Command buffer: submit 1,000 mutations from worker threads, verify consistency
  - Re-run identical inputs and verify deterministic world-state hashes
- Treat 100K-entity timing as a benchmark case, not an unmeasured <1ms promise

### Sub-stage 3.4: Bounded Cross-Thread Communication ★
- Select queue primitives per audited channel topology:
- Build in `src/threading/`:
  - Bounded SPSC frame-packet channel (main publisher, renderer consumer)
  - Per-worker journals rather than an MPMC ECS mutation queue
  - Network completion channel selected from actual producer/consumer count
  - `ScratchAllocator` — per-worker arena with explicit alignment, capacity,
    overflow, reset, and lifetime behavior
  - `FrameSlot` with generation and
    `Free → Writing → Published → Reading → Free` states
  - Blocking wake-up using a semaphore, condition variable, or
    `atomic::wait`; do not busy-poll an atomic flag
- Separate immutable render snapshots from durable upload/destruction requests
- Use direct inline handoff when producer and consumer share one owner. Enable
  bounded SPSC plus blocking backpressure only for independently progressing
  owners; never wait for the same thread to consume its own queue.
- Do not conflate CPU frame slots, GPU frames in flight, and swapchain images
- For every bounded channel, test full/empty, overflow, stalled consumer,
  wraparound/ABA, shutdown wake-up, and destruction lifetime
- Benchmark 10K synthetic commands with named CPU/build settings and a stalled
  consumer case
- Measure publish-to-consume p50/p95/p99 latency, queue depth, and backpressure

### Sub-stage 3.5: Core + Scheduler Integration
- Create `src/core/` wrapping carbonengine/core
- Create `src/scheduler/` wrapping carbonengine/scheduler
- Write a simple tasklet test: spawn tasklets, send messages through channels
- Verify greenlet scheduling works correctly on Windows
- Note: scheduler will eventually be deprecated in favor of the job system; keep only for Python compatibility

### Sub-stage 3.6: Blue Minimal Integration
- Create `src/blue/` wrapping carbonengine/blue
- Build a minimal embedded Python interpreter
- Use the Python/Stackless ABI pinned by the Stage 2 gate; do not select or
  change interpreter ABI in this implementation stage
- Get Python to call C++ and C++ to call Python
- Create a configurable stub game loop without coupling simulation rate to
  presentation rate
- Test loading a simple Python script
- Key: Python has one asserted owner thread. Worker completions carry native
  owned data to a main-thread callback queue; no borrowed `PyObject*` crosses
  a thread boundary.

### Sub-stage 3.7: Optional and Proprietary Feature Boundaries
- Build the selected modern vcpkg features and record concrete missing
  headers, symbols, SDKs, and licenses
- Do not create speculative Perforce or allocator stubs; the audited modern
  configuration has no build-blocking Perforce source dependency
- Keep Wwise, Granny, 3Dconnexion, and renderer vendor SDKs as separate
  optional-feature decisions
- If a concrete allocator API is missing, implement the smallest typed adapter
  and test alignment, failure, lifetime, and cross-module ownership
- Add thread-local scratch allocators for job system workers
- Verify the project compiles and links with all disabled features explicit

### Sub-stage 3.8: Logging & Diagnostics
- Carbon's logging infrastructure — implement stdout/file logging
- Add Tracy or similar profiler integration with multi-thread awareness
  - Instrument main/Python, renderer and I/O owners, Taskflow workers, and
    serial task lanes
  - Task visualization in Tracy UI
  - Lock contention hotspots
- Add assertion and crash handling
- Keep the real Crashpad handler measurement lane green; production Crashpad
  wrapper and upload policy remain a separate adapter decision

### Sub-stage 3.9: Integration Smoke Test — Multicore
- Build the full project with multicore foundation
- Startup sequence: construct runtime services → initialize ECS/Core/Blue →
  load Python → run 1,000 ticks with parallel jobs → request stop → drain in
  dependency order → quiesce binding dependencies → finalize Python while its
  services remain alive → destroy those services
- Verify:
  - Required owner threads run; no optional subsystem thread is assumed
  - Job system respects the configured process-wide worker budget
  - ECS handles 10K+ entities
  - Bounded channels enforce ownership, ordering, and backpressure
  - Injected task/owner failures reach the typed runtime supervisor
- Measure: scaling vs one worker, frame latency, context switches, queue depth,
  and channel latency

### Gate / Deliverable
- A working executable with multicore job system, ECS, and thread-safe communication
- All concrete optional/proprietary feature dependencies are identified,
  disabled or licensed, and tested; no speculative Perforce stubs remain
- The project builds green on CI
- Native integration gate passes 1,000 multicore ticks under randomized worker
  counts and validates the deterministic per-tick state and final hash
- The full Windows Carbon host lane completes 1,000 ticks and records explicit
  p50/p95/p99 timing and thread-budget evidence; its owned simulation slice
  passes the 16.667 ms p99 deadline with one measured worker
- Exercised Linux core tests produce no TSan reports; this is not proof that
  no race exists
- Profiler shows deadlines and headroom, not a requirement to keep every core
  busy

---

## STAGE 4 — Network Protocol & Crypto

### Objective
Implement the audited machoNet EVE-marshal protocol and establish a complete
session with the approved server-side interface. Protobuf is out of scope for
this game-socket path.

### Sub-stage 4.1: Source-First Protocol Specification
- Pin the exact server-side build and client build used for conformance
- Derive framing, marshal opcodes, handshake states, services, and errors from
  approved source before relying on packet inference
- Confirm the audited frame shape:
  `[4-byte LE length][0x7E][4-byte LE map count][marshaled payload]`
- Document the custom EVE marshal values, object graphs, shared-string table,
  packed rows, limits, and malformed-input behavior
- Use captures only as differential evidence against the source-derived spec
- Redact credentials/session material and never commit sensitive PCAPs

### Sub-stage 4.2: EVE Marshal Codec
- Implement a bounds-checked reader and writer in `src/network/marshal/`
- Use typed variants for every supported opcode; do not expose unvalidated
  pointers or recursive structures without depth/size limits
- Preserve integer widths, floating-point bits, object identity/reference
  rules, token tables, dictionary semantics, and packed-row schemas
- Reject length overflow, excessive nesting/allocation, invalid references,
  duplicate forbidden fields, and trailing data according to the spec
- Build golden vectors from the approved source and the local protocol corpus
- Add round-trip, property, differential, and fuzz tests

### Sub-stage 4.3: Session Crypto and Authentication State Machine
- Implement the documented six-step PLACEBO flow and explicit states:
  `WAIT_VERSION → WAIT_COMMAND → WAIT_CRYPTO → WAIT_AUTH →`
  `WAIT_FUNC_RESULT → SESSION`
- Implement AES-256-CBC framing, IV behavior, padding validation, key lifetime,
  and authentication-state errors exactly as the pinned server expects
- Document that CBC alone is not authenticated encryption and this legacy
  compatibility mode is not equivalent to TLS/AEAD. For untrusted remote
  networks, require a separately reviewed secure transport or a versioned
  protocol upgrade.
- Do not invent a username/password hash or session-token flow; model the
  actual approved packets and transitions
- The audited game port is not TLS. Keep HTTPS/TLS endpoint support separate
  from machoNet session crypto.
- Zero transient key material where practical and never log credentials,
  plaintext authentication payloads, keys, or tokens

### Sub-stage 4.4: Client Network Runtime
- Implement in `src/network/`:
  - TCP connection generations and explicit connect/disconnect/reconnect state
  - Incremental framing with partial receive/send support
  - One owned buffer and operation record per outstanding I/O operation
  - Typed dispatch from decoded machoNet messages to main-owner commands
  - Heartbeat, timeout, cancellation, and stale-completion rejection
- Enforce the pinned v0.12.3.1 loopback-only deployment profile by default;
  reject accidental non-loopback use in release configuration
- Start with one completion owner per TCP stream to preserve ordering
- Realize that owner with raw IOCP and exactly one dequeuer, or serialize
  callbacks through a per-connection strand. If callbacks can overlap, use an
  MPSC handoff plus explicit sequence ordering rather than calling it SPSC.
- For Windows thread-pool I/O, pair `StartThreadpoolIo` with each operation,
  call `CancelThreadpoolIo` when synchronous failure will not generate a
  callback, and wait/cancel callbacks before freeing operation state
- Retain operation memory until cancellation completion is observed
- Test version exchange, authentication, service discovery, character list,
  character selection, and world entry

### Sub-stage 4.5: RPC Inventory and Extension Boundary
- Catalog the existing inventory, fitting, market, chat, movement, and world
  RPCs by their actual server-side service and method names
- Keep existing machoNet compatibility separate from proposed relevancy work
- Specify proposed `EntityEnter`, `EntityLeave`, `EntityDelta`, and
  `RelevancySnapshot` as a versioned server-side extension only after the base
  client passes conformance
- Define extension negotiation, sequence/generation rules, resynchronization,
  forced relevance, byte budgets, and unknown-message behavior

### Sub-stage 4.6: Network Tests
- Build a deterministic mock from checked-in, non-sensitive golden vectors
- Test partial frames, coalesced frames, malformed values, size/depth limits,
  timeouts, zero-byte completion, disconnect, cancellation races, and reconnect
- Test stale completions and stale messages from an old connection generation
- Differential-test the codec and state machine against the pinned server-side
  reference
- Run coverage-guided fuzzing on framing, marshal decoding, and state changes

### Sub-stage 4.7: Protocol Documentation
- Write the versioned protocol specification in `/docs/protocol/`
- Separate verified base protocol, optional gRPC/Quasar paths, and proposed
  Ithax extensions
- Include state diagrams, limits, error behavior, golden-vector provenance,
  and compatibility policy

### Gate / Deliverable
- Client completes the base handshake, authenticates, and enters a session
- Golden vectors pass in both directions against the pinned server-side build
- Fuzz and malformed-input suites complete without crash or unbounded work
- Proposed relevancy messages are not required for the base-protocol gate

---

## STAGE 5 — TrinityAL Vulkan Backend & Minimal Scene

### Objective
Add Vulkan as a new first-class TrinityAL backend and render a procedural scene
through Trinity's public path. The audited Trinity source implements D3D11,
D3D12, Metal, and a stub only; Vulkan-Headers and VMA dependencies do not make
it a Vulkan renderer. Top-level CMake selects `trinity_stub` when the pinned
package is installed; the current installed Trinity package is purged.

Real game assets remain deferred to Stage 8. A standalone Vulkan triangle is
useful bootstrap evidence but does not pass this stage until the same work runs
through TrinityAL.

### Sub-stage 5.0: Feasibility, Semantics, and Packaging
- Pin the Trinity commit and record the currently enabled features and targets
- Inventory every TrinityAL interface, capability bit, primary-context global,
  resource-lifetime rule, and shader artifact consumed by higher Trinity code
- Write a backend conformance matrix using D3D12/Metal source behavior as
  references; do not assume their semantics map directly to Vulkan
- Add an explicit `BUILD_VULKAN` option, vcpkg `vulkan` feature,
  `TRINITY_VULKAN` platform identifier, and Vulkan TrinityAL/Trinity targets
- Select and document the loader/dispatch approach. Direct loader calls,
  Volk, and vk-bootstrap are options; avoid overlapping bootstrap layers.
- Keep Windows Vulkan as the first platform. Linux WSI and MoltenVK are
  separate portability gates, not automatic results of adding Vulkan.

### Sub-stage 5.1: Capability Negotiation and Diagnostics
- Use Vulkan 1.3 as the provisional minimum profile, then validate it against
  the supported hardware matrix before freezing the requirement
- Enumerate instance version, layers, and extensions before instance creation;
  enable only supported names required by the selected platform/profile
- Create the minimal platform window and `VkSurfaceKHR` before physical-device
  selection so presentation support, surface formats, and present modes are
  part of device acceptance
- Query the loader API version and physical-device properties, features,
  extensions, limits, formats, memory heaps, queue families, surface formats,
  and present modes
- Require and explicitly enable the feature bits for dynamic rendering,
  synchronization2, and timeline semaphores; never infer enabled features from
  header version or extension presence
- Require graphics and presentation support plus platform surface extensions
  and `VK_KHR_swapchain` for windowed operation
- Prefer one family supporting graphics and presentation, but correctly
  support separate present families. Do not require dedicated transfer or
  compute queues.
- Hard-reject devices missing requirements, score the remaining devices, log
  every decision, and support deterministic device selection for tests
- Enable `VK_LAYER_KHRONOS_validation` when available in development and
  require it in Vulkan CI
- Add `VK_EXT_debug_utils`, object names, command labels, and a structured,
  deduplicated validation callback without PII
- Query and enable `VK_EXT_device_fault` and
  `VkPhysicalDeviceFaultFeaturesEXT::deviceFault` only when supported and
  selected; diagnostic commands cannot be used merely because headers exist

### Sub-stage 5.2: Renderer Ownership and Parallel Recording
- Introduce one renderer coordinator that owns queue submission, presentation,
  frame retirement, and mutable Trinity primary-context state
- Do not require every Vulkan call or device creation call to run on one
  thread. Follow Vulkan's externally-synchronized object rules.
- Give each recording worker a command pool and descriptor pool for each
  reusable GPU frame slot; never access or reset one pool concurrently or
  before its timeline value is complete
- Start with primary command buffers. Add secondary command buffers and
  parallel pass recording only after profiling; provide dynamic-rendering
  inheritance data when secondary buffers require it.
- Give each `VkQueue` a submission owner or explicit external lock
- On Win32, keep the platform message pump active during surface/swapchain
  create, acquire, present, resize, and teardown. Never block the pump waiting
  for a renderer operation that may synchronously send a window message.
- Keep the native window alive until swapchain and surface teardown completes
- If the platform/main owner also coordinates rendering, consume frame data
  inline. Use a blocking SPSC frame handoff only when producer and consumer are
  independently progressing owners.
- Migrate Blue GPU preparation and Trinity global-context use to the selected
  renderer owner before moving that owner off the main thread
- Assert ownership at API boundaries and test both main-owned and dedicated
  renderer-owner configurations where supported

### Sub-stage 5.3: Frame and Swapchain Lifecycle
- Keep these independent:
  - CPU frame-packet slots and generations
  - GPU `FrameContext` objects, configurable and initially two
  - Swapchain images selected from surface capabilities
- Store per-worker command/descriptor pools, staging ranges, and completion
  values in each `FrameContext`
- Use a binary acquire semaphore per frame context
- Use a binary render-finished semaphore per swapchain image so presentation
  wait semaphores are not reused before presentation releases them
- Reuse that semaphore only after the corresponding image is reacquired and
  its acquire synchronization is waited, proving that the prior presentation
  operation consumed its wait
- Use timeline semaphores for internal submission completion and resource
  retirement; keep WSI acquire/present synchronization binary
- Handle resize, minimization and zero extent without a busy loop
- On every creation/recreation, re-query capabilities, formats/color spaces,
  and present modes; clamp extent and image count and select only supported
  image usage, transform, composite alpha, and sharing mode
- Handle `VK_SUBOPTIMAL_KHR`, `VK_ERROR_OUT_OF_DATE_KHR`, and
  `VK_ERROR_SURFACE_LOST_KHR` through typed lifecycle transitions
- Treat FIFO as the guaranteed present mode. Mailbox and immediate are
  optional user settings selected only when advertised.
- Recreate swapchain-dependent resources without invalidating unrelated
  durable resources or using destroyed images
- Pass the old swapchain when appropriate and retire application-owned views
  and WSI synchronization only after rendering and presentation use completes
- Destroy application-owned views/framebuffers, never swapchain-owned
  `VkImage` handles
- When `VK_EXT_swapchain_maintenance1` or a promoted equivalent is enabled,
  use present fences for teardown. Document the unextended wait-idle fallback
  as a practical WSI limitation, not formal presentation-completion proof.
- On `VK_ERROR_SURFACE_LOST_KHR`, recreate the surface, revalidate queue-family
  presentation and device suitability, and never pass an old swapchain tied to
  the lost surface

### Sub-stage 5.4: Synchronization and Resource-State Tracking
- Use `vkQueueSubmit2` and `vkCmdPipelineBarrier2`
- Track each image/subresource's layout, stage, access, and queue-family owner;
  track equivalent buffer hazards
- Generate barriers from declared render-graph use, including transfer-to-use,
  write-to-read, read-to-write, write-to-write, and present transitions
- Use release/acquire ownership transfers only when queue families differ
- Compile submission-level `VkSubmitInfo2` waits, signals, and stage masks,
  including swapchain acquire and render-finished synchronization. Barriers do
  not replace semaphore ordering between queues or submissions.
- Wait for the narrowest required timeline values; avoid `vkDeviceWaitIdle` in
  normal frame or upload paths
- Add synchronization validation and focused tests for every transition class

### Sub-stage 5.5: Memory, Uploads, and Deferred Destruction
- Use VMA as a real Vulkan device allocator in this backend. Trinity's current
  VMA virtual-block use is not a device-memory implementation.
- Respect `bufferImageGranularity`, alignment, memory-type flags, non-coherent
  atom size, and mapped flush/invalidate requirements
- Query and monitor memory budgets when available
- Use bounded persistently mapped staging and readback rings with completion
  values; define allocation failure and backpressure behavior
- Decode assets on the shared CPU executor, then submit durable upload requests
  to the renderer coordinator
- Start transfers on the graphics queue. Add a dedicated transfer queue only
  when available, correctly synchronized, and measurably beneficial.
- Retire resources, descriptors, pipelines, and staging ranges only after the
  last GPU timeline value that references them has completed
- Apply configurable upload byte/time budgets per frame

### Sub-stage 5.6: SPIR-V, Reflection, Descriptors, and Pipelines
- Add a Vulkan shader platform to Trinity's compiler; the current compiler has
  D3D11, D3D12, and Metal targets only
- Define a versioned Vulkan shader ABI: HLSL register/space mapping,
  descriptor-set conventions, constant-buffer and matrix layout, coordinate
  conventions, push constants, specialization constants, and vertex inputs
- Compile HLSL to SPIR-V for target environment `vulkan1.3` offline for release
  builds and optionally at runtime for developer hot reload
- Run `spirv-val --target-env vulkan1.3` and normalize reflection for descriptor
  sets, bindings, push constants, vertex inputs, specialization constants, and
  stages
- Key shader artifacts by source/include hashes, compiler version, entry point,
  target environment, shader ABI version, defines, and flags
- Create shader modules and pipelines from SPIR-V; shader bytecode is not a GPU
  transfer upload
- Start with conventional descriptor sets and per-worker/per-frame pools. Once
  a set is referenced by recorded or pending work, treat it as immutable;
  replace/version it and retire the old set by timeline completion.
- Treat descriptor indexing/bindless resources as a separately negotiated
  feature tier with fallback paths. Update-after-bind additionally requires its
  exact feature bits, layout/pool flags, limits, and external synchronization.
- Validate serialized pipeline caches against vendor/device IDs, driver data,
  and `pipelineCacheUUID`; a cache miss must remain correct

### Sub-stage 5.7: Vulkan Render Graph
- Keep this graph separate from the Taskflow CPU DAG
- Require each pass to declare image/buffer reads, writes, attachment use, and
  queue intent
- Compile declarations into pass order, dead-pass culling, resource lifetime,
  image layouts, and synchronization2 barriers
- Start with one graphics queue, dynamic rendering, and no transient aliasing
- Add aliasing or multi-queue scheduling only after validation and profiling
- Test graph compilation independently from Vulkan command emission

### Sub-stage 5.8: Procedural Scene Through TrinityAL
- Reuse the selected platform window/surface from capability negotiation
- Present a clear frame, then a triangle, through the actual TrinityAL Vulkan
  context and shader path
- Integrate Carbon Mesh with a procedural ship-like mesh
- Add transforms, an orbit camera, depth buffering, a procedural texture, and
  a simple starfield or sky pass
- Publish camera and visible draw data in an immutable frame packet
- Move the milestone scene into the render graph before passing the stage

### Sub-stage 5.9: Recovery, Tooling, and Tests
- Route every `VkResult` through typed handling; distinguish status,
  recoverable WSI changes, out-of-memory, and device loss
- Distinguish host and device OOM. Permit at most a bounded cache-eviction and
  retry path at a safe phase; otherwise stop allocation and fail cleanly
  without reading or destroying undefined output handles.
- On device loss, stop submission, gather `VK_EXT_device_fault` data when
  previously enabled, invalidate all device-owned handles, and either rebuild
  from CPU resource recipes or exit cleanly. Never continue using old handles.
- Add RenderDoc capture support and Vulkan timestamp queries
- Add deterministic offscreen image-readback tests with tolerance rules
- Use a software ICD for portable offscreen CI and scheduled NVIDIA, AMD, and
  Intel hardware lanes for driver coverage
- Vulkan CI must install and require `VK_LAYER_KHRONOS_validation` with
  synchronization validation; fail setup rather than silently skipping the
  zero-error gate. Validation remains optional only in developer/release runs.
- Test WSI on a real/hidden platform surface; do not assume
  `VK_EXT_headless_surface` is universally available
- Inject failures to test cleanup and device-loss control flow

### Gate / Deliverable
- Vulkan builds as a real TrinityAL/Trinity target without D3D11 or D3D12
- SPIR-V artifacts pass `spirv-val`
- Capability logs explain every accepted or rejected physical device
- Clear, triangle, procedural mesh, depth, texture, and readback tests pass
  through TrinityAL with zero validation and synchronization-validation errors
- Repeated resize, minimize, restore, out-of-date, surface-loss, and shutdown
  tests complete without deadlock, leak, or premature resource reuse
- CPU frame slots, GPU frame contexts, swapchain images, and present semaphores
  have independent, tested lifetimes
- Performance results name hardware, driver, resolution, present mode, build,
  workload, and p50/p95/p99 frame times; no unmeasured FPS claim passes a gate

---

## STAGE 6 — Python Runtime & Script Loading

### Objective
Load locally extracted client scripts for interoperability without
redistributing proprietary code. The audited target uses Python 2.7/Stackless
semantics and `.pyj` bytecode, so Python 3 compatibility is a deliberate port,
not an assumed upgrade.

### Sub-stage 6.1: Python Module Path Research
- Extract the full Python script directory from the official EVE client installation
- Document the module structure:
  - `carbon/` — engine Python bindings
  - `eve/` — game-specific scripts
  - `exefile/` — application-specific scripts
- Identify the entry point Python module
- Document all imports and dependencies

### Sub-stage 6.2: Python Bytecode Extraction
- Read `code.ccp` and its zlib-wrapped Python 2.7 `.pyj` code objects
- Verify magic, marshal version, compression, and code-object schema against
  the pinned client build before selecting a decompiler
- Evaluate compatible `uncompyle6`, `pycdc`, or equivalent tooling with golden
  samples; do not assume modern `.pyc` support covers `.pyj`
- Decide and document one runtime strategy:
  - embed compatible Python 2.7/Stackless semantics locally; or
  - transpile/port to supported Python and maintain a compatibility test suite
- Keep extracted/decompiled output outside source control and release packages

### Sub-stage 6.3: Dependence Mapping
- Create a dependency graph of all Python modules
- Identify which modules can load vs which fail due to missing C++ bindings
- Prioritize: modules needed for login → character selection → station → space
- Create a loading test: attempt to import each module and log failures

### Sub-stage 6.4: C++ Binding Stubs
- Trace the pinned bootstrap corpus and implement its first missing CarbonUI
  native bindings before attempting the login flow; do not assume every module
  shares one import chain
- For each missing C++ binding that the Python code needs:
  1. Identify the C++ class/method signature
  2. Create a stub .cpp and .h file
  3. Expose it via Blue's `PyExpose` mechanism
  4. Verify the Python module loads without error
- Prioritize by the loading order determined in 6.3
- CarbonUI stubs are temporary native bindings. Stage 7 hosts the extracted
  Python framework and implements the native drawing/input surface it expects.

### Sub-stage 6.5: Python Loader Sequence
- Trace and implement the candidate bootstrap sequence:
  1. Set `sys.path` to EVE's script directory
  2. Set up the `carbon` module package
  3. Initialize the `blue` Python module
  4. Initialize the `trinity` Python module
  5. Initialize the `destiny` Python module
  6. Run the startup script identified from the pinned client
- Treat this ordering as a hypothesis until traced against the pinned build;
  verify the resulting imports, owner thread, and shutdown order

### Sub-stage 6.6: Resource Streaming Stubs
- The scripts will request resources (textures, models, audio) through Blue's resource system
- Implement stub resource loaders that:
  1. Log the resource request
  2. Return a placeholder (checkerboard texture, unit cube mesh)
- This allows Python scripts to run without real assets

### Sub-stage 6.7: Script Execution Smoke Test
- Startup: initialize core → init blue → load Python → execute startup script → enter game loop
- Measure: script loading time, memory at idle, Python heap size
- List all modules loaded and all stubs hit

### Gate / Deliverable
- The client can boot the EVE Python script environment
- The versioned bootstrap/login import corpus loads; every stub is typed,
  counted, documented, and fails explicitly outside its supported behavior
- The game loop runs Python coroutines
- A system for logging missing bindings

---

## STAGE 7 — CarbonUI Host and UI Rendering

### Objective
Host the proprietary Python `carbonui/` package extracted on the user's
machine and implement the native rendering, resource, text, and input bindings
it requires. CarbonUI is absent from the open repositories but is not an
unknown closed C++ framework that must automatically be rebuilt from scratch.

### Sub-stage 7.1: CarbonUI Contract Inventory
- Inventory the audited `carbonui/` and `eveui/` modules, import graph, base
  classes, native modules, resource calls, and startup order
- Trace a minimal login screen and record every native call and data contract
- Document widget hierarchy, layout, focus, event propagation, clipping,
  animation, text shaping, DPI, and accessibility behavior
- Generate compatibility tests from observed calls without committing client
  scripts or assets

### Sub-stage 7.2: Native Host Boundary
- Implement the smallest typed Blue binding surface required by the traced UI
- Keep Python execution on its owner thread
- Convert Python state into owned immutable UI draw data; never retain borrowed
  Python references in renderer or worker queues
- Marshal worker completions back to the Python callback phase
- Add thread-owner assertions and teardown tests at every Python C API boundary

### Sub-stage 7.3: Vulkan UI Render Pass
- Add a render-graph overlay pass through TrinityAL Vulkan
- Support rectangle/image batches, font atlas glyphs, scissor rectangles,
  transforms, opacity, layers, and stable painter ordering
- Batch only compatible pipeline, texture, font, blend, and clip state; never
  promise one draw call for arbitrary UI
- Implement DPI-aware coordinates and correct sRGB/linear compositing
- Use Vulkan object labels, timestamps, and RenderDoc captures in tests

### Sub-stage 7.4: Text and Input Correctness
- Use a maintained text shaping/rasterization stack and define Unicode,
  fallback font, bidirectional text, IME, selection, and clipboard behavior
- Route pointer, wheel, keyboard, text, and IME events through one ordered
  main-owner phase
- Implement focus, capture, bubbling, drag, double-click, repeat, and shortcuts
- Test high DPI, resize, focus loss, keyboard-only navigation, and screen-reader
  feasibility for new Ithax-owned UI

### Sub-stage 7.5: Compatibility Milestones
- Render a script-created rectangle, image, label, text field, and button
- Render the extracted login screen with placeholder user-local resources
- Add character-selection, overview, HUD, inventory, and market screens only
  after the preceding compatibility corpus passes
- Record unsupported calls as typed failures; do not silently no-op behavior

### Sub-stage 7.6: Developer UI
- Dear ImGui may be used for developer tooling through its Vulkan backend
- Keep ImGui outside the CarbonUI compatibility contract and production HUD
- Qt is not part of the renderer architecture unless a separate tool proves a
  concrete need and completes a license/deployment review

### Sub-stage 7.7: Clean-Room Fallback
- If local script hosting is legally or technically rejected, build an
  Ithax-owned retained UI against the documented behavior
- Do not mix decompiled implementation details into a claimed clean-room path
- Treat this as a separately estimated project, not an invisible fallback

### Gate / Deliverable
- User-local scripts can create and manage UI through typed native bindings
- Login UI renders through the Vulkan render graph with placeholder assets
- Mouse, keyboard, text, IME, focus, clipping, DPI, and resize tests pass
- No Python API is called off the Python owner thread

---

## STAGE 8 — Asset Pipeline

### Objective
Extract and load real game assets (models, textures, shaders, audio) from the official EVE client.

### Sub-stage 8.1: Resource Format Analysis
- Use the audited modern client layout:
  - `resfileindex.txt` — resource manifest
  - `ResFiles/00..ff` — content-addressed resource store
  - `.black` — compiled Blue-serialized resources
  - `.dds` — textures (standard DirectDraw Surface)
  - `.gr2` — models (Granny 3D format)
  - `.wem` / `.bnk` — Wwise media and banks
- Do not build `.stuff` or `.red` support for client build 3396210 unless a
  separately supported older build proves it is required
- Document every enabled format's header, limits, compression, integrity, and
  malformed-input behavior

### Sub-stage 8.2: ResFiles Store Reader
- Parse the pinned `resfileindex.txt` format with strict path and size checks
- Resolve logical resource paths to content-addressed bucket files
- Prevent path traversal, symlink escape, integer overflow, and unbounded reads
- Support lookup, streaming reads, integrity verification, and cancellation
- Test known entries, missing files, corrupt indexes, duplicate mappings, and
  client-version mismatch

### Sub-stage 8.3: Resource Index/Database
- Extract the resource manifest from the official client
- Use the manifest directly first; add SQLite only if profiling justifies a
  derived index and define invalidation by client build/hash
- Create a resource manager that:
  1. Receives a resource ID from Python
  2. Looks up the file in the manifest or justified derived index
  3. Opens the resolved content-addressed file
  4. Returns the raw data
  5. Caches frequently accessed resources

### Sub-stage 8.4: Texture Loader
- Load `.dds` textures (using carbonengine/imageio)
- Create backend-neutral Trinity resources backed by Vulkan images
- Support: BC1-BC7 compression, mipmaps, cubemaps
- Validate format support and choose explicit transcode/fallback behavior
- Implement texture cache
- Test: load a ship texture, display it on a quad

### Sub-stage 8.5: Model Loader
- Load `.gr2` (Granny 3D) models
- Choose a legally reviewed local conversion path or optional user-supplied
  Granny SDK integration; do not redistribute proprietary SDK/runtime content
- Convert to a versioned internal Carbon mesh artifact
- Create backend-neutral Trinity buffers backed by Vulkan buffers
- Support: multi-LOD meshes, skeleton (for animation later)
- Test: load and render a real EVE ship model

### Sub-stage 8.6: Material System
- Parse EVE material definitions (likely in Python scripts)
- Implement a material system:
  - Diffuse texture
  - Normal map
  - Specular/gloss map
  - Emissive map
  - Shader selection
- Create a material cache

### Sub-stage 8.7: Asset Loading Pipeline via Job System
- Implement async resource loading on the shared bounded executor:
  1. Python requests a resource via Blue's resource interface
  2. Main owner validates and enqueues a typed load operation
  3. A worker reads from ResFiles, decompresses, validates, and decodes
  4. The worker publishes an owned durable upload request
  5. Renderer coordinator allocates/stages it within GPU upload budgets
  6. Completion returns to the main/Python callback queue
- Version requests so cancelled, evicted, or superseded results are discarded
- Define CPU bytes, GPU bytes, operation count, age, and priority backpressure
- Loading progress reporting via job system counters
- Resource streaming (load LOD levels progressively) as low-priority background jobs
- Cache eviction: least-recently-used asset tracking, evict from GPU memory on budget pressure

### Sub-stage 8.8: Asset Extraction Tool
- Write a standalone CLI tool (`tools/extract_assets/`) that:
  - Reads from an official EVE client installation
  - Extracts all needed assets
  - Converts to a format our client can natively load
  - Outputs a `resources/` directory with the converted assets
- This tool will be shipped as a separate utility (users need their own EVE install)
- Never include extracted output in source control, CI artifacts, installers, or
  release packages

### Gate / Deliverable
- Real EVE ship models render in space
- Real textures display correctly
- Resource manager works with real official client data
- Extraction tool documented and tested

---

## STAGE 9 — Input, Window & Audio

### Objective
Full input handling, window management, and audio system.

### Sub-stage 9.1: Input System
- Study Carbon's input model (from exefile Windows code)
- Implement:
  - Keyboard state tracking (key down/up/repeat)
  - Mouse state tracking (position, buttons, scroll wheel)
  - Raw input (for high-DPI mice)
  - Gamepad/joystick support (360/One/Xbox)
- Implement key binding system (configurable controls)
- Expose input state to Python
- Test: Python script can read mouse position and key states

### Sub-stage 9.2: Window Management
- Implement window creation (using Carbon patterns or SDL2):
  - Fullscreen, windowed, borderless windowed modes
  - Resolution switching
  - Multi-monitor support
  - VSync control
  - Window icon (from EVE resources)
- Implement UI scaling (DPI awareness)
- Implement focus handling (pause when minimized)

### Sub-stage 9.3: Audio System Integration
- Integrate carbonengine/audio:
  - Set up Wwise SDK (2025.1.5.9095 as specified)
  - Audit Wwise and Carbon Audio thread-affinity/callback requirements before
    assigning an owner
  - Implement audio listener (attached to camera position)
- Audio command buffer pattern:
  - Main thread (Python) submits audio commands: `PlaySound`, `SetListener`, `UpdateSource`
  - Use inline dispatch when main is the audio owner; otherwise a bounded SPSC
    channel feeds an independently progressing audio owner
  - Coalesce only idempotent parameter updates; never drop ordered play/stop
    events without an explicit policy
  - Add a permanent audio thread only if Wwise affinity or measured blocking
    justifies it
- Load Wwise sound banks through shared asset jobs, then publish owned data to
  the selected audio owner
- Implement sound prioritization (Carbon's custom system)
- Test: play a ship engine sound, pan with camera rotation

### Sub-stage 9.4: Spatial Audio
- Implement 3D audio positioning:
  - Position sound sources in 3D space
  - Distance attenuation
  - Doppler effect (for ships passing by)
- Integrate with Wwise spatial audio
- Implement the spatial audio clustering system (from `spatial-audio-clustering` repo)

### Sub-stage 9.5: UI Audio
- Implement UI sound triggers:
  - Button click/hover
  - Window open/close
  - Notification sounds
- Connect to Python UI events
- Test: click a button, hear a click sound

### Gate / Deliverable
- Full input handling (kb+mouse+gamepad)
- Window can be resized, full-screened, minimized
- Audio plays: ambient, ship, UI sounds
- Sound follows camera movement

---

## STAGE 10 — Multicore Game Loop & World State

### Objective
Build a deadline-driven game loop around one bounded worker executor, explicit
owners, stable phase boundaries, and supervised shutdown. Success is lower
frame latency and headroom under a reproducible workload, not a fixed thread
count or maximum CPU utilization.

### Sub-stage 10.1: Parallel Frame Loop Design
- Define owner roles, not hard-coded thread numbers:

| Role | Initial responsibility |
|------|------------------------|
| Main/platform/Python owner | Window events, input, Python, ECS structural commit, Python callbacks |
| Taskflow executor | Fixed-step helpers, culling, animation, particles, pathfinding, asset decode |
| Renderer coordinator | Render graph, GPU queue submission, presentation, GPU lifetime |
| I/O completion owner | Socket completions, framing, lightweight decode, connection state |
| Audio owner | Only when Carbon Audio/Wwise affinity requires one |

- Create one process-wide `ThreadBudget`
- Discover logical processors and CPU sets available to this process
- Include Carbon/oneTBB, Blue, OS thread-pool, driver, and SDK workers in that
  budget; configure or reserve headroom for pools Taskflow does not own
- Reserve true affinity owners, then construct Taskflow with an explicit count
- Benchmark worker counts instead of assuming every logical processor is a
  profitable worker
- Use these main-owner phases:
  1. Poll platform input and drain a bounded amount of network work
  2. Deliver native worker completions to Python
  3. Run the Python/tasklet phase
  4. Commit server and Python structural journals to the ECS
  5. Run required fixed steps sequentially on simulation-lane-owned state so
     each catch-up step consumes the previous step's result
  6. Commit the final simulation journal on the main owner
  7. Build the render-preparation Taskflow from the resulting stable state
  8. Run only jobs needed for the current render snapshot
  9. Merge worker journals/chunks deterministically
  10. Publish one immutable frame packet to a free generation slot
  11. Send ordered network/audio commands through their bounded channels
- I/O completion and renderer progress independently; the main phase never
  waits for "last frame's I/O"
- Background jobs may span frames if their versioned results are not required
  by the current packet

### Sub-stage 10.2: Thread Synchronization & Pacing
- Pace CPU publication with bounded frame slots, not a direct
  render-to-main-vsync atomic flag
- Block the producer only when no frame slot is free; make low-latency/drop
  policy configurable and never drop durable upload/destruction requests
- If main also owns the renderer, render/consume inline instead of blocking on
  a frame channel whose consumer is the same thread
- Use generation-tagged slot states:
  `Free → Writing → Published → Reading → Free`
- Publish with release semantics and acquire before reading
- Do not reset frame allocators until every consumer releases that generation
- Determine Destiny's fixed simulation rate from the pinned server/protocol
  audit; do not guess 10, 30, or 60 Hz
- Use an accumulator and fixed-size simulation steps, preserve previous/current
  snapshots for render interpolation, and cap catch-up steps
- On catch-up overflow, record overload and resynchronize prediction from
  authoritative state instead of silently skipping arbitrary simulation time
- Define runtime states:
  `Starting → Running → StopRequested → Draining → Stopped/Failed`
- Reject new work after `StopRequested`, use cooperative stop tokens, consume
  every future/error, and never block a Taskflow worker on nested same-executor
  work
- Stop external producers first, cancel/drain I/O, and finish required compute
- Quiesce renderer/audio/resource services, drain Python callbacks, and
  finalize Python or explicitly invalidate every wrapper while all binding
  dependencies still exist; destroy ECS/renderer/audio/resources afterward
- Test stalls, full channels, long frames, exceptions, cancellation at each
  phase, minimized windows, and shutdown with work in flight

### Sub-stage 10.3: Server-Driven World State in EnTT
- Derive entity lifecycle from verified base protocol messages first
- If the proposed relevancy extension is negotiated, map its versioned
  `EntityEnter`, `EntityLeave`, `EntityDelta`, and `RelevancySnapshot` semantics
  to the same internal command types
- Maintain a checked mapping from server object ID and connection generation to
  local EnTT entity and entity version
- Keep ECS structural mutation on the main owner:
  - Enter creates an entity and required components at the commit phase
  - Leave invalidates the server mapping immediately and optionally leaves a
    separate visual-fade entity before final destruction
  - Snapshot atomically replaces one server generation
  - Delta applies only to a matching baseline, sequence, connection, and entity
    generation
- Never infer client authority from ECS ownership. The server remains
  authoritative for gameplay state; ECS is the client's current replica.
- Implement interpolation from server timestamps and measured update cadence
- Bound extrapolation time and visibly recover from stale data; do not couple
  networking correctness to visual LOD
- Implement bulk updates correctly:
  1. Decode into typed `PositionDelta` records
  2. Validate connection generation, baseline, and sequence
  3. Resolve server ID to local entity plus generation
  4. Reject stale, missing, duplicate, or reused mappings by protocol policy
  5. Sort/partition by component storage index where profitable
  6. Apply scatter writes or genuinely contiguous runs
- Reserve `memcpy` for homogeneous, already-resolved contiguous spans. A record
  containing interleaved IDs and values cannot be copied directly into an EnTT
  component pool.
- Keep structural enter/leave work separate from value updates
- Benchmark a simple reference implementation against bulk paths using
  randomized entity order, destruction/reuse, duplicates, missing IDs, and
  packet order; remove the unsupported 25 ms/7 ms claims
- Let jobs read stable component pools during a declared phase. Copy snapshots
  only when lifetime or transformation requirements justify their cost.

### Sub-stage 10.4: Destiny Fixed-Step Task Lane
- Audit Destiny's actual ownership, globals, callbacks, allocators, and server
  reconciliation contract before changing its state model
- Start Destiny as a single-concurrency task lane on the shared executor
- Run it at the verified fixed simulation rate, not an assumed render rate
- Feed a stable input view and emit a private result journal for main-owner
  commit
- Parallelize only proven independent helpers such as broadphase partitions or
  versioned path queries; merge in deterministic order
- Discard stale asynchronous results by request/entity generation
- Add a permanent physics thread only if profiling demonstrates a benefit after
  accounting for context switches and the process-wide CPU budget
- Test deterministic replay, long-frame catch-up, authoritative correction,
  entity destruction/reuse, and shutdown while a task is active

### Sub-stage 10.5: I/O Completion Integration
- Use the Stage 4 completion-oriented socket runtime
- Raw IOCP with exactly one dequeuer, or an explicitly serialized
  per-connection strand, may publish through SPSC
- Overlapping Windows thread-pool I/O callbacks are multiple producers and
  require MPSC handoff plus sequence-based ordering
- Give every operation an owned `OVERLAPPED`, buffer, operation type,
  connection generation, and sequence; retain it through cancellation
  completion
- Handle partial send/receive, zero-byte completion, reconnect generations,
  framing across buffers, and stale completion delivery
- Use a bounded SPSC incoming channel only when the implementation proves one
  serialized producer. Use bounded SPSC outgoing while main remains the sole
  producer; change topology when a real additional producer is introduced.
- Bound incoming work by encoded bytes, decoded bytes, message count, and
  oldest-message age; drain to a per-frame budget without waiting for I/O
- Coalesce only absolute latest-wins state explicitly declared safe by the
  protocol. Never drop deltas, authentication, lifecycle, inventory, or other
  ordered state transitions.
- On unrecoverable overflow, request a full versioned snapshot if supported or
  reconnect. Throttling outgoing sends does not solve incoming overflow.
- Keep machoNet AES session handling here. The audited game socket is not TLS;
  HTTPS/TLS endpoints remain separate services.
- Treat all packet ceilings as benchmark outputs, not architectural guarantees

### Sub-stage 10.6: Worker Job Scheduling
- Build the per-frame CPU graph from declared data access:
  1. **Culling jobs** (`ParallelFor` over all `RenderComponent` ranges):
     - Frustum cull (camera view frustum)
     - Distance cull (beyond draw distance)
     - Occlusion cull (using last frame's depth)
  2. **Animation jobs** (`ParallelFor` over ships with skeletons):
     - Skin/LOD blending
     - Procedural animation (engine gimbal, turret tracking)
  3. **Particle jobs** (`ParallelFor` over active particle systems):
     - Update positions, velocities, lifetimes
     - Emit new particles
     - Kill expired particles
  4. **LOD jobs** (`ParallelFor` over all meshes):
     - Compute screen-space error
     - Select LOD level
     - Queue LOD transition
  5. **Physics helper jobs** (`ParallelFor` over broadphase collision pairs):
     - Narrowphase collision detection
     - Contact generation
- Require every node to declare component/resource reads and writes
- Reject undeclared read/write and write/write conflicts
- Give each worker private output chunks and journals; never let workers append
  concurrently to a channel described as SPSC
- Merge visible draw chunks in deterministic graph/task order into one immutable
  frame packet
- Wait only for jobs required by the current packet. Asset, pathfinding, and
  other versioned background work may complete in a later frame.
- Keep Taskflow's CPU dependencies separate from the Vulkan render graph's GPU
  resource dependencies
- Choose grain size from measurements; tiny per-entity tasks can cost more than
  serial iteration

### Sub-stage 10.7: State Synchronization & Prediction
- Implement client-side prediction only for protocol behavior verified safe to
  predict
- Tag input history, predicted state, and server snapshots with monotonic
  sequence/tick identifiers
- On authoritative updates, replay valid pending inputs where the protocol
  permits and correct visual error with a bounded policy
- Show speculative module/UI feedback distinctly and roll it back on rejection
- Implement reconciliation:
  - Server is authoritative for: position, velocity, damage, modules, items
  - Client is authoritative for: camera, UI state, input, settings
- Never apply an update to an entity ID whose local generation no longer
  matches
- Test delayed, reordered, duplicated, lost, and corrected messages against a
  deterministic reference model

### Sub-stage 10.8: Debug & Profiling Overlay
- Build per-thread profiler overlay:
  - Tracy integration for all threads
  - In-game display: frame time breakdown per thread
  - Worker count, utilization, context switches, and runnable/blocked time
  - Queue capacity/depth in messages and bytes, oldest age, and overflows
  - Task graph visualization (DAG of current frame's jobs)
- Build thread state viewer:
  - What each thread is currently doing
  - How many jobs pending in queue
  - Frame-slot, future, semaphore, queue, and GPU timeline wait times
- Build latency overlay:
  - Server→client ping
  - Input→render latency (frames from click to pixel)
  - Simulation→render state propagation delay
- Export raw benchmark/trace metadata with commit, CPU/GPU, driver, build,
  resolution, present mode, workload, and configuration

### Sub-stage 10.9: Grid Subdivision Manager ★

This is a separate AGPL server-side workstream. Audit its existing worker and
broadcast bridge before adding another worker architecture. The base client
must not depend on this extension.

- Replay representative movement, fleet, warp, targeting, and event traces
- Compare fixed grids, hashed grids, adaptive octrees, and other interest
  structures using candidate pairs, encoded bytes, boundary duplication,
  update latency, migration cost, and memory
- Select cell sizes, split/merge thresholds, depth, hysteresis, and update rates
  from measured deadline budgets rather than the current 100 km/500-ship
  placeholders
- Define one authoritative owner for each simulation entity at every tick
- Define cross-owner messages with generation, sequence, capacity,
  backpressure, acknowledgment, retry/failure, and deterministic ordering
- Use a two-phase migration so an entity is never simultaneously absent or
  authoritative in two workers
- Keep simulation correctness independent from slower presentation/relevancy
  updates; per-cell time dilation needs explicit cross-cell semantics
- Negotiate a versioned client extension with baseline/delta generations and a
  full snapshot recovery path
- Test non-uniform clustering, all entities in one leaf, boundary thrashing,
  warp bursts, split/merge oscillation, worker crash/restart, delayed messages,
  duplicate delivery, and snapshot recovery
- Use 100K entities only as a stress scenario. Report hardware, distribution,
  event rate, encoded bytes, p50/p95/p99 tick latency, missed deadlines, and
  queue depth; do not convert it into an unsupported client FPS claim.

### Gate / Deliverable
- Explicit thread budget prevents oversubscription at tested CPU counts
- Ownership assertions, deterministic journal merge, bounded channels, and
  supervised shutdown pass stress and fault-injection tests
- ECS handles the documented workload with frame-time headroom on named
  reference hardware; timing percentiles replace fixed unmeasured limits
- Client connects to the approved server-side interface and maintains world
  state sync with interpolation
- Two clients see authoritative movement and recover from induced reorder,
  reconnect, overflow, and stale generations
- Debug overlay exposes CPU task and GPU render graphs, waits, queues, and
  input-to-present latency
- Base gate passes without the optional grid extension; extension tests become
  mandatory only when that capability is negotiated

---

## STAGE 11 — Login, Auth & Connection Flow

### Objective
The full user journey from launch to in-game space.

### Sub-stage 11.1: Launcher Application
- Create a lightweight launcher application:
  - No rendering (console or simple UI)
  - Pinned localhost server profile (`127.0.0.1`) for v0.12.3.1
  - User registration (if not already done)
  - Authentication
  - Patching (if we implement updates)
- Use the same auth flow from Stage 4
- The launcher starts the game with server/profile configuration. Pass an
  authentication artifact only if the pinned protocol defines one, and use
  protected IPC rather than exposing credentials/tokens on the command line.
- Do not expose remote/LAN configuration until the external service has a
  separate hardened
  threat model, secure transport, authentication, and deployment gate

### Sub-stage 11.2: Login Screen
- Render EVE's login screen:
  - Character model display (rotating ship or avatar)
  - Username/password fields
  - "Log In" button
  - Error message display
- Use the UI framework from Stage 7
- Enter the Stage 4 machoNet authentication state machine; do not assume a
  separate HTTP auth endpoint
- Handle: success, wrong password, account banned, server offline

### Sub-stage 11.3: Character Select
- Render the character selection screen:
  - Character name, race, bloodline, portrait
  - "Enter World" button
  - "Create Character" button
- Fetch the character list from the approved server-side interface
- Display character information
- Handle: no characters, character creation flow

### Sub-stage 11.4: World Entry Sequence
- Implement the transition from character select to in-game:
  1. Send character selection to server
  2. Receive session initialization data
  3. Load initial resources (station interior or space scene)
  4. Mount solar system data
  5. Create player entity (ship)
  6. Set initial camera
  7. Transition UI from loading screen to HUD
- Handle loading progress display

### Sub-stage 11.5: Station Environment
- If the player spawns in a station:
  - Render station interior (or a placeholder scene)
  - Display station services UI (fitting, market, hangar)
  - Implement dock/undock transition
- This requires: station model loading, station interior rendering, station UI screens

### Sub-stage 11.6: Error Handling & Recovery
- Handle network disconnections:
  - Detect timeout
  - Show "connection lost" dialog
  - Attempt reconnection
  - Resume state on reconnect
- Handle server shutdown gracefully
- Handle invalid state transitions

### Gate / Deliverable
- Full flow: launch → login → character select → enter world → in space
- All error states handled
- Reconnection works
- Station environment renders (or placeholder)

---

## STAGE 12 — Basic In-Game Rendering

### Objective
Render a full solar system with ships, stations, effects, and the EVE HUD.

### Sub-stage 12.1: Solar System Rendering
- Render all solar system objects:
  - Sun (emissive sphere with glow)
  - Planets (textured spheres)
  - Moons (smaller textured spheres)
  - Asteroid belts (particle system distribution)
  - Gates (model rendering)
  - Stations (model rendering)
  - Warp lines (dynamic lines)
- Load real system data from the approved server-side interface (or SDE)
- Position objects correctly using astronomical data (AU scale)

### Sub-stage 12.2: Ship Rendering
- Render player ship:
  - Model (LOD0 for close, LOD1+ for distance)
  - Textures (diffuse, normal, specular, emissive)
  - Engine trails (particle system)
  - Shield effect (glow/hull shader)
  - Ship name label (UI billboard)
- Render other ships:
  - Same as player, but simpler LOD
  - Color coding by corporation/alliance
  - Distance-based culling

### Sub-stage 12.3: Effect System
- Implement particle system:
  - Engine exhaust (trails)
  - Explosions (sprites + mesh debris)
  - Shield impacts (sparks)
  - Warp effects (stretch/tunnel)
- Implement shader effects:
  - Shield bubbles (fresnel glow)
  - Module activation (pulse highlight)
  - Target lock (crosshair + brackets)
  - Cynosural field

### Sub-stage 12.4: Bracket & Label Rendering ★

Brackets and the overview carry critical tactical information. Under measured
load, bracket-only presentation can replace optional 3D detail for selected
entities without changing authoritative gameplay state.

- Implement the overview bracket system:
  - Brackets (square brackets around objects)
  - Labels (object name, distance, type)
  - Icons (object type icon)
- Implement the overview panel (list of objects with sort/filter):
  - This is the most important UI in EVE
  - Maintain a versioned compatibility corpus for required sort, filter,
    selection, keyboard, and update behavior
- **Relevancy and presentation tiers:**
  - Select full, compact, icon-only, or hidden presentation from gameplay
    importance, projected size, distance, user filters, and measured budgets
  - Do not expose server partition cells as a required visual LOD model
  - Fleet members, targets, and other critical entities follow verified
    gameplay rules and remain visible when required
- **Bracket-only mode (the "blob shadow" equivalent for EVE):**
  - Start with >500 non-critical visible ships as a tunable hypothesis; select
    the final transition from frame-time budgets and user testing
  - No 3D mesh, no textures, no effects, no particles for any ship marked "bracket-only."
  - Player's own ship + current locked targets + fleet members stay at full 3D LOD0.
  - Batch compatible instances, but expect CPU preparation, uploads, vertices,
    glyphs, clipping, and fragment cost to grow with visible count
  - User-toggleable: "Tactical Overlay" button forces bracket mode on/off regardless of ship count.
  - Use a configurable, tested cross-fade when changing 3D/bracket policy
- **Effect culling in bracket mode:**
  - All non-fleet/non-target particle effects, engine trails, shield effects, module glows hidden.
  - Fleet effects visible but throttled to 50% particle count.
  - Target effects fully visible.
  - Explosion effects (ship deaths) always visible regardless of mode (important gameplay feedback).
- Implement bracket visibility (distance fade, occluded vs visible)
- **Bracket batching for GPU:**
  - Batch brackets by compatible pipeline, texture/font atlas, blend, and clip
    state; one draw call is not a correctness requirement
  - Benchmark 2,000+ brackets with realistic glyph, overlap, and clipping data
  - Cache stable shaped text runs; update dynamic distance and screen position
    data without rebuilding unchanged names/corporation text

### Sub-stage 12.5: HUD Rendering
- Implement HUD elements:
  - Ship HP/shield/armor/structure bars
  - Capacitor display
  - Speed/target speed
  - Module slots (high, med, low)
  - Selected item info
  - System name
  - Timer display
- These are rendered as UI overlay on the 3D scene

### Sub-stage 12.6: Targeting System
- Implement target lock visual:
  - Target bracket
  - Target info window (name, distance, velocity, HP)
  - Lock indicator (progress ring)
- Render target lines (line from ship to target)

### Sub-stage 12.7: Performance Optimization Pass
- Implement:
  - Object culling (frustum, distance, occlusion) — runs on worker threads via job system
  - LOD switching — computed in parallel on worker jobs
  - Draw-list batching — workers build private visible chunks; renderer merges
    them in deterministic render-graph order
  - Instancing for asteroid belts — GPU instanced rendering, setup in parallel
  - Render target management
  - GPU particle systems — compute shader updates with explicit CPU dispatch,
    resource lifetime, synchronization, and fallback paths
- Profile and optimize against a documented frame-time budget:
  - Use debug overlay from Stage 10.8 to identify per-thread bottlenecks
  - Attribute waits and remove avoidable critical-path stalls; do not require
    zero blocking or an arbitrary sub-0.1ms wait
  - Preserve GPU headroom and input latency; >90% utilization is not a goal in
    a vsync-limited or latency-sensitive client

### Gate / Deliverable
- Full solar system renders with planets, belts, gates, stations
- Ships render with effects
- Overview brackets work
- HUD displays ship status
- Targeting system works
- The reference scene meets its named hardware/resolution p95/p99 frame-time
  budget with input-latency headroom

---

## STAGE 13 — Feature Implementation Rounds

### Objective
Implement all major EVE gameplay features in the client, round by round.

### Multicore Context
All feature work in this stage operates on the multicore foundation from Stage 10:
- **ECS** is the current client-side replica; the server-side implementation
  remains authoritative for
  gameplay state. New features add only justified component types.
- **Job system** runs only work with declared safe dependencies in parallel;
  serial work remains serial when ownership or workload requires it.
- **Bounded channels** carry immutable frame packets and ordered audio/network
  commands. Python calls typed owner-thread bindings that publish native data.
- **Python** (main owner) orchestrates logic, reads input, and submits ECS
  commands. Only dependency-safe native work runs on workers; ECS structural
  commits, renderer submission, and affinity-bound work stay with their owner.

### Round 13.1: Ship Controls
- Movement:
  - Orbit at distance
  - Approach
  - Keep at range
  - Align to
  - Warp to (zero, 100km, at range)
  - Dock / Jump
  - Stop
- Module control:
  - Activate/deactivate modules
  - Toggle modules
  - Overload modules
- UI integration:
  - Ship controls panel (movement buttons)
  - Module hotbar / rack display
  - Selected item actions

### Round 13.2: Cargo, Inventory & Fitting

**AUTHORITY:** The server-side implementation owns container, inventory, cargo,
hangar, and fitting
decisions. The client maintains a versioned replica, pending presentation, and
UI state, but never authoritatively validates or arbitrates item movement.

#### 13.2a: Server-Side Prerequisites
The audited server-side build already contains inventory/fitting handlers. Before
client work, run conformance tests for the exact required behavior and record
gaps rather than assuming the subsystem is absent:

- Container state, ownership, capacity, hierarchy, and permissions
- Stack/move operations and failure/rollback semantics
- Fitting slot, CPU, power-grid, calibration, skill, and ownership validation
- Notifications or refresh semantics for externally changed contents
- Atomicity, idempotency, duplicate request, reconnect, and concurrent-client
  behavior

#### 13.2b: Existing RPC Contract (documented in Stage 4)
Use actual pinned server-side service/method names and payloads. The audited handler
inventory includes `GetContainerContents`, `GetInventory`, `GetInventoryFromId`,
`Add`, `MultiAdd`, `MultiMerge`, `StackAll`, `FitFitting`, `StripFitting`, and
`DestroyFitting`. Do not invent `*Request/Response` message names.

- Document request/return/error schemas and server notifications for each flow
- Add typed client commands without changing wire names
- If a required notification is missing, version it as an explicit server
  extension and add snapshot recovery

#### 13.2c: Client-Side Implementation (View Layer Only)
- Cargo window UI:
  - Item listing with icons, names, quantities from the documented inventory
    query result
  - Drag/drop invokes the documented move RPC; authoritative item
    state updates only from the server response/snapshot
  - No client-side stack splitting or validation — all logic is server-authoritative
  - Optional optimistic presentation is a separate pending visual state, never
    an authoritative ECS mutation; clear or revert it on response/timeout
- Ship fitting window UI:
  - Slot display (high/med/low/rig) populated from server
  - Drag from cargo to slot invokes the documented fitting RPC; server validates
    and responds
  - Stats display (CPU, powergrid, calibration) — read-only, server-computed
  - Fitting requirements shown as server-provided metadata
- Hangar access UI:
  - Ship hangar list populated from server
  - Item hangar list populated from server
  - Corporate hangar with access control (server-enforced)
  - In-space container access: jetcan, cargo container, wreck — all server-authoritative
  - Container tree/hierarchy rendered from server-provided structure

#### 13.2d: Client Inventory Model
- Keep versioned immutable inventory/container snapshots in the inventory
  feature model; this UI/domain data does not automatically belong in the
  per-frame world ECS
- An ECS entity may carry a lightweight inventory handle/version only when a
  world system needs it
- Do not represent every item as an ECS entity without a measured world-system
  access requirement
- Authoritative snapshots update only from server results/notifications;
  pending optimistic presentation remains separate

### Round 13.3: Market & Economy
- Implement market window:
  - Item search
  - Buy/sell orders display
  - Price history chart
  - Order creation (buy/sell)
  - Order management (modify, cancel)
- Implement wallet:
  - Balance display
  - Transaction log
  - Journal
- Note: The market service is a separate process in the server-side deployment

### Round 13.4: Chat & Social
- Implement chat system:
  - Channel list (local, corp, alliance, fleet)
  - Message display with timestamps
  - Text input
  - Tab completion for names
  - Channel join/create/leave
- Implement contacts:
  - Contact list (friends, enemies, watchlist)
  - Add/remove contacts
  - Standing display
- Implement fleet:
  - Fleet window (members, wings, squads)
  - Fleet broadcast
  - Fleet history

### Round 13.5: Scanning & Probing
- Implement the scanner:
  - Directional scanner
  - Combat scanner probes
  - Scan result display
  - Probe control (launch, move, recall)
  - Scan strength / deviation display
- Implement the probe UI:
  - System map overlay
  - Probe range spheres
  - Result signatures

### Round 13.6: Agent Missions & NPC Interaction
- Implement agent interaction:
  - Agent window (dialog, mission description)
  - Mission objective display
  - Mission tracking
  - Reward display
- Implement NPC interaction:
  - NPC dialogue
  - Bounty payout
  - Security status changes

### Gate / Deliverable
- All major EVE features work in the client
- Feature audit against the server-side implementation status
- Comprehensive bug tracking for each feature

---

## STAGE 14 — Localization, Modding & Tooling

### Objective
Support multiple languages, modding API, and developer tooling.

### Sub-stage 14.1: Localization System
- Integrate carbonengine/localization
- Extract EVE's localization data from the official client
- Implement locale switching (language menu)
- Support: English, German, French, Russian, Japanese, Chinese, Korean
- Test: switch to German, verify all UI text changes

### Sub-stage 14.2: Modding API
- Design a plugin system:
  - C++ plugin DLL loading
  - Python plugin loading
  - Hook points: pre-render, post-render, pre-update, post-update, packet intercept
  - Plugin manifest format
- Document the modding API
- Example plugin: "Show extra ship stats"

### Sub-stage 14.3: Developer Console
- Implement an in-game developer console:
  - `~` to open
  - Python REPL
  - Commands: `fps`, `objects`, `tp [system]`, `spawn [type]`, `reloadui`
  - Command history
  - Autocomplete
- Implement a Python remote debugger (connect with pdb or similar)

### Sub-stage 14.4: Overlay & Performance Tools
- Implement:
  - Frame-time graph (CPU/GPU timing breakdown)
  - Memory profiler
  - Network monitor (packet log viewer)
  - GPU profiler (Vulkan timestamp queries; calibrated timestamps optional)
  - Resource browser (view loaded textures, models, materials)
  - Scene explorer (browse all world objects)

### Gate / Deliverable
- Localization works for all major languages
- Plugin system with example plugins
- Dev console with Python REPL
- Performance overlay tools

---

## STAGE 15 — Testing Infrastructure

### Objective
Comprehensive testing: unit, integration, protocol conformance, and performance.

### Sub-stage 15.1: Unit, Race, and Lifetime Tests
- Write unit tests for all modules:
  - `jobs`: task scheduling, work stealing, DAG execution, contention
  - `ecs`: entity creation, component add/remove, parallel view, command buffer
  - `threading`: each selected channel topology, generations, frame slots,
    wake-up, overflow, teardown, and scratch allocators
  - `core`: thread pool, file system, memory allocation
  - `scheduler`: tasklet lifecycle, channel operations
  - `blue`: Python owner assertions, callbacks, finalization, resource loading
  - `trinity`: SPIR-V, capability selection, render graph, resource lifecycle,
    swapchain transitions, device-loss control flow
  - `destiny`: fixed-step lane, deterministic replay, stale path results
  - `network`: framing/marshal, state machine, cancellation, reconnect generation
  - `ui`: widget layout, event routing, input handling
- Run portable engine-core concurrency tests on Linux with Clang
  `-fsanitize=thread -g -O1`
- Windows is not listed as a supported Clang TSan target; keep MSVC Windows
  lanes for platform, Vulkan WSI, IOCP, Carbon, and integration coverage
- TSan detects races only in exercised instrumented code. A clean run is not
  proof of race or deadlock freedom, and lock contention is a profiling metric,
  not a TSan correctness error.
- Add ASan/UBSan where supported, Vulkan validation and synchronization
  validation, watchdogs, fault injection, and race regression tests
- Stress tests:
  - Randomized supported worker counts submitting mixed task graphs
  - Declared disjoint ECS writes plus conflicting-graph rejection
  - Stalled producers/consumers, full channels, wraparound, shutdown, and
    object destruction/recreation
  - Repeated seeds with deterministic world-state hashes
- Use Google Test for C++, pytest for Python
- Target 80%+ line coverage on Ithax-owned core modules and no sanitizer or
  validation reports in the exercised suites

### Sub-stage 15.2: Protocol Conformance Tests
- Write a reference test suite:
  - Connect, authenticate, send each message type
  - Verify server responds correctly
  - Verify malformed packets are rejected
  - Test session timeout and renewal
- Run against the pinned server-side reference nightly
- Generate a protocol conformance report

### Sub-stage 15.3: Integration Tests
- Write integration tests:
  - Boot sequence (start to login screen)
  - Login flow (success, failure, timeout)
  - Character select → world entry
  - Undock → fly → warp → dock
  - Market buy/sell cycle
  - Chat send/receive
- Use a local approved server-side instance as the test fixture
- Automate in CI

### Sub-stage 15.4: Performance Benchmarks — MULTICORE
- Write performance benchmarks:
  - Startup time (cold boot, thread init, job system warmup)
  - **Multicore scaling** (1/2/4/8/16 workers where supported for job system
    and safe ECS iteration)
  - **Critical-path phase time** (main, simulation lane, required workers, and
    renderer)
  - **I/O service latency** reported separately; include it in a frame critical
    path only when a measured frame dependency exists
  - **Job system overhead** (task dispatch latency, work stealing efficiency)
  - **Channel latency** (publish-to-consume time for frame/audio/network data)
  - Scene render (empty system, 10 ships, 100 ships, 1000 ships)
  - UI frame time (idle, heavy window with 1000 list items)
  - Network latency (ping, packet processing time)
  - Memory (idle, after 1 hour, after resource cache filled)
  - **Scheduling health** (utilization, context switches, runnable/blocked time,
    queue depth/age, frame-slot waits, and GPU timeline waits)
- Use warm-up, repeated runs, confidence/variance reporting, and p50/p95/p99
  latency. Preserve raw machine-readable results.
- Record commit, hardware, OS, driver, compiler, build flags, worker count,
  resolution, present mode, scene, entity distribution, and server workload
- Track stable benchmarks in CI but run GPU/driver benchmarks on controlled
  hardware; do not compare unrelated shared runners as regressions
- Establish budgets only after baselining. Prefer frame deadlines, missed-frame
  rate, input-to-present latency, and headroom over utilization targets.

### Sub-stage 15.5: Fuzz Testing — Multicore Safe
- Fuzz the network parser:
  - Random bytes → deserialize → check for crashes
  - Invalid packet lengths
  - Out-of-order messages
  - Duplicate sessions
- Fuzz the resource loader:
  - Corrupted texture files
  - Invalid mesh data
  - Malformed localization files
- Fuzz SPIR-V/reflection metadata boundaries and render-graph declarations
- Fuzz lifecycle sequences: resize, out-of-date, minimize, device loss,
  cancellation, reconnect, and shutdown

### Gate / Deliverable
- Every PR runs all tests designated for the portable/Windows PR tier
- Nightly server-side, scheduled fuzz, and controlled GPU-vendor lanes have
  separate
  freshness policies and must be current for release qualification
- Protocol conformance report
- Performance benchmark dashboard
- Fuzz testing runs with no crash, unbounded allocation/work, sanitizer report,
  or Vulkan validation report in the exercised corpus

---

## STAGE 16 — Packaging, Distribution & Community

### Objective
Ship a usable client to users and build a community around it.

### Sub-stage 16.1: Installer & Updates
- Create an installer:
  - Windows NSIS or MSI installer
  - Include Ithax-owned binaries, launcher, notices, and permitted runtimes only
  - Never include extracted client scripts/assets; the setup wizard creates
    user-local output from the user's installation
  - Distribute the server-side implementation only as a clearly separate AGPL
    component with complete corresponding-source/license compliance, or
    require a separate install
  - Include Wwise or other proprietary runtimes only when their redistribution
    terms explicitly permit it
- Implement an update system:
  - Version manifest (JSON)
  - Differential updates (binary diff for sections)
  - Rolling back on failure
  - Update verification (checksums + signatures)

### Sub-stage 16.2: Configuration & Settings
- Implement settings system:
  - Graphics (resolution, quality presets, vsync, anti-aliasing)
  - Audio (master volume, effects, music, voice)
  - Controls (key bindings, mouse sensitivity, camera speed)
  - Network (pinned localhost profile initially; remote settings remain gated)
  - UI (language, font size, overview settings)
- Serialize to JSON/INI
- Settings window in-game

### Sub-stage 16.3: First-Time Setup Wizard
- Implement a setup wizard:
  1. Welcome screen
  2. Where is your EVE Online installation? (for asset extraction)
  3. Run asset extraction tool
  4. Server configuration (localhost or remote)
  5. Account creation (or login if exists)
  6. Ready to play!

### Sub-stage 16.4: Documentation Site
- Create a documentation site (using Doxygen + Sphinx):
  - Architecture overview
  - Build guide
  - Developer guide
  - Modding API reference
  - Protocol specification
  - Configuration guide
  - FAQ

### Sub-stage 16.5: Community Infrastructure
- Set up:
  - GitHub Discussions for community Q&A
  - Discord server for real-time chat
  - Issue tracker with templates
  - Contribution guide (`CONTRIBUTING.md`)
  - Code of conduct
  - Feature request process
- Write onboarding guides for new contributors

### Sub-stage 16.6: Alpha Release
- First public alpha release:
  - Feature set: login, undock, fly, warp, chat, market, fitting
  - Known issues documented
  - System requirements documented
  - Installation guide
- Release channels: GitHub Releases, website download

### Sub-stage 16.7: Release Cadence
- Define release cadence:
  - Nightly builds (dev channel)
  - Weekly alpha builds
  - Monthly feature releases
- Automate release creation
- Add telemetry (opt-in) for crash reporting

### Sub-stage 16.8: Long-Term Roadmap
- Publish a public roadmap:
  - Features planned for alpha 2
  - Beta milestones
  - 1.0 criteria
- Gather community feedback and prioritize

### Gate / Deliverable
- Installable client on Windows
- Setup wizard works
- Documentation site live
- Community channels active
- Alpha release published
- Required PR lanes and the current NVIDIA/AMD/Intel hardware matrix pass for
  the release candidate

---

## Risk Register

| Risk | Probability | Impact | Mitigation |
|------|------------|--------|------------|
| **Vulkan is a new Trinity backend, not a fallback** | HIGH | CRITICAL | Complete Stage 5.0 interface/semantic inventory, backend conformance tests, SPIR-V toolchain, validation, WSI lifecycle, and hardware CI before depending on it. |
| **Trinity primary context has hidden main-thread assumptions** | HIGH | HIGH | Inventory global-context access, add ownership assertions, redirect Blue GPU preparation, and migrate renderer ownership separately from Vulkan API work. |
| **Vulkan synchronization/lifetime error** | HIGH | CRITICAL | Resource-state tracking, per-worker/per-frame pools, timeline retirement, per-swapchain-image present semaphores, synchronization validation, resize/device-loss fault tests. |
| **Driver/device variation** | MEDIUM | HIGH | Explicit capability profile and fallbacks, software-ICD tests, scheduled NVIDIA/AMD/Intel lanes, structured capability logs. |
| **Worker oversubscription** | HIGH | HIGH | One explicitly sized Taskflow executor and process-wide thread budget; benchmark worker counts and context switches. |
| **ECS ownership or journal race** | HIGH | CRITICAL | One structural owner, declared read/write sets, disjoint worker ranges, deterministic journals, Linux TSan plus randomized stress tests. |
| **Queue guarantee mismatch or unbounded growth** | MEDIUM | CRITICAL | Select by real topology; define capacity/order/backpressure/wake-up/shutdown; test saturation and stalled consumers. Do not claim moodycamel MPMC is wait-free or globally ordered. |
| **Runtime shutdown use-after-free/deadlock** | HIGH | CRITICAL | Supervised lifecycle, cooperative stop, producer-first shutdown, future error consumption, I/O cancellation completion, quiesce dependencies, finalize/invalidate Python wrappers while dependencies live, then destroy services. |
| **Python owner/GIL bottleneck** | MEDIUM | HIGH | Assert one Python owner, release the GIL only around native work that touches no Python objects, marshal native results back, profile before considering subinterpreters/free-threaded Python. |
| **Python 2.7/Stackless compatibility** | HIGH | CRITICAL | Freeze the audited script ABI, choose embed-versus-port explicitly, and maintain import/behavior conformance tests. |
| **CarbonUI/script hosting is legally or technically rejected** | MEDIUM | CRITICAL | Obtain legal review, never redistribute extracted code, keep output user-local, and estimate a genuinely separate clean-room UI fallback. |
| **Asset extraction/SDK redistribution violation** | MEDIUM | CRITICAL | Ship no extracted output; review Granny/Wwise/vendor terms and notices; scan releases and CI artifacts for prohibited content. |
| **Project license and notice compliance** | HIGH | HIGH | Keep original Ithax work under MIT, retain dependency notices, and complete legal review before distribution. |
| **Server-side AGPL compliance error** | MEDIUM | HIGH | Keep the component separate, publish modifications and corresponding source as required, and obtain legal review before bundling. |
| **Localhost-only service exposed remotely** | MEDIUM | CRITICAL | Bind the pinned server-side service to `127.0.0.1`; do not offer LAN/public settings until a separate security, TLS/AEAD, authentication, abuse, and operations review passes. |
| **Base protocol confused with proposed relevancy extension** | HIGH | HIGH | Pass source-derived machoNet conformance first; negotiate/version extensions and preserve snapshot recovery. |
| **Spatial partition hotspot or migration desync** | HIGH | CRITICAL | Replay-driven structure choice, one owner per entity, generation/sequence protocol, two-phase migration, clustered/boundary/failure tests. |
| **Unsupported performance promises** | HIGH | HIGH | Label hypotheses, record complete benchmark context and raw results, and gate on p95/p99 deadlines/headroom rather than FPS, draw-call, or utilization slogans. |

---

## Cross-Reference Sources

### Local Verified Evidence

- `BUILD_GUIDE.md` and
  `tools/vcpkg/buildtrees/carbon-trinity/config-x64-windows-debug-out.log`
  describe the last configured stub-only baseline; current vcpkg status marks
  Trinity purged.
- `cmake/overlay-ports/carbon-trinity/portfile.cmake`: feature switches list
  shader compiler, D3D11, D3D12, Metal, and Granny, but no Vulkan backend.
- The local `external/carbon/trinity` v5.0.0 comparison checkout also stops at
  D3D11, D3D12, Metal, and stub, but the pinned 4.0.2 links below are the
  authoritative implementation baseline.
- [Pinned TrinityAL build targets](https://github.com/carbonengine/trinity/blob/4675ceaaa445f7fd44a1dc97472c8efa4ad8599c/trinityal/CMakeLists.txt)
- [Pinned Trinity shader platforms](https://github.com/carbonengine/trinity/blob/4675ceaaa445f7fd44a1dc97472c8efa4ad8599c/shadercompiler/Platforms.h)
- `external/carbon/blue/src/BlueAsyncRes.cpp`: Blue already performs background
  resource work and hands GPU preparation to its main queue.
- [Pinned Trinity render-context ownership](https://github.com/carbonengine/trinity/blob/4675ceaaa445f7fd44a1dc97472c8efa4ad8599c/trinity/Tr2RenderContext.h)
- `docs/gap-analysis.md`: machoNet framing/marshal/session, Python 2.7 `.pyj`,
  CarbonUI, and ResFiles findings. Its v0.12.2 path/version and "licenses
  cleared" conclusion are stale and must be updated to v0.12.3.1.
- The pinned server-side reference: build 3396210 target and localhost-only
  security boundary.

### Vulkan Authority and Open Source

- [Vulkan Guide: versions](https://docs.vulkan.org/guide/latest/versions.html)
- [Vulkan Guide: feature queries](https://docs.vulkan.org/guide/latest/querying_extensions_features.html)
- [Vulkan Guide: threading](https://docs.vulkan.org/guide/latest/threading.html)
- [Vulkan Guide: queues](https://docs.vulkan.org/guide/latest/queues.html)
- [Vulkan Guide: WSI](https://docs.vulkan.org/guide/latest/wsi.html)
- [Vulkan validation overview](https://docs.vulkan.org/guide/latest/validation_overview.html)
- [Swapchain semaphore reuse](https://docs.vulkan.org/guide/latest/swapchain_semaphore_reuse.html)
- [Synchronization examples](https://docs.vulkan.org/guide/latest/synchronization_examples.html)
- [Dynamic rendering sample](https://docs.vulkan.org/samples/latest/samples/extensions/dynamic_rendering/README.html)
- [Vulkan portability initiative](https://docs.vulkan.org/guide/latest/portability_initiative.html)
- [Khronos Vulkan Samples](https://github.com/KhronosGroup/Vulkan-Samples)
- [Vulkan Memory Allocator usage](https://gpuopen-librariesandsdks.github.io/VulkanMemoryAllocator/html/usage_patterns.html)
- [Vulkan pipeline caches](https://docs.vulkan.org/guide/latest/pipeline_cache.html)
- [Vulkan device-fault sample](https://docs.vulkan.org/samples/latest/samples/extensions/device_fault/README.html)
- [Granite render-graph design](https://themaister.net/blog/2017/08/15/render-graphs-and-vulkan-a-deep-dive/)
- [Filament FrameGraph](https://github.com/google/filament/blob/main/filament/src/fg/FrameGraph.h)

### Multicore Authority and Open Source

- [Taskflow executor and ownership](https://taskflow.github.io/taskflow/ExecuteTaskflow.html)
- [oneTBB scheduler constraints](https://uxlfoundation.github.io/oneTBB/main/tbb_userguide/Guiding_Task_Scheduler_Execution.html)
- [EnTT entity storage and multithreading](https://github.com/skypjack/entt/blob/master/docs/md/entity.md)
- [EnTT MIT license](https://github.com/skypjack/entt/blob/master/LICENSE)
- [moodycamel ConcurrentQueue guarantees](https://github.com/cameron314/concurrentqueue)
- [bgfx internals and frame buffering](https://bkaradzic.github.io/bgfx/internals.html)
- [Wicked Engine job system](https://github.com/turanszkiji/WickedEngine/blob/master/WickedEngine/wiJobSystem.cpp)
- [Filament job system](https://github.com/google/filament/blob/main/libs/utils/src/JobSystem.cpp)
- [CPython thread-state rules](https://docs.python.org/3/c-api/threads.html)
- [Windows CPU sets](https://learn.microsoft.com/en-us/windows/win32/procthread/cpu-sets)
- [Windows I/O completion ports](https://learn.microsoft.com/en-us/windows/win32/fileio/i-o-completion-ports)
- [Windows I/O cancellation](https://learn.microsoft.com/en-us/windows/win32/api/ioapiset/nf-ioapiset-cancelioex)
- [Fixed-timestep reference](https://gafferongames.com/post/fix_your_timestep/)
- [Clang ThreadSanitizer](https://clang.llvm.org/docs/ThreadSanitizer.html)
- [Google Benchmark guidance](https://google.github.io/benchmark/user_guide.html)

---

## Immediate Next Steps (Next 30 Days)

These items preserve the stage-order rule above.

1. Package all required notices and update the gap analysis to the pinned
   server-side baseline and the authoritative pinned Trinity 4.0.2 baseline.
2. Restore dependencies from the current manifests, configure/build from a
   clean generated directory, run the full current suite (including newly
   declared targets), and label the prior 18/18 result as historical.
3. Complete Stage 1 with reproducible scripts, a checked-in CI workflow, and a
   v0.12.3.1 localhost server startup/conformance smoke test.
4. Complete the Stage 2 Python/Stackless ABI ADR and the process-wide
   `ThreadBudget` inventory covering Taskflow, oneTBB, Blue, OS, driver, and SDK
   pools. The decision records, runtime probe, and Blue/IO/Crashpad lanes are
   recorded; remaining Carbon and SDK workloads remain explicit gates.
5. Complete the Stage 2 pinned TrinityAL interface, semantic,
   primary-context, and backend-conformance matrix. The Vulkan boundary and
   initial matrix are recorded in `docs/architecture/`.
6. Write the Vulkan ADR: a new TrinityAL backend, provisional Vulkan 1.3
   profile and shader ABI, Win32 WSI/message-pump policy, and separate
   Linux/MoltenVK gates. The ADR is recorded.
7. Run reproducible Taskflow/ECS grain-size and worker-count benchmarks with
   raw results, then close the Stage 2 architecture gate. Worker and grain
   sweeps are recorded; a clean committed rerun remains a release-evidence
   requirement.
8. Extend Stage 3 with the remaining owner measurements, graph lifetime,
   deterministic journals, same-owner inline paths, bounded channels, lifecycle
   supervision, and Linux TSan/Windows stress tests.
9. Implement and conformance-test Stage 4 base machoNet marshal/session before
   designing any relevancy/grid extension.
10. Only after Stages 1-4 pass, add `BUILD_VULKAN`, the SPIR-V target, validated
    instance/device/surface selection, and the first triangle through TrinityAL.
