# Pillar 1 — Correctness & Exceptions (slice 1: content data layer)

Reference range: `main..HEAD` plus uncommitted slice-1 files.
Independent reviewer: `reviewer` delegate session (round 1, `implements: no`).
Raw output: `evidence/independent-review-round1.txt`.

## Findings (as reviewed, before fixes)

- [Blocker] `src/core/data/EContentFieldType.h:7-13` — the enum declared only
  `Text, Number, Boolean, TextArray, NumberArray`, while
  `src/core/data/FContentLoader.cpp:23,32,46,52,66,87`,
  `src/gameplay/data/FUnitContent.h:35` and
  `src/gameplay/data/FSkillContent.h:31` referenced `EContentFieldType::Id` and
  `EContentFieldType::IdArray`. Slice was unbuildable; the harness reported
  `ok R20 clean` (false negative, pattern does not see enum variants).
- [High] `src/core/data/FContentLoader.cpp:134` — `document.value("schema_version", 0)`
  throws `nlohmann::json::type_error` when the key exists with a non-integer
  type, or when the document is not an object. No `try`/`catch` on this path
  (unlike `src/core/AssetManager.cpp:52-56`), so a malformed asset aborted the
  process instead of logging `nullopt` (R19/R22).
- [High] `src/core/data/FContentLoader.cpp:137-207` — duplicate row `id` accepted
  silently; `src/core/data/FContentTable.h:15-22` `find()` returns the first
  match, so a repeated id shadowed a later row with no log (R22).
- [Medium] `src/core/data/FContentLoader.cpp:198` — the unknown-key loop
  (`for (const nlohmann::json::const_iterator& entry : element.items())`)
  declared the loop variable as an iterator while `items()` yields
  `iteration_proxy_value`, i.e. it did not compile. Found by the implementer's
  syntax-only `cl` check (`evidence/compiler-syntax-check.txt`), not by the
  reviewer.
- [Medium] `src/core/data/FContentRow.h:36-38` — `integer()` truncated
  `double -> int` with no explanation.
- [Medium] `src/core/data/FContentLoader.cpp` — no test seam (R19③): no fixture
  asset exercises missing/unknown/wrong-type/duplicate/`schema_version` paths.
- [Low] `src/gameplay/GameplayServices.cpp:34` — `Shutdown` logs nothing
  (observation debt, not a failure path).

## Fixes applied (implementer)

- Added `Id` / `IdArray` to `src/core/data/EContentFieldType.h` with the storage
  relationship documented (`Id` shares `Text` storage, `IdArray` shares
  `TextArray`); `FContentLoader.cpp`'s three `-Wswitch` switches already covered
  both variants.
- `src/core/data/FContentLoader.cpp`: `document.is_object()` guard, version read
  via `find("schema_version")` + `is_number_integer()`, duplicate-id rejection
  with an insert-only `std::unordered_set` (never iterated, so R16 is unaffected),
  and the `items()` loop variable deduced.
- `src/core/data/FContentRow.h`: documented the truncation and why the loader
  cannot distinguish an int field from a double field.
- Verified with `cl /Zs` (syntax-only, 7 TUs) → clean. Build gate stays
  `PENDING (owner-run)`.

Status: all Pillar-1 findings closed except the test seam, which is recorded as
debt in `pass-2.md` (no test target exists in this repo yet).
