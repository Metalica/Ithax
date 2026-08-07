# Repository And IP Separation Plan

Status: Internal governance record

Date: 2026-08-02

This document defines the separation required for public releases, funded
work, and outreach to Fenris Creations. It is an engineering and governance
plan, not legal advice. Counsel must review game-specific interoperability,
proprietary formats, trademarks, SDK terms, and distribution plans.

## Goals

1. Make Carbon contributions independently useful and easy to review.
2. Keep original Ithax runtime work free of server-side implementations and
   extracted content.
3. Prevent proprietary user files from entering source control or releases.
4. Keep AGPL server work out of client dependency and release graphs.
5. Preserve a provable origin and license for every distributed file.
6. Isolate sponsor-confidential work from all public Git history.

## Non-Negotiable Boundaries

- A public Carbon patch must build and test with generated fixtures only.
- The generic runtime must not depend on a game protocol or a
  server-side implementation.
- An interoperability adapter may depend on the runtime; the runtime must not
  depend on the adapter.
- User-supplied content remains outside every Git repository and release.
- Extracted scripts, bytecode, assets, indexes, caches, and converted output
  must never be used as public test fixtures.
- A server-side implementation remains a separate process, repository,
  license domain, install, and release decision.
- Confidential sponsor material must never enter a public fork, issue, action
  log, artifact, or commit message.

## Target Repository Model

### `carbon-trinity-vulkan`

Purpose: A narrow fork of `carbonengine/trinity` for upstreamable renderer work.

Contents:

- TrinityAL Vulkan backend changes.
- SPIR-V shader target and generic shader fixtures.
- Backend conformance tests and generated procedural scenes.
- Required upstream license and third-party notices.

Exclusions:

- Ithax runtime code.
- Protocol, launcher, server, or asset-import code.
- Proprietary assets, shaders, captures, or SDKs.
- Claims that Fenris sponsors or endorses the fork without written approval.

License: Retain Trinity's MIT license and `NOTICE.md`. Preserve all applicable
third-party attribution and altered-source notices.

### `ithax-runtime`

Purpose: Project-owned, game-neutral native runtime and test harness.

Contents:

- Runtime lifecycle and ownership supervision.
- Bounded job execution and process-wide thread budgeting.
- ECS storage and deterministic worker journals.
- Platform, diagnostics, generated scenes, and benchmark harnesses.
- Public Carbon package integration through released or pinned sources.

Exclusions:

- Game-specific service names, packet schemas, authentication, or UI.
- User extraction, converted content, and server startup.
- Carbon source copied without its original license and notices.

License: Original Ithax work is MIT-licensed under the root `LICENSE` and
`vcpkg.json`. Carbon and third-party components retain their own licenses,
copyright notices, and required attribution terms.

### `ithax-content-importer`

Purpose: A standalone tool that reads user-selected local files and writes to a
user-local data directory.

Contents:

- Original importer code.
- Format tests built only from original or freely licensed fixtures.
- A manifest recording source version and local conversion results without
  uploading content.

Exclusions:

- Bundled input or output from a commercial game installation.
- Decompilers, DRM bypasses, keys, credentials, or downloaded content.
- Automatic discovery or upload that is not necessary for local operation.

Release state: Private or unreleased until counsel reviews the exact formats,
workflow, applicable agreements, and proposed distribution.

### `ithax-game-adapter`

Purpose: Optional game-specific bindings above stable runtime interfaces.

Contents:

- Clean, original adapters that have passed legal and provenance review.
- Publicly documented protocol support only when distribution is authorized.
- No copied or translated proprietary implementation.

Release state: Separate and unpublished unless written authorization and legal
review support publication. Carbon and runtime milestones cannot depend on it.

### `server-fork`

Purpose: Any independently maintained server changes.

Boundary:

- Keep the fork in its own AGPL-3.0 repository and build.
- Do not copy server code into an Ithax client repository.
- Publish modifications and corresponding source as the license requires.
- Do not bundle a server with an Ithax release until an AGPL and distribution
  review is complete.
