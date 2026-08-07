# Stage 3 Native Multicore Smoke

Status: Partial Stage 3 implementation slice; the full Stage 3 gate remains
open. Date: 2026-08-06.

## Scope

The `ithax-stage3-multicore-smoke` target is a native-only vertical slice. It
uses the existing pinned Taskflow and EnTT dependencies and adds no queue
dependency. It does not initialize Blue, Python, Trinity rendering, network
protocols, Tracy, Crashpad, audio, or proprietary SDKs.

## Ownership Contracts

- `JobRuntime` owns one explicitly sized Taskflow executor. It rejects new
  submissions after stop and consumes task failures through `TaskHandle::Wait`.
- `EcsWorld` owns the EnTT registry and records its creating thread as the
  structural owner. Workers read an immutable snapshot and write private
  `EcsJournal` objects.
- `EcsWorld::MergeJournals` sorts worker journals, requires contiguous worker
  indexes, rejects overlapping entity ranges, and applies updates on the owner
  thread.
- `FramePacketChannel` is a bounded SPSC ring. It assigns one producer and one
  consumer thread, blocks on full or empty state, and wakes both sides on close.
- `FrameSlotPool` owns fixed-size byte payloads behind move-only write/read
  leases. Slots transition `Free → Writing → Published → Reading → Free`,
  increment a generation on reuse, and reject stale tokens.
- `FramePacket` carries logical metadata and an optional slot token. Channel
  capacity bounds descriptors; slot count and payload capacity bound owned
  frame storage independently.
- `ScratchAllocator` owns a fixed-capacity, owner-thread bump arena. It validates
  power-of-two alignment, rejects overflow without growing, and `Reset()`
  invalidates all previously returned spans.
- `RuntimeSupervisor` owns the job runtime and a bounded registry of task
  handles. It exposes `Starting → Running → StopRequested → Draining →
  Stopped/Failed`, preserves the first typed failure source, and provides a
  cooperative `std::stop_token`.
- `RuntimeSupervisor::RequestStop(channel)` requests the stop token, rejects
  further jobs, and closes the supplied channel. The channel remains externally
  owned; its producer and consumer must join before either object is destroyed.

## Lifecycle Contract

The supervisor serializes submission with drain so a task accepted before a
stop request is either tracked and consumed or rejected before executor
submission. Deferred task results are consumed during `Drain`, including
exceptions. Job, owner, and frame-channel failures retain their first source;
the supervisor continues cleanup and finishes in `Failed` rather than hiding
the original error. A successful cleanup finishes in `Stopped`.

The pending-task registry is bounded at `MAX_PENDING_RUNTIME_TASKS`. The
supervisor does not own external owner threads, channel objects, Carbon pools,
or vendor workers. Shutdown is cooperative: a task that ignores the stop token
cannot be forcefully interrupted by portable C++; external blocking work needs
its own cancellation and completion contract. Those lifetimes remain explicit
integration contracts.

The slot pool does not forcibly reset active storage on close. Close wakes
blocked acquire calls, rejects new writers, permits already-published slots to
drain, and leaves writing/reading leases responsible for cancel/release. The
pool must outlive all leases and owner threads; `IsQuiescent()` is the shutdown
invariant. A closed consumer may call `DiscardPublished` for a queued
descriptor it intentionally drops. The channel does not own or reclaim slot
payloads, so queued descriptors must be drained or explicitly discarded before
the pool is destroyed.

Scratch allocations are valid only while the allocator remains alive and until
the next reset. The allocator is constructed and used by one worker owner; it
does not synchronize or grow for other threads. Reset is the lifetime boundary
for all prior spans and does not promise to clear their old bytes.

## Verification

Run the focused test from the configured build tree:

```powershell
& "tools\cmake\bin\ctest.exe" `
  --test-dir build-stage2 -C Debug `
  -R "^stage3_multicore_smoke$" --output-on-failure
```

The smoke test covers deterministic journal merging, disjoint parallel ranges,
Taskflow error consumption, supervisor lifecycle and failure propagation,
cooperative stop-token cancellation, deferred-result draining, bounded pending
tasks, slot payload bounds and generations, lease ownership, slot close wake-up,
channel/slot token handoff, scratch capacity/alignment/overflow/reset and
ownership, channel full and empty states, stalled-consumer
backpressure, producer ownership, packet ordering, and producer/consumer close
wake-up. It is not evidence that the remaining Carbon, driver, audio, Crashpad,
Tracy, or SDK worker owners are measured or controlled.

The separate `ithax-stage3-multicore-integration` target is the native 1,000-tick
gate. It exercises randomized worker-count order, per-worker scratch use,
dedicated frame producer and consumer owners, per-tick state and frame checks,
and an external owner failure report. The separate external-owner measurement
target validates process-thread observation with synthetic named owners; it
does not substitute for actual Carbon-owner workloads.
