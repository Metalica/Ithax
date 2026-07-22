# EVE Online Open Source Client — Draft Development Plan (10x Slow)

## Connecting Carbon Engine → eve.js

---

## Scope & Landscape

### What's Open Source (Carbon Engine — github.com/carbonengine, 33 repos)

| Repo | Purpose | License |
|------|---------|---------|
| **trinity** (422★) | Rendering engine — DirectX/Metal/Vulkan abstraction, shader compiler | MIT |
| **destiny** (185★) | Game world simulation, physics, pathfinding | MIT |
| **core** (153★) | Low-level cross-platform OS abstractions | MIT |
| **scheduler** (64★) | Greenlet coroutine scheduler (Stackless-compatible) | MIT |
| **blue** (26★) | Python/C++ interop, game loop, resource loading | MIT |
| **exefile** (21★) | Executable host process, crashpad, platform init | MIT |
| **io** (48★) | Async networking (modified Python socket/ssl) | PSF-2.0 |
| **mesh** (34★) | 3D mesh, skeleton, animation serialization | MIT |
| **audio** (32★) | Wwise audio wrapper, sound prioritization | MIT |
| **resources** (23★) | Resource packaging, delivery pipeline | MIT |
| **math** (18★) | Vectors, matrices, quaternions | MIT |
| **pathfinder** (17★) | Route finding over EVE map data | MIT |
| **imageio** (16★) | Bitmap image I/O (multiple formats) | MIT |
| **localization** | Localization framework | MIT |
| **vcpkg-registry** | Dependency registry for all Carbon modules | MIT |
| — + 18 more | (audio tools, db, parser, grpc, prometheus, etc.) | MIT/Apache2 |

### What's NOT Open Source (Critical Gaps)

| Missing Component | Impact | Mitigation Strategy |
|-------------------|--------|---------------------|
| **CarbonUI** — UI framework | No UI rendering system. The UI layer (used for all EVE menus, HUD, overview, fitting windows) is either closed-source or embedded in proprietary Python scripts. | Must reverse-engineer from official client, or build a replacement UI framework (e.g., Dear ImGui or Qt-based) |
| **EVE Python game scripts** — all game logic | All actual game behavior (ship control, module activation, inventory, market, chat) is Python that loads into Blue. None of this is in the open repos. | Must either dump/parse Python bytecode from official client, or reimplement from eve.js protocol behavior |
| **Game assets** — models, textures, audio, effects | The official client has ~50 GB of art assets (ships, stations, UI textures, shaders, audio banks). These are proprietary. | Users must provide their own copy of the official client for asset extraction |
| **Perforce dependencies** (destiny, blue) | Some repos reference proprietary deps behind Perforce | Requires stubs or community-maintained alternatives |
| **Quasar/gRPC layer** (partially open) | Newer networking/protobuf event pipeline | Partially in `grpc` repo; rest needs RE |
| **Launcher & auth proxy** | The official launcher handles authentication, patching, SSO | Must build a lightweight launcher that talks to eve.js directly |
| **Economy/market systems** | Intentionally closed by Fenris (said to handle >$50M/year) | Handled by eve.js server-side; client just needs to render market UI |

### What eve.js Provides

eve.js is a Node.js server emulator (evejs.ru/vekotov/evejs, 1,191 commits, AGPL). It implements the server-side protocol that the official EVE client connects to. It handles:
- Authentication & session management
- World simulation (ships, stations, NPCs)
- Market system
- Chat
- Solar system state

