# Fenris Submission Pack — README

Status: Review-ready draft. Public repository validation is recorded, but
legal review and committed full-build evidence remain open.

## What This Pack Is

The assembled draft documentation for the Fenris Creations engineering
pilot proposal. Every document here is
word-checked against the submission word rules — only clean client
wording is used. The client is referred to as a **clean client**
throughout.

## Pack Contents (send order)

| # | Document | Purpose |
|---|----------|---------|
| 1 | `00-cover-letter.md` | Email draft + submission cover |
| 2 | `docs/fenris-carbon-pilot-proposal.md` | The proposal itself (parent folder) |
| 3 | `01-engineering-practices.md` | Engineering and security commitments |
| 4 | `02-findings-summary.md` | Audited gaps, neutral engineering tone |
| 5 | `03-reproducible-build.md` | Clean-build evidence |
| 6 | `04-test-evidence.md` | Test suite evidence (CTest 19/19) |
| 7 | `05-ci-evidence.md` | CI pipeline evidence |
| 8 | `06-scope-and-boundaries.md` | Clean client scope statement |
| 9 | `07-ai-disclosure.md` | AI-assisted work disclosure (required by Fenris contribution governance) |
| 10 | `08-legal-disclosure.md` | Legal disclosure and review status |

## Before Sending (fill these in)

- [ ] `00-cover-letter.md`: [NAME], [EMAIL]
- [ ] `docs/fenris-carbon-pilot-proposal.md`: Name, email, repository
      URL at line 7
- [ ] `03-reproducible-build.md`: local result recorded; committed clean run
      and transcript still need attaching
- [ ] `04-test-evidence.md`: final CTest result recorded; transcript still
      needs attaching
- [ ] `05-ci-evidence.md`: validation link pasted; full-build link still pending
- [ ] `08-legal-disclosure.md`: dependency metadata and unresolved Carbon
      Trinity Audio API record reviewed
- [ ] `02-findings-summary.md`: verify findings still match the
      audited baseline commit
- [x] `07-ai-disclosure.md`: drafted with verified official sources;
      no further action needed unless facts change

## Evidence Mapping

Each proof-pack item maps to a document here:

| Evidence item | Document |
|---------------|----------|
| 1. Proposal with identity filled | 00 + proposal |
| 2. Clean build from scratch | 03 |
| 3. CI evidence | 05 |
| 4. Test evidence 19/19 | 04 |
| 5. Loopback conformance evidence | 03 (conformance check section) |
| 6. Repository separation (Gates 1-4) | 06 + parent-folder
      `repository-ip-separation-plan.md` |
| 7. Scope statement verbatim | 06 |
| 8. AI-assisted work disclosure | 07 |
| 9. Legal disclosure + verified sources | 08 |

## Word Rules (never violated)

- Do not include prohibited vocabulary, internal references, or private
  metadata in the attachments.
- Use: **clean client**, **server-side implementation**,
  "reusable Carbon engineering".
- `docs/gap-analysis.md` is INTERNAL ONLY and must never be attached.

## After Submission

1. Record the submission in the knowledge base under
   `fenris_submission_stage1`.
2. Keep the dated submission record with the evidence archive.
