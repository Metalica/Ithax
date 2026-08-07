# Third-Party License Review Packet

Status: Technical provenance inventory only. Ten records now have explicit
SPDX metadata from pinned source notices. No legal review or distribution
approval has been obtained.
The default build remains unsuitable for release until a qualified reviewer
confirms all obligations.

## Evidence Source

The inventory is based on the generated SPDX documents and installed copyright
files under the recorded `vcpkg_installed-proof2` prefix. Recreate it with
`scripts/supply-chain.ps1`. The source revisions are pinned in each portfile or
in the SPDX resource record.

| Package | Candidate expression | Evidence | State |
|---------|----------------------|----------|-------|
| `amd-fidelityfx-cacao` | MIT | Pinned `license.txt` | Metadata corrected; review required |
| `amd-fidelityfx-cas` | MIT | Pinned `LICENSE.txt` | Metadata corrected; review required |
| `bsdiff-drake127` | BSD-2-Clause | Pinned `LICENSE` | Metadata corrected; review required |
| `carbon-trinityaudioapi` | MIT | Pinned `LICENSE.md` | Metadata corrected; review required |
| `crashpad` | Apache-2.0 | Pinned `LICENSE` | Metadata corrected; review required |
| `libb2` | CC0-1.0 | Pinned `COPYING` | Metadata corrected; review required |
| `libyaml` | MIT | Pinned `License` | Metadata corrected; review required |
| `mikktspace` | LicenseRef-MikkTSpace | Pinned `mikktspace.h` | Custom terms; review required |
| `openssl` | OpenSSL | Pinned dual OpenSSL/SSLeay `LICENSE` | Metadata corrected; review required |
| `protobuf` | BSD-3-Clause | Pinned `LICENSE` | Metadata corrected; review required |

“Observed evidence” is not a legal conclusion. In particular, adding a
candidate SPDX identifier to a manifest would not establish that the project
may redistribute the package or that all notices and patent terms are
acceptable. The SPDX bundle must be regenerated after these metadata changes.
All ten candidate expressions now have pinned source evidence. This is not a
legal conclusion; qualified review is still required before distribution.

## Evidence URLs

- CACAO: https://github.com/GPUOpen-Effects/FidelityFX-CACAO/tree/0ddca95e6714727a252ead345591ca8f2598f261
- CAS: https://github.com/GPUOpen-Effects/FidelityFX-CAS/tree/d70012e4afff58e907b0201aac041c8a2679590d
- bsdiff: https://github.com/ccpgames/bsdiff-drake127/tree/8f75c72d64cbca903eaa840d627fab207fdec406
- Crashpad: https://chromium.googlesource.com/crashpad/crashpad/+/db9863a2177a2b3d329bc31fb307302b80b00dd0
- libb2: https://github.com/BLAKE2/libb2/tree/2c5142f12a2cd52f3ee0a43e50a3a76f75badf85
- libyaml: https://github.com/yaml/libyaml/tree/2c891fc7a770e8ba2fec34fc6b545c672beb37e6
- MikkTSpace: https://github.com/mmikk/MikkTSpace/tree/3e895b49d05ea07e4c2133156cfa94369e19e409
- OpenSSL: https://github.com/openssl/openssl/tree/OpenSSL_1_1_1k
- Protobuf: https://github.com/protocolbuffers/protobuf/tree/v3.6.0.1
- Trinity Audio API: https://github.com/carbonengine/trinityaudioapi/tree/0a3a12c8f42e747b5287c7c6cadbaab06bf84786

## Release Decision

Do not publish a binary, installer, or distribution claim from the default
prefix while any record remains `NOASSERTION`. CI may build and upload the
evidence bundle for review; it must not treat a passing build as legal approval.
