# AI Disclosure — Fenris Carbon Pilot Submission

Status: Final, prepared 2026-08-02.

## 1. Purpose

Fenris Creations' published contribution governance requires
contributors to disclose the use of LLM-based tools. In an interview
about the open-source Carbon release (GamesIndustry.biz, 1 July
2026), Ben Hunter, Senior Development Director for Core Technology,
stated:

> "We don't mind you using an LLM, but you have to disclose it
> because we may subject it to different scrutiny than if it were
> not disclosed."

This document is that disclosure, made before submission and
voluntarily. It applies to all engineering work performed for this
pilot to date.

## 2. Official Carbon Open-Source Context

Verified directly from the official public sources:

| Fact | Source |
|------|--------|
| Official GitHub organization | `github.com/carbonengine` (33 public repositories) |
| Official project page | `fenris.com/carbon` |
| Initial public release | 1 July 2026 |
| Primary license | MIT (most modules); spatial audio clustering under Apache License 2.0; IO module under the Python Software Foundation License |
| Maintainer | Fenris Creations (formerly CCP Games) |
| Scope | Cross-platform game engine framework; powers EVE Online and EVE Frontier; ~23 years of live-service operation |

The work submitted under this pilot is built from these official
open-source Carbon modules via the published vcpkg registry, in
accordance with the project's repository and IP separation plan.

## 3. How AI-Assisted Tools Were Used

An LLM-based coding assistant was used as a tool throughout the
engineering workflow. The human engineer owned every decision and
performed final review and acceptance of every artifact.

| Work item | AI-assisted tool role | Human role |
|-----------|----------------------|------------|
| Reproducible build script (`scripts/build.ps1`) | Drafting and debugging the script | Requirements, review, execution on local hardware |
| Pinned Windows CI workflows (`stage1.yml`, `full-build.yml`) | Drafting validation and pinned dependency workflows | Review, policy decisions |
| vcpkg overlay ports and manifest integration | Drafting build recipes and patches | Review, acceptance, license checks |
| Python smoke tests and the loopback conformance fixture | Drafting and implementing test code | Review, execution, verification of results |
| Documentation (build guide, plan, evidence pack) | Drafting text | Fact-checking, approval |
| Verification runs (builds, CTest) | Executing commands and interpreting output | Final sign-off on every result |

## 4. What Was Not AI-Assisted

The following were exclusively human-owned and remain under the
engineer's sole responsibility:

- Architecture and scope decisions, including module selection
- Legal and compliance analysis, including the repository and IP
  separation decisions and this submission's word rules
- The clean-client engineering boundary
- Hardware provisioning and environment configuration
- Final acceptance of all code, documentation, and evidence
- The decision to submit this work to Fenris Creations

## 5. Quality and Verification

All AI-assisted artifacts were treated as unverified drafts until a
human reviewed them. Every code artifact is covered by the project's
verification gates:

- 19/19 CTest tests passing on the recorded working tree (including
  the loopback conformance fixture, which is a test harness with zero
  game logic)
- Local final gate output is recorded; a public full-build run remains pending
- Security practices applied throughout: no secrets in the
  repository, bounded resource use, typed error handling, pinned
  dependencies

No generated artifact was accepted into the repository without human
review and, where applicable, a passing test or build.

## 6. Commitment

- This disclosure accompanies every submission document; nothing in
  the pack is presented as purely human-written work.
- Future contributions will maintain the same disclosure standard
  and will follow Fenris contribution guidelines and testing
  criteria before submission.
- The engineer will provide additional detail about any
  AI-assisted artifact on request, subject to the confidentiality
  of the project's internal analysis documents.

## Sources

- `github.com/carbonengine` — official organization and repositories
- `fenris.com/carbon` — official Carbon project page
- GamesIndustry.biz, "EVE Online's Carbon engine is now open
  source: Fenris Creations explains why", 1 July 2026 (LLM
  disclosure policy and release details)
