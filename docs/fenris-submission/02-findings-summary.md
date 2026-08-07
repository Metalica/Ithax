# Findings Summary — Carbon Trinity Audit

Status: Review draft. Neutral engineering gaps from a staged
audit of the open Carbon Trinity baseline. This is not a defect
report against Carbon; it is the map of work the pilot proposes.

## Audited Baseline

- Carbon Trinity 4.0.2, pinned at commit
  `4675ceaaa445f7fd44a1dc97472c8efa4ad8599c`.
- 18 Carbon modules build green in the recorded uncommitted working tree;
  the full module list and recipe are in `03-reproducible-build.md`.

## Renderer Findings

| Area | Observed state | Opportunity |
|------|----------------|-------------|
| TrinityAL backends | D3D11, D3D12, Metal present; Vulkan is a stub | First-class Vulkan path |
| Shader targets | No Vulkan shader target | HLSL-to-SPIR-V contract with reflection |
| Primary context | Ownership rules partially documented | Defined single-owner device/queue model |
| WSI | Win32 surface path unverified | Windowed surface, swapchain selection |
| Synchronization | Resource-state tracking not exercised on Vulkan | Explicit barriers and state model |
| Lifecycle | Swapchain recreation, resize, minimize, device loss | Deterministic recovery + tests |

## Architecture Findings

| Area | Observed state | Opportunity |
|------|----------------|-------------|
| Concurrency | oneTBB and Blue concurrency in use | Bounded, measured multicore prototype |
| Thread budget | Process-wide budget not inventoried | Inventory before adding workers |
| Task graphs | No measured CPU-side scene prep graph | p50/p95/p99 latency reporting |
| Ownership | Renderer and Python ownership rules | Preserved in any prototype |

## What This Means For The Pilot

Milestone 1 reproduces the enabled build and produces a backend
conformance and shader-interface matrix. Milestones 2-3 deliver the
Vulkan path above. Milestone 4 measures the multicore opportunity
without replacing Carbon's existing concurrency or making
performance promises.

## Verification

Each finding is tied to the audited baseline commit and the staged
development plan. The conformance matrix delivered in Milestone 1
makes every claim independently checkable by the Carbon team.
