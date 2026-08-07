# Stage 3 Native Multicore Integration Gate

Status: Native integration gate, the Windows host p99 gate, and named Blue, IO,
Crashpad, and shader-compiler owner lanes implemented; remaining external
owners, Carbon DB provider, and Linux TSan remain open.
Date: 2026-08-06.

## Scope

`ithax-stage3-multicore-integration` is a bounded native-only gate. It does not
initialize Blue, Python, Trinity rendering, network protocols, Tracy, Crashpad,
audio, or proprietary SDKs. Those owners must not be inferred from this test.

## Workload

- Creates 10,000 owner-thread EnTT entities.
- Runs 1,000 total ticks, partitioned across each worker count from one through
  the measured bound, capped at four workers.
- Shuffles the worker-count order with the fixed seed `0x5EED0301`.
- Builds immutable snapshots and deterministic private journals on every tick.
- Uses a thread-local fixed-capacity scratch arena in each Taskflow worker.
- Validates every merged transform, velocity, scratch result, frame hash, and
  frame sequence.
- Sends frame records through dedicated producer and consumer owner threads,
  a bounded SPSC channel, and generation-checked frame slots.
- Validates the expected state on every tick and records the deterministic final
  world hash after all worker-count phases.
- Reports an owner failure from a real external thread and verifies that the
  supervisor retains `FailureSource::Owner` through drain.

## Shutdown Order

The frame producer and consumer stop first. The frame channel closes, queued
records drain, leases release, and the slot pool reaches `IsQuiescent()`. The
runtime supervisor then rejects new jobs, drains accepted jobs, and reaches
`Stopped`. The owner-failure scenario separately reaches `Failed` without
hiding the original typed failure.

## External-Owner Probe

`ithax-stage3-external-owner-measurement` starts bounded synthetic renderer,
I/O, audio, and Crashpad-like owner threads one at a time. It captures process
thread counts before, during, and after each active phase and writes JSON Lines.
The `scripts/stage3-external-owner.ps1` runner retains commit and workload
metadata in `artifacts/benchmarks/stage3-external-owner.jsonl`.

These are measurement-path and lifecycle checks only. Synthetic owners are not
evidence for any Carbon or vendor worker count. Separate real-owner lanes now
cover Carbon IO, Blue resource loading, the Crashpad handler, and the Carbon
shader compiler. Trinity rendering, Wwise, Carbon DB, and vendor-SDK workloads
remain unknown until they run under observation.

## Named Owner Evidence

The real-owner tests are registered as `carbon-owner` CTest cases:

- `carbon_io_owner_measurement` runs the installed Carbon Python IO extensions,
  Scheduler tasklets, and bounded loopback/SSL/select checks.
- `carbon_blue_owner_measurement` initializes embedded Python and Blue, loads a
  real cold resource, verifies warm cache reuse, and drains pending work.
- `crashpad_owner_measurement` starts the installed handler, requests a
  non-crashing dump, and observes a pending report in a temporary database.
- `stage3_shader_compiler_owner_measurement` runs the installed Carbon
  `ShaderCompiler.exe` against the bounded fixture at worker counts 1, 2, and
  4 and records per-run peak thread observations.

The detailed measurements and limitations are recorded in
`thread-budget-evidence.md`.

## Verification

```powershell
& "tools\cmake\bin\ctest.exe" `
  --test-dir build-stage2 -C Debug `
  -R "^(stage3_multicore_integration|stage3_external_owner_measurement)$" `
  --output-on-failure
```

The native gate is evidence for the bounded Ithax-owned runtime only. Full
Stage 3 still requires the remaining actual external-owner workloads and
exercised Linux ThreadSanitizer coverage.