- Do not present network-process separation as a legal conclusion by itself.

The Fenris proposal excludes this repository completely.

### `fenris-private-pilot`

Purpose: Restricted work explicitly covered by a signed agreement or NDA.

Boundary:

- Host it in a separately access-controlled organization or account.
- Do not use a private branch in a public repository as the security boundary.
- Do not merge or transplant confidential history into a public repository.
- Publish an independently reviewed change only after written release approval.
- Keep sponsor credentials, symbols, artifacts, and issue discussions private.

## Dependency Direction

```text
carbon-trinity-vulkan <--- upstream Carbon dependencies
          ^
          |
    ithax-runtime <--- generated fixtures and public test data
          ^
          |
 ithax-game-adapter <--- user-local content directory

ithax-content-importer ---> user-local content directory

server-fork <--- network boundary ---> ithax-game-adapter

fenris-private-pilot ---> public code plus authorized private inputs
```

No arrow may point from public Carbon or runtime code toward proprietary user
content, a server-side implementation, or sponsor-confidential code.

## Content Classification

Every file used by development or release must fit one class:

### Class A: Ithax-Owned Original Work

Allowed in the public runtime after the project license and contribution model
are resolved. Record author, commit, license, and review evidence.

### Class B: Carbon Upstream Work

Allowed under the exact repository license. Preserve copyright, license,
notices, attribution, and any altered-source requirement.

### Class C: Other Open-Source Work

Allowed only after compatibility, version, notice, source-offer, and binary
redistribution requirements are recorded in a dependency inventory.

### Class D: Sponsor-Confidential Work

Allowed only in the restricted sponsor repository under the signed agreement.
It cannot be inferred, summarized, or ported into public code without review
and written release approval.

### Class E: User-Supplied Proprietary Content

Allowed only in a user-local runtime directory. It is never a repository file,
CI input, uploaded diagnostic, release artifact, screenshot, or benchmark
fixture without explicit written permission.

### Class F: AGPL Server Work

Allowed only in the separately licensed server fork. Client code may implement
an independently specified network interface, subject to legal review, but may
not copy server implementation code.

Unclassified content is prohibited by default.

## Contribution And Copyright Model

Choose one model before accepting an external contribution:

1. Use DCO sign-off and keep contributor copyrights under one public license.
2. Use a reviewed CLA if relicensing, assignment, or sponsor sublicensing is a
   real requirement.
3. Use direct copyright assignments only when contributors knowingly sign a
   separate agreement.

DCO sign-off alone does not assign copyright. Do not promise Fenris exclusive
rights, relicensing rights, or ownership of community contributions unless the
actual agreements provide them.

Required contribution rules:

- The contributor owns the work or is authorized by its owner and employer.
- The contribution contains no confidential, decompiled, or extracted code.
- The contribution contains no proprietary test data or generated derivatives.
- The contributor identifies copied or adapted open-source code and its license.
- Every commit includes the required `Signed-off-by` line when DCO is selected.
- Upstream Carbon changes follow the upstream repository's contribution rules.

Required repository files:

- `LICENSE`
- `NOTICE.md` or `THIRD_PARTY_NOTICES.md`
- `CONTRIBUTING.md`
- `SECURITY.md`
- `CODE_OF_CONDUCT.md`
- A DCO or CLA policy
- A machine-readable dependency lock and software bill of materials

Use SPDX identifiers for new source files where the chosen license and file
style support them. Do not replace upstream headers without permission.

## CI And Release Controls

Public CI must:

- Build from a clean checkout without access to user or sponsor directories.
- Use generated, original, or clearly licensed test fixtures.
- Scan commits and artifacts for secrets, archives, binaries, and prohibited
  content signatures.
- Verify dependency licenses and required notices.
- Produce an SBOM for distributable binaries.
- Publish only artifacts selected by an explicit release allowlist.
- Keep logs free of paths, usernames, credentials, and user content names.
- Fail when an input lacks a provenance classification.

