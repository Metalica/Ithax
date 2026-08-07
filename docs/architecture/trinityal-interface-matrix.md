# TrinityAL Interface Matrix

Status: Stage 2 source inventory complete; runtime conformance is open.
Date: 2026-08-06

## Reading The Matrix

This matrix describes the pinned Carbon Trinity 4.0.2 source. It does not
claim that an interface has passed a device test.

- `F` means a backend implementation is present in the pinned source.
- `I` means the public interface compiles, but the implementation is a
  no-device path, an unsupported feature path, or an explicit stub.
- `N` means the interface is present but deliberately no-op or returns an
  unsupported result for that backend.
- `-` means the backend does not provide that interface or source path.

The current root build installs only `trinity_stub`. DX11, DX12, and Metal
are source-supported Carbon configurations, not current project test lanes.

## Backend Matrix

| Contract | Public interface | DX11 | DX12 | Metal | Stub |
|---|---|:---:|:---:|:---:|:---:|
| Platform selection | `TrinityALForward.h` | F | F | F | I |
| Adapter and presentation data | `Tr2AdapterStructures.h` | F | F | F | I |
| Capabilities | `Tr2CapsAL.h` | F | F | F | I |
| Adapter information | `Tr2VideoAdapterInfoAL.h` | F | F | F | I |
| Primary render context | `Tr2PrimaryRenderContextAL.h` | F | F | I | I |
| Immediate render context | `Tr2RenderContextAL.h` | F | F | F | I |
| Swap chain | `Tr2SwapChainAL.h` | F | F | F | I |
| Buffers | `Tr2BufferAL.h` | F | F | F | I |
| Textures | `Tr2TextureAL.h` | F | F | F | I |
| Constant buffers | `Tr2ConstantBufferAL.h` | F | F | F | I |
| Samplers | `Tr2SamplerStateAL.h` | F | F | F | I |
| Vertex layouts | `Tr2VertexLayoutAL.h` | F | F | F | I |
| Shaders | `Tr2ShaderAL.h` | F | F | F | I |
| Shader programs | `Tr2ShaderProgramAL.h` | F | F | F | I |
| Resource sets | `Tr2ResourceSetAL.h` | F | F | F | I |
| Render-pass hints | `Tr2RenderPassAL.h` | N | N | F | N |
| Fences | `Tr2FenceAL.h` | F | F | F | I |
| GPU timers | `Tr2GpuTimerAL.h` | F | F | F | I |
| Occlusion queries | `Tr2OcclusionQueryAL.h` | F | F | F | I |
| Pipeline statistics | `Tr2PipelineStatsQueryAL.h` | F | F | F | I |
| RT bottom level | `Tr2RtBottomLevelAccelerationStructureAL.h` | N | F | F | N |
| RT top level | `Tr2RtTopLevelAccelerationStructureAL.h` | N | F | F | N |
| RT pipeline state | `Tr2RtPipelineStateAL.h` | N | F | F | N |
| RT shader table | `Tr2RtShaderTableAL.h` | N | F | F | N |
| Upscaling | `upscaling/Tr2UpscalingAL.h` | F | F | F | N |
| Streamline | `Tr2StreamlineAL.h` | F | F | - | - |
| Render-context events | `ITr2RenderContextEvents.h` | F | F | F | I |
| Resource lifetime and memory class | `Tr2DeviceResourceAL.h` | F | F | F | I |

## Feature Details

### Capabilities

The platform capability headers define the following compile-time contract:

| Capability | DX11 | DX12 | Metal | Stub |
|---|:---:|:---:|:---:|:---:|
| Buffer shader resources | yes | yes | yes | no |
| Buffer counters | yes | yes | no | no |
| Unordered access | yes | yes | yes | no |
| Compute | yes | yes | yes | yes |
| Texture arrays | yes | yes | yes | yes |
| MSAA samples | yes | yes | yes | yes |
| Render-pass hints | no | no | yes | no |
| Heap views | no | yes | yes | no |
| Shader-program samplers | no | yes | not defined | no |
| Parallel contexts | not defined | not defined | yes | no |
| Ray tracing | no | yes | yes | no |

The runtime capability object remains authoritative for hardware-dependent
features. In particular, DX12 and Metal compile ray-tracing support but may
report it unavailable for a selected adapter.

### Upscaling

The factory implementations select these techniques before checking runtime
availability:

