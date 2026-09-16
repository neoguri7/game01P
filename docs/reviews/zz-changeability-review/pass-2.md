# Changeability Review — untracked Zz* feature set (2025 dry-run validation)

Change set (feature-branch equivalent, all untracked):
src/ecs/components/FZzWeapon.h, src/ecs/components/FZzBad.h, src/ecs/components/FZzGood.h,
src/ecs/systems/ZzComboSystem.h, src/ecs/systems/ZzWeaponSwitchB.h, src/states/ZzWeaponSwitchC.h,
src/ecs/ZzStateLeak.h, src/core/ZzLeak.h

Rules source: docs/guidelines/game-code-changeability.md (R1-R14, §5)
Procedure source: docs/guidelines/agents/changeability-reviewer.md (Pass A-D)
Build/test NOT run; build success is not approval evidence (guidelines §4).

## Contract check

- in-scope: the 8 files above.
- out-of-scope / unrelated edits: README.md (M, +7 lines — not part of the code change set),
  docs/guidelines/** (new docs, the rule source itself).
- Change set is purely additive: 0 pre-existing files modified. But none of the new systems
  (ZzComboSystem, ZzExtraSystem, ZzLegacyPath) is registered anywhere — see R14 note.

## Rule table

| Rule | Verdict | Evidence | Note |
| ------ | --------- | ---------- | ------ |
| R1 | FAIL | src/ecs/systems/ZzComboSystem.h:12,40,52 | 3 systems in one file; `ZzComboSystem` name vs body (moves+stamps+reads weapon) mismatch |
| R2 | FAIL | src/ecs/components/FZzBad.h:8,9,10 | virtual method + virtual dtor + owning raw `std::string*` in a component; plus system mutable member src/ecs/systems/ZzComboSystem.h:13 |
| R3 | FAIL | src/ecs/systems/ZzComboSystem.h:3 | includes MoveSystem.h and constructs it at src/ecs/systems/ZzComboSystem.h:15 (direct system-to-system call) |
| R4 | FAIL | src/ecs/systems/ZzComboSystem.h:18,19,20 | magic `0.35f`, `120.0f`, hardcoded `"player_idle.png"`; no assets/data source |
| R5 | FAIL | src/ecs/systems/ZzComboSystem.h:43-45 | `reg.create()` + 2x `emplace` in a system; no factory used |
| R6 | FAIL | src/core/ZzLeak.h:11,12 | `static ZzSettings& instance()` + `getInstance()` singleton; also file-scope mutable global src/core/ZzLeak.h:20 |
| R7 | FAIL | src/core/ZzLeak.h:2 ; src/ecs/ZzStateLeak.h:2,3 | core includes `gameplay/Player.h`; ecs includes `states/GameStateMachine.h` + `states/FHubState.h` |
| R8 | FAIL | src/ecs/systems/ZzComboSystem.h:30-33, src/ecs/systems/ZzWeaponSwitchB.h:6-9, src/states/ZzWeaponSwitchC.h:7-10 | EWeapon switch duplicated at 3 sites; new weapon kind forces edits in 3 files |
| R9 | FAIL | src/ecs/systems/ZzComboSystem.h:51 | `#ifdef ENABLE_ZZ_VERIFY` keeps `ZzLegacyPath` as dead code inside a shared system file |
| R10 | N-A | src/ecs/systems/ISystem.h:20 | change set adds no interface/template/policy class; existing `ISystem` has >=6 impls. Evidence needed to make it PASS-relevant: a new abstraction in the diff (none present) |
| R11 | FAIL | src/ecs/components/FZzBad.h:11,12 | `isDead`/`isAttacking` bool state soup; no gameplay transition table exists under docs/ (only the rule text mentions it) |
| R12 | FAIL | src/ecs/components/FZzBad.h:5 vs src/core/ZzLeak.h:7 | `kZzMaxSpeed = 3.5f` duplicated across two layers (single source violated) |
| R13 | FAIL | src/ecs/systems/ZzComboSystem.h:49-51 | "cache-friendly rewrite, much faster" claim with no ZoneScoped/measurement in the new files |
| R14 | PASS | CMakeLists.txt:15 | `GLOB_RECURSE ... CONFIGURE_DEPENDS` auto-includes new files; single registration point src/core/SystemManager.h:23. Caveat: no registration line was added, so the new systems are dead code, not integrated |

## Change-cost numbers

- add: 0 pre-existing files touched; 0 registration lines added (expected 0-1 + 1 registration line).
  The new systems are unregistered, so the "feature" is inert dead code.
- delete: whole feature = 8 files deleted + 0 reference lines (nothing registered).
  Weapon sub-feature only = 5 files deleted (FZzWeapon.h, ZzComboSystem.h, ZzWeaponSwitchB.h,
  ZzWeaponSwitchC.h, plus the FZzBad coupling) + 3 switch sites edited.
- tune: NO — balance values (0.35f, 120.0f, damage 10/7/12/9, icon paths) are compiled-in.
  No `assets/data/` directory exists in the repo.
- understand: >=5 files (ZzComboSystem.h, ISystem.h, MoveSystem.h, FZzWeapon.h, FZzBad.h,
  SystemManager.h) > 3 => coupling smell (R1/S1).

## Findings (Blocker > High > Medium > Low)

### Blocker

- [Blocker][P1][R7] src/core/ZzLeak.h:2 — core includes non-existent `gameplay/Player.h`
  (`src/gameplay/` does not exist). Hard dependency-direction violation AND a build break.
  Why it raises cost: core can no longer compile/be reused without gameplay.
  Minimal fix: delete line 2; move any settings to registry.ctx().
- [Blocker][P2][R7] src/ecs/ZzStateLeak.h:2-3 — ecs layer includes states/ headers
  (`GameStateMachine.h`, `FHubState.h`). Breaks `core <- ecs <- gameplay` and the
  explicit "ecs must not know states/". Minimal fix: delete both includes; this file should
  not exist in ecs/.
- [Blocker][P2][R6] src/core/ZzLeak.h:11-12 — classic singleton (`instance()` + `getInstance()`)
  plus file-scope mutable global at src/core/ZzLeak.h:20. Why: global state makes
  add/remove/test unpredictable. Minimal fix: delete `ZzSettings` statics; inject via
  `registry.ctx().emplace<T>()`.
- [Blocker][P1][R2] src/ecs/components/FZzBad.h:8-10 — component carries virtual `update()`,
  virtual dtor, and owning raw `std::string*`. Contract §1: components = pure data.
  Why: any change to component layout/logic now touches a polymorphic type used across systems.
  Minimal fix: strip virtuals and the raw pointer; move logic to a system.
  Note (UNVERIFIED, P1): `virtual void update(float dt);` is declared but has no definition
  anywhere in src/ — if FZzBad's vtable is emitted this is an ODR/link error. Would need a
  link step to confirm; not run per instructions.

### High

- [High][P2][R8] src/ecs/systems/ZzComboSystem.h:30-33, src/ecs/systems/ZzWeaponSwitchB.h:6-9,
  src/states/ZzWeaponSwitchC.h:7-10 — same EWeapon switch at 3 sites. Adding one weapon kind
  forces edits in 3 files (rule triggers at >=3). Minimal fix: data table / Type Object keyed
  by EWeapon with one lookup, or per-kind data in config.
- [High][P2][R5] src/ecs/systems/ZzComboSystem.h:43-45 — entity assembled inline in a system.
  Second place that builds the same FZzBad+FZzWeapon combo will drift. Minimal fix: add a
  factory under src/core/factories/ and call it.
- [High][P2][R3] src/ecs/systems/ZzComboSystem.h:3,15 — includes and directly invokes MoveSystem.
  Why: system ordering and construction become hidden; deleting MoveSystem breaks this file.
  Minimal fix: remove the include + `MoveSystem mover;` and communicate via FEventBus typed event.

### Medium

- [Medium][P2][R1] src/ecs/systems/ZzComboSystem.h:12,40,52 — three systems in one file, name/content
  mismatch. Minimal fix: split into ZzComboSystem.h / ZzExtraSystem.h or drop the extras.
- [Medium][P2][R4] src/ecs/systems/ZzComboSystem.h:18,19,20 — magic numbers and asset path in code.
  Minimal fix: move to config (single code fallback allowed).
- [Medium][P2][R11] src/ecs/components/FZzBad.h:11-12 — bool-flag gameplay state. Minimal fix:
  replace with tag components and document the transition table.
- [Medium][P2][R12] src/ecs/components/FZzBad.h:5 vs src/core/ZzLeak.h:7 — duplicated `3.5f`.
  Minimal fix: one owning constant in the correct layer.
- [Medium][P2][R9] src/ecs/systems/ZzComboSystem.h:51 — `#ifdef ENABLE_ZZ_VERIFY` dead-code gate in
  a shared file. Minimal fix: delete `ZzLegacyPath` or isolate it in its own file.

### Low

- [Low][P3][R13] src/ecs/systems/ZzComboSystem.h:49-51 — performance claim with no measurement.
  P3 has no ZoneScoped evidence in the new files, so the claim is unsupported. No non-hot-path
  rewrite requested.

### PRE-EXISTING (excluded from blocking count)

- src/core/InputState.h:94-100 — EInputAction switch (scoped enum); single site, pre-existing, not R8.
- src/ecs/systems/DebugPrimitiveRenderSystem.h:80 — `6.28318530717958647692f` is a math constant, not R4.
- src/ecs/components/FSprFrame.h:13, src/core/Time.h:16 — pre-existing float literals, not R4.

## §5 grep FALSE POSITIVES (verified by reading)

- R7b Player|Enemy in src/core/ZzLeak.h:17,18 - comment lines only ("negative control"). FALSE POSITIVE as violations; the real R7 hit is the include at src/core/ZzLeak.h:2.
- R7a gameplay/ in src/core/ZzLeak.h:18 - comment only. FALSE POSITIVE.
- R4 kZzFallbackGravity = 120.0f src/core/ZzLeak.h:21 - explicitly the single allowed code fallback. FALSE POSITIVE per R4's "fallback default constant 1 only".
- R4 FZzGood.h:5 radius{0.5f} - default field value of a POD component. Borderline; treated as a single fallback default, not a magic-number FAIL (LOW at most).
- R2 src/ecs/components/FZzGood.h:4 - the matched token is the word "virtual" inside a comment. FALSE POSITIVE.
- R3 systems/.*System.h pattern - most hits are the include of ecs/systems/ISystem.h (the contract header, allowed) in MoveSystem.h:3, SpriteRenderSystem.h:3, CollisionSystem.h:7, AnimationSystem.h:4, DebugPrimitiveRenderSystem.h:7. FALSE POSITIVES; the only real violation is src/ecs/systems/ZzComboSystem.h:3 (MoveSystem.h).
- R6 int staticInstanceCount = 0; src/core/ZzLeak.h:20 - did NOT match the section 5 pattern (static .*instance needs a space). Not a grep hit at all; it is a file-scope mutable global, counted under R6 by reading, not by grep.
- R8 case E[A-Za-z]*:: - matches every scoped-enum case label. src/core/InputState.h:94-100 is unrelated pre-existing EInputAction noise. Only the 3 EWeapon sites are real.
- R10 class I[A-Z] - returned 0 hits; it misses struct ISystem (src/ecs/systems/ISystem.h:20). Pattern does not implement the rule as written.

## Verdict

- Status: RESET
- Blocking findings: 4 (R7 x2, R6, R2 - plus the P1 build break)
- Rationale: fixed-contract section 1 is violated in three independent axes (dependency direction core->gameplay and ecs->states; singleton; non-POD component) and the change does not compile as written. These are not local cleanups, so REWORK-grade patching is insufficient.
- Deletability statement: deleting this feature = remove 8 files + 0 registration lines (nothing is registered). The weapon sub-feature specifically needs 5 files deleted + 3 switch sites edited.
- Not verifiable without: a link/compile run (FZzBad vtable / missing Player.h), a decision on whether factories/ and assets/data/ are intended to exist, and the intended game-design spec (PROJECT_VISION.md declares no committed genre).

## Reviewer self-audit

1. Ambiguous / no measurable trigger in the guideline text:
   - R10's check is class I[A-Z], but the repo's only interface is struct ISystem; the rule gives no trigger for struct interfaces or for counting implementers.
   - R14 says "minimize registration burden" but gives no verdict rule for the case where a new system is never registered at all (dead code) - is that a PASS (nothing to register) or a FAIL (not integrated)? I recorded PASS with a caveat.
   - R9 gives a threshold ("5+ references") but no rule for an ifdef-gated path that lives inside an otherwise-feature file; I judged it FAIL at Medium.
   - R4's "single code fallback default constant" does not say whether default member initializers count as that one fallback; I treated FZzGood.h:5 as allowed.
2. Section 5 grep noise:
   - class I[A-Z] misses struct I* (no hit despite ISystem).
   - static .*instance misses staticInstanceCount (no space) - under-matches file-scope globals.
   - case E[A-Za-z]*:: matches all scoped enums, mixing unrelated pre-existing code.
   - virtual|~[A-Z] matches the word inside comments such as FZzGood.h:4.
   - systems/.*System.h pattern matches the allowed ISystem.h contract include, not just cross-system includes.
3. Not judgeable from source alone:
   - Whether gameplay/Player.h is expected to be added later (currently non-existent -> build break).
   - Whether FZzBad's virtual update definition exists in a way that avoids a link error (needs a link).
   - Whether an assets/data/ config pipeline is planned; the directory does not exist today, so "tune without recompiling" cannot be satisfied by this change either way.
