# Review verdict — gameplay foundation, **slice 1: content data layer**

```text
implements: no
base: main @ 9dc4208
head: feature/gameplay
round 1: REWORK  (Blocker 1 / High 2 / Medium 4 / Low 2)
round 2: REWORK  (9/9 round-1 findings FIXED; 0 new Blocker/High; 4 new Low — all fixed)
status: REWORK — no Blocker/High open; blocked only by UNVERIFIED build gate + R19③ test seam
```

Round 1 (independent reviewer) found one Blocker, two High and several Medium
findings; the implementer fixed them, committed them as `a88fcea`, and round 2
(a different independent reviewer) re-verified every fix from source and re-ran
the harness. Round 2 also executed the `R9` revert test in a `/tmp` copy and
verified the pi-lens false-positive claim.

## What this change contract promised

- **What:** the content data layer — `src/core/data/` (schema types + schema-driven
  JSON loader), `src/core/AssetManager.h|.cpp` + `src/core/FAssetDigest.h`
  (digest-keyed immutable content cache), `src/gameplay/GameplayServices.*` and
  `src/gameplay/data/` (typed projections for units/skills), plus
  `assets/data/{units,skills}.json` and the `nlohmann-json` dependency.
- **Why:** load balance/content from data so a designer can tune numbers without
  recompiling, and so a whole content table can be deleted without touching
  gameplay code.
- **Not doing:** no ECS systems, no entity creation, no save format
  (`schema_version` only anticipates it), no run/hub logic, no rendering, no
  hot-path optimization work (`pass-3.md` is N/A).

Deviation found in round 1 and accepted: the contract's §Boundary sentence said the
string-key JSON reads live in `src/gameplay/data/` decoders. The implementation
puts them in `src/core/data/FContentLoader.cpp` (one schema-driven loader) and
leaves `src/gameplay/data/*` as typed decoders with no JSON. The reviewer ruled the
implementation **better** for `R7`; `contract.md` §Boundary was corrected to match.

## Findings and closure

| # | Sev | Rule | Finding | Status |
| --- | ----- | ------ | --------- | -------- |
| 1 | Blocker | R20 | `EContentFieldType` lacked `Id`/`IdArray` used by the loader + gameplay headers → slice unbuildable | FIXED (round-2 verified) |
| 2 | High | R19/R22 | unguarded `schema_version` read threw `nlohmann::json::type_error` | FIXED |
| 3 | High | R22 | duplicate row `id` silently shadowed the later row | FIXED |
| 4 | Medium | — | `items()` loop declared `const_iterator` over `iteration_proxy_value` (did not compile) | FIXED |
| 5 | Medium | R23 | contract claimed string-key reads in `src/gameplay/data/`; they are in `src/core/data/FContentLoader.cpp` | FIXED (contract corrected) |
| 6 | Medium | R33 | provisional balance values in `assets/data/*.json` indistinguishable from confirmed | FIXED (`"_comment"`) |
| 7 | Medium | R25 | repo-wide `no-bit-fields` disable in `.pi-lens.json` | FIXED (reverted; `git diff main -- .pi-lens.json` empty) |
| 8 | Low | R1 | `src/core/AssetManager.h` defined two types | FIXED (`FAssetDigest.h`) |
| 9 | Low | R33 | `integer()` truncation undocumented | FIXED |
| 10 | Low (r2) | R33 | `EContentFieldType.h` comment promised cross-table validation the loader never does | FIXED (comment now points at `FContentRegistry.cpp:49-54`) |
| 11 | Low (r2) | R20 | id-field probe rejected `Id`, so the type was unusable for row identity | FIXED (probe accepts `Text` or `Id`) |
| 12 | Low (r2) | record | `pass-2.md` claimed the `FAssetDigest` split removes nlohmann from includers | FIXED (correction in `pass-2.md`) |
| 13 | Low (r2) | evidence | `evidence/compiler-syntax-check.txt` was a filename list, not evidence of a clean compile | FIXED (provenance + `UNVERIFIED` caveats) |

## Architecture audit

`docs/architecture/game01P-architecture.md` referenced by the process does not
exist in this repository (verified: `ls docs/architecture/` → `No such file or
directory`). The audit was therefore run against the documents that do exist:

- `PROJECT_VISION.md`, `README.md` — goal: ECS/SDL3 C++23 game, gameplay separated
  from core, data-driven content.
- `docs/design/game-design.md`, `docs/design/game-design-feedback.md` — the
  gameplay model this slice feeds (units/skills tables).
