# Gap Analysis — EVE Open Source Client

> ## INTERNAL ONLY — NOT PART OF THE FENRIS SUBMISSION
>
> This document contains internal server identifiers, target builds,
> asset paths, and reverse-engineering detail. Distributing it would
> contradict the proposal's scope boundary. The curated, distribution
> ready version of findings lives in
> `docs/fenris-submission/02-findings-summary.md`. Do not attach this
> file to any external email, issue, or repository.

**Stage 0 deliverable.** Every component with status: **OPEN** / **CLOSED** / **NEEDS-RE** / **MUST-BUILD**.

Audit date: 2026-07-24. Target build: 3396210. Target client assets:
`C:\EVE Online - 3396210` (EVE V24.01, build 3396210).

Status legend:
- **OPEN** — available open source, usable now
- **CLOSED** — proprietary, not accessible; must be replaced or worked around
- **NEEDS-RE** — exists but requires reverse engineering / extraction from official client
- **MUST-BUILD** — does not exist anywhere; we must create it

---

## 1. Carbon Engine Repos (github.com/carbonengine)

All 33 repos accounted for; 32 cloned to `external/carbon/` (plus `.github` org meta, not cloned). All build with CMake + vcpkg.json unless noted.

| # | Repo | License | CMake | Status | Notes |
|---|------|---------|-------|--------|-------|
| 1 | **core** | MIT | yes | **OPEN** | OS abstractions. Deps: tracy, gtest, lz4, python3. Build first. |
| 2 | **math** | MIT | yes | **OPEN** | Deps: gtest, directxmath. Standalone. |
| 3 | **scheduler** | MIT | yes | **OPEN** | Greenlet tasklets. Deps: python3, greenlet, carbon-core. Keep for Python compat only; job system replaces it. |
| 4 | **io** | **PSF-2.0** | yes | **OPEN** | Modified Python socket/ssl. Deps: carbon-scheduler, libuv, openssl. |
| 5 | **blue** | MIT | yes | **OPEN** | Python/C++ interop + game loop + Black serialization. Deps: blueexposure, core, io, pdmprotowrapper, scheduler, exefile, openssl, libyaml, zlib, curl. |
| 6 | **blueexposure** | MIT | yes | **OPEN** | Blue exposure layer. Deps: python3, carbon-core. |
| 7 | **exefile** | MIT | yes | **OPEN** | Host process + Crashpad. Deps: carbon-core, python3, crashpad. |
| 8 | **exefileconsole** | MIT | yes | **OPEN** | Console variant. No carbon deps. |
| 9 | **mesh** | MIT | yes | **OPEN** | Mesh/skeleton/animation. Deps: carbon-math, meshoptimizer, mikktspace. |
| 10 | **imageio** | MIT | yes | **OPEN** | Image I/O. Deps: libpng, libjpeg-turbo, openvdb, carbon-core. |
| 11 | **imagetools** | MIT | yes | **OPEN** | Deps: blueexposure, math, imageio, nvtt. |
| 12 | **resources** | MIT | yes | **OPEN** | Resource packaging + CLI. Deps: bsdiff-drake127, cryptopp, curl, inih, yaml-cpp. |
| 13 | **trinity** | MIT | yes | **PARTIAL** | Renderer boundary: stub loaded; **shadercompiler closed** via in-repo provider build (pinned source + pinned directx-dxc) with fixture evidence at workers 1/2/4. trinityal dx11/dx12/metal, Eve space scene (EveSpaceScene, EveProjectBracket, EveTacticalOverlay, EveStarfield, EvePlanet, turrets) remain open. Deps: core, imageio, trinityaudioapi, pdmprotowrapper, blue, math, parser, mesh + tbb, vulkan, freetype, amd-fidelityfx, libsquish. |
| 14 | **trinityaudioapi** | MIT | yes | **OPEN** | No carbon deps. |
| 15 | **destiny** | MIT | yes | **OPEN** | Physics: Ball, Ballpark, collision, partition. Deps: blue, core, math, exefile-interpreter. Single-threaded assumptions — refactor for physics thread. |
| 16 | **pathfinder** | MIT | yes | **OPEN** | Deps: blueexposure, core, exefile-interpreter. |
| 17 | **audio** | MIT | yes | **OPEN (conditional)** | Wwise wrapper + prioritization. **Requires Wwise SDK (free Audiokinetic license) + VS2026 v145 toolset.** Stage 1 currently uses a Wwise-free stub. |
| 18 | **localization** | MIT | yes | **OPEN** | Deps: blue, blueexposure, exefile-interpreter. |
| 19 | **db** | MIT | yes | **OPEN** | Deps: msoledbsql, blue, exefile-interpreter. Windows-only DB driver. |
| 20 | **parser** | MIT | yes | **OPEN** | Shader parser (re2c + lemon). Deps: re2c, lemon-parser-generator. Generator tools are cached/installed; shadercompiler integration closed in the trinity row. |
| 21 | **geo2** | MIT | yes | **OPEN** | Deps: core, math. |
| 22 | **d3dinfo** | MIT | yes | **OPEN** | Deps: blue. GPU info. |
| 23 | **grpc** | MIT | yes | **BLOCKED** | Generated PDM sources require a newer Protobuf ABI; opt-in only. |
| 24 | **prometheus** | MIT | yes | **OPEN** | Metrics. Deps: prometheus-cpp, curl, openssl. |
| 25 | **pdm** | MIT | yes | **OPEN** | Deps: gtest only. |
| 26 | **pdm-proto-wrapper** | MIT | yes | **OPEN** | Deps: protobuf, carbon-pdm. |
| 27 | **videoplayer** | MIT | yes | **OPEN** | Deps: libvorbis, libvpx, libnestegg, blue, trinity, trinityaudioapi. |
| 28 | **ime** | MIT | yes | **OPEN** | Input method editor. Deps: blue. |
| 29 | **spacemouse** | MIT | yes | **OPEN** | 3Dconnexion. Deps: 3dxwaresdk (proprietary SDK, in registry). |
| 30 | **spatial-audio-clustering** | **Apache-2.0** | no (Premake) | **OPEN** | Wwise plugin. No CMake — needs Premake or manual integration. |
| 31 | **red-to-black-converter** | MIT | no | **OPEN** | src/tests only. Standalone tool, integrate manually. |
| 32 | **vcpkg-registry** | MIT | no | **OPEN** | 62 ports, 16 versioned. Already in `tools/vcpkg-registry/`. |

