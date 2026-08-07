# Engineering And Security Practices

Status: Review draft. Commitments, not claims. Every change submitted to
Carbon under this engagement follows these practices.

## Reproducibility

- Dependencies are pinned by the committed vcpkg registry baselines, manifest
  constraints, and pinned source revisions in overlay ports.
- Build recipes live in the repository with exact tool versions,
  flags, and platform notes (see the build guide).
- Every benchmark or test run records: commit, compiler, flags, OS,
  hardware, workload, and raw output.
- A clean-environment build must succeed from the documented recipe
  alone — no undocumented local state.

## Code Quality

- Lines limited to 80 characters; auto-formatter enforced.
- Strict typing throughout; no implicit or untyped fallbacks.
- Named constants only; no magic numbers in logic.
- Functions are small and single-purpose (5-20 lines target, 50
  maximum).
- Feature-based file organization; no mixed-purpose modules.

## Errors

- Errors are typed and explicit — never silently swallowed.
- No empty catch blocks; every failure path is handled or surfaced.
- Failures are logged with context: stage, input reference, and
  outcome.

## Security

- No secrets, keys, or credentials in code, logs, or commits.
- All external input is validated before use; user data is never
  interpolated into executable contexts.
- Bounded loops and bounded memory; no unbounded resource use.
- Structured, leveled logging with no personal data.
- Dependency scanning remains a release-readiness task; the current CI
  workflows do not claim that scan.

## Testing

- All new code ships with tests; changes that touch behavior must
  not lower coverage.
- Integration smoke coverage is required per enabled module.
- Push CI runs fast repository validation. The full build and CTest suite are
  available through the manual `full-build.yml` workflow.

## Provenance And Licensing

- Every file retains its applicable license and third-party notices.
- Carbon-derived changes keep the upstream license and attribution.
- New project-owned work is licensed deliberately and documented
  before funded implementation begins.
- Nothing enters a public repository without a provenance review.

## Delivery

- Upstream-reviewable commits: small, documented, rebase-friendly.
- No validation or synchronization-validation findings in exercised
  acceptance tests.
- Claims are evidence-backed: commit, logs, and reproducible
  commands, never anecdotes.