- `docs/guidelines/game-code-changeability.md` — R1–R33 (the review contract).
- `DEV_WORKFLOW.md` — branch/commit/review workflow.

Audit result: the slice follows the documented direction (core knows nothing about
units/skills, `R7` holds — round 2 confirmed the only reference to
`gameplay/` outside gameplay is `src/main.cpp:8`; content is data, not code; the
immutable-cache shape is the one `R15`/`R16` describe). Two audit gaps are owed:

1. No architecture document exists, so "does this match the architecture?" is
   answered only by design docs + guideline, not by a normative architecture spec.
   **Action owed:** write one, or name the design docs as the normative source.
2. The guideline's "known engine limit" section claimed pi-lens C-dispatch false
   positives appear **only** in fixtures and never in an `OK (repo)` scan. That was
   false; the section now records the observed `src/` behaviour
   (`docs/guidelines/game-code-changeability.md:698-706`), and round 2 reproduced
   the mechanism in the analyzer source (`pi-lens` 4.1.6 extension map:
   `c: [".c", ".h"]`). Consequence recorded in the guideline: a harness hit count
   is **not** a finding count, and an `ok R… clean` line must never be used as PASS
   evidence for a `src/` `.h` — this is exactly how the round-1 Blocker slipped
   past `ok R20 clean`.

## Evidence

| Artifact | Path |
| ---------- | ------ |
| Independent review, round 1 (raw) | `docs/reviews/gameplay-foundation/evidence/independent-review-round1.txt` |
| Independent review, round 2 (raw) | `docs/reviews/gameplay-foundation/evidence/independent-review-round2.txt` |
| Harness calibration output | `docs/reviews/gameplay-foundation/evidence/verify-changeability-calibrate.txt` |
| Harness repo output | `docs/reviews/gameplay-foundation/evidence/verify-changeability-repo.txt` |
| §5 manual command bundle output | `docs/reviews/gameplay-foundation/evidence/section5-manual-checks.txt` |
| Syntax-only compile check (provenance + caveats) | `docs/reviews/gameplay-foundation/evidence/compiler-syntax-check.txt` |
| Slice diff (reviewed range) | `docs/reviews/gameplay-foundation/evidence/slice1-review-range.txt` |

Pillar records: `pass-1.md` (correctness), `pass-2.md` (structure),
`pass-3.md` (optimization, N/A — no optimization claim).

## Verdict inputs

- **Blocker / High counts:** two rounds each ended with 0 open Blocker and 0 open
  High; all 13 findings above are closed. Change/disproof evidence is in
  `evidence/independent-review-round2.txt` §1–§2.
- **Change cost, as numbers (round-2 re-measured):**
  - add a content table → 2 new files (`src/gameplay/data/FXContent.{h,cpp}`) +
    edits in `src/gameplay/data/FContentRegistry.{h,cpp}` + 1 data file, 0
    pre-existing behaviour edited;
  - delete the slice → **20 files** delete + `src/main.cpp` **3 edits** (include
    `:8`, `Initialize` `:37`, `Shutdown` `:44`) + restore
    `src/core/AssetManager.h` + revert `CMakeLists.txt` and `vcpkg.json`;
  - tune a balance number → `assets/data/*.json` only, no code change, no rebuild;
  - understand it → 5 files.
- **UNVERIFIED (must not be counted as PASS):**
  - **build gate not run** — `scripts/check-windows.ps1 -Configuration Debug`
    (owner-run; no MSVC/vcpkg in either review environment). The syntax-only
    `cl /Zs` check does not link and does not install the vcpkg manifest.
  - `R19③` test seam — no fixture exercises `FContentLoader::load` failure paths
    (missing / unknown / wrong-type / duplicate / `schema_version`).
  - `R10`/`R16` counts are manual (`rg`), not tool-verified.
  - `R13` N-A (no measured hot path).

## Ready to merge?

**No — but not because of the code.** Nothing Blocker/High is open, and round 2
found the fixes structurally sound. The slice clears when:

1. the owner runs the build gate `scripts/check-windows.ps1 -Configuration Debug`
   and the result is pasted into `pass-2.md` (this is the only thing round 2 could
   not supply);
2. the `R19③` test seam is closed (a fixture + expected-failure test for the loader)
   — or explicitly accepted as debt by the owner;
3. `docs/architecture/game01P-architecture.md` is written or the design docs are
   declared normative (audit gap 1, non-blocking debt).

Merge conflict surface: none — `main` has not moved since `9dc4208`.
