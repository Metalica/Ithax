# About Ithax

Ithax is an open source multicore EVE Online client foundation built around
the open Carbon engine modules (MIT). It implements the bounded native
multicore foundation, the machoNet network conformance path, and a real
TrinityAL Vulkan rendering backend — each with reproducible gates and
measured evidence.

## What Ithax Provides

- **Bounded multicore foundation (Stage 3):** one runtime-owned Taskflow
  executor sized by a measured process thread budget; owner-thread EnTT
  world with deterministic per-worker journals; bounded SPSC frame packets
  with generation-checked slots; fixed-capacity worker scratch arenas; a
  lifecycle supervisor with typed failure propagation.
- **Network conformance (Stage 4):** machoNet framing, EVE marshal codec,
  the six-step PLACEBO session handshake, AES-256-CBC session crypto,
  connection generations, reconnect and stale-completion handling —
  verified against golden vectors, a deterministic mock, and a
  coverage-guided fuzz lane.
- **TrinityAL Vulkan backend (Stage 5):** a real Vulkan 1.3 device path
  with VMA allocation, synchronization2, dynamic rendering, timeline
  semaphores, a compiled render graph, and an offline HLSL-to-SPIR-V
  toolchain validated with `spirv-val`. The milestone scene clears, draws
  a triangle and a Carbon Mesh procedural ship, orbits the camera,
  survives resize and minimize/restore, verifies a deterministic readback,
  recreates a lost surface, and records named-hardware p50/p95/p99
  frame-time evidence with zero validation errors.
- **Measured Carbon owners:** real owner lanes for Carbon IO, Carbon Blue
  resource loading, Crashpad, the embedded Python/Blue host, and thread
  budget observations recorded in `docs/architecture/thread-budget-evidence.md`.

## Current Milestones

| Stage | Milestone | Status |
|-------|-----------|--------|
| 0 | Gap analysis catalog | Complete |
| 1 | Audited Carbon baseline builds, loopback conformance | Complete |
| 2 | Architecture ADRs + reproducible job benchmark | Complete |
| 3 | Multicore smoke and 1,000-tick integration gate | Complete |
| 4 | Client passes the session contract (marshaling + handshake) | Complete |
| 5 | First TrinityAL Vulkan scene through the render graph | Complete |
| 6+ | Python bootstrap imports, login screen, real ship | Open |

The full stage plan and evidence requirements are in
`DRAFT_DEVELOPMENT_PLAN.md`; architecture decisions are recorded in
`docs/architecture/`.

## Scope Boundaries

- Server-side implementations and proprietary game content are outside this
  repository's distribution scope.
- The network profile is localhost-only and not hardened for LAN or public
  Internet deployment.
- Audio is a Wwise-free stub; the proprietary Wwise SDK is not
  redistributed by this project.
- Carbon gRPC is opt-in and currently blocked by generated-code
  compatibility with the legacy PDM Protobuf ABI.

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

## Repository

- `src/` — Ithax-owned native code, including the TrinityAL Vulkan backend
  under `src/trinityal-vulkan/`
- `cmake/` — vcpkg overlays, triplets, toolchains, and the standalone
  TrinityAL Vulkan build
- `scripts/` — reproducible dependency, build, test, and acceptance automation
- `docs/` — architecture, protocol, and gap-analysis documents
- `tests/` — Python conformance fixtures and measurement lanes

See `README.md` for the current status, `BUILD_GUIDE.md` for build
instructions, and `CONTRIBUTING.md` for contribution policy.
