# ADR-0003: Vulkan As A TrinityAL Backend

Status: Accepted for Stage 2 design; implementation is deferred to Stage 5.
Date: 2026-08-06

## Context

The plan needs a Vulkan renderer, but the existing Carbon package does not
provide one. Vulkan headers or a virtual allocator dependency are not evidence
of a Vulkan device backend. The renderer also has primary-context ownership
assumptions that must be solved independently from API selection.

## Verified Baseline

- The authoritative Carbon Trinity baseline is 4.0.2 at commit
  `4675ceaaa445f7fd44a1dc97472c8efa4ad8599c`, pinned by
  `cmake/overlay-ports/carbon-trinity/portfile.cmake`.
- Pinned TrinityAL exposes D3D11, D3D12, Metal, and stub backends. It has no
  Vulkan platform identifier, backend source, CMake target, or shader compiler
  target. The relevant source is `trinityal/include/TrinityALForward.h`.
- `Tr2VirtualAllocator.cpp` uses VMA virtual allocation; it does not create a
  Vulkan instance, device, surface, or queue.
- `Tr2RenderContext.h` contains a hidden global primary render context, and
  rendering/resource code uses main-thread render-context access macros.
- Blue performs background resource work but queues GPU preparation to the main
  owner. See `external/carbon/blue/src/BlueAsyncRes.cpp`.
- The shader compiler currently advertises D3D11, D3D12, and Metal only. See
  `shadercompiler/Platforms.h`.
- The root smoke target links `trinity_stub`; it does not exercise a device or
  window. See `CMakeLists.txt`.
- The local Trinity v5 comparison checkout also has no Vulkan backend. It is not
  an upgrade path for this decision.

## Decision

1. Keep Carbon Trinity 4.0.2 as the implementation baseline.
2. Add Vulkan as a new opt-in TrinityAL backend; do not replace the stub path.
3. Target Windows and Win32 WSI first. Linux WSI and MoltenVK are separate
   portability gates.
4. Use Vulkan 1.3 as the provisional profile, subject to hardware validation.
5. Create an explicit Vulkan primary context with documented device, queue,
   surface, and presentation ownership. Do not silently move the existing
   global context to another thread.
6. Use VMA for real Vulkan device allocation, conventional descriptor sets,
   and primary command buffers first. Defer bindless, indirect, and ray
   tracing capabilities to later profiles.
7. Define a versioned HLSL-to-SPIR-V shader and reflection ABI. Validate shader
   modules and descriptor layouts before integrating higher Trinity code.
8. Require validation and synchronization-validation coverage in Vulkan CI and
   in the local backend conformance suite.

## Initial Conformance Matrix

The public-interface inventory is complete in
`docs/architecture/trinityal-interface-matrix.md`. Each conformance row below
still requires a test and a recorded result before the backend is enabled.

| Area | Required evidence |
|------|-------------------|
| Loader and instance | Version, layers, extensions, and debug messenger |
| Win32 WSI | Surface, formats, present modes, resize, minimize, restore |
| Device selection | Features, limits, queues, deterministic selection |
| Lifetime | Buffers, images, views, samplers, retirement, destruction |
| Rendering | Clear, depth, viewport, indexed, instanced, procedural triangle |
| Shaders | HLSL to SPIR-V, validation, reflection, descriptor compatibility |
| Synchronization | Fences, timeline values, swapchain semaphores, barriers |
| Readback | Deterministic image readback and resource mapping |
| Diagnostics | Object labels, command markers, timestamps, deduplicated errors |
| Ownership | Primary-context assertions and thread-affinity violations |

Parallel command recording is disabled initially. It may be enabled only after
command-pool ownership, resource lifetime, and deterministic submission tests
pass.

## Implementation Gates

- Inventory every public TrinityAL interface and primary-context access before
  changing the Carbon interface.
- Add `BUILD_VULKAN`, a `vulkan` feature, `TRINITY_VULKAN`, and explicit target
  exports without changing the default stub build.
- Select one loader/dispatch approach; do not combine overlapping bootstrap
  layers.
- Define swapchain and device-loss state machines before scene integration.
- Add validation, synchronization, resize, minimized-window, and teardown
  tests before the first real ship or UI asset.
- Route a clear and triangle through TrinityAL itself. A standalone Vulkan demo
  does not satisfy this gate.

## Rejected Alternatives

- Upgrading to Trinity v5 for Vulkan: the comparison checkout has no Vulkan
  backend and would add migration risk without solving the gap.
- Treating Vulkan headers or VMA virtual allocation as backend support.
- Moving all rendering to a dedicated thread before primary-context ownership
  and Blue GPU-preparation paths are audited.
