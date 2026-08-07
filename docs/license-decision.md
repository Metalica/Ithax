# Project License Decision

Status: Decided for the current public client repository.
Date: 2026-08-02

## Decision

Original Ithax work in this repository is licensed under the MIT License. The
root `LICENSE` file and `vcpkg.json` use the same SPDX identifier: `MIT`.

Carbon modules and other dependencies are not relicensed by this decision.
Their original licenses, copyright notices, NOTICE files, and attribution
requirements remain applicable to every source or artifact that includes them.

## Rationale

The package metadata, project plan, dependency matrix, and proposal already
identified MIT as the intended license for original client work. The previous
Apache-2.0 root file was an unfilled template and conflicted with that metadata.

## Remaining Review

The contributor policy must identify the applicable copyright-owner process
before accepting external contributions. Legal review remains required for
server separation, proprietary content, optional SDKs, and distribution
notices.
