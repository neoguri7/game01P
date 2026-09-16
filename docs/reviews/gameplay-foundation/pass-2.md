# Pillar 2 — Structure (slice 1: content data layer)

Reviewer: `reviewer` delegate session — round 1, `implements: no` (self-approval
rule satisfied: the implementer session did not author this table).
Raw output (unabridged): `evidence/independent-review-round1.txt`.
Range: `main..HEAD` + uncommitted slice-1 files.

## Reviewer calibration (required evidence)

```text
$ bash scripts/verify-changeability.sh --calibrate
changeability bundle: OK (calibrate)

$ bash scripts/verify-changeability.sh
PRE-EXISTING (baselined, not blocking): 27 hit line(s) from tests/changeability/baseline.txt
changeability bundle: OK (repo)
```

Full raw output: `evidence/verify-changeability-calibrate.txt`,
`evidence/verify-changeability-repo.txt`. §5 manual command bundle raw output:
`evidence/section5-manual-checks.txt`.
`NOT CHECKED (manual)` from the script (verbatim): R16 unordered_*, R1 judgement,
R9 revert-diff, R10 abstraction counts, R12 out-of-contract files, R13
optimization evidence, R14 registration sites, R19 test seam, R21 registration
site, R24 observation coverage, R28/R29 absent paths, R30/R31/R32/R33
judgement items. No `OK (repo)` line is used as PASS evidence for these.

## Changed files (with LOC)

- `src/core/data/` (new, 8 files, ~387 LOC): `EContentFieldType.h`,
  `FContentField.h`, `FContentValue.h`, `FContentRow.h`, `FContentTable.h`,
  `FContentSchema.h`, `FContentLoader.h`, `FContentLoader.cpp`.
- `src/core/AssetManager.h` (+54/-11), `src/core/AssetManager.cpp` (new, ~95 LOC),
  `src/core/FAssetDigest.h` (new, ~26 LOC).
- `src/gameplay/` (new, ~287 LOC): `GameplayServices.{h,cpp}`,
  `data/FContentRegistry.{h,cpp}`, `data/FUnitContent.{h,cpp}`,
  `data/FSkillContent.{h,cpp}`.
- `assets/data/{units,skills}.json` (new), `CMakeLists.txt`, `vcpkg.json`,
  `src/main.cpp`, `.pi-lens.json`, `scripts/verify-changeability.sh`,
  `docs/guidelines/game-code-changeability.md`.

## Rule-by-rule verdicts (reviewer round 1 + implementer fixes)

