# Contract — gameplay-foundation (Pass 0 oracle)

Branch: `feature/gameplay` (base = `main` @ merge of `feature/docs`).
Design oracle: `docs/design/game-design.md` (DRAFT; only **확정** items may be
implemented — `미결` items are left as `// 미결:` comment stubs, see §Marker).

## What changes

1. **A new gameplay layer `src/gameplay/`** is created (was "미생성(예정)" in the
   changeability guideline adoption table). Layout:
   - `src/gameplay/components/` — gameplay components (`F` prefix, POD only, R2)
   - `src/gameplay/systems/` — gameplay systems (one type per file, R1/R20/R32)
   - `src/gameplay/factories/` — the only entity-creation site for gameplay (R5)
   - `src/gameplay/data/` — typed content projections (per-table struct + `decode*`
     function; no JSON — see §Boundary)
   - `src/gameplay/run/`, `src/gameplay/skills/`, `src/gameplay/dungeon/`,
     `src/gameplay/hub/` — run/meta/rules subsystems
2. **A content data layer `assets/data/*.json`** (was "미생성(예정)"), with
   `schema_version` on every file (R17) and schema validation on load (R22).
3. **`scripts/verify-changeability.sh` and the guideline adoption table** are
   updated so a now-existing `src/gameplay/` and `assets/data/` are verified
   instead of being reported as "미채택 / N-A" forever.
4. **`docs/design/game-design-feedback.md`** — implementer's self-questioning
   findings on `game-design.md` (contradictions, undefined terms, 미결 gaps).
5. **`nlohmann-json`** is added to `vcpkg.json` (header-only) as the JSON parser
   for the data layer.
6. **`src/core/data/`** — the schema + loader (`EContentFieldType`, `FContentField`,
   `FContentValue`, `FContentRow`, `FContentTable`, `FContentSchema`,
   `FContentLoader`) and `src/core/FAssetDigest.h` (split out of
   `src/core/AssetManager.h` when the placeholder boundary became a real one).
   This is the JSON string-key boundary (see §Boundary). `src/core/AssetManager.h`
   keeps only the boundary service itself.

## What must NOT change (oracle)

- **Dependency direction**: `core/` <- `ecs/` <- `gameplay/` (and `states/` may
  use `core/` + `ecs/`). No `gameplay/` (or `states/`) include from `core/` or
  `ecs/` (R7a). No `Player`/`Enemy` identifier in `core/` or `ecs/` **code**
  (R7b) — gameplay vocabulary lives only in `gameplay/`.
- **Platform boundary**: SDL3/OS APIs stay in `src/core/`. `gameplay/` joins
  `ecs/`, `states/`, `debug/` as SDL-free (R26 applied to the new layer).
- **Existing prototype behavior**: title/hub/scene flow, engine ability
  pipeline, existing 5 systems, and `FBaseState`/`GameStateMachine` semantics
  keep working. Gameplay state must never be added to `FBaseState` (R11).
- **Communication**: systems talk through typed `FEventBus` events only (R3,
  R27). No string-name dispatch, no `void*` payloads.
- **Creation**: gameplay entities are created only in `src/gameplay/factories/`
  (R5). `registry.ctx()` service injection is not entity creation.
- **Values**: all balance numbers (HP, AP, cooldowns, ranges, drop tables, room
  counts) live in `assets/data/`; code keeps at most one fallback per value on
  the *same line* with a `// fallback:` marker (R4).
- **Ownership/determinism**: no `new`/`delete`, no owning raw pointers (R15);
  seeded RNG only, no wall-clock in simulation, fixed iteration order (R16).
- **Observation**: every new system has `ZoneScopedN(name())` and a `LOG_*` on
  its state/failure path (R31/R32); no `#if`-guarded logs (R31).
- **Single registration site**: gameplay systems register in
  `RegisterCoreSystems`-style explicit call sites, no self-registering statics
  or registration DSL macros (R21).

## Boundary decision (R23 / R25) — binding for reviews

- **File IO and path assembly** happen only in `src/core/AssetManager.*`
  (R25-allowlisted). Gameplay code passes a **logical asset id** and receives
  text/bytes; it never opens a file and never assembles a path.
- **String-key JSON reads** happen only inside `src/core/data/FContentLoader.cpp`
  (the schema-driven loader), each such line carrying `// boundary:` — the
  per-line allowlist R23/R25 provide. Rationale: a single schema-driven loader
  keeps JSON, key strings and validation in one place, while the decode target
  types (units, skills, runes, rooms, dungeons) stay gameplay concepts in
  `src/gameplay/data/*Content.h|.cpp`, so R7 still holds. **A reviewer that
  rejects the declared boundary must say so as a finding with the alternative**
  (e.g. schema-driven positional decode per gameplay table), not silently pass it.
- Runtime code (systems, run/hub logic, components) holds typed structs only;
  a string-key read outside `src/core/data/FContentLoader.cpp` is a FAIL. The
  literal "string-key reads live in `src/gameplay/data/` decoders" wording this
  section carried in Pass 0 was factually wrong about the implementation and was
  corrected in the round-1 review (see `pass-2.md` → R23).

## Marker convention for undecided design items (R33-compatible)

`game-design.md` states: *미결 항목이 남아 있으면 관련 코드를 쓰지 않는다.*
So undecided values/rules are **not** invented in code. They appear as:

```cpp
// 미결(design §4): 이동 AP 최종 수치 — assets/data/units.json 값으로만 결정.
int moveAp{1};  // fallback: 잠정값 (design §4 확정 잠정치)
```

Rules: the marker names the design section; a provisional numeric value must
also carry `// fallback:` on the same line (R4); no decision is made in code.

## Non-goals (out of scope)

- No rendering of the battle board beyond the existing debug-primitive path.
- No final UI/UX; hub/prep screens stay minimal and data-driven.
- No save/resume format yet (§6 저장/이어하기) — deferred until the run model
  stabilizes; if implemented it must carry `schema_version` (R17/R28).
- No rebalancing of the existing prototype systems.
- No new third-party dependencies other than `nlohmann-json`.

## Build gate

Builds are owner-run (`DEV_WORKFLOW.md` → Builds Are Human-Run). Every review
pass records `build gate: PENDING (owner-run)` with:
`scripts/check-windows.ps1 -Configuration Debug`. Adding `nlohmann-json` means
the owner must re-run the vcpkg manifest install once.