The client talks to eve.js over TCP using the **machoNett** protocol (EVE's proprietary RPC protocol using Google Protocol Buffers).

---

## Core Design Decision: Multicore from Day One

Carbon Engine runs everything on a single greenlet thread. Our client **rejects this architecture**. We build a modern multicore foundation from the start, using Carbon repos as *modules* inside our own parallel engine.

### Architectural Principles

| Principle | Why |
|-----------|-----|
| **Job system, not greenlets** | Carbon's cooperative tasklets can't use multiple cores. We use a thread pool + work-stealing task scheduler. |
| **ECS for world state** | Carbon uses monolithic Python dicts for state. We use an Entity Component System with cache-friendly archetypes that worker threads can iterate in parallel. |
| **Thread-local command buffers** | Each thread produces render/audio/network commands locally. Dedicated consumer threads drain these buffers lock-free. |
| **Python stays on main thread** | GIL-bound, but all engine work (physics, culling, particles, animation) runs on workers behind it. Python sends commands via lock-free queues. |
| **Lock-free communication** | No std::mutex on hot paths. SPSC/MPMC queues for cross-thread message passing. |

### Target Multicore Architecture

```
┌──────────────────────────────────────────────────────────────────────────────┐
│                         eve-client (this project)                              │
├──────────────────────────────────────────────────────────────────────────────┤
│                                                                                │
│  ┌────────────────────────────────────────────────────────────────────────┐   │
│  │  JOB SYSTEM  (thread pool + work stealing + DAG task graph)              │   │
│  │                                                                          │   │
│  │  Thread 0 (Main)   │  Thread 1 (Render)   │  Thread 2 (Physics)        │   │
│  │  ┌──────────────┐  │  ┌─────────────────┐  │  ┌──────────────────────┐  │   │
│  │  │ Python (Blue)│  │  │ Trinity          │  │  │ Destiny              │  │   │
│  │  │ Game logic   │  │  │ Draw calls       │  │  │ Ship movement        │  │   │
│  │  │ UI scripts   │  │  │ Shader compile   │  │  │ Collision detection  │  │   │
│  │  │ Input poll   │  │  │ Swap chain       │  │  │ Pathfinding          │  │   │
│  │  │ Camera       │  │  │ Resource upload  │  │  │ Physics tick         │  │   │
│  │  └───────┬──────┘  │  └────────┬────────┘  │  └─────────┬────────────┘  │   │
│  │          │           │         │              │           │                │   │
│  │  ┌───────┴──────┐  │  ┌───────┴────────┐  │  ┌─────────┴────────────┐  │   │
│  │  │ Lock-free    │  │  │ Lock-free      │  │  │ Lock-free            │  │   │
│  │  │ cmd buffer   │  │  │ cmd buffer     │  │  │ cmd buffer           │  │   │
│  │  │→ Render cmds │  │  │→ GPU uploads   │  │  │→ ECS mutations       │  │   │
│  │  │→ Audio cmds  │  │  │→ Frame present │  │  │→ Event emits         │  │   │
│  │  │→ Network cmds│  │  └────────────────┘  │  └──────────────────────┘  │   │
│  │  └──────────────┘  │                      │                             │   │
│  │                     │                      │                             │   │
│  │  Thread 3 (IO)     │  Thread 4 (Assets)   │  Thread 5..N (Workers)      │   │
│  │  ┌──────────────┐  │  ┌─────────────────┐  │  ┌──────────────────────┐  │   │
│  │  │ Socket TCP   │  │  │ Stuffit extract  │  │  │ Animation blending  │  │   │
│  │  │ Packet parse │  │  │ Texture decode   │  │  │ Particle updates    │  │   │
│  │  │ machoNett    │  │  │ Mesh convert     │  │  │ Frustum culling     │  │   │
│  │  │ Auth flow    │  │  │ DDS load         │  │  │ LOD computation     │  │   │
│  │  │ Heartbeat    │  │  │ Sound bank load  │  │  │ Occlusion testing   │  │   │
│  │  └──────────────┘  │  └─────────────────┘  │  │ SDF gen              │  │   │
│  │                     │                      │  │ Parallel-for loops   │  │   │
│  │                     │                      │  └──────────────────────┘  │   │
│  └──────────┬──────────┴──────────┬───────────┴──────────────────────────┘   │
│             │                     │                                           │
│  ┌──────────┴─────────────────────┴──────────────────────────────────────┐   │
│  │  ECS (Entity Component System) — cache-friendly, parallel-iterable     │   │
│  │  - Ships, modules, bullets, effects, asteroids = entities + components │   │
│  │  - Archetype storage = contiguous arrays per component type            │   │
│  │  - Command buffer for thread-safe mutations (no locks on hot path)     │   │
│  │  - Job system parallel_for over component arrays each frame             │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
│             │                                                               │
│  ┌──────────┴──────────────────────────────────────────────────────────┐   │
│  │  UI Framework (CarbonUI replacement)                                  │   │
│  │  - UI widgets defined on main thread (Python)                         │   │
│  │  - Render commands submitted to dedicated UI command buffer           │   │
│  │  - Consumed by render thread as overlay pass                          │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
│                                                                             │
│  Carbon Modules Used (adapted): core, math, mesh, pathfinder, imageio,      │
│  trinity (render thread), destiny (physics thread), audio (main thread),    │
│  blue (main thread), scheduler (migrated to job system), io (IO thread)     │
│                                                                             │
│  New Components: Taskflow (job system), EnTT (ECS), moodycamel::ConQ        │
└───────────────────────────┬───────────────────────────────────────────────┘
                            │
                            │ machoNett protocol (TCP + Protobuf)
                            ▼
┌──────────────────────────────────────────────────────────────────────────────┐
│                      eve.js server emulator                                   │
│  (Node.js, AGPL, evejs.ru/vekotov/evejs)                                    │
├──────────────────────────────────────────────────────────────────────────────┤
│  Network Handler → World Sim → Market Server → Database (SQLite)              │
└──────────────────────────────────────────────────────────────────────────────┘
```

---

## STAGE 0 — Gap Analysis & Complete Catalog

### Objective
Create an exhaustive inventory of what exists, what's missing, and what must be built.

### Sub-stage 0.1: Repo Audit
- Clone all 33 carbonengine repos
- For each: determine build status, dependency list, build success/failure
- Document any Perforce-gated dependencies
- Locate where CarbonUI actually lives (search all repos for UI-related headers)
- Document the `vcpkg-registry` dependency graph (produce a full DAG)

### Sub-stage 0.2: Protocol Survey
- Read eve.js source code thoroughly (especially `server/` directory)
- Document every RPC call and message type the server handles
- Map the start-up sequence: auth → char select → world entry
- Identify the crypto/encryption layer (if any)

### Sub-stage 0.3: Asset Inventory
- Download the official EVE client (free-to-play from eveonline.com)
- Catalog the asset directory structure
- Identify file formats used (.red, .black, .stuff, .blue, etc.)
- Determine which assets are essential for a minimal client

### Sub-stage 0.4: Licensing Review
- Produce a license compliance matrix for all dependencies
- Ensure eve.js AGPL license is compatible with our MIT approach (AGPL has network-use clause implications)
- Document all trademark considerations (EVE Online is a registered trademark)

### Gate / Deliverable
A document (`docs/gap-analysis.md`) that lists every component with status: **OPEN** / **CLOSED** / **NEEDS-RE** / **MUST-BUILD**.

---

## STAGE 1 — Environment & Build System

### Objective
Get a reproducible build of every Carbon component that can be built.

### Sub-stage 1.1: Toolchain Setup
- Install MSVC 2026+ (v145 toolchain as specified by audio repo)
- Install CMake 3.28+
- Install vcpkg with Windows triplets
- Install Python 3.11+ (CPython)
- Install Doxygen 1.12.0+ (for docs builds)
- Install Node.js 20+ (for eve.js)
- Install Git with LFS support

### Sub-stage 1.2: vcpkg Registry Configuration
- Clone carbonengine/vcpkg-registry
- Configure `vcpkg-configuration.json` to use both Microsoft baseline AND carbon registry
- Set `PATH_TO_VCPKG_ROOT` environment variable
- Build all ports defined in the registry
- Document any failures or missing dependencies

### Sub-stage 1.3: Build Core Module Stack
Build in dependency order, verifying each:
1. `core` — should be first, no external deps (OS abstractions)
2. `math` — pure math, no deps on core
3. `scheduler` — Python C extension, depends on Python + greenlet
4. `io` — depends on scheduler
5. `blue` — depends on core, scheduler, io
6. `mesh` — self-contained
7. `imageio` — self-contained
8. `resources` — depends on blue, core
9. `trinity` — complex, depends on core, mesh, math
10. `destiny` — may require Perforce stubs
11. `audio` — depends on Wwise SDK
12. `exefile` — depends on blue, core
13. `pathfinder` — standalone
14. `localization` — standalone
15. Remaining repos

### Sub-stage 1.4: eve.js Environment
- Clone eve.js from `https://evejs.ru/vekotov/evejs`
- Run `npm install`
- Run `StartServer.bat` — verify SDE download and DB creation
- Run `Play.bat` — verify it starts without errors
- Document the configuration format (`evejs.config.local.json`)

### Sub-stage 1.5: CI Pipeline
- Set up GitHub Actions (or similar) for:
  - Automated builds of all Carbon modules
  - eve.js server startup smoke test
  - Trivy/dependency scanning
- Create reproducible build scripts in `/scripts/`

### Gate / Deliverable
- A fully reproducible build of all buildable Carbon modules
- eve.js running locally with a working server process
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

### Sub-stage 2.3: Trinity Rendering Deep Dive
- Study `trinityal/` — the hardware abstraction layer
- Document shader compilation pipeline (shadercompiler + Yacc grammar)
- Understand the scene graph structure
- Study the effect system (how ships/effects are rendered)
- Document the draw call submission pattern
- Understand the camera and viewport management

### Sub-stage 2.4: Destiny Simulation Deep Dive
- Study the physics model (ship movement, collision detection)
- Understand the pathfinding system
- Document the simulation tick structure
- Note any Perforce-gated interfaces (these need stubs)

### Sub-stage 2.5: Resource Pipeline
- Study `resources` CLI tool
- Understand the .red/.black file format
- Study the `red-to-black-converter` tool
- Document the resource manifest format

### Sub-stage 2.6: IO & Networking Layer
- Study the machoNet packet format
- Understand how `_carbon_socket` wraps standard Python sockets
- Document the tasklet-blocking pattern
- Study SSL/TLS integration

### Sub-stage 2.7: Parallel Architecture Research
- Study open-source job systems:
  - **Taskflow** — modern C++ task graph parallelism, header-only
  - **Intel TBB** — oneAPI threading building blocks
  - **EnkiTS** — lightweight task scheduler used in games
- Study ECS implementations:
  - **EnTT** — header-only, archetype-based, used in commercial games
  - **Flecs** — ECS with relations, systems, pipeline
  - Decide: EnTT (proven in games, Apache 2.0, minimal overhead)
- Study lock-free queue implementations:
  - **moodycamel::ConcurrentQueue** — MPMC, wait-free
  - **Dmitry Vyukov's SPSC queue** — bounded lock-free
- Study rendering thread patterns:
  - Naughty Dog's "A Parallel Renderer" (GDC)
  - UE5's rendergraph / RDG design
  - Trinity's `trinityal` abstraction — can we push it onto a dedicated thread?
- Produce a parallel architecture recommendation document

### Sub-stage 2.8: eve.js Server Internals
- Study the network handler in eve.js
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

---

## STAGE 3 — Core Module Porting & Stub Creation

### Objective
Create a minimal, buildable, linkable client project that incorporates Carbon modules and stubs for missing pieces.

### Sub-stage 3.1: Monorepo Setup
- Create the `eve-client/` monorepo structure
- Decide on submodule vs subtree vs separate repo strategy for Carbon dependencies
- Set up `vcpkg.json` for the project
- Create top-level `CMakeLists.txt` that includes all needed Carbon modules
- Add external dependencies to vcpkg: Taskflow, EnTT, moodycamel::ConcurrentQueue

### Sub-stage 3.2: Job System — THE FOUNDATION ★
- This is the most critical architectural decision. Build this FIRST, before ANY Carbon module.
- Integrate **Taskflow** as the job system core:
  - Thread pool with work stealing (auto-detects core count)
  - DAG task graph (tasks with dependencies)
  - `parallel_for`, `parallel_reduce` for data parallelism
  - Async task submission from any thread
- Build a thin wrapping layer in `src/jobs/`:
  - `JobSystem` — singleton, init/shutdown, owns thread pool
  - `Job` — base class for custom job types
  - `JobHandle` — future-like handle with completion callback
  - `ParallelFor` — template for parallel iteration over ECS component arrays
  - `JobProfiler` — Tracy integration for task visualization
- Write comprehensive tests:
  - Schedule 100,000 tasks, verify all complete
  - Task with dependency chain (A→B→C→D)
  - `parallel_for` over 10M elements, compare to single-threaded speedup
  - Measure: overhead per task, queue contention, cache thrashing
- Target: <1µs task overhead, >90% CPU utilization on 8-core machine

### Sub-stage 3.3: ECS Integration ★
- Integrate **EnTT** as the entity component system:
  - Entity creation/destruction (using registry)
  - Component pools (archetype storage, cache-friendly)
  - `view()` and `group()` for parallel iteration
  - `ctx()` for shared (singleton) components
- Build a wrapping layer in `src/ecs/`:
  - `EcsWorld` — owns registry, manages entity lifecycle
  - `ComponentFactory` — runtime component registration for Python exposure
  - `EcsSerializer` — serialize/deserialize ECS state for network sync
  - `EcsCommandBuffer` — thread-safe mutation queue (deferred entity ops)
- Define initial component types (as C++ structs):
  - `ShipComponent` — position, velocity, rotation, angular vel
  - `RenderComponent` — mesh ID, material ID, LOD level
  - `PhysicsComponent` — mass, collision shape, forces
  - `ModuleComponent` — active modules, capacitor draw
  - `TargetComponent` — target ID, lock status, range
  - `NetworkComponent` — entity ID on server, dirty flags
- Write tests:
  - Create 10,000 entities, iterate all components, measure throughput
  - Parallel `view` iteration from 8 worker threads
  - Entity component add/remove stress test
  - Command buffer: submit 1,000 mutations from worker threads, verify consistency
- Target: parallel iteration of 100K entities in <1ms

### Sub-stage 3.4: Lock-Free Cross-Thread Communication ★
- Integrate moodycamel::ConcurrentQueue (MPMC) and Dmitry Vyukov's SPSC queue:
- Build in `src/threading/`:
  - `MpmcQueue<T>` — multiple producer, multiple consumer (for job results)
  - `SpscQueue<T>` — single producer, single consumer (for render commands)
  - `ScratchAllocator` — thread-local arena allocator (zero alloc on hot path)
  - `Fence` — cross-thread sync point (atomic flag, not mutex)
  - `FramePacing` — synchronize main, render, physics threads to match vsync
- Build the triple-buffered render command buffer:
  - Main thread writes to buffer 1 (current frame)
  - Render thread reads from buffer 0 (previous frame)
  - Buffer 2 is spare
  - Each frame: swap buffers, signal fence, render thread wakes
- Build the audio command buffer (same pattern)
- Build the network command buffer (same pattern)
- Test: main thread submits 10K commands/frame, render thread consumes at 60 FPS
- Measure: latency from submit to consume (target <1 frame)

### Sub-stage 3.5: Core + Scheduler Integration
- Create `src/core/` wrapping carbonengine/core
- Create `src/scheduler/` wrapping carbonengine/scheduler
- Write a simple tasklet test: spawn tasklets, send messages through channels
- Verify greenlet scheduling works correctly on Windows
- Note: scheduler will eventually be deprecated in favor of the job system; keep only for Python compatibility

### Sub-stage 3.6: Blue Minimal Integration
- Create `src/blue/` wrapping carbonengine/blue
- Build a minimal embedded Python interpreter
- Get Python to call C++ and C++ to call Python
- Create a stub game loop that runs at 60 Hz
- Test loading a simple Python script
- Key: Python runs ONLY on main thread; engine commands go through lock-free queues

### Sub-stage 3.7: Stub Perforce Dependencies
- Identify every Perforce-gated include in blue, destiny, and other repos
- Create stub headers with minimal implementations for each
- Verify the project compiles and links with stubs
- Document what real behavior the stubs should eventually have

### Sub-stage 3.8: Memory & Allocator Stubs
- Carbon has custom allocators; these may be in the Perforce-gated code
- Implement temporary `malloc`-backed stubs
- Future-proof the allocator API for replacement
- Add thread-local scratch allocators for job system workers

### Sub-stage 3.9: Logging & Diagnostics
- Carbons logging infrastructure — implement stdout/file logging
- Add Tracy or similar profiler integration with multi-thread awareness
  - Instrument all threads (main, render, physics, IO, workers)
  - Task visualization in Tracy UI
  - Lock contention hotspots
- Add assertion and crash handling
- Wrap Crashpad (from exefile repo)

### Sub-stage 3.10: Integration Smoke Test — Multicore
- Build the full project with multicore foundation
- Startup sequence: init job system → init ECS → init core → init blue → load Python → start threads → run 1000 game loop ticks with parallel jobs → shutdown
- Verify:
  - Main, render, physics, IO threads all running
  - Job system processes parallel tasks on all cores
  - ECS handles 10K+ entities
  - Lock-free queues deliver commands between threads
- Measure: speedup vs single-core, thread utilization, queue latency

### Gate / Deliverable
- A working executable with multicore job system, ECS, and thread-safe communication
- All Perforce stubs identified and created
- The project builds green on CI
- Smoke test passes: 1000 multicore ticks with no data races
- Profiler shows all cores utilized

---

## STAGE 4 — Network Protocol & Crypto

### Objective
Implement the machoNett protocol client and establish a TCP connection to eve.js.

### Sub-stage 4.1: Protocol Capture & Analysis
- Set up Wireshark or tcpdump to capture traffic between official EVE client and eve.js
- Use eve.js's own logging to correlate packets with server-side handling
- Capture the full sequence:
  1. TCP handshake
  2. Version exchange
  3. Authentication request/response
  4. Character list request
  5. Character selection
  6. World entry
  7. Solar system data
  8. A few minutes of gameplay (ship movement, chat)
- Save captures as PCAP files for reference

### Sub-stage 4.2: machoNett Protocol Parser
- From captures and eve.js source, define the packet format:
  - Length prefix (4 bytes? 8 bytes?)
  - Message type ID
  - Session ID
  - Serialized payload (Protobuf? XML? Custom Blue format?)
- Write a standalone Python tool that can parse PCAP files into readable messages
- Document each message type with its fields

### Sub-stage 4.3: Client-Side Network Stack
- Implement in `src/network/`:
  - TCP connection manager (connect, disconnect, reconnect)
  - Packet serialization/deserialization
  - Message handler registry
  - Heartbeat/keepalive
- Integrate with the tasklet scheduler (using carbonengine/io patterns)
- Test: connect to eve.js, send a version message, receive response

### Sub-stage 4.4: Authentication Flow
- Study eve.js authentication code
- Implement the client side:
  - Username/password → hash → send
  - Receive session token
  - Maintain session state
- Test: log in to eve.js, verify session active

### Sub-stage 4.5: SSL/TLS Support
- If the protocol uses SSL (the `io` repo has `_carbon_ssl`):
  - Implement certificate handling
  - Implement self-signed cert support for dev
  - Test: secure connection to eve.js

### Sub-stage 4.6: Network Unit Tests
- Write a mock eve.js server in Python/Node for testing
- Test: connect, auth, disconnect, reconnect with session restore
- Test: malformed packets, timeouts, connection drops
- Verify behavior matches the official client

### Sub-stage 4.7: Protocol Documentation
- Write complete protocol specification in `/docs/protocol/`
- Include message format diagrams
- Document error codes
- Reference captures as examples
- This becomes the reference for all future network development

### Gate / Deliverable
- Client can connect to eve.js, authenticate, and maintain a session
- Captures are reproducible on demand
- Protocol documentation is comprehensive
- Mock server supports unit testing

---

## STAGE 5 — Minimal Rendering Pipeline

### Objective
Render a 3D scene using Trinity's rendering pipeline. No UI yet — just a ship in space.

### Sub-stage 5.1: Render Thread & Command Buffer ★
- Before any rendering code, establish the render thread pattern:
  - Create dedicated render thread (Thread 1 in architecture)
  - Render command buffer (triple-buffered SPSC queue from Stage 3.4)
  - Render command types: `ClearFrame`, `DrawMesh`, `SetCamera`, `SetShader`, `Present`
  - Main thread enqueues commands, render thread dequeues and executes
- Implement fence-based frame pacing:
  - Main thread: write to buffer, signal fence, continue
  - Render thread: wait on fence, drain buffer, execute, present, swap
  - Two-frame pipeline: main is preparing frame N+1 while render is executing frame N
- Test: main thread enqueues 10K draw commands, render thread executes at 60 FPS
- Measure: command buffer latency, main thread stall time, render thread idle time

### Sub-stage 5.2: Window & Device Creation
- Implement window creation (SDL2 or GLFW, or use Carbon's own)
- Create a D3D12 device (primary target — the original game uses DirectX)
- Device creation MUST happen on the render thread (D3D12 is thread-aware)
- Implement the swap chain
- Present a clear frame (just a colored background) — via command buffer
- Integrate into the game loop

### Sub-stage 5.3: Trinity Abstraction Layer on Render Thread
- Integrate `trinityal/` (the hardware abstraction layer)
- Initialize trinityal context on the render thread (D3D12 device, command queue)
- Implement the rendering context (owned by render thread)
- Create the render state manager
- Implement resource binding (vertex buffers, index buffers, constant buffers)
- Resource creation commands submitted from main thread, consumed on render thread
- Verify: all D3D12 API calls happen exclusively on the render thread

### Sub-stage 5.4: Shader Pipeline
- Study the shadercompiler in Trinity
- Compile a minimal shader (vertex + pixel) — done on asset thread
- Upload compiled shader to render thread via command buffer
- Create the shader reflection system
- Render a single triangle (first draw call from render thread)

### Sub-stage 5.5: Mesh Loading & Rendering
- Integrate carbonengine/mesh
- Create a simple mesh format converter (from official .red/.black to a debug format)
- Or: create a procedural mesh (a simple ship shape)
- Load the mesh on asset thread, upload vertex buffers to render thread
- Render the mesh via draw call command buffer
- Add basic transformations (rotation, translation)

### Sub-stage 5.6: Camera System
- Implement a camera class (position, target, up vector)
- Camera owned by main thread (Python), but projection/view matrices shipped to render thread
- Implement orbit camera (mouse drag to rotate, scroll to zoom)
- Implement projection matrix (perspective)
- Verify: ship rotates in space, camera orbits around it
- Match EVE's camera behavior (orbit distance, rotation limits)
- Measure: camera data copy latency to render thread

### Sub-stage 5.7: Simple Space Background
- Render a starfield (particle system or skybox)
- Particle updates run on worker threads (job system parallel_for)
- Implement basic nebula color (static cubemap or procedural)
- Create the "space" scene environment
- Apply fog/atmosphere effects

### Sub-stage 5.8: GPU Upload Queue
- Implement dedicated GPU upload thread (or use IO thread):
  - Texture uploads (D3D12: staged resources, copy queue)
  - Mesh uploads (vertex/index buffers)
  - Shader bytecode upload
- Upload queue: main/asset thread submits upload commands, GPU upload thread processes
- Use D3D12 copy queue for async upload (non-blocking on render thread)
- Test: upload 100 textures asynchronously, verify no render thread stalls

### Sub-stage 5.9: Trinity Perforce Stubs
- Any Trinity components behind Perforce need stubs
- Identify missing texture loading, material system components
- Create temporary placeholder implementations

### Gate / Deliverable
- A window showing a ship model (or placeholder mesh) rotating in space
- Camera orbits with mouse — camera data flows through command buffer
- Stars and basic nebula in background — particles updated in parallel on workers
- Frame rate > 60 FPS (target: render thread never blocked by main thread)
- GPU upload queue verified async (no stalls during asset streaming)

---

## STAGE 6 — Python Runtime & Script Loading

### Objective
Load and execute EVE Online's Python game scripts. This is the hardest stage because the game logic is not open source.

### Sub-stage 6.1: Python Module Path Research
- Extract the full Python script directory from the official EVE client installation
- Document the module structure:
  - `carbon/` — engine Python bindings
  - `eve/` — game-specific scripts
  - `exefile/` — application-specific scripts
- Identify the entry point Python module
- Document all imports and dependencies

### Sub-stage 6.2: Python Bytecode Extraction
- EVE uses compiled Python (`.pyc`) files, not source (`.py`)
- Write a tool to extract and decompile `.pyc` files
- Use `uncompyle6` or `pycdc` or similar
- Document any custom CPython build flags (Stackless Python compatibility)
- Extract: method signatures, class hierarchy, function bodies

### Sub-stage 6.3: Dependence Mapping
- Create a dependency graph of all Python modules
- Identify which modules can load vs which fail due to missing C++ bindings
- Prioritize: modules needed for login → character selection → station → space
- Create a loading test: attempt to import each module and log failures

### Sub-stage 6.4: C++ Binding Stubs
- For each missing C++ binding that the Python code needs:
  1. Identify the C++ class/method signature
  2. Create a stub .cpp and .h file
  3. Expose it via Blue's `PyExpose` mechanism
  4. Verify the Python module loads without error
- Prioritize by the loading order determined in 6.3

### Sub-stage 6.5: Python Loader Sequence
- Implement the correct bootstrap sequence:
  1. Set `sys.path` to EVE's script directory
  2. Set up the `carbon` module package
  3. Initialize the `blue` Python module
  4. Initialize the `trinity` Python module
  5. Initialize the `destiny` Python module
  6. Run the startup script (`__main__` or `exefile.py`)
- Verify no import errors on startup

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
- All modules load (some with stubs)
- The game loop runs Python coroutines
- A system for logging missing bindings

---

## STAGE 7 — UI Framework

### Objective
Display the EVE user interface — this must be built from scratch since CarbonUI is not open source.

### Sub-stage 7.1: CarbonUI Research
- Search all Carbon repos for UI-related code
- Analyze the Python-side UI code in extracted scripts:
  - How are windows defined? (XML? Python objects?)
  - How are UI textures loaded?
  - How is the HUD structured?
- Determine: is CarbonUI a C++ library with Python bindings, or primarily Python?
- Document the UI element hierarchy: `Window → Container → Button/Label/Etc`

### Sub-stage 7.2: UI Backend Selection
- Evaluate options:
  - **Dear ImGui** — fast, D3D12 integration, but doesn't look like EVE's UI
  - **Custom CarbonUI reimplementation** — faithful to original but enormous effort
  - **Qt** — robust but heavy licensing concerns
  - **Hybrid**: Dear ImGui for debug/tooling, custom for in-game HUD
- Decision: use Dear ImGui for development with a CarbonUI-compatible API surface
- Layer: C++ UI API → Python exposure → Python EVE scripts call it

### Sub-stage 7.3: UI Rendering Layer
- Implement basic UI rendering:
  - Rectangle fills
  - Text rendering (TrueType font atlas)
  - Image rendering (from resource stubs)
  - Scissor rect/clipping
- Integrate with the Trinity rendering pipeline (render UI as an overlay pass)

### Sub-stage 7.4: UI Element Widget Set
- Implement basic widget types:
  - Window (moveable, resizable, closeable)
  - Button (text + background + hover/press states)
  - Label (text, alignment)
  - TextEdit (single-line, multi-line)
  - ListBox / ScrollView
  - Checkbox, RadioButton
  - Dropdown / ComboBox
  - ProgressBar
  - Slider
- Style: EVE's dark theme with blue/gold accents (match screenshots)

### Sub-stage 7.5: UI Layout System
- Implement layout containers:
  - VerticalLayout, HorizontalLayout
  - GridLayout
  - DockLayout (top, bottom, left, right, fill)
- Implement flow layout (auto-wrap)
- Implement anchor constraints (left, right, top, bottom, center)
- Test: render a simple window with buttons that matches EVE's login screen layout

### Sub-stage 7.6: Input Routing
- Route mouse events to UI elements:
  - Hover detection
  - Click handling
  - Drag support
- Route keyboard events:
  - Text input
  - Focus management (Tab order)
  - Shortcut keys
- Implement event bubbling and capture

### Sub-stage 7.7: Expose UI to Python
- Create Blue bindings for all UI widgets
- Write a simple Python script that creates a window with a button
- Load the script from Blue, run it, verify the UI appears
- Measure: UI frame time, draw call count, memory per widget

### Sub-stage 7.8: CarbonUI Compatibility Layer
- Map CarbonUI Python API calls to our implementation
- This is the critical integration step: EVE's Python scripts call CarbonUI functions
- Implement enough of the CarbonUI API to render the login screen

### Gate / Deliverable
- Python scripts can create and manage UI elements
- The login screen UI renders (even with placeholder textures)
- Mouse/keyboard input works
- UI composited on top of 3D scene

---

## STAGE 8 — Asset Pipeline

### Objective
Extract and load real game assets (models, textures, shaders, audio) from the official EVE client.

### Sub-stage 8.1: Resource Format Analysis
- Analyze official client file formats:
  - `.stuff` — main resource archive format
  - `.red` / `.black` — compiled resources
  - `.blue` — Blue-serialized objects
  - `.dds` — textures (standard DirectDraw Surface)
  - `.gr2` — models (Granny 3D format)
  - `.wwise` / `.bnk` — audio banks
- Document each format's header, sections, compression

### Sub-stage 8.2: Stuffit Archive Reader
- Implement a reader for `.stuff` archives
- Support: listing contents, extracting files, seeking
- Test: extract a known file, verify checksums
- Language: C++ with Python bindings (so Python game scripts can request resources)

### Sub-stage 8.3: Resource Index/Database
- Extract the resource manifest from the official client
- Build a local resource database (SQLite): file ID → path → type → offset → size
- Create a resource manager that:
  1. Receives a resource ID from Python
  2. Looks up the file in the database
  3. Extracts from `.stuff` archives
  4. Returns the raw data
  5. Caches frequently accessed resources

### Sub-stage 8.4: Texture Loader
- Load `.dds` textures (using carbonengine/imageio)
- Create D3D12 texture resources
- Support: BC1-BC7 compression, mipmaps, cubemaps
- Implement texture cache
- Test: load a ship texture, display it on a quad

### Sub-stage 8.5: Model Loader
- Load `.gr2` (Granny 3D) models
- Convert to the internal Carbon mesh format
- Create D3D12 vertex/index buffers
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
- Implement async resource loading using the job system (Thread 4 — Asset IO):
  1. Python requests a resource via Blue's resource interface
  2. Main thread enqueues a "LoadAsset" job in the job system
  3. Asset thread picks up the job: extract from .stuff, decompress, decode
  4. Job completion callback: asset data uploaded to render thread via GPU upload queue
  5. Python callback invoked: resource is ready
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
  - Implement sound engine initialization on its own audio worklet (or main thread with dedicated command buffer)
  - Implement audio listener (attached to camera position)
- Audio command buffer pattern:
  - Main thread (Python) submits audio commands: `PlaySound`, `SetListener`, `UpdateSource`
  - Audio thread/worker drains buffer and calls Wwise API
  - No audio API calls from main thread — all through command buffer
- Load Wwise sound banks from extracted assets (on asset thread)
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
The multicore game loop is the heart of the client. Five+ threads run in parallel, synchronized via fences and lock-free queues. This replaces Carbon's single-threaded greenlet model entirely.

### Sub-stage 10.1: Parallel Frame Loop Design
- Define the multicore frame pipeline (per frame N):

```
FRAME N:
                         MAIN THREAD (T0)
                         ├─ Wait for IO thread to deliver packets from frame N-1
                         ├─ Poll input → shared input state
                         ├─ Run Python game logic tick (GIL-bound, single thread)
                         │   ├─ Python reads input state (lock-free snapshot)
                         │   ├─ Python mutates ECS via EcsCommandBuffer
                         │   ├─ Python submits UI commands to UI command buffer
                         │   └─ Python submits audio commands to audio buffer
                         ├─ Ship camera state → render command buffer
                         ├─ Ship ECS snapshot to physics thread
                         └─ Signal fence: "main done"
                              │
              ┌───────────────┼───────────────┐
              │               │               │
         PHYSICS T2       RENDER T1       IO T3
         Wait on fence    Wait on fence    (runs continuously)
         Read ECS snap    Read render cmd  Read socket
         Run Destiny      Execute draws    Parse packets
         Collision        Dispatch GPU     Push to packet queue
         Pathfinding      Present frame    Signal "new packets"
         Write results                     
         → ECS mutations                   
              │               │               
              └───────────────┘               
                         │
                    WORKER T5..N
                    (runs on every frame)
                    ├─ parallel_for over ShipComponent → animation blend
                    ├─ parallel_for over RenderComponent → frustum cull
                    ├─ parallel_for over ParticleComponent → update
                    ├─ parallel_for over LODComponent → compute LOD
                    └─ parallel_reduce over all objects → occlusion query
```

- Implement frame timing:
  - Fixed timestep for physics (10 Hz or 30 Hz, matching eve.js)
  - Variable timestep for render (as fast as possible, capped to vsync)
  - Main thread Python logic at 60 Hz
  - IO thread runs continuously (no frame alignment)
  - Workers process jobs in parallel, signaled by job graph completion
- Implement the frame fence system (3 atomic flags per frame, double-buffered)
- Test: measure frame time breakdown per thread, identify bottlenecks

### Sub-stage 10.2: Thread Synchronization & Pacing
- Implement frame pacing:
  - Render thread: wait for swap chain vsync, then signal main thread "ready for next"
  - Main thread: wait for "ready" signal, then start frame N+1 logic
  - Physics thread: run at fixed rate, may skip ticks if too slow
  - IO thread: no frame sync, runs at its own pace
- Handle overshoot:
  - If main thread takes too long → physics thread skips a tick
  - If render thread takes too long → drop frame (don't queue work ahead)
  - If IO thread queues >N packets → main thread spends extra time draining
- Test: stress test with low FPS scenarios (200+ ships, heavy UI)

### Sub-stage 10.3: World State via ECS + Network
- World state lives in the ECS, not monolithic containers:
  - Each solar system object = entity with PositionComponent, RenderComponent, etc.
  - Ship entities created/destroyed by server messages
  - Module states = ModuleComponent on ship entity
  - Bullets = ProjectileComponent on temporary entities
- Data flow: IO thread parses server packets → deserializes → pushes ECS mutations via EcsCommandBuffer → main thread applies at start of each tick
- Data flow reverse: Python game logic writes player actions → network command buffer → IO thread sends to server
- Implement state interpolation:
  - Server sends updates at ~10 Hz
  - Client interpolates between known positions (stored ring buffer per entity)
  - Extrapolate if no update received within expected window
- ECS snapshot system for physics thread:
  - Main thread emits a read-only snapshot of relevant component arrays
  - Physics thread reads snapshot, runs simulation, writes delta back
  - No locks needed (snapshot is immutable during read)

### Sub-stage 10.4: Destiny on the Physics Thread
- Port carbonengine/destiny to run on the physics thread (Thread 2):
  - Destiny operates on ECS component data (not its own state)
  - Ship movement: read PositionComponent, VelocityComponent, compute new position
  - Collision detection: spatial hash + broadphase → narrowphase → resolve
  - Pathfinding: run on worker jobs (can be parallelized per ship)
  - Autopilot: waypoint chain → pathfinder → movement commands
- Destiny code may need refactoring (Carbon wrote it single-threaded)
  - Identify static/global state, move to per-entity components
  - Thread-local scratch memory for collision queries
- Test: 100 ships all moving simultaneously, verify physics thread keeps up at 60 Hz

### Sub-stage 10.5: IO Thread & Network Integration
- IO thread (Thread 3) runs independently:
  - TCP socket to eve.js (non-blocking, overlapped I/O on Windows)
  - Read loop: recv → accumulate buffer → parse complete packets → push to queue
  - Write loop: drain outgoing packet queue → send to server
- Packet queue between IO thread and main thread:
  - MPMC queue for incoming packets (IO writer, main reader)
  - MPSC queue for outgoing packets (main writer, IO reader)
- Implement backpressure:
  - If packet queue grows > 1000, throttle sends
  - If server sends too fast, drop non-critical packets
- SSL/TLS handled on IO thread (OpenSSL or Schannel)

### Sub-stage 10.6: Worker Job Scheduling
- Implement per-frame job graph:
  1. **Culling jobs** (parallel_for over all RenderComponent):
     - Frustum cull (camera view frustum)
     - Distance cull (beyond draw distance)
     - Occlusion cull (using last frame's depth)
  2. **Animation jobs** (parallel_for over ships with skeletons):
     - Skin/LOD blending
     - Procedural animation (engine gimbal, turret tracking)
  3. **Particle jobs** (parallel_for over active particle systems):
     - Update positions, velocities, lifetimes
     - Emit new particles
     - Kill expired particles
  4. **LOD jobs** (parallel_for over all meshes):
     - Compute screen-space error
     - Select LOD level
     - Queue LOD transition
  5. **Physics helper jobs** (parallel_for over collision pairs from broadphase):
     - Narrowphase collision detection
     - Contact generation
- Job graph dependencies:
  - Culling depends on camera data from main thread
  - LOD depends on culling results (distance)
  - All jobs must complete before render thread drains command buffer
- Use Taskflow's DAG scheduler to manage dependencies

### Sub-stage 10.7: State Synchronization & Prediction
- Implement client-side prediction:
  - Player ship movement predicted locally (no latency)
  - When server update arrives, compute error and correct smoothly
  - Module activation predicted (show cooldown immediately, trust server)
- Implement reconciliation:
  - Server is authorative for: position, velocity, damage, modules, items
  - Client is authorative for: camera, UI state, input, settings
- Implement entity creation/removal from server messages:
  - Server says "entity 1234 enters system" → IO thread queues entity creation
  - Main thread applies at next tick: EcsWorld::create() with server-provided components
  - Server says "entity 1234 destroyed" → main thread schedules removal

### Sub-stage 10.8: Debug & Profiling Overlay
- Build per-thread profiler overlay:
  - Tracy integration for all threads
  - In-game display: frame time breakdown per thread
  - Thread utilization (% busy per frame)
  - Queue depths (render cmd count, packet queue size, job count)
  - Task graph visualization (DAG of current frame's jobs)
- Build thread state viewer:
  - What each thread is currently doing
  - How many jobs pending in queue
  - Fence wait times
- Build latency overlay:
  - Server→client ping
  - Input→render latency (frames from click to pixel)
  - Physics→render state propagation delay

### Gate / Deliverable
- Five+ threads running in parallel: main, render, physics, IO, workers
- Game loop sustains 60 FPS with 8-core utilization > 70%
- ECS handles 10,000+ entities, parallel iteration < 2ms
- Client connects to eve.js, maintains world state sync with interpolation
- Two clients can see each other move, prediction feels smooth
- Debug overlay shows per-thread breakdown
- Frame time budget: main < 8ms, render < 8ms, physics < 4ms, workers < 6ms

---

## STAGE 11 — Login, Auth & Connection Flow

### Objective
The full user journey from launch to in-game space.

### Sub-stage 11.1: Launcher Application
- Create a lightweight launcher application:
  - No rendering (console or simple UI)
  - Server address configuration
  - User registration (if not already done)
  - Authentication
  - Patching (if we implement updates)
- Use the same auth flow from Stage 4
- The launcher starts the game client with a session token

### Sub-stage 11.2: Login Screen
- Render EVE's login screen:
  - Character model display (rotating ship or avatar)
  - Username/password fields
  - "Log In" button
  - Error message display
- Use the UI framework from Stage 7
- Connect to eve.js auth endpoint
- Handle: success, wrong password, account banned, server offline

### Sub-stage 11.3: Character Select
- Render the character selection screen:
  - Character name, race, bloodline, portrait
  - "Enter World" button
  - "Create Character" button
- Fetch character list from eve.js
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
- Load real system data from eve.js (or SDE)
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

### Sub-stage 12.4: Bracket & Label Rendering
- Implement the overview bracket system:
  - Brackets (square brackets around objects)
  - Labels (object name, distance, type)
  - Icons (object type icon)
- Implement the overview panel (list of objects with sort/filter):
  - This is the most important UI in EVE
  - Must match EVE's behavior exactly
- Implement bracket visibility (distance fade, occluded vs visible)

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
  - Draw call batching — prepare commands on main thread, submit on render thread
  - Instancing for asteroid belts — GPU instanced rendering, setup in parallel
  - Render target management
  - GPU particle systems — compute shader updates, no CPU involvement
- Profile and optimize frame time to hit 60 FPS:
  - Use debug overlay from Stage 10.8 to identify per-thread bottlenecks
  - Ensure no thread blocks another (fence wait times < 0.1ms)
  - Verify GPU utilization > 90% during heavy scenes

### Gate / Deliverable
- Full solar system renders with planets, belts, gates, stations
- Ships render with effects
- Overview brackets work
- HUD displays ship status
- Targeting system works
- 60 FPS in a system with 100+ objects

---

## STAGE 13 — Feature Implementation Rounds

### Objective
Implement all major EVE gameplay features in the client, round by round.

### Multicore Context
All feature work in this stage operates on the multicore foundation from Stage 10:
- **ECS** is the source of truth for all world state. New features add new component types.
- **Job system** runs all simulation logic in parallel. New systems are written as job graphs.
- **Command buffers** for render, audio, network. Python never calls these APIs directly.
- **Python** (main thread) orchestrates logic, reads input, writes ECS commands. Engine runs on workers.

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
- Implement cargo window:
  - Item listing with icons, names, quantities
  - Drag & drop between containers
  - Stack/unstack items
- Implement ship fitting window:
  - Slot display (high/med/low/rig)
  - Drag from cargo to slot
  - Show stats (CPU, powergrid, calibration)
  - Show fitting requirements
- Implement hangar access:
  - Ship hangar
  - Item hangar
  - Corporate hangar
  - Container access (jetcan, cargo container)

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
- Note: Market server is a separate process in eve.js

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
- Feature audit against eve.js's implementation status
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
  - Frame graph (frame time breakdown)
  - Memory profiler
  - Network monitor (packet log viewer)
  - GPU profiler (D3D12 queries)
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

### Sub-stage 15.1: Unit Tests — WITH THREAD SANITIZER
- Write unit tests for all modules:
  - `jobs`: task scheduling, work stealing, DAG execution, contention
  - `ecs`: entity creation, component add/remove, parallel view, command buffer
  - `threading`: lock-free queues (SPSC, MPMC), fences, scratch allocators
  - `core`: thread pool, file system, memory allocation
  - `scheduler`: tasklet lifecycle, channel operations
  - `blue`: Python bindings, resource loading
  - `trinity`: shader compilation, mesh loading, device creation (on render thread)
  - `destiny`: physics, pathfinding (on physics thread)
  - `network`: packet parse/serialize, state machine (on IO thread)
  - `ui`: widget layout, event routing, input handling
- **Thread Sanitizer (TSan)** runs on every test:
  - All multicore tests compiled with `-fsanitize=thread`
  - CI fails on any data race, deadlock, or lock contention
  - Regression test for previously fixed races
- Stress tests:
  - 8 threads submitting 1M jobs, verify no races
  - 8 threads reading/writing ECS via command buffer, verify consistency
  - 8 threads pushing/popping lock-free queues, verify no lost items
- Use Google Test for C++, pytest for Python
- Target: 70%+ line coverage on core modules; ZERO data races permitted

### Sub-stage 15.2: Protocol Conformance Tests
- Write a reference test suite:
  - Connect, authenticate, send each message type
  - Verify server responds correctly
  - Verify malformed packets are rejected
  - Test session timeout and renewal
- Run against eve.js nightly
- Generate a protocol conformance report

### Sub-stage 15.3: Integration Tests
- Write integration tests:
  - Boot sequence (start to login screen)
  - Login flow (success, failure, timeout)
  - Character select → world entry
  - Undock → fly → warp → dock
  - Market buy/sell cycle
  - Chat send/receive
- Use a local eve.js instance as the test server
- Automate in CI

### Sub-stage 15.4: Performance Benchmarks — MULTICORE
- Write performance benchmarks:
  - Startup time (cold boot, thread init, job system warmup)
  - **Multicore scaling** (1/2/4/8/16 core scaling for job system, ECS parallel_for)
  - **Per-thread frame time** (main, render, physics, IO, workers — target each <8ms)
  - **Job system overhead** (task dispatch latency, work stealing efficiency)
  - **Command buffer latency** (submit→consume time for render/audio/network)
  - Scene render (empty system, 10 ships, 100 ships, 1000 ships)
  - UI frame time (idle, heavy window with 1000 list items)
  - Network latency (ping, packet processing time)
  - Memory (idle, after 1 hour, after resource cache filled)
  - **Thread utilization** (% busy per thread, idle time, fence wait time)
- Track benchmarks in CI (graph over time)
- Set performance budgets:
  - Job dispatch overhead < 1µs per task
  - Render command buffer latency < 1 frame
  - Physics thread < 4ms per tick
  - IO thread < 1ms per packet batch
  - All threads combined utilization > 70% on 8-core

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

### Gate / Deliverable
- CI pipeline runs all tests on every PR
- Protocol conformance report
- Performance benchmark dashboard
- Fuzz testing runs with no crashes

---

## STAGE 16 — Packaging, Distribution & Community

### Objective
Ship a usable client to users and build a community around it.

### Sub-stage 16.1: Installer & Updates
- Create an installer:
  - Windows NSIS or MSI installer
  - Include: client binary, extracted assets, launcher, eve.js server
  - Optional: auto-install Visual C++ redistributable, Wwise runtime
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
  - Network (server address, port, proxy)
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

---

## Risk Register

| Risk | Probability | Impact | Mitigation |
|------|------------|--------|------------|
| CarbonUI not open-sourced | HIGH | CRITICAL | Rebuild UI from scratch in Stage 7; Dear ImGui or custom |
| EVE Python scripts cannot be extracted/decompiled | MEDIUM | CRITICAL | Reimplement game logic based on eve.js protocol analysis |
| Game assets cannot be legally extracted | MEDIUM | HIGH | Provide extraction tool; users must own EVE client |
| Perforce dependencies block compilation | HIGH | MEDIUM | Create stub implementations; contribute back to Carbon |
| eve.js is AGPL licensed; community concerns | LOW | MEDIUM | Keep client MIT; eve.js runs as separate process |
| **Data races in multicore engine** | **HIGH** | **CRITICAL** | Lock-free queues everywhere; no shared mutable state without audit; thread sanitizer in CI |
| **Taskflow/Ecs integration bugs** | **MEDIUM** | **HIGH** | Extensive unit tests; Tracy profiling for deadlocks |
| **Python GIL bottlenecks main thread** | **MEDIUM** | **MEDIUM** | Keep Python work minimal on main thread; offload to job system where possible |
| **Destiny single-threaded assumption conflicts** | **HIGH** | **HIGH** | Refactor Destiny state into ECS components; thread-local physics scratch |
| Performance of Trinity (D3D12) on older GPUs | MEDIUM | MEDIUM | Vulkan fallback (trinityal); lower quality presets |
| Fenris Creations changes license or C&D | LOW | HIGH | Legal review before publication; clean-room design |
| Windows-only limits adoption | LOW | MEDIUM | Trinity has OSX support; Linux via Vulkan |

---



## Immediate Next Steps (Next 30 Days)

1. Initialize this directory as a git repository
2. Clone and build carbonengine/core — prove the toolchain works
3. Clone carbonengine/scheduler — understand tasklets (to later replace with job system)
4. Clone eve.js — get it running locally
5. Download the official EVE client — inspect asset directory
6. STUDY: Taskflow, EnTT, moodycamel::ConcurrentQueue — prototype a minimal job system in a sandbox project. Schedule 10,000 tasks. Measure speedup.
7. Write the `docs/gap-analysis.md` document with the initial catalog
8. Write the `docs/architecture.md` document with the multicore architecture design
9. Set up the CI pipeline on GitHub
10. Create the `scripts/` directory with the reproducible build scripts