| Rule | Verdict | Evidence |
| ------ | --------- | ---------- |
| R1 | PASS (accepted deviation) | one type per new data file; `src/core/AssetManager.h` held two (`FAssetDigest`+`FAssetManager`) — fixed by moving `FAssetDigest` to `src/core/FAssetDigest.h` |
| R2 | PASS | no `virtual`, no raw owning pointer in `src/core/data/**`, `src/gameplay/**` |
| R3 | PASS | no systems in this slice |
| R4 | PASS | same-line `// fallback:` at `src/gameplay/data/FUnitContent.cpp:9,10,13,16`, `src/gameplay/data/FSkillContent.cpp:9,12`; values supplied by `assets/data/*.json` |
| R5 | PASS | no entity creation (`src/gameplay/factories/` not yet created) |
| R6 | PASS | only static `Shutdown` members |
| R7 | PASS | `rg '#include "(gameplay\|states)/' src/core src/ecs` → none; no `Player`/`Enemy`/`Unit`/`Skill` identifiers in `core/`/`ecs/` |
| R8 | PASS | `EContentFieldType` switched in `src/core/data/FContentLoader.cpp` only |
| R9 | UNVERIFIED | needs revert diff; reviewer's deletability estimate below |
| R10 | PASS (manual) | `FContentTable` 2 instantiations (`src/gameplay/data/FContentRegistry.h:16-17`), `FContentLoader::load` 2 call sites (`src/gameplay/data/FContentRegistry.cpp:28,37`) |
| R11 | PASS | no gameplay state added to `src/states/FBaseState.h` |
| R12 | PASS | edit set matches contract (see contract correction for the boundary wording) |
| R13 | N-A | no measured hot path; debt registered |
| R14 | PASS | `CMakeLists.txt:15` `GLOB_RECURSE ... CONFIGURE_DEPENDS` covers new files |
| R15 | PASS | `std::shared_ptr<const FAssetDigest>` throughout `src/core/AssetManager.cpp` |
| R16 | PASS (manual) | `cache_` only `find`/`emplace` (`src/core/AssetManager.cpp:36,59`); duplicate-id set insert-only (`src/core/data/FContentLoader.cpp`) |
| R17 | PASS (now automated) | `schema_version` in `assets/data/units.json:2`, `assets/data/skills.json:2`; checked in `src/core/data/FContentLoader.cpp` |
| R18 | N-A | no threads |
| R19 | PASS (one debt) | every failure path logs + returns `nullopt`; `try`/`catch` present at `src/core/AssetManager.cpp:52-56`; new guards at `src/core/data/FContentLoader.cpp`; §5③ test seam = debt |
| R20 | FAIL → fixed | undefined `Id`/`IdArray` (Round-1 Blocker); enum extended in `src/core/data/EContentFieldType.h` |
| R21 | PASS | `FAssetManager::Initialize` registered once by `src/core/Engine.cpp:94`; `FGameplayServices::Initialize` called once from `src/main.cpp` |
| R22 | PASS | missing/unknown/wrong-type/**duplicate** key + dangling cross-table skill ref all reject + log (`src/core/data/FContentLoader.cpp`, `src/gameplay/data/FContentRegistry.cpp:49-54`) |
| R23 | FAIL → fixed | string-key reads are in `src/core/data/FContentLoader.cpp` (`find` calls) and now carry `// boundary:`; contract wording corrected |
| R24 | PASS | no new toggles |
| R25 | PASS | only `std::ifstream` in the repo is `src/core/AssetManager.cpp:73,86`; no `std::filesystem` |
| R26 | PASS | no SDL in `src/gameplay/**`, `src/core/data/**` |
| R27 | PASS | no name dispatch, no `void*` payload |
| R28 | N-A | no save/snapshot layer |
| R29 | N-A | single static binary, no boundary struct |
| R30 | PASS | no duplicated basenames over the thresholds |
| R31 | PASS | no `#if`-guarded logs; loaders log state/failure |
| R32 | PASS | `ZoneScopedN` on every load path: `src/core/AssetManager.cpp:30`, `src/gameplay/GameplayServices.cpp:15`, `src/gameplay/data/FContentRegistry.cpp:24`, `src/core/data/FContentLoader.cpp:124` |
| R33 | PASS | `// invariant:` in `src/gameplay/data/FUnitContent.h:26`, `src/gameplay/data/FSkillContent.h:22`; `// 미결(design §N):` stubs; `// boundary:`/`// fallback:` markers |

## N-A debt table

| Rule | Missing path | Debt |
| ------ | -------------- | ------ |
| R13 | no measured hot path | owe `pass-3.md` numbers when one lands |
| R18 | no threads | declare `// thread-affinity:` if added |
| R28 | no save format | `schema_version` required when implemented |
| R29 | no boundary struct | version it if a module boundary appears |
| R19③ | no test target in repo | content-loader failure paths have no executable test; add a fixture + expected-failure test when a test target exists |

## Structure findings and resolution

- [Medium] R23 boundary wording: implementation puts the string-key reads in
  `src/core/data/FContentLoader.cpp` (schema-driven), while `src/gameplay/data/*`
  holds typed decoders only. Reviewer ruled **ACCEPT with a contract correction**
  (this is strictly better for R7 than the contract's literal sentence).
  → `contract.md` §Boundary corrected. Same-line `// boundary:` markers added.
- [Medium] Provisional balance numbers in `assets/data/*.json` were
  indistinguishable from confirmed values (JSON cannot carry `// 미결`).
  → top-level `"_comment"` added to both assets naming them 프로토타입/잠정값;
  `src/gameplay/data/*.cpp` keep the `// 미결(design §N):` + `// fallback:` markers.
- [Medium] `.pi-lens.json` carried a repo-wide `no-bit-fields` disable with no
  reason. → reverted: the finding is the documented pi-lens `.h`-as-C engine
  limit, already recorded in the guideline, so suppressing it repo-wide was wrong.
- [Low] `src/core/AssetManager.h` defined two types → `FAssetDigest` moved to
  `src/core/FAssetDigest.h`. **Correction (round-2 finding):** this does *not*
  "remove nlohmann from every includer" — `src/core/FAssetDigest.h:3` includes
  `<nlohmann/json.hpp>` and `src/core/AssetManager.h:3` includes `FAssetDigest.h`,
  so every includer still pulls nlohmann. The split buys one-type-per-file (R1),
  not dependency narrowing.
- [Low, round 2] `src/core/data/EContentFieldType.h` said `Id` is "validated as a
  cross-table reference", which the loader never does (it treats `Id` as `Text`
  storage); the real check is the consumer loop at
  `src/gameplay/data/FContentRegistry.cpp:49-54` → comment corrected (R33).
- [Low, round 2] a schema declaring its identity field as `Id` was rejected by the
  id-field probe in `src/core/data/FContentLoader.cpp` → the probe now accepts
  `Text` or `Id`, so the type is usable for the field it describes (R20).
- [Low, round 2] `evidence/compiler-syntax-check.txt` lists TU filenames only
  (no command, no exit code) → provenance rewritten in that file; the syntax
  check is recorded as implementer-reported and `UNVERIFIED`, not as clean
  evidence.
- [Low] `integer()` truncation → documented in `src/core/data/FContentRow.h`.

## Change-cost answers (numbers)

- **add** a new content table: 3 files touched (`src/gameplay/data/FXContent.{h,cpp}`
  - `src/gameplay/data/FContentRegistry.{h,cpp}` registration + `assets/data/*.json`);
  0 pre-existing behaviour edited.
- **delete** the slice: ~19 files, `src/main.cpp:36-40` (one call + comment),
  revert `src/core/AssetManager.h` — the boundary file is pre-existing, so the
  revert is not literally one line.
- **tune** a number: yes, `assets/data/*.json` only (code change not required).
- **understand**: 5 files — `src/core/data/FContentSchema.h`,
  `src/core/data/FContentLoader.cpp`, `src/core/AssetManager.h`,
  `src/gameplay/data/FUnitContent.h`, `src/gameplay/data/FContentRegistry.cpp`.

## Verdict inputs

- Deletability: delete `src/gameplay/**` + `src/core/data/**` + `assets/data/*`
  - `src/core/AssetManager.cpp`, remove `src/main.cpp:36-40`, revert
  `src/core/AssetManager.h`.
- Blocker/High counts at review: **Blocker 1 / High 2** → all resolved (see
  `pass-1.md`).
- `build gate: PENDING (owner-run)` — command
  `scripts/check-windows.ps1 -Configuration Debug`. Not run by the reviewer, so
  the gate is `UNVERIFIED`, not PASS.
- Implementer syntax-only check (NOT the gate, no linking, no vcpkg manifest
  install): `cl /nologo /std:c++latest /EHsc /permissive- /utf-8 /W4 /Zs` over the
  7 new/changed TUs → clean, `evidence/compiler-syntax-check.txt`.
