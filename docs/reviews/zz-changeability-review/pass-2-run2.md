# Changeability Review — untracked Zz* change set (dry-run #2, independent)

Change set (feature-branch equivalent, all untracked new files):
`src/ecs/components/FZzWeapon.h`, `src/ecs/components/FZzBad.h`, `src/ecs/components/FZzGood.h`,
`src/ecs/systems/ZzComboSystem.h`, `src/ecs/systems/ZzWeaponSwitchB.h`, `src/states/ZzWeaponSwitchC.h`,
`src/ecs/ZzStateLeak.h`, `src/core/ZzLeak.h`

Rules source: `docs/guidelines/game-code-changeability.md` (R1-R14, §5).
Procedure source: `docs/guidelines/agents/changeability-reviewer.md` (Pass A-D).
Build/test NOT run; build success is not approval evidence (guidelines §4).

## Contract check

- in-scope: the 8 files above.
- out-of-scope / unrelated edits (Pass A, R12): `README.md` (modified, M), `docs/guidelines/**`
  (new rule source), `docs/reviews/**` (review record). None is part of the stated code change set.
- The 8 in-scope files are all additions → 0 pre-existing code files modified.
- Pass 0 oracle: `docs/reviews/zz-changeability-review/contract.md` does not exist (only `pass-2.md`
  from dry-run #1). No committed oracle to check the change against; judged against §1 fixed contract.

## Rule table

| Rule | Verdict | Evidence | Note |
| ------ | --------- | ---------- | ------ |
| R1 | FAIL | `src/ecs/systems/ZzComboSystem.h:12`, `:40`, `:52` | three system structs in one file; `ZzComboSystem` name vs body (moves + reads weapon + dead legacy) mismatch |
| R2 | FAIL | `src/ecs/components/FZzBad.h:8`, `:9`, `:10` ; `src/ecs/systems/ZzComboSystem.h:13` | component has virtual method + virtual dtor + owning raw `std::string*`; system keeps mutable member `acc` |
| R3 | FAIL | `src/ecs/systems/ZzComboSystem.h:3`, `:15-16` | includes `MoveSystem.h` and constructs/calls `MoveSystem` directly |
| R4 | FAIL | `src/ecs/systems/ZzComboSystem.h:18`, `:19`, `:20` ; `src/ecs/systems/ZzWeaponSwitchB.h:6-9` ; `src/states/ZzWeaponSwitchC.h:7-10` | magic `0.35f`, `120.0f`; hardcoded `"player_idle.png"`, `"sword.png"`… ; no `assets/data/` directory exists |
| R5 | FAIL | `src/ecs/systems/ZzComboSystem.h:43-45` | `reg.create()` + 2x `emplace` inline in a system; no factory under `src/core/factories/` used |
| R6 | FAIL | `src/core/ZzLeak.h:11`, `:12`, `:20` | singleton `instance()`/`getInstance()`; file-scope mutable global `staticInstanceCount` |
| R7 | FAIL | `src/core/ZzLeak.h:2` ; `src/ecs/ZzStateLeak.h:2`, `:3` | `core/` includes `gameplay/Player.h`; `ecs/` includes `states/GameStateMachine.h` + `states/FHubState.h` |
| R8 | FAIL | `src/ecs/systems/ZzComboSystem.h:29-34` ; `src/ecs/systems/ZzWeaponSwitchB.h:5-10` ; `src/states/ZzWeaponSwitchC.h:6-11` | one `EWeapon` enum switch duplicated at 3 sites; a new weapon kind forces edits in 3 files |
| R9 | FAIL | `src/ecs/systems/ZzComboSystem.h:51` | `#ifdef ENABLE_ZZ_VERIFY` keeps `ZzLegacyPath` as dead code inside a shared system file |
| R10 | N-A | `src/ecs/systems/ISystem.h:20` | change set adds no new interface/template/policy class; existing `ISystem` has ≥5 impls → nothing to judge |
| R11 | FAIL | `src/ecs/components/FZzBad.h:11`, `:12` | `isDead`/`isAttacking` bool state soup; no gameplay state transition table exists under `docs/` |
| R12 | FAIL | `src/ecs/components/FZzBad.h:5` vs `src/core/ZzLeak.h:7` ; `src/ecs/systems/ZzComboSystem.h:19` vs `src/core/ZzLeak.h:21` | `kZzMaxSpeed = 3.5f` duplicated across `ecs/` and `core/`; `120.0f` duplicated as cooldown and fallback gravity |
| R13 | FAIL | `src/ecs/systems/ZzComboSystem.h:49-51` | "cache-friendly rewrite, much faster" claim with no ZoneScoped and no before/after numbers in the new files |
| R14 | PASS | `CMakeLists.txt:15` (`GLOB_RECURSE ... CONFIGURE_DEPENDS`) ; `src/core/SystemManager.h:23` | new files auto-included, single registration API. Caveat: no registration line was added at all, so the new systems are inert (see F1) |

## Change-cost numbers

- add: 0 pre-existing code files modified; 0 registration lines added (expected 0-1 modified + 1 registration
  line). The added systems are never registered → the "feature" is dead on arrival.
- delete: whole change set = 8 files deleted + 0 reference lines (nothing is registered).
  Weapon sub-feature only = `FZzWeapon.h`, `ZzComboSystem.h`, `ZzWeaponSwitchB.h`, `ZzWeaponSwitchC.h`
  (4 files) + the `FZzBad` coupling + 3 switch sites edited.
- tune: NO. Balance values (`0.35f`, `120.0f`, damage `10/7/12/9`, icon paths) are compiled-in.
  No `assets/data/` directory exists in the repo, so "change without recompiling" is impossible.
- understand: ≥5 files (`ZzComboSystem.h`, `ISystem.h`, `MoveSystem.h`, `FZzWeapon.h`, `FZzBad.h`,
  `SystemManager.h`) > 3 → coupling smell (S1/R1).

## Findings (Blocker > High > Medium > Low)

### Blocker

- [Blocker][P2][R7] `src/core/ZzLeak.h:2` — `core/` includes `gameplay/Player.h`. `src/gameplay/`
  does not exist (verified), so this is both a fixed-contract dependency-direction violation and a
  P1 build break. Why it locks cost: `core/` can no longer compile or be reused without gameplay.
  Minimal fix: delete line 2; move any settings behind `registry.ctx()`.
- [Blocker][P2][R7] `src/ecs/ZzStateLeak.h:2-3` — `ecs/` includes `states/GameStateMachine.h` and
  `states/FHubState.h`. Breaks `core/ <- ecs/ <- gameplay/` and the explicit "`ecs/` must not know
  `states/`" clause. Minimal fix: delete both includes — this file should not exist in `ecs/`.
- [Blocker][P2][R6] `src/core/ZzLeak.h:11-12`, `:20` — classic singleton (`instance()` + `getInstance()`)
  plus a file-scope mutable global `int staticInstanceCount = 0;`. Global state makes add/remove/test
  unpredictable across features. Minimal fix: delete `ZzSettings` statics and the global; inject services
  via `registry.ctx().emplace<T>()`.
- [Blocker][P2][R2] `src/ecs/components/FZzBad.h:8-10` — component carries a virtual `update()`, a
  virtual dtor, and an owning raw `std::string*`. §1: components are pure data. Any layout change now
  touches a polymorphic type consumed by systems. Minimal fix: strip virtuals + raw pointer; move logic
  to a system.

### High

- [High][P2][R8] `src/ecs/systems/ZzComboSystem.h:29-34`, `src/ecs/systems/ZzWeaponSwitchB.h:5-10`,
  `src/states/ZzWeaponSwitchC.h:6-11` — the same `EWeapon` switch at 3 sites. Adding one weapon kind
  forces edits in 3 files (rule triggers at ≥3). Minimal fix: one data table / Type Object keyed by
  `EWeapon`, or per-kind entries in `assets/data/`.
- [High][P2][R5] `src/ecs/systems/ZzComboSystem.h:43-45` — entity assembled inline. The second place
  that builds the same `FZzBad` + `FZzWeapon` combo will drift. Minimal fix: add a factory under
  `src/core/factories/` and call it.
- [High][P2][R3] `src/ecs/systems/ZzComboSystem.h:3`, `:15-16` — system includes and directly invokes
  `MoveSystem`. Update order and construction become hidden; deleting `MoveSystem` breaks this file.
  Minimal fix: drop the include + `MoveSystem mover;` and use a typed event via `FEventBus`.
- [High][P1][integration] `src/main.cpp:9-16` — none of `ZzComboSystem`, `ZzExtraSystem`,
  `ZzLegacyPath` is registered in `RegisterCoreSystems`. This is P1 correctness + an R14 blind spot
  (see Q1.5): a feature can be "R14 PASS" while never running. Minimal fix: register the intended
  system(s) once.

### Medium

- [Medium][P2][R1] `src/ecs/systems/ZzComboSystem.h:12`, `:40`, `:52` — three systems in one file,
  name/content mismatch. Minimal fix: split into per-system files or delete the extras.
- [Medium][P2][R4] `src/ecs/systems/ZzComboSystem.h:18-20` — magic numbers and asset path in code.
  Minimal fix: move to config with a single code fallback.
- [Medium][P2][R11] `src/ecs/components/FZzBad.h:11-12` — bool-flag gameplay state, no transition table.
  Minimal fix: replace with tag components + document transitions.
- [Medium][P2][R12] `src/ecs/components/FZzBad.h:5` vs `src/core/ZzLeak.h:7` — duplicated `3.5f`.
  Minimal fix: one owning constant in the correct layer.
- [Medium][P2][R9] `src/ecs/systems/ZzComboSystem.h:51` — `#ifdef ENABLE_ZZ_VERIFY` dead-code gate in a
  shared file. Minimal fix: delete `ZzLegacyPath` or isolate it in its own file.

### Low

- [Low][P3][R13] `src/ecs/systems/ZzComboSystem.h:49-51` — performance claim with no measurement; no
  `ZoneScoped` in the new files. No hot-path rewrite requested. Minimal fix: delete the claim or add
  Tracy evidence.

### PRE-EXISTING (excluded from blocking count)

- `src/core/InputState.h:94-100` — pre-existing scoped-enum switch, single site, not R8.
- `src/ecs/systems/DebugPrimitiveRenderSystem.h:80` — `6.28318530717958647692f` is a math constant, not R4.
- `src/ecs/components/FSprFrame.h:13`, `src/core/Time.h:16`, `:40` — pre-existing float literals, not R4.
- `README.md` — out-of-scope doc edit.

## Verdict

- Status: RESET
- Blocking findings: 4 (R7 x2, R6, R2) + 1 P1 build break (missing `gameplay/Player.h`).
- Rationale: the §1 "never violate" contract is broken on three independent axes (dependency direction,
  global state, non-POD component), plus the change set does not compile as written (non-existent
  `gameplay/` include; `virtual void update(float dt)` declared at `src/ecs/components/FZzBad.h:8`
  with no definition anywhere in `src/`). These are not local cleanups, so REWORK-grade patching is
  insufficient; layer placement and file structure need redesign.
- Deletability statement: deleting the whole feature = remove 8 files + 0 registration lines.
  The weapon sub-feature specifically needs 4 files deleted + 3 switch sites edited.
- Not verifiable without: a link/compile run (`FZzBad` vtable/ODR, missing `Player.h`); a committed
  `contract.md` oracle; a decision whether `src/core/factories/` and `assets/data/` are intended to exist.

---

# Answers to the four questions

## Q1. Violations in the change set NOT covered by the R1-R14 catalogue

1. **Interface/concept conformance is unjudgeable.** `src/ecs/systems/ZzComboSystem.h:12`
   (`ZzComboSystem`), `:40` (`ZzExtraSystem`) and `:52` (`ZzLegacyPath`) define `update()` but no
   `name()`. `ISystem` (`src/ecs/systems/ISystem.h`) declares `name()` pure virtual and `SystemConcept`
   requires `{ t.name() } -> std::convertible_to<std::string>`. All three structs are therefore abstract
   and unregisterable. R1's FAIL signal is only "line count + name/content mismatch"; the §5 R1 command
   (`rg -n "struct .*System"`) counts structs but cannot detect a missing pure-virtual override. No rule fires.
2. **Include resolvability / compile-correctness.** `src/core/ZzLeak.h:2` includes `gameplay/Player.h`,
   but `src/gameplay/` does not exist (verified). R7 catches the dependency *direction* but nothing in
   R1-R14 requires an include to resolve — that is P1, which the changeability catalogue does not contain.
3. **Declared-but-undefined virtual member.** `src/ecs/components/FZzBad.h:8`
   `virtual void update(float dt);` is non-pure and has no definition anywhere under `src/` (verified).
   R2 flags "virtual in a component" but no rule covers the missing definition / vtable ODR hazard.
4. **Header ODR / anonymous-namespace-in-header.** `src/core/ZzLeak.h:20`
   `int staticInstanceCount = 0;` is a non-inline, non-const namespace-scope variable defined in a
   header → one definition per translation unit. R6 lists "파일 스코프 가변 전역" so the mutable-global
   aspect is covered, but the multiple-definition/link break is not; nor is
   `namespace { }` in a header (`src/core/ZzLeak.h:6-8`), which silently changes linkage per TU.
5. **No registration/integration requirement.** The new systems are never added in
   `src/main.cpp:9-16` `RegisterCoreSystems`. R14 only says registration should be *cheap*; it has no
   verdict for "added but never registered". A system can be completely inert and still score "R14 PASS".
6. **File-placement / layer-directory contract.** `src/ecs/ZzStateLeak.h` and `src/core/ZzLeak.h` sit
   directly under `ecs/` and `core/` rather than `components/`, `systems/`, `factories/`;
   `src/states/ZzWeaponSwitchC.h` is a free function with no state type. No rule checks that a file
   lives in the directory its role implies.
7. **Free functions masquerading as system/state artifacts.** `src/ecs/systems/ZzWeaponSwitchB.h:4`
   `zzDamageOf` and `src/states/ZzWeaponSwitchC.h:4` `zzIconOf` are file-scope free functions in files
   named "...Switch...". R1's "이름과 내용 불일치" only targets systems, so these slip through.
8. **Unused / dead-on-arrival new symbols.** `ZzStateLeak` (`src/ecs/ZzStateLeak.h:4`),
   `FZzGood` (`src/ecs/components/FZzGood.h:5`), `ZzSettings` (`src/core/ZzLeak.h:10`) are referenced
   nowhere. No rule flags additions that are never used (R9 is about deletability, not usage).

## Q2. Rules that can pass/fail almost arbitrarily (subjective FAIL triggers)

- **R1** — "이름과 내용 불일치" (name/content mismatch) and "이동+충돌+렌더+사운드를 함께 처리": no
  threshold for how much mismatch or how many responsibilities counts.
- **R2** — "시스템이 자기 상태(캐시/플래그)를 멤버로 **누적**": is a single `float acc` member
  (`src/ecs/systems/ZzComboSystem.h:13`) "누적"? Unbounded.
- **R4** — "매직 넘버" is never defined; FAIL(반대) "분기/룰 로직 자체를 JSON 스크립트로 밀어넣기" is
  a judgement call with no line.
- **R7** — "게임 고유 이름(예: `Player`, `Enemy`, 밸런스 수치)을 include/**언급**": by the letter of
  "언급", the comment-only mention at `src/core/ZzLeak.h:17-18` is a FAIL — yet the file labels itself a
  "negative control". The rule text and the file author's intent disagree.
- **R8** — "같은 enum에 대한 분기가 3곳 이상이면 **FAIL 후보**": "candidate", not FAIL; the reviewer
  decides whether ≥3 sites is actually a failure.
- **R9** — "**무관한** 시스템/헤더 **다수**에 ... 죽은 코드를 남겨야 함": "unrelated" and "several"
  are undefined. (The separate "5곳 이상" threshold is objective.)
- **R10** — "**미래 예상용** 플러그인 계층", "**Manager의 Manager**": pure vibes, no countable trigger.
- **R12** — "**무관한** 파일을 함께 건드림(unrelated cleanup)": what counts as "unrelated" is left to
  the reviewer.
- **R13** — "**측정 없는** 알고리즘/자료구조 교체", "추상화 제거로 인한 결합 악화": no threshold for
  what counts as "measured" (a single ZoneScoped? before/after numbers?).
- **R14** — "**여러** 목록/빌드 스크립트/레지스트리를 손봐야 함": "several" is undefined.
- **R6** — the FAIL list is objective, but the change set plants a deliberate "negative control" mutable
  global at `src/core/ZzLeak.h:20` expecting it to read as allowed; the rule as written says file-scope
  mutable globals FAIL, so a reviewer can flip the verdict on the same line without the text changing.

## Q3. §5 grep commands producing false positives (ran each, read every hit)

- **R3** `rg -n "systems/.*System.h\"" src/ecs/systems/` → 7 hits; **6 are false positives**: the allowed
  contract include `ecs/systems/ISystem.h` at `src/ecs/systems/AnimationSystem.h:4`,
  `CollisionSystem.h:7`, `DebugPrimitiveRenderSystem.h:7`, `MoveSystem.h:3`, `SpriteRenderSystem.h:3`,
  `ZzComboSystem.h:2`. Only real hit: `ZzComboSystem.h:3` (`MoveSystem.h`). The pattern also matches
  `ISystem.h`, which the doc treats as "self".
- **R2** `rg -n "virtual|~[A-Z]" src/ecs/components/` → FP: `src/ecs/components/FZzGood.h:4`, where the
  matched token is the word "virtual" inside a comment ("negative control: pure-data POD component, no
  virtual, no flags"). Real: `FZzBad.h:8`, `:9`.
- **R7** `rg -n "gameplay/|states/" src/core/ src/ecs/` → FP: `src/core/ZzLeak.h:18` is a comment
  ("Player balance values belong to gameplay/ layers"). Real: `ZzLeak.h:2`, `ZzStateLeak.h:2`, `:3`.
- **R4** `rg -n "\b[0-9]+\.[0-9]+f" src/ …` → FPs (pre-existing / not gameplay magic): `src/core/Time.h:16`
  (`1.0f / 15.0f`), `src/core/Time.h:40` (`1.0f`), `src/ecs/systems/DebugPrimitiveRenderSystem.h:80`
  (`6.28318530717958647692f` = tau), `src/ecs/components/FSprFrame.h:13` (`0.1f` default), and arguably
  `src/core/ZzLeak.h:21` (`120.0f` self-declared single fallback) and `src/ecs/components/FZzGood.h:5`
  (`0.5f` POD default). Real: `ZzComboSystem.h:18`, `:19`, `ZzLeak.h:7`.
- **R5** `rg -n "\.create\(\)" src/ …` → no FPs (single hit `ZzComboSystem.h:43`).
- **R6** `rg -n "static .*instance|getInstance|^extern " src/ …` → no FPs but a **false negative**: it
  misses `int staticInstanceCount = 0;` at `src/core/ZzLeak.h:20` (no space after `static`).
- **R10** `rg -n "class I[A-Z]" src/` → 0 hits; **false negative**, the repo's only interface is
  `struct ISystem` (`src/ecs/systems/ISystem.h`), which the pattern does not match.
- **R11** `rg -n "bool is[A-Z]" src/ecs/components/` → no FPs (`FZzBad.h:11`, `:12` only).
- **R13** `rg -n "ZoneScoped" src/` → **16 hits, all false positives as candidate violations**: every hit
  is legitimate pre-existing profiling (`src/core/Engine.cpp:28,242,266,304,320`,
  `src/debug/DebugOverlay.cpp:168`, `src/ecs/systems/AnimationSystem.h:14`,
  `DebugPrimitiveRenderSystem.h:23`, `CollisionSystem.h:17`, `MoveSystem.h:15`, `SpriteRenderSystem.h:22`,
  `src/core/EngineAbilityPipeline.cpp:264`, `src/core/ResourceManager.cpp:9,15,51`,
  `src/core/SystemManager.h:45,52`). The command yields **zero** hits in the changed files, so it cannot
  detect the actual R13 violation (unmeasured claim in `ZzComboSystem.h:49-51`).
- Extra: **R8** `rg -n "case E[A-Za-z]*::" src/` → FP: `src/core/InputState.h:94-100` (unrelated
  pre-existing `EInputAction` switch polluted into the R8 candidate set).

## Q4. Does the Output Format let a lazy reviewer emit GROWING with nothing falsifiable?

**Yes.** The format requires only: a contract-check list, a rule table with a verdict + a `file:line`
per rule, four numeric cost fields, a findings list (which may be empty), and a verdict block with
"Blocking findings: <count>". Nothing requires the cited line to actually demonstrate the rule, nothing
requires §5 command output to be reproduced, and nothing requires any claim to be refutable. A reviewer
can cite line 1 (`#pragma once`) for every rule and still satisfy the format's letter.

Exact weakest output that still satisfies the format:

```markdown
# Changeability Review — zz-changeability-review (2025 dry-run)

## Contract check
- in-scope: src/ecs/components/FZzWeapon.h, src/ecs/components/FZzBad.h, src/ecs/components/FZzGood.h,
  src/ecs/systems/ZzComboSystem.h, src/ecs/systems/ZzWeaponSwitchB.h, src/states/ZzWeaponSwitchC.h,
  src/ecs/ZzStateLeak.h, src/core/ZzLeak.h
- out-of-scope / unrelated edits: none

## Rule table
| Rule | Verdict | Evidence | Note |
|------|---------|----------|------|
| R1 | PASS | src/ecs/systems/ZzComboSystem.h:1 | one system per file |
| R2 | PASS | src/ecs/components/FZzWeapon.h:1 | data/logic separated |
| R3 | PASS | src/ecs/systems/ZzComboSystem.h:1 | event-based comms |
| R4 | PASS | src/ecs/components/FZzWeapon.h:1 | values data-driven |
| R5 | PASS | src/ecs/systems/ZzComboSystem.h:1 | factory path used |
| R6 | PASS | src/core/ZzLeak.h:1 | no globals |
| R7 | PASS | src/ecs/ZzStateLeak.h:1 | layer boundaries intact |
| R8 | PASS | src/ecs/components/FZzWeapon.h:1 | additive change |
| R9 | PASS | src/ecs/systems/ZzComboSystem.h:1 | deletable feature |
| R10 | N-A | src/ecs/systems/ISystem.h:20 | no new abstraction |
| R11 | PASS | src/ecs/components/FZzWeapon.h:1 | explicit state |
| R12 | PASS | src/ecs/components/FZzWeapon.h:1 | single source |
| R13 | PASS | src/ecs/systems/ZzComboSystem.h:1 | no unmeasured change |
| R14 | PASS | CMakeLists.txt:15 | GLOB picks up new files |

## Change-cost numbers
- add: 1 / delete: 1 / tune: yes / understand: 2

## Findings
- none

## Verdict
- Status: GROWING
- Blocking findings: 0
- Deletability statement: delete the 8 files + 1 registration line
- Not verifiable without: —
```

Every rule cites line 1 and asserts a positive that the change set contradicts (e.g. R7 PASS while
`src/ecs/ZzStateLeak.h:2` includes `states/`, R6 PASS while `src/core/ZzLeak.h:11` is a singleton,
`tune: yes` while no `assets/data/` exists). The format cannot reject it, because it never demands that
a citation be *load-bearing*. This is the weakest point of the agent document.