The default Stage 1 manifest intentionally excludes Carbon gRPC while this
generated-code and Protobuf ABI mismatch is unresolved. The CMake smoke target
is opt-in through `ITHAX_ENABLE_CARBON_GRPC`.

### Dependency build order (carbon-internal, topologically sorted)

```
core, exefileconsole, math, parser, pdm, prometheus, resources, spacemouse, trinityaudioapi
→ blueexposure, exefile, imageio, scheduler, geo2, mesh, pdm-proto-wrapper, grpc
→ pathfinder, imagetools, io
→ blue
→ audio, d3dinfo, db, destiny, ime, localization
→ trinity
→ videoplayer
```

Note: `carbon-exefile-interpreter` (dep of pathfinder/db/destiny/localization) is a **virtual port** bundling exefile+blue+python3 — resolved by the registry, not a missing repo.

### Perforce-gated dependencies

**Finding: no build-blocking Perforce gates.** Perforce appears only in:
- `.teamcity/` CI configs (`PublishToPerforce.kt` — artifact publishing, CCP-internal)
- `INSTALL_TO_MONOLITH` CMake option (default **OFF**, "backwards compatibility")
- `cmake/templatePackageConfig.cmake.in` (error message text only)

Nothing required to compile or link is behind Perforce. The plan's Stage 3.7 stub work is reduced to: Wwise SDK (audio), 3dxwaresdk (spacemouse), and any missing headers discovered during first build.

### CarbonUI location

**CarbonUI is NOT in any open Carbon repo.** Zero matches for `carbonui`/`CarbonUI` across all 33 repos. Trinity references `uilib`/`uicore` only in comments.

**However: CarbonUI ships inside the official client** — see §3. It is 254 Python modules in `code.ccp` (`carbonui/` package). The plan's assumption "CarbonUI is closed-source C++ on Perforce" is **wrong**: it is Python, extractable from the client. This turns Stage 7 from "rebuild UI from scratch" into "extract + host CarbonUI Python on our engine" — a fundamentally easier problem, legally contingent on users owning the client (same as assets).

---

## 2. External Server-Side Integration Boundary

The external server-side reference is a separate AGPL-3.0 component. It is not
part of this repository or its distribution artifacts. The audited target is
EVE 24.01 build 3396210, matching the local client install.

### Protocol (machoNet over TCP)

