# Cover Letter — Fenris Creations Engineering Pilot

Status: Draft. Fill placeholders, attach pack, send via
`partner.relations@fenris.com` (fallback `info@fenris.com`).

Date: 2026-08-02

## Email Draft

**Subject: Carbon Trinity Vulkan + Multicore Engineering Pilot —
Proposal and Evidence**

Hello Fenris Creations,

I have attached a proposal for a paid, milestone-based engineering
pilot on Carbon Trinity: a first-class Vulkan path through TrinityAL,
followed by a measured, bounded multicore prototype. The proposal
covers scope, acceptance principles, and an IP model; each milestone
is independently reviewable, and you may stop, redirect, or extend
the engagement after any milestone.

Supporting evidence is attached and summarized here:

- **Reproducibility.** A pinned Carbon integration manifest, a
  documented build guide, and a clean-build procedure. `docs/
  fenris-submission/03-reproducible-build.md` records the recipe and
  the current local gate output; the committed clean-run log remains
  pending.
- **Tests.** 19/19 tests pass on the recorded uncommitted working tree;
  the raw transcript remains pending in `04-test-evidence.md`.
- **CI.** Public Stage 1 repository validation passes; the full build remains
  a manual workflow. Details are in `05-ci-evidence.md`.
- **Licensing.** Nine dependency metadata records have pinned-source evidence;
  `carbon-trinityaudioapi` remains open for legal review.
- **Findings.** A neutral audit of the audited Trinity baseline is in
  `02-findings-summary.md` — engineering gaps, not defect claims.
- **Governance.** Repository and IP separation is documented in the
  referenced separation plan, so contributions are upstream
  reviewable and independently useful.

I request a 30-minute technical scoping call with the Carbon renderer
or engine owner to discuss the branch, the problems worth solving,
and the contribution, CI, license, and hardware process. The call
details and open questions are in the proposal's "Requested Next
Step" section.

Thank you for your time.

[NAME]
[EMAIL]
https://github.com/Metalica/Ithax.git

## Attachment Checklist

- [ ] `fenris-carbon-pilot-proposal.md`
- [ ] `01-engineering-practices.md`
- [ ] `02-findings-summary.md`
- [ ] `03-reproducible-build.md`
- [ ] `04-test-evidence.md`
- [ ] `05-ci-evidence.md`
- [ ] `06-scope-and-boundaries.md`
- [ ] `repository-ip-separation-plan.md`
- [ ] `07-ai-disclosure.md`
- [ ] `08-legal-disclosure.md`

## Final Checks Before Send

- [ ] No forbidden vocabulary anywhere in the attachments (submission
      word rules).
- [ ] No internal references or private metadata in the attachments.
- [ ] The proposal's remaining placeholders ([NAME], [EMAIL]) are filled.
- [ ] Every evidence file references a real, dated run.
