# Test Evidence

Status: Historical local CTest output captured from the uncommitted working
tree at the recorded base commit. A committed-revision transcript and public
full-build run are still required for submission.

## Suite Run

| Field | Value |
|-------|-------|
| Command | Pinned CTest on `build-proof-space`, Debug configuration |
| Result | 19/19 passed, 0 failed |
| Base commit | `f5fa2716ef6dd78eb75f9ab2daf19909cbde3202` |
| Revision note | Evidence predates the clean export; captured from an uncommitted base tree |
| Build result | Debug build completed before CTest |
| Date | 2026-08-02 |
| Raw output | Captured in the final local run; no transcript file is attached |

Exact command:

```powershell
& ".\tools\cmake\bin\ctest.exe" `
  --test-dir build-proof-space -C Debug --output-on-failure
```

## Smoke Coverage By Module

The final run exercised these 19 registered tests:

- `foundation_smoke`
- `scheduler_smoke`
- `io_smoke`
- `blue_native_smoke`
- `blue_smoke`
- `conformance_stub_smoke`
- `mesh_native_smoke`
- `imageio_native_smoke`
- `resources_native_smoke`
- `trinity_native_smoke`
- `destiny_smoke`
- `audio_native_smoke`
- `pathfinder_native_smoke`
- `localization_native_smoke`
- `db_native_smoke`
- `parser_native_smoke`
- `geo2_native_smoke`
- `d3dinfo_native_smoke`
- `exefile_native_smoke`

The loopback test is a Python harness with zero game logic. The direct pinned
Python self-test also returned:

```text
{"event":"conformance_stub_selftest","status":"pass"}
```

## Conformance Deliverable (Milestone 1)

The pilot's first funded deliverable extends this evidence into a
backend conformance and shader-interface matrix: one row per
backend, one column per interface contract, with pass/fail per
platform. The source inventory is recorded in
`docs/architecture/trinityal-interface-matrix.md`. Runtime conformance remains
an explicit future gate for each enabled backend.

## Claim

The exact CTest command above reproduced 19/19 passing tests on the recorded
working tree. This is local evidence only; it does not establish a committed
revision or public full-build CI result. Attach the raw transcript before
submission.