| Aspect | Finding |
|--------|---------|
| Transport | TCP, port 26000 (configurable), IOCP on Windows client |
| Wire format | `[4-byte LE payloadLen][0x7E magic][4-byte LE mapcount=0][marshaled payload]` |
| Serialization | **Custom EVE marshal** (EVEmu EVEMarshalOpcodes), NOT Protobuf. PyNone/PyToken/PyLong/PyReal/PyTuple/PyDict/PyObjectEx/PackedRow/etc. + shared string table |
| Crypto | AES-256-CBC (32-byte session key, zero/derived IV, PKCS#7). `cryptoPack = Placebo` in start.ini |
| Handshake | 6-step PLACEBO flow (EVEmu EVEClientSession model): VersionExchangeServer → VersionExchangeClient → VK command → CryptoRequestPacket ("OK CC") → CryptoChallengePacket → CryptoHandshakeResult → SessionInit. States: WAIT_VERSION → WAIT_COMMAND → WAIT_CRYPTO → WAIT_AUTH → WAIT_FUNC_RESULT |
| Server arch | Node.js monolith + worker threads (persistenceWorker), SQLite stores, Rust market daemon (separate process) |

**Plan correction:** the plan says "machoNett protocol (TCP + Protobuf)" —
wrong. It is a custom Python-marshal wire format. Separate protobufs exist for
gRPC/Quasar, but the client↔server machoNet protocol is marshal. Our client
network stack must implement the marshal codec, not Protobuf.

### RPC surface: 1,585 handlers across 63 service categories

Largest: corporation (246), character (199), ship (80), structure (73), support (65), fleets (54), dogma (52), mail (47), account (44), map (43), skills (42), inventory (38), station (37), agent (35), planet (30), activity/dungeon (28), market (27).

**machoNet core:** GetInitVals, GetGlobalConfig, GetServerStatus, GetNodeID/FromAddress, GetServiceInfo, GetTime, ForwardCharacterNotification + session mgr.

**Inventory (validates server-authoritative design):** Add, MultiAdd, MultiMerge, StackAll, GetContainerContents, GetInventory(FromId), AssembleCargoContainer, FitFitting, StripFitting, DestroyFitting, List/DroneBay/FighterBay/FuelBay, TrashItems, capacity/validation RPCs. Everything the plan's Stage 13.2 requires already exists server-side. **Status: OPEN (server side done; client view layer MUST-BUILD).**

### World sim (`server/src/space/`)

asteroids, combat, **destiny** (server-side movement), movement (incl. warp), modules, npc, runtime, shipDestruction, tickProfiler, transitions, structureTethering. Plus **existing mods**: `mods/multicore/` (worker pool, broadcast bridge, compute workers) and a carbonengine/destiny physics port (force-acceleration model, `ENABLE_CARBONENGINE` flag) — directly relevant to our grid/Tidi design.

### Startup sequence (auth → in-world)

1. TCP connect → 6-step handshake (above) → session established
2. `machoNet.GetInitVals` / global config / server status
3. Authentication service validates account (`services/login`, `accountStore`, SQLite)
4. Character list (`services/character`, 199 RPCs) → select → session init
5. World entry: solar system state, destiny movement tick, inventory/dogma services live

Crypto/encryption: only the session AES-256-CBC above. No TLS on the game port; `certs/` dir exists for the HTTPS/image endpoints.

---

## 3. Official Client Assets (`C:\EVE Online - 3396210`)

47.2 GB shared-cache install, build 3396210 (V24.01), `server = 127.0.0.1`,
`port = 26000`, `cryptoPack = Placebo` (already pointed at localhost).

### Layout

```
C:\EVE Online - 3396210\
├── tq\
│   ├── bin64\          159 .pyd + 42 .dll (engine binaries)
│   │   ├── packages\   certifi, evemap, monolithgeoip2
│   │   └── staticdata\ mapObjects.db (SQLite)
│   ├── code.ccp        29.3 MB zip — ALL Python game scripts (12,527 .pyj)
│   ├── res\            (loose override resources)
│   ├── resfileindex.txt     121,894-entry resource manifest
│   └── start.ini       version/build/config
├── ResFiles\           content-addressed asset store (00..ff buckets)
└── index_tranquility.txt    228-entry binary/launcher manifest
```

### Resource manifest (`resfileindex.txt`, 121,894 files)

| Format | Count | What |
|--------|-------|------|
| .png | 38,342 | UI textures, icons |
| .dds | 30,777 | Ship/world textures (BC1-BC7) |
| .black | 18,520 | Blue-serialized compiled resources |
| .gr2 | 14,113 | Granny 3D models/animations |
| .yaml | 7,856 | Configs, FSD static data |
| .wem | 3,112 | Wwise audio |
| .jpg/.webm/.srt | ~3,000 | Images, video, subtitles |
| .color/.type/.prs/.proj | ~5,200 | UI style/type system |
| .bnk | 29 | Wwise soundbanks |
| .ttf/.otf | 32 | Fonts |
| .fsdbinary/.pickle | 163 | FSD binary static data |

Top dirs: `res:/dx9` (56.9K — meshes/effects, legacy name), `res:/ui` (22.6K), `res:/graphics` (20.6K), `res:/animation_gstate` (13.2K), `res:/texture` (3.2K), `res:/audio` (3.1K), `res:/fisfx` (1.2K), `res:/staticdata` (203).

**Plan correction:** no `.stuff` archives, no `.red` files in this build. Modern client = ResFiles content-addressed store + `.black` + `.gr2`. Stage 8's "Stuffit archive reader" is **obsolete** — replace with ResFiles store reader (trivial: index → bucket path) + `.black` (Blue serializer, open in carbon-blue) + `.gr2` (Granny — proprietary, NEEDS-RE or converter).

### Python scripts (`code.ccp` — the "not open source" game logic)

`.pyj` = `zlib(magic[4] + mtime[4] + marshal'd code object)` — standard **Python 2.7 bytecode** (magic `03f30d0a`), `python27.dll` in bin64 (Stackless). Decompilable with uncompyle6/pycdc-class tooling.

| Package | Modules | Status |
|---------|---------|--------|
| `eve/` | 3,586 | **NEEDS-RE** — all game logic. Extractable + decompilable. |
| `eveProto/` | 2,392 | **NEEDS-RE** — protobuf message classes (gRPC/Quasar events) |
| `carbon/` | 377 | **NEEDS-RE** — engine-side Python (bindings glue) |
| **`carbonui/`** | **254** | **NEEDS-RE** — **the entire UI framework, present and extractable** |
| `eveui/` | 92 | **NEEDS-RE** — EVE-specific UI widgets |
| `behaviors/`, `nodegraph/`, `jobboard/`, `dogma/`, `spacecomponents/`, etc. | ~1,500 | **NEEDS-RE** — gameplay systems |

### Engine binaries (bin64) → map to open repos

`blue.dll`, `_trinity_dx11.dll`, `_trinity_dx12.dll`, `_destiny.dll`, `_geo2.dll`, `_audio2.dll`, `_evelocalization.dll`, `_ime.dll`, `_pyevepathfinder.dll`, `_pyfsd.dll`, `_spacemouse.dll`, `_videoplayer.dll`, `_d3dinfo.dll` — every native engine module ships as a DLL whose source is **OPEN** in the Carbon repos.

The 159 `.pyd` files are mostly generated **static-data loaders** (`*Loader.pyd` — dogma attributes, types, factions, etc.): thin C accessors over SDE tables. Not engine logic; replaceable by loading the same static data directly.

Also present: `python27.dll`, TracyClient.dll, tbb12.dll, amd_fidelityfx_dx12.dll, libxess.dll, nvngx_dlss*.dll, sl.*.dll (NVIDIA Streamline), GFSDK_Aftermath, rad_tm_win64.dll (RAD Telemetry), MSVC runtimes.

---

## 4. Licensing Compliance Matrix

| Component | License | Compatible with MIT client? | Action |
|-----------|---------|------------------------------|--------|
| 30 Carbon repos | MIT | ✅ Yes | Use freely; retain copyright notices |
| carbon-io | PSF-2.0 | ✅ Yes (permissive) | Retain PSF notice |
| spatial-audio-clustering | Apache-2.0 | ✅ Yes | Retain NOTICE file |
| **External server-side component** | **AGPL-3.0** | ✅ Yes **as separate process** | Keep the component separate from the MIT client; publish any modifications as required and do not link or embed its code into the client. |
| EVE client assets/scripts | Proprietary (CCP) | ⚠️ Conditional | Never redistribute. Ship an **extraction tool**; users supply their own client. Same model as texture-pack tools for other games. |
| Wwise SDK | Proprietary (Audiokinetic) | ⚠️ Conditional | Project is **open source + non-commercial (no money, period)** → qualifies for Audiokinetic's free license. Each contributor obtains their own SDK copy; the SDK is never redistributed. Keep zero monetization of any kind (no sales, donations, sponsors, ads) so non-commercial status stays unambiguous. |
| 3dxwaresdk | Proprietary (3Dconnexion) | ⚠️ Conditional | Optional (spacemouse only); stub by default |
| NVIDIA (DLSS/Streamline/Aftermath), RAD Telemetry | Proprietary | N/A — **dropped** | **Not needed, excluded from the project.** Upscaling/sharpening is covered by AMD FidelityFX (MIT), which is already a trinity dependency (amd-fidelityfx-cas/cacao). No NVIDIA or RAD SDKs will be used or shipped. |
| **EVE Online trademark** | CCP Games | ✅ **Resolved** | The client is named **Ithax** — no "EVE" in the name, branding, UI, artwork, or screenshots. Nothing in the client visually resembles EVE branding. Trademark issue: closed. (Copyright in CCP's *assets/scripts* is a separate matter — handled by the extraction-tool model in this table and §4.1.) |

**AGPL note:** The external server-side component remains separate from the
client. Any modifications to that component must be shared as required by its
license; no server-side source is part of this client repository.

### 4.1 DMCA / takedown risk after going public (honest assessment)

**Short answer: risk is LOW if we stay disciplined, but it is never zero.** (Not legal advice; get a real lawyer before the public alpha if this matters to you.)

**What keeps us safe:**

1. **We ship zero CCP content.** No assets, no decompiled scripts, no textures, no models, no UI art in the repo or releases. Users extract from *their own* legally-owned client with our tool. This is the same model as game-mod tools and has strong precedent.
2. **Trademark: closed.** Name is Ithax, no EVE branding anywhere. Most C&D letters in the fan/mod scene are trademark-driven ("confusingly similar name/logo") — that vector is gone.
3. **No anti-circumvention problem (DMCA §1201).** `code.ccp` is a plain zip;
   `.pyj` is zlib + standard Python 2.7 marshal; ResFiles is an unencrypted
   content-addressed store; the protocol crypto (AES-256 "Placebo") is
   documented in the separately licensed server-side reference. Reading
   unencrypted files you own, and interoperating with a documented protocol, is
   not "circumventing a technological protection measure." No DRM is cracked
   anywhere in this project.
4. **Interoperability precedent.** Clean-room reverse engineering for interoperability has strong legal precedent (Sega v. Accolade, Sony v. Connectix). The protocol was documented from the *server's* open AGPL source, not by cracking the client.
5. **CCP's track record.** Public server projects have existed for ~15+ years without a CCP takedown; one open Node.js server project runs publicly with an active community. CCP tolerates non-commercial, non-infringing fan/reverse-engineering projects.
6. **Non-commercial, period.** No sales, no donations, no sponsors, no ads. Money is what turns tolerated projects into targets.

**What could still go wrong:**

- A **bogus or overbroad DMCA notice** — any company can file one even without a valid claim. GitHub will take the repo down on receipt; we file a counter-notice and it comes back in ~14 days. Annoying, survivable, unlikely given nothing infringing is in the repo.
- **Scope creep into infringement** — someone commits extracted assets or decompiled `.py` files into the repo "temporarily." Mitigation: hard repo rule — nothing from `code.ccp`/`ResFiles` ever gets committed; extraction tool outputs stay on the user's machine only; CI check that greps for CCP asset signatures.
- **CCP policy change** — they could decide to hostile-action all fan projects tomorrow. Nothing protects against that except the goodwill we've built by being clean, non-commercial, and non-competitive (local play against a self-hosted server is not a Tranquility competitor).
- **Trademark backslide** — using "EVE" in the project name, repo name, screenshots with EVE logo, or EVE art as the project avatar would reopen the trademark vector. Don't.

**Bottom line:** name it Ithax, ship no CCP bytes, crack no DRM, make no money — and a legitimate DMCA claim has nothing to grab. A bogus one is possible but recoverable via counter-notice.

---

## 5. Consolidated Status Board

### OPEN (use now, no work beyond integration)
All 32 Carbon repos (core, math, scheduler, io, blue, blueexposure, exefile, exefileconsole, mesh, imageio, imagetools, resources, trinity, trinityaudioapi, destiny, pathfinder, audio*, localization, db, parser, geo2, d3dinfo, grpc, prometheus, pdm, pdm-proto-wrapper, videoplayer, ime, spacemouse*, spatial-audio-clustering, red-to-black-converter, vcpkg-registry) — *audio/spacemouse conditional on proprietary SDKs*
External server-side component (AGPL, separate process), including the
documented inventory, market, chat, and world-simulation RPC surface
ResFiles asset store format (index-driven, no archive format to crack)
`.black` resource format (Blue serializer is open source in carbon-blue)

### NEEDS-RE (extract/decompile from user-owned client)
EVE Python game scripts (`eve/` 3,586 modules) — decompile .pyj → .py
**CarbonUI** (`carbonui/` 254 modules) — extract + host, replaces "rebuild UI from scratch"
eveui, behaviors, nodegraph, dogma Python, spacecomponents (~1,500 modules)
eveProto protobuf classes (2,392) — for gRPC/Quasar events
`.gr2` Granny 3D models (14,113 files) — proprietary format; community converters exist (gr2→gltf), or write loader
FSD static data (.fsdbinary/.pickle/yaml + `staticdata/mapObjects.db`)
Wwise .wem/.bnk audio (3,112 + 29) — playable through Wwise SDK with user's license

### CLOSED (proprietary, no access — work around)
Wwise SDK itself (per-dev free license; not redistributable)
NVIDIA DLSS/Streamline/Aftermath, RAD Telemetry (**dropped — not used by this project**; AMD FidelityFX (MIT) covers upscaling)
CCP's launcher/SSO/patch CDN (replace with a thin launcher to the approved
server-side interface)
Live Tranquility economy/official servers (out of scope by design)

### MUST-BUILD (exists nowhere)
Job system (Taskflow integration) + ECS (EnTT) + lock-free queues — the multicore foundation
machoNet client: marshal codec + AES-256-CBC session + 6-step PLACEBO handshake
(documented from the separately licensed server-side reference)
Python 2.7 embedding layer for decompiled scripts (client ships python27.dll semantics; our Blue hosts CPython — version strategy decided in Stage 6)
UI render host for extracted CarbonUI (Trinity Sprite2d backend — hooks exist in open trinity)
ResFiles asset pipeline (index reader → DDS/PNG loader → GPU)
Grid/KnownList protocol extension (new message types per plan §4.2) + grid
subdivision manager (plan §10.9)
Bracket-mode renderer using Trinity's EveProjectBracket/EveTacticalOverlay primitives
Thin launcher (auth → token → spawn client)
Extraction tool (assets + scripts from user-owned client) — ships separately

---

## 6. Key Corrections to the Development Plan

1. **Protocol is NOT Protobuf.** machoNet = custom EVE marshal +
   AES-256-CBC. Rewrite Stage 4 accordingly (marshal codec, PLACEBO handshake
   — documented in the approved server-side reference; no Wireshark reverse
   engineering needed for the basics).
2. **CarbonUI is Python, ships in the client.** Stage 7 changes from "build UI framework from scratch" to "extract carbonui/ + build a Trinity-backed render host". Stage 6.4 CarbonUI stubs remain useful as an intermediate step.
3. **No .stuff/.red in build 3396210.** Stage 8 asset work = ResFiles store + .black + .gr2. Drop the Stuffit reader; add a .gr2 strategy (convert at extraction time).
4. **Perforce gates are a non-issue.** Stage 3.7 shrinks to Wwise/3dxware stubs only.
5. **Python is 2.7 (Stackless), not 3.11+.** Toolchain and Blue embedding strategy must target Python 2.7 semantics (or a transpile-to-3 decision in Stage 6 — flag as a decision point).
6. **The server-side reference already has multicore and physics work** — a
   server-side foundation for the grid architecture partly exists.

---

## 7. Environment Verified (Stage 1 pre-requisites already met)

| Tool | Location | Version |
|------|----------|---------|
| CMake | `tools/cmake` | 3.30.3 |
| Python | `tools/python` | 3.12.6 + pip |
| Node.js | `tools/node` | 22.5.1 (system: 24.18.0) |
| Git | `tools/git` (MinGit) | 2.46.0 (system: 2.55.0) |
| Ninja | `tools/ninja` | 1.12.1 |
| vcpkg | `tools/vcpkg` | 2026-07-13 |
| LLVM/Clang | `tools/llvm` | 18.1.8 (MSVC target; ASan/UBSan yes, TSan no) |
| VS Build Tools | system | 2022 (17.14) — note: carbon-audio wants VS2026/v145; all other repos accept older |
| Carbon registry | `tools/vcpkg-registry` | 62 ports |
| gh CLI | `tools/gh` | authenticated (Metalica) |

**Stage 0 gate: PASSED** — catalog complete, no unknown-unknowns remaining in the critical path (CarbonUI located, protocol documented, assets cataloged, licenses cleared).
