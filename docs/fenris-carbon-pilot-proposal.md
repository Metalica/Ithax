# Carbon Vulkan and Multicore Engineering Pilot

Status: External outreach draft

Prepared for: Fenris Creations, Carbon Engine team

Prepared by: [Name] | [Email] | https://github.com/Metalica/Ithax.git

Date: 2026-08-02

## Executive Request

I propose a paid, milestone-based engineering pilot to evaluate and implement
a first-class Vulkan path for Carbon Trinity, followed by a measured prototype
for bounded multicore CPU work. The initial request is a technical scoping call,
not approval of a finished backend or a clean client.

Fenris describes Carbon as a cross-platform framework, states that open source
drives its innovation, and invites the community to contribute and build on its
components. This pilot converts that opportunity into reviewable engineering
deliverables with explicit acceptance gates.

## Opportunity

The audited Carbon Trinity 4.0.2 baseline at commit
`4675ceaaa445f7fd44a1dc97472c8efa4ad8599c` provides D3D11, D3D12, Metal, and
stub TrinityAL backends. It does not currently provide a Vulkan backend or a
Vulkan shader target.

A Vulkan implementation could broaden Carbon's renderer options and provide a
portable validation target. A bounded multicore prototype could then measure
CPU-side scene preparation without replacing Carbon's existing oneTBB and Blue
concurrency or making unsupported performance promises.

## Existing Evidence

- A pinned Carbon integration manifest and CMake smoke harness exist.
- A historical local baseline records a Debug build and 19/19 passing CTest
  tests.
- Public Stage 1 repository validation passes; the committed full build remains
  a manual workflow and has not produced a public run.
- Nine dependency metadata records have pinned-source evidence; one Carbon
  audio API record remains unresolved for legal review.
- TrinityAL interfaces, primary-context ownership, shader targets, Vulkan WSI,
  synchronization, and resource lifetime risks have been audited in a staged
  development plan.
- Performance targets are treated as hypotheses until measured on named
  hardware with reproducible workloads.

This evidence demonstrates preparation. It is not represented as production
readiness or proof of a current committed full build.

## Proposed Milestones

### 1. Discovery And Reproducibility

- Agree on the supported Carbon branch, platforms, hardware, and coding rules.
- Reproduce the enabled Carbon build and tests from a clean environment.
- Deliver a TrinityAL backend conformance and shader-interface matrix.
- Agree on acceptance criteria before implementation begins.

### 2. Vulkan Bootstrap Through TrinityAL

- Add explicit Vulkan build and package features.
- Implement Win32 surface, instance, device, queue, and swapchain selection.
- Add structured capability and validation diagnostics.
- Render a procedural scene through TrinityAL, not a side executable.

### 3. Shader, Resource, And Lifecycle Path

- Define a deliberate HLSL-to-SPIR-V and reflection contract.
- Implement resource-state tracking and synchronization2 barriers.
- Cover resize, minimize, swapchain recreation, retirement, and device loss.
- Add validation, conformance, and deterministic lifecycle tests.

### 4. Measured Multicore Prototype

- Inventory the process-wide thread budget before adding workers.
- Prototype bounded CPU task graphs for independent scene preparation.
- Preserve renderer and Python ownership rules.
- Report p50, p95, and p99 latency, scaling, waits, and context switches.

Each milestone is independently reviewable. Fenris may stop, redirect, or
extend the engagement after an accepted milestone.

## Deferred Scope

Client gameplay, content extraction, login flow, inventory, market, chat,
scanning, and full-world presentation are outside this four-milestone pilot.
They remain deferred until the required legal approvals, authoritative
interface evidence, and separate acceptance criteria exist. No unsupported
implementation or compatibility claim is made for that deferred scope.

## Acceptance Principles

- No proprietary game assets, scripts, data, SDKs, or confidential code.
- No server-side implementation, protocol adapter, launcher, or
  authentication work.
- Upstream-reviewable changes with tests, documentation, and provenance.
- No Vulkan validation or synchronization-validation findings in exercised
  acceptance tests.
- Results recorded with commit, compiler, build flags, OS, driver, CPU, GPU,
  workload, and raw benchmark output.
- NVIDIA, AMD, and Intel coverage is negotiated rather than implied.
- Production-readiness claims require separate acceptance criteria.

## Commercial And IP Model

Compensation is for defined engineering, integration, review support,
documentation, and maintenance. It is not payment for access to code Fenris
already receives under an open-source license.

Carbon-derived changes would retain the applicable upstream license and
notices. Original project-owned work is MIT-licensed under the repository's
LICENSE and package metadata. Any funded assignment, exclusivity, warranties,
indemnity, or a private maintenance term would require a separate written
agreement and price.

Confidential work, if requested after an NDA, would use a separate restricted
repository. Nothing would enter a public repository without written release
approval and a provenance review.

## Scope Boundary

The repository is a development and conformance harness. This proposal is only
for reusable Carbon engineering. It does not request endorsement of the
repository, a clean client built from open Carbon modules, any server-side
implementation, or any use of Fenris content.
Any game-specific interoperability work would require separate written
authorization, legal review, scope, and repositories.

## Requested Next Step

Please route this proposal to the Carbon renderer or engine owner for a
30-minute technical scoping call. The call should identify:

1. The Carbon branch and problems Fenris considers worth solving.
2. Whether Vulkan, multicore preparation, or a smaller first contribution is
   useful to the current roadmap.
3. The expected contribution, review, CI, license, and hardware process.
4. Whether Fenris prefers a paid discovery sprint, milestone contract, or
   another vendor or contributor arrangement.

Public Partner Relations route:
`partner.relations@fenris.com`

General fallback: `info@fenris.com`

## Public References

- Fenris Carbon: <https://fenris.com/carbon>
- Fenris contact routes: <https://fenris.com/contact-us>
- Carbon Trinity: <https://github.com/carbonengine/trinity>
- Carbon organization: <https://github.com/carbonengine>
