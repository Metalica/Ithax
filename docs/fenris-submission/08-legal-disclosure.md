# Legal Disclosure — Fenris Carbon Pilot Submission

Status: Draft, prepared 2026-08-02; legal and identity review remain pending.

## 1. Nature Of This Document

This disclosure accompanies the Fenris Carbon Pilot Submission. It
states, plainly, what the submission is and is not, and what legal
and license limits apply. Nothing in this pack is legal advice; the
engineer accepts responsibility for the accuracy of the submission
and will correct any error on request.

## 2. What This Submission Is

- Reusable Carbon engineering: build reproducibility, test
  infrastructure, documentation, and (under proposal milestones)
  a Vulkan path and a bounded multicore prototype for Carbon
  Trinity.
- Work derived from the official open-source Carbon modules
  (github.com/carbonengine), used under their published licenses.
- Disclosed use of LLM-based coding assistance (see
  `07-ai-disclosure.md`), in line with Fenris's published
  contribution governance.

## 3. What This Submission Is Not

- Not a game client, launcher, protocol adapter, authentication
  system, or server-side implementation.
- Not derived from an unreviewed server-side project.
- Not a distribution of EVE Online game content: no proprietary
  game assets, game scripts, art, data files, SDKs, or confidential
  code are included in or claimed by this submission.
- Not a claim of production readiness, performance, or compatibility
  beyond what the recorded evidence shows.

## 4. License Compliance

| Component | License | Compliance |
|-----------|---------|------------|
| Carbon engine modules (trinity, core, blue, mesh, destiny, etc.) | MIT License | Attribution and notices preserved; MIT terms apply |
| `carbon-trinityaudioapi` 2.0.3 | MIT License | Pinned `LICENSE.md` is preserved; legal review required |
| Spatial audio clustering module | Apache License 2.0 | License text and notices preserved where used |
| Carbon IO module | Python Software Foundation License | PSF terms apply where used |
| New project-owned work (scripts, tests, docs) | As stated per artifact; no third-party license is infringed |

Source revisions are pinned in the manifest, registry baselines, and overlay
portfiles. Ten previously unresolved records now carry explicit candidate SPDX
metadata backed by pinned notices. The historical SPDX report contains ten
`NOASSERTION` records and must be regenerated. Distribution compliance is not
claimed until source and legal review is complete.

The detailed package evidence and source links are recorded in
`docs/license-review.md`. This packet is technical evidence for legal review,
not a legal conclusion.

## 5. No Warranty, No Guarantee

All artifacts are provided as-is, without warranty of any kind,
express or implied, including fitness for a particular purpose. A
passing build or test proves the recorded run only. No outcome —
funding, acceptance, or future maintenance — is promised or implied.

## 6. Evidence Honesty

- Every claim of a build, test result, or benchmark is tied to a
  recorded commit, compiler version, build flags, OS, driver, and
  hardware.
- Any claim that cannot be reproduced from a committed revision is
  marked pending, not claimed.
- The engineer will disclose any error found after submission and
  provide corrected evidence.

## 7. Verification Sources (from us, verified 2026-08-02)

The following sources were retrieved and read directly during
preparation. Each states what it verifies:

| # | Source | What it verifies |
|---|--------|------------------|
| 1 | `github.com/carbonengine` (organization page) | Official org: 33 public repositories, Iceland, project page `fenris.com/carbon`, C++/CMake, EVE Online & EVE Frontier |
| 2 | `github.com/orgs/carbonengine/repositories` | Repo list and license column: MIT for trinity, core, blue, mesh, destiny, audio, and most modules; IO listed under a non-MIT license |
| 3 | `fenris.com/carbon` (official page) | "Cross-platform game engine framework"; components Trinity, Destiny, CarbonUI, CarbonIO, CarbonAudio, Python scripting; official repo links |
| 4 | `github.com/carbonengine/.github` + `CODE_OF_CONDUCT.md`, `SECURITY.md` (raw) | Organization-wide community health files; governance contact (`opensource@ccpgames.com`) |
| 5 | GamesIndustry.biz feature, "EVE Online's Carbon engine is now open source: Fenris Creations explains why" (1 July 2026) | Release rationale; MIT focus; LLM disclosure policy quote ("We don't mind you using an LLM, but you have to disclose it…"); security posture; Godot governance discussions; plugin architecture plans |
| 6 | OpenSourceForU, "MIT-Licensed Carbon Engine Is Now Open Source On GitHub" (July 2026) | License split: MIT primary, Apache 2.0 spatial audio clustering, PSF License for IO; commercial use permitted |
| 7 | GamingOnLinux, "Carbon engine framework powering EVE Online is now open source" (1 July 2026) | Release date; Fenris Creations formerly CCP Games; scope (engine framework, not live game) |
| 8 | GameDeveloper.com, "EVE Online's cross-platform engine framework goes fully open source" (6 July 2026) | Corroborates release and repository location |

Cross-checks: the license claims in sources 2, 3, 5, and 6 agree;
the release date claim in sources 5, 7, and 8 agrees. No source
conflicts with another on any fact used in this pack.

## 8. Contact

Corrections, provenance requests, or clarification requests
regarding this disclosure may be directed to the engineer via the
contact details in the proposal.
