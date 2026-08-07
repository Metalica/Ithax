# Scope And Boundaries

Status: Review draft. This document states what the clean client is,
what this engagement is, and what it is not. It is included in the
submission so there is no ambiguity.

## What The Clean Client Is

The clean client is independently developed and built from open Carbon engine
modules. It ships zero proprietary
content — no assets, no extracted scripts, no SDKs. Users interact
only with their own locally owned data, and nothing proprietary
enters the repositories or releases.

## What This Engagement Is

A paid, milestone-based engineering pilot for reusable Carbon work:

1. Discovery and reproducibility — clean build, conformance and
   shader-interface matrix, acceptance criteria.
2. Vulkan bootstrap through TrinityAL — surface, device, queue,
   swapchain, diagnostics, procedural scene.
3. Shader, resource, and lifecycle path — HLSL-to-SPIR-V contract,
   resource-state tracking, resize and device-loss coverage, tests.
4. Measured multicore prototype — thread-budget inventory, bounded
   task graphs, latency reporting (p50/p95/p99).

Each milestone is independently reviewable. Fenris may stop,
redirect, or extend the engagement after an accepted milestone.

## What This Engagement Is Not

- Not a server-side implementation, protocol adapter, launcher, or
  authentication project.
- Not a distribution of game content, scripts, data, or SDKs.
- Not a request for endorsement of the clean client.
- Not a performance promise — targets are hypotheses until measured
  on named hardware with reproducible workloads.
- Not production-readiness certification — that requires separate
  acceptance criteria.

## Boundaries

- Game-specific interoperability requires separate written
  authorization, legal review, scope, and repositories.
- No proprietary files enter public repositories or releases.
- No claims of Fenris sponsorship or endorsement without written
  approval.
- Upstream reviewable changes only: tests, documentation, and
  provenance for every contribution.

## Repository Separation

The referenced repository separation plan governs this project:
Carbon-facing work lives in a narrow, upstreamable fork; runtime
work lives in a separate project-owned repository; game-specific
adapters stay unpublished until written authorization and legal
review. This document is the governance statement behind that
separation.
