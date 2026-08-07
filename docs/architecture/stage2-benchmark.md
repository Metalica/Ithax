# Stage 2 Benchmark Evidence

Status: Local baseline; not a release performance claim.
Date: 2026-08-06

## Scope

This run measures the selected Taskflow and EnTT candidates before Stage 3
runtime integration. It schedules independent tasks, then compares EnTT
component creation and iteration with a monolithic vector loop. The checksums
must remain deterministic across every sample.

## Configuration

- Compiler: MSVC 19.44.35228.0 from Visual Studio Build Tools 17.14.51.
- Host logical thread count reported by the benchmark: 8.
- Taskflow: 4.1.0.
- EnTT: 3.16.0.
- Tasks: 10,000.
- Entities: 10,000.
- Repetitions per worker count: 5.
- Worker counts: 1, 2, 4, 8, and 16.
- Build prefix: `vcpkg_installed-clean` plus local Taskflow and EnTT ports.
- Source state: `f5fa2716ef6dd78eb75f9ab2daf19909cbde3202-dirty`.

The raw machine-readable records are in
`artifacts/benchmarks/stage2.jsonl` and
`artifacts/benchmarks/stage2-grain.jsonl`. Reproduce the worker sweep with:

```powershell
& ".\scripts\stage2-benchmark.ps1" `
  -BuildRoot "build-stage2" `
  -OutputPath "artifacts\benchmarks\stage2.jsonl" `
  -TaskCount 10000 `
  -EntityCount 10000 `
  -Repetitions 5 `
  -Workers 1,2,4,8,16
```

The grain-size sweep uses the same runner with task counts of 100, 1,000,
10,000, and 100,000, three repetitions, and the same worker counts:

```powershell
& ".\scripts\stage2-benchmark.ps1" `
  -BuildRoot "build-stage2" `
  -OutputPath "artifacts\benchmarks\stage2-grain.jsonl" `
  -TaskCounts 100,1000,10000,100000 `
  -EntityCount 10000 `
  -Repetitions 3 `
  -Workers 1,2,4,8,16
```

## Results

Values are p50/p95/p99 microseconds from the raw records.

| Workers | Taskflow | EnTT | Monolithic |
|---------:|---------:|-----:|-----------:|
| 1 | 8710.3/8790.1/8790.1 | 9689.3/9816.8/9816.8 | 189.7/191.9/191.9 |
| 2 | 7473.8/7530.7/7530.7 | 9863.4/9997.4/9997.4 | 189.1/193.2/193.2 |
| 4 | 7224.3/7299.4/7299.4 | 9969.8/10074.4/10074.4 | 185.6/188.7/188.7 |
| 8 | 8462.6/8716.4/8716.4 | 10095.6/10232.1/10232.1 | 187.1/188.9/188.9 |
| 16 | 9580.2/9810.1/9810.1 | 10089.5/10307.8/10307.8 | 184.1/191.1/191.1 |

Every task and entity checksum passed. Taskflow reached its best observed
median at four workers and regressed at eight and sixteen, so the initial
worker budget must be measured rather than set to the host CPU count. The EnTT
number includes registry creation and component insertion; it is not a claim
that EnTT iteration alone is slower than a monolithic loop.

## Grain-Size Results

The following are Taskflow p50 microseconds and the best worker count from the
three-sample grain sweep:

| Tasks | Best workers | Taskflow p50 |
|------:|-------------:|-------------:|
| 100 | 1 | 299.3 |
| 1,000 | 2 | 1027.1 |
| 10,000 | 4 | 7862.8 |
| 100,000 | 4 | 85745.6 |

Small graphs are dominated by executor setup and scheduling overhead. The
worker count cannot be selected independently of task size.

## Verification

The Stage 2 build configured and built with CMake, and CTest passed 20/20,
including `stage2_benchmark_smoke`. The results are a dirty-tree local
baseline and require a clean committed rerun before becoming a tracked
performance comparison.

## Next Gates

- Repeat the sweep on a clean committed revision.
- Add task-size and grain-size cases before selecting executor overhead
  budgets.
- Add an actual parallel EnTT disjoint-range case with ownership assertions.
- Compare the measured executor count with the `ThreadBudget` inventory.