`.gitignore` is a convenience, not a compliance control. A file that was once
committed remains in history even after it is ignored or deleted.

If prohibited content enters Git history:

1. Stop pushes, CI publication, and releases.
2. Restrict access and preserve the facts needed for incident review.
3. Notify the project owner and counsel.
4. Remove public artifacts and rotate any exposed secret.
5. Rewrite history only under a reviewed, coordinated remediation procedure.
6. Re-run provenance and release scans before restoring publication.

## Sponsor Workflow

1. Send only the public proposal and a clean, reproducible evidence bundle.
2. Start with public Carbon code and generated fixtures whenever possible.
3. Sign a written scope, acceptance, payment, and IP agreement before work.
4. Sign an NDA before receiving any confidential material or repository access.
5. Create the restricted repository only after its owner and access list exist.
6. Record each input as public, confidential, or prohibited before use.
7. Obtain written approval before publishing sponsor-reviewed work.
8. Keep game-specific authorization separate from Carbon engineering approval.

## Current Outreach Blockers

The following must be fixed or explicitly disclosed before claiming release
or distribution readiness:

- Copyright ownership for new Ithax files is not documented.
- No DCO, CLA, or contribution policy is checked in.
- `docs/gap-analysis.md` contains stale legal conclusions and calls licensing
  "cleared" even though the development plan reopens that gate.
- The gap analysis says the project must accept no sponsors to preserve a
  claimed Wwise non-commercial license. Sponsorship invalidates that operating
  assumption; Wwise must stay disabled unless separate written terms allow it.
- The gap analysis references an older server baseline and overstates legal and
  trademark certainty.
- The 19-test result is historical; public Stage 1 validation passes, but the
  committed full build and current installed dependency state remain pending.
- Public-facing metadata uses game trademarks and needs a consistent
  nominative-use disclaimer and legal review.
- The public release process has no automated prohibited-content or notice gate.

Do not send `docs/gap-analysis.md` as an external legal or licensing statement
in its current form.

## Readiness Gates

### Gate 1: Ownership And Licensing

- Preserve the MIT decision for original Ithax work and record the copyright
  owner for every original file.
- Record the copyright owner for every original file.
- Select DCO or CLA before accepting contributions.
- Generate and verify third-party notices.
- Review Wwise, Granny, 3Dconnexion, and other optional SDK terms separately.

### Gate 2: Repository Separation

- Establish the Carbon fork and generic runtime boundaries.
- Keep importer, adapter, server, and sponsor work separate.
- Remove all cross-boundary build dependencies.
- Document user-local data locations and ensure release exclusion.

### Gate 3: Reproducible Evidence

- Restore dependencies from pinned manifests in a clean environment.
- Configure, build, and test without private or user content.
- Add CI for the public default feature set.
- Preserve raw test and benchmark metadata.
- Distinguish historical evidence from current results.

### Gate 4: External Review

- Review the proposal for technical accuracy and unsupported claims.
- Have counsel review game-specific language and planned distribution.
- Confirm that the contact package contains no confidential or user content.
- Request a Carbon technical scoping call through Partner Relations.

## Immediate Execution Order

1. Preserve the MIT license decision and document the copyright owner in a
   short ADR.
2. Select DCO or CLA and add the contribution policy.
3. Correct stale legal, Wwise, version, and build claims in the gap analysis.
4. Reproduce the public Carbon integration from a clean checkout and add CI.
5. Split Carbon, runtime, importer, adapter, and server work along this plan.
6. Prepare a small public evidence bundle with generated fixtures only.
7. Send the Carbon pilot proposal to Fenris Partner Relations.
8. Do not begin game-specific or confidential work without separate written
   authorization.

## Public References

- Fenris Carbon: <https://fenris.com/carbon>
- Fenris contact routes: <https://fenris.com/contact-us>
- Carbon Trinity: <https://github.com/carbonengine/trinity>
- Developer Certificate of Origin: <https://developercertificate.org/>
- SPDX: <https://spdx.dev/>
