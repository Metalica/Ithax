# CI Pipeline Evidence

Status: The public Stage 1 Validation run passed on `main`. It is a
lightweight repository check; the full build remains a manual workflow and has
not produced a public run.

## Pipeline Definition (current)

| Workflow | Checks |
|----------|--------|
| `stage1.yml` | Pinned Python, quality, release-boundary checks |
| `full-build.yml` | `scripts/build.ps1`, CTest smoke suite |
| `full-build.yml` | Quality, license, SPDX evidence upload |
| Format/lint | Not implemented |
| Dependency vulnerability scan | Not implemented |

## Evidence Fields

| Field | Value |
|-------|-------|
| Provider | [GitHub Actions / other] |
| Repository | https://github.com/Metalica/Ithax.git |
| Workflow files | `.github/workflows/stage1.yml`, `full-build.yml` |
| Commit | `9e45dddfd8b126ced7f1613b28701a95323a04e0` |
| Run URL | https://github.com/Metalica/Ithax/actions/runs/30764714133 |
| Status | Passed; public lightweight validation on `main` |
| Date of run | 2026-08-02 |

## Current Status

- Push and pull-request workflow: fast validation only.
- Full build and test workflow: manual `workflow_dispatch` only.
- Public Stage 1 Validation run: passed; URL recorded above.
- Public full build and test run: not captured; manual workflow remains.

## Rule

Validation evidence must identify the exact workflow and checks performed.
The full-build evidence requires a public run URL with a green status at the
date of submission; a lightweight validation run is not a build result.
