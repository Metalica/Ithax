# Third-Party Notice

Original project-owned work is dual-licensed:

- The MIT License in `LICENSE` applies to the general project codebase.
- The PolyForm Noncommercial License 1.0.0 in `LICENSE-POLYFORM` applies to
  the Ithax multicore foundation (Stage 3 and Stage 10 work) and the Ithax
  TrinityAL Vulkan backend (Stage 5 work), including the sources under
  `src/trinityal-vulkan/`. Commercial use of that work requires a separate
  written license from the copyright holder.

This file records the principal external components used by the default build.
It is not a substitute for the license and notice files installed by each
dependency.

## Carbon Engine

Most enabled Carbon modules are MIT-licensed. Carbon IO is distributed under
the Python Software Foundation License. The applicable upstream license and
notice files are installed by the vcpkg ports and must remain with releases.

## Proprietary Components

- Microsoft OLE DB Driver for SQL Server is proprietary and remains governed by
  Microsoft's vendor terms.
- Wwise, Granny, and 3Dconnexion SDK integrations are optional and are not
  redistributed by the default build.

## Reproducibility

The source revisions and registry baselines are recorded in
`vcpkg-configuration.json`, the overlay portfiles, and `BUILD_GUIDE.md`.
Release artifacts require a generated dependency inventory and SBOM from the
committed build.
