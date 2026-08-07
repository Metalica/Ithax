# Reproducible Build Evidence

Status: Historical local gate output is captured from the pre-clean-export
working tree at the base commit below. The result is not submission-ready: a
committed full-build transcript, public full-build CI run, and license review
remain open.

## Recipe (from the build guide)

- Generator: Visual Studio 2022
- Dependencies: vcpkg with the pinned manifest and overlay ports
- Toolchain: MSVC 19.44.35228.0 with the v143 toolset
- Module set: the 18 enabled Carbon modules listed in the README

## Evidence Fields

| Field | Value |
|-------|-------|
| Base commit | `f5fa2716ef6dd78eb75f9ab2daf19909cbde3202` |
| Revision note | Evidence predates the clean export; captured from an uncommitted base tree |
| Dependency prefix | `vcpkg_installed-proof2` |
| Build directory | `build-proof-space` |
| Compiler | MSVC 19.44.35228.0, MSBuild 17.14.51 |
| CMake | 3.30.3 |
| Build flags | Visual Studio 2022, x64, Debug, manifest install off |
| OS | Windows 10 Enterprise 10.0.19045, build 19045 |
| CPU | AMD Ryzen 3 5425U with Radeon Graphics |
| GPU | AMD Radeon (TM) Graphics |
| Driver | 31.0.21925.1001 |
| Date | 2026-08-02 |
| Final build result | MSBuild completed successfully with parallelism 2 |
| Raw transcript | Not attached; commands and results are recorded below |

## Final Gate Commands

The final build reused the configured `build-proof-space` directory after the
fresh dependency installation. CMake was not on `PATH`, so the pinned binary
was invoked explicitly:

```powershell
& ".\tools\cmake\bin\cmake.exe" `
  --build build-proof-space --config Debug --parallel 2
& ".\tools\cmake\bin\ctest.exe" `
  --test-dir build-proof-space -C Debug --output-on-failure
& ".\scripts\quality.ps1" `
  -PythonPath ".\vcpkg_installed-proof2\x64-windows-debug\tools\python3\python.exe"
```

Results:

- The Debug build completed successfully.
- CTest passed 19/19 tests with 0 failures.
- The quality and release-boundary checks passed.

## Clean-Build Procedure

1. Clone from a fresh checkout of the committed revision.
2. Run `scripts/build.ps1` with fresh install and build roots.
3. Use `-Parallel 2` on this Windows host to keep build concurrency bounded.
4. Capture the full console output; it becomes the evidence archive.

Example clean-run command:

```powershell
& ".\scripts\build.ps1" `
  -InstallRoot vcpkg_installed-proof-final `
  -BuildRoot build-proof-final `
  -CMakePath tools\cmake\bin\cmake.exe `
  -Parallel 2
```

## Loopback Conformance Check (proof-pack item 5)

The check uses a loopback conformance fixture
(`tests/conformance_stub.py`) — a test harness with zero game logic. It proves
that the bounded fixture can bind 127.0.0.1 and answer its probe. It does not
prove an external protocol implementation.

| Field | Value |
|-------|-------|
| Direct command | Pinned Python running `tests/conformance_stub.py` |
| Direct result | `{"event":"conformance_stub_selftest","status":"pass"}` |
| CTest test | `conformance_stub_smoke` passed in the final 19/19 run |
| Date | 2026-08-02 |

## Dependency And License Evidence

The target prefix contains 130 target-triplet package records and 130
per-package SPDX 2.2 documents. The vcpkg inventory also reports 25 host
records, for 155 records in total. The generated SPDX files are evidence for
this prefix, not a committed release SBOM.

Reproduce the inventory and SPDX count from the repository root:

```powershell
& ".\tools\vcpkg\vcpkg.exe" list --x-json `
  --x-install-root=".\vcpkg_installed-proof2" `
  --triplet=x64-windows-debug
$share = Resolve-Path `
  ".\vcpkg_installed-proof2\x64-windows-debug\share"
@(Get-ChildItem -LiteralPath $share -Filter "vcpkg.spdx.json" `
  -Recurse -File).Count