| Backend | Factory techniques | Source |
|---|---|---|
| DX11 | FSR1 | `dx11/upscaling/Tr2UpscalingALDx11.cpp` |
| DX12 | FSR1, FSR3, DLSS, XeSS | `dx12/upscaling/Tr2UpscalingALDx12.cpp` |
| Metal | MetalFX, FSR1 | `metal/upscaling/Tr2UpscalingALMetal.mm` |
| Stub | none | `stub/upscaling/Tr2UpscalingALStub.cpp` |

The factory can reject a technique after construction when its SDK or adapter
does not support it. A factory entry is therefore not a hardware guarantee.

### Streamline

`Tr2StreamlineAL.h` is compiled only for DX11 and DX12. The DX11 contract is
DLSS/NIS-oriented. DX12 additionally exposes DLSS frame generation, Reflex,
and PCL markers. There is no Metal or stub Streamline contract in the pinned
source.

## Primary-Context Ownership

Primary-context behavior is not uniform across the existing backends:

| Backend | Ownership evidence | Constraint for Vulkan |
|---|---|---|
| DX11 | Static primary pointer in `Tr2RenderContextAL`. | Preserve one owner and assert thread affinity around device and present calls. |
| DX12 | `m_ownerDevice`; `SetPrimaryRenderContext` is a no-op. | Keep device, queue, command allocator, and command-list ownership explicit. |
| Metal | Primary flag, Metal work queue, queue index, and `SetAsPrimary`. | Do not infer that parallel encoders make resources or presentation thread-safe. |
| Stub | Primary wrapper alias and no-device compatibility paths. | Never use stub success as device or WSI evidence. |

`TrinityALForward.h` sets `TRINITY_PLATFORM_HAS_PRIMARY_CONTEXT` only for
DX11 and DX12. Metal and stub define `Tr2PrimaryRenderContextAL` as an alias
of `Tr2RenderContextAL`. Blue queues background resource preparation back to
the main owner; this must be preserved until the caller graph is audited.

## Vulkan Boundary

Vulkan is absent from `TrinityALForward.h`, the TrinityAL CMake source list,
and the shader compiler platform list. Vulkan headers and VMA in the root
prefix do not change that fact. The Stage 2 decision is therefore:

- add a new opt-in Vulkan backend rather than changing an existing row;
- target Win32 WSI first and keep Linux WSI and MoltenVK separate gates;
- define an explicit instance, surface, device, queue, allocator, swap-chain,
  and presentation owner;
- use the existing common contracts as the compatibility target, with
  unsupported features reported explicitly;
- keep parallel command recording disabled until command-pool ownership,
  lifetime, and deterministic submission tests pass.

## Required Evidence Before Enabling Vulkan

| Area | Required evidence |
|---|---|
| Loader and instance | Version, layers, extensions, and debug messenger |
| Win32 WSI | Surface, formats, present modes, resize, minimize, restore |
| Device selection | Features, limits, queues, deterministic selection |
| Lifetime | Buffers, images, views, samplers, retirement, destruction |
| Rendering | Clear, depth, viewport, indexed, instanced, triangle |
| Shaders | HLSL-to-SPIR-V version, validation, reflection, descriptors |
| Synchronization | Fences, timeline values, semaphores, barriers |
| Readback | Deterministic image readback and resource mapping |
| Diagnostics | Labels, markers, timestamps, bounded error reporting |
| Ownership | Primary-context assertions and affinity violations |

Each row requires a test and recorded result. A standalone Vulkan sample does
not satisfy the TrinityAL gate.

## Source Evidence

- `external/carbon/trinity/trinityal/CMakeLists.txt` is the backend source
  inventory and public-header list.
- `external/carbon/trinity/trinityal/include/TrinityALForward.h` defines the
  platform identifiers and primary-context alias behavior.
- `external/carbon/trinity/trinityal/*/Tr2CapsAL*.h` defines capability flags.
- `external/carbon/trinity/trinityal/*/upscaling/` defines factory coverage.
- `external/carbon/trinity/shadercompiler/Platforms.h` lists shader compiler
  platform coverage.
- `docs/architecture/trinity-vulkan-adr.md` records the backend decision.
- `CMakeLists.txt` currently links `ithax-trinity-native-smoke` to
  `trinity_stub`, which proves only the no-device loading path.
