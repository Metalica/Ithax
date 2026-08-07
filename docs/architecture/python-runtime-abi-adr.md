# ADR-0001: Python Runtime And ABI

Status: Accepted for Stage 2; implementation gates remain open.
Date: 2026-08-06

## Context

Carbon exposes Python bindings, tasklets, resource loading, and the game loop.
The client also needs to host user-local legacy scripts in a later stage. The
open Carbon sources use the word Stackless, but that does not establish that a
Stackless interpreter is required by the current build.

## Verified Evidence

- The installed interpreter is CPython 3.12.9, not Stackless Python. Evidence:
  the installed `python3` SPDX record and the current CMake Python discovery.
- Carbon Scheduler documents a Greenlet implementation of Stackless-compatible
  behavior in `external/carbon/scheduler/doc/source/designDocuments/`.
- Scheduler tasklets are owned by the thread that created them. The divergence
  notes explicitly do not reproduce cross-thread Stackless behavior.
- Scheduler and Blue extensions link directly to `Python3::Python` in
  `external/carbon/scheduler/CMakeLists.txt` and
  `external/carbon/blue/CMakeLists.txt`.
- Carbon keeps compatibility symbols such as `RunStackless` and
  `StacklessMain` in `external/carbon/blue/src/BlueOS.cpp`.
- Exefile initializes standard CPython with `PyConfig` and
  `Py_InitializeFromConfig` in `external/carbon/exefile/ExeFile.cpp`.
- The current Destiny smoke test starts Blue before importing the Destiny
  extension in `tests/destiny_smoke.py`.

## Decision

1. Use the pinned CPython 3.12.9 runtime for the native client.
2. Use Carbon Scheduler's Greenlet-backed, Stackless-compatible API surface.
3. Treat the extension ABI as CPython 3.12-specific, not `abi3`.
4. Rebuild every native Python extension against the exact runtime used by the
   client. Do not mix system Python, vcpkg Python, and user Python DLLs.
5. Keep one Python owner thread. Native workers return owned data through a
   bounded callback channel and never call the Python C API directly.
6. Keep legacy Python 2.7 or Stackless assets outside the distributable source
   tree. Their hosting or translation is a later, separately reviewed gate.

## Consequences

- Python extension filenames and import behavior remain Carbon build-flavor
  conventions; `_debug` is not a portable ABI tag.
- A Python runtime update requires rebuilding and rerunning the import corpus.
- Greenlet scheduling is a compatibility mechanism, not permission to move
  tasklets between workers.
- The Stage 3 job system must publish native results to the Python owner rather
  than attempting to parallelize Python execution.

## Acceptance Gates

- Add a deterministic Exefile `/py` smoke test with checked exit status and
  output. The current `/?` test proves only the help path.
- Record the exact CPython, Greenlet, Scheduler, and extension revisions in
  build evidence.
- Add owner-thread assertions around interpreter initialization, tasklet
  creation, callback draining, and finalization.
- Test cancellation and shutdown while native completion callbacks are queued.
- Audit the user-provided legacy script corpus before choosing embedding,
  translation, or a clean-room replacement.

## Rejected Alternatives

- Requiring Stackless Python now: the current open build uses CPython plus
  Greenlet and does not provide evidence that a Stackless interpreter is
  needed.
- Using `abi3`: Carbon links to the concrete Python library and uses runtime
  APIs outside a declared stable ABI boundary.
- Calling Python from Taskflow workers: this violates the single-owner rule and
  would make interpreter lifetime and shutdown unsafe.