```

Reproduce the license and copyright report:

```powershell
$share = Resolve-Path `
  ".\vcpkg_installed-proof2\x64-windows-debug\share"
$docs = @(Get-ChildItem -LiteralPath $share `
  -Filter "vcpkg.spdx.json" -Recurse -File)
$rows = foreach ($file in $docs) {
  $doc = Get-Content -LiteralPath $file.FullName -Raw |
    ConvertFrom-Json
  $port = @($doc.packages | Where-Object {
    $_.SPDXID -eq "SPDXRef-port"
  })[0]
  [PSCustomObject]@{
    Name = $port.name
    Version = $port.versionInfo
    License = $port.licenseConcluded
    Copyright = @($doc.files | Where-Object {
      $_.fileName -match "(^|/)copyright$"
    }).Count -gt 0
  }
}
$rows | Group-Object License | Sort-Object Name
$rows | Where-Object { -not $_.Copyright } | Sort-Object Name
```

The final SPDX `licenseConcluded` counts were:

| License expression | Count |
|--------------------|------:|
| `(Apache-2.0 OR MIT)` | 1 |
| `(BSD-3-Clause OR GPL-2.0-only)` | 1 |
| `(FTL OR GPL-2.0-or-later)` | 1 |
| `Apache-2.0` | 1 |
| `blessing` | 1 |
| `BSD-2-Clause` | 3 |
| `BSD-3-Clause` | 8 |
| `BSD-3-Clause AND ISC AND curl` | 1 |
| `BSL-1.0` | 62 |
| `bzip2-1.0.6` | 1 |
| `libpng-2.0` | 1 |
| `LicenseRef-Microsoft-OLE-DB` | 1 |
| `LicenseRef-vcpkg-null` | 2 |
| `MIT` | 30 |
| `MPL-2.0` | 1 |
| `NOASSERTION` | 10 |
| `PSF-2.0` | 2 |
| `Python-2.0` | 1 |
| `Zlib` | 2 |

The recorded prefix produced ten `NOASSERTION` records before the overlay
metadata corrections: `amd-fidelityfx-cacao`,
`amd-fidelityfx-cas`, `bsdiff-drake127`, `carbon-trinityaudioapi`,
`crashpad`, `libb2`, `libyaml`, `mikktspace`, `openssl`, and `protobuf`.
All ten now have candidate expressions in their pinned overlay manifests. The
SPDX evidence must be regenerated to verify the new count, and all distribution
claims remain subject to legal review.

The repository now provides a focused evidence command that does not rebuild
the project:

```powershell
& ".\scripts\supply-chain.ps1" `
  -VcpkgPath ".\tools\vcpkg\vcpkg.exe" `
  -InstallRoot ".\vcpkg_installed-proof2" `
  -OutputRoot ".\artifacts\supply-chain"
```

It records the vcpkg license report and copies every per-package SPDX document
into a hashed evidence bundle. The command intentionally does not use
`-FailOnNoAssertion`; the unresolved records remain a human legal gate.

One target SPDX port record has no installed `copyright` file:
`boost-uninstall`. The `carbon-trinityaudioapi` port now installs its pinned
upstream `LICENSE.md`.

## Current Status

- The final local Debug build passed in the uncommitted working tree.
- The final CTest run passed 19/19 tests, including the loopback fixture.
- The quality and release-boundary checks passed.
- The generated dependency SPDX evidence contains unresolved assertions noted
  above; legal review is still required.
- The focused license and SPDX evidence command is available, but it does not
  convert `NOASSERTION` into an approval.
- A clean committed run, identity review, and public full-build evidence remain
  pending. See `05-ci-evidence.md`.

## Verification Command

```powershell
& ".\tools\cmake\bin\ctest.exe" `
  --test-dir build-proof-space -C Debug --output-on-failure
```

Observed: 19/19 tests passed (see `04-test-evidence.md`).
