# Review verdict — gameplay foundation, **slice 1: content data layer**

```text
implements: no
base: main
head: feature/gameplay (working tree, slice 1 uncommitted at review time)
status: REWORK
```

`REWORK` = round 1 (independent reviewer) found one Blocker, two High and several
Medium findings. All Pillar-1 findings are fixed in the working tree; the slice is
**not** cleared to merge until round 2 confirms the fixes and the build gate is
actually run (both outstanding, see "Ready to merge?" below).

## What this change contract promised (revised)

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
units/skills, `R7` holds; content is data, not code; the immutable-cache shape is
the one the guideline's `R15`/`R16` describe). Two audit gaps are recorded:

1. No architecture document exists, so "does this match the architecture?" is
   answered only by design docs + guideline, not by a normative architecture spec.
   **Action owed:** write one, or name the design docs as the normative source.
2. The guideline's "known engine limit" section claims pi-lens C-dispatch false
   positives appear **only** in fixtures and never in an `OK (repo)` scan. That
   claim was false during this review (false positives were observed on `src/`
   `.h`/`.cpp` files). The section now records the observed behaviour; the claim
   itself should be re-verified the next time the harness changes.

## Evidence

| Artifact | Path |
| ---------- | ------ |
| Independent review, round 1 (raw) | `docs/reviews/gameplay-foundation/evidence/independent-review-round1.txt` |
| Harness calibration output | `docs/reviews/gameplay-foundation/evidence/verify-changeability-calibrate.txt` |
| Harness repo output | `docs/reviews/gameplay-foundation/evidence/verify-changeability-repo.txt` |
| §5 manual command bundle output | `docs/reviews/gameplay-foundation/evidence/section5-manual-checks.txt` |
| Implementer syntax-only compile check | `docs/reviews/gameplay-foundation/evidence/compiler-syntax-check.txt` |
| Slice diff (reviewed range) | `docs/reviews/gameplay-foundation/evidence/slice1-review-range.txt` |

Pillar records: `pass-1.md` (correctness), `pass-2.md` (structure),
`pass-3.md` (optimization, N/A — no optimization claim).

## Verdict inputs

- **Blocker / High counts:** round 1 = Blocker 1 / High 2. All fixed.
  Remaining debt: the content-loader failure paths have **no executable test**
  (no test target exists in this repo yet) — recorded in `pass-2.md`.
- **Change cost, as numbers:**
  - add a content table → 3 files (+ 1 data file), 0 pre-existing behaviour edited;
  - delete the slice → ~19 files + `src/main.cpp:36-40` + revert
    `src/core/AssetManager.h`;
  - tune a balance number → `assets/data/*.json` only, no code change, no rebuild;
  - understand it → 5 files.
- **UNVERIFIED (must not be counted as PASS):**
  - `R9` revert diff — estimated, not executed.
  - **build gate not run** — `scripts/check-windows.ps1 -Configuration Debug`.
    Only a syntax-only `cl /Zs` check was run by the implementer
    (`evidence/compiler-syntax-check.txt`); it does not link and does not install
    the vcpkg manifest, so a missing dependency or link error would not appear.
  - `R10`/`R16` counts are manual (`rg`), not tool-verified.

## Ready to merge?

**No.** Blockers to clearing the slice:

1. round 2 independent review of the fixes (Blocker/High closure must be
   re-verified by someone who did not write the fix — the self-approval rule);
2. the build gate actually run and its result pasted into `pass-2.md` as
   `PASS (owner-run)` or a failure;
3. `R9` revert diff executed once the slice is committed.

Neither architecture gaps (above) nor the missing test target block this slice;
they are recorded debt.
