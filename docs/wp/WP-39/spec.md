# WP-39: Combat stakes — party crits, battle objectives, first-contact log

Size: L · Player-facing: yes · Certainty: settled · Base commit: the commit that adds this file · Branch: wp/WP-39

## 1. Goal
Every battle gets something riding on it. Party skills can crit (double damage, 15%), each battle carries one seeded bonus objective worth +1 material, and the run log records when the two sides first meet. Enemy damage stays exactly what the intent shows; the win condition stays "defeat the enemies".
- Decisions it rests on: `docs/design/expansion-plan.md` §3 (this WP's game design); `docs/design/decisions.md` — "가장 중요한 설계 기준은 '기대'의 극대화 와 '적절한 변수성'", "일반 전투의 보너스 목표 허용" (2026-11-11, the exception to "목표 다양성은 보스에만 둔다"), "무작위성만으로 실패를 만들면 '부당한 난이도'가 된다", "쓰러져도 패널티가 누적되지 않는다", "기본 스탯 확정 = hp·공격력·마법공격력·속도·방어력·마법방어력"; `docs/design/combat-math.md` §1 (pipeline order).

## 2. Player experience
- **Decision added or changed**
  - Crit: hit the enemy the base damage already kills (certain), or the one that only a crit kills (15% to save a turn). The base number is always dealt, so a plan never fails because of a roll; a crit only adds.
  - Objective `Unbroken` (no ally goes down) favours pulling a hurt unit back; `Swift` (win by round 5, i.e. before round 6 starts) favours pressing forward. One of them is active per battle, so the same encounter asks for different play.
- **What the player sees before committing**
  - Aim preview plate on each hostile target: `-8  15% -16` (base damage, crit chance, crit damage). No crit part when a crit cannot raise the damage.
  - Top bar, the line under "EMBER DUNGEON" (today `<room name>  /  seed <n>`): `Unbroken +1: No ally goes down` in gold while the objective can still be met; the same text with ` (lost)` dimmed once it cannot; after the battle ` (met)` in gold or ` (lost)` dimmed. While an objective is shown, the room name and seed leave this line (both stay in the run log); a battle without an objective keeps the old line.
  - Combat log at battle start: `Objective: Unbroken - No ally goes down (+1 material).` (the room name is still in the log header and the run log)
  - On a crit: the damage pop reads `CRIT -16` and is larger; the log line carries ` / CRIT`.
  - Reward screen message: `Objective met: Unbroken (+1 material).` or `Objective missed: Unbroken.`
- **Playtest hypothesis**: expect the player to say at least once in a session of two dungeons that they chose a target for the crit chance or changed a move because of the objective · observe `room_end.objective`, `room_end.crits` in the run log and the playtest notes (boss rooms pick from the same two objectives; whether `Swift` is ever met there is part of what the log shows) · kill-if the player never mentions the objective line or the crit numbers in the session, or a note calls a wipe "unfair" because of chasing an objective.

## 3. Behaviors
| ID | Behavior | Proving test |
|---|---|---|
| F1 | WHEN a skill is previewed or resolved IF `FCombatRuntime::critChancePercent` is 0 (the default) THEN the system SHALL report `critDamage == 0` and deal exactly the preview damage. | `combat_stakes_tests::ZeroChanceNeverCrits` |
| F2 | WHEN a party unit previews a damaging skill on a hostile target IF crit chance > 0 THEN `critDamage` SHALL be the boss-capped value of `uncapped damage * critMultiplierPercent / 100` when that exceeds the preview damage, else 0, and `FSkillPreview::critChancePercent` SHALL equal the runtime value; WHEN the actor is not a party unit THEN `critDamage` and `critChancePercent` SHALL be 0. | `combat_stakes_tests::PreviewCarriesCrit` |
| F3 | WHEN a party skill resolves IF the crit roll succeeds THEN the target SHALL lose `critDamage` HP, the Skill battle event's target SHALL have `crit == true`, the combat log line SHALL contain ` / CRIT`, and `FRoomBattleStats::crits` SHALL increase by 1 when that context exists. | `combat_stakes_tests::CritRollDealsCritDamage` |
| F4 | WHEN an enemy skill resolves IF crit chance is 50 THEN the target SHALL lose exactly the preview damage and `critRng.state` SHALL be unchanged. | `combat_stakes_tests::EnemiesNeverCrit` |
| F5 | WHEN a party skill without statuses resolves IF crit chance is 50 THEN `rng.state` SHALL equal its value before and `critRng.state` SHALL differ from its value before. | `combat_stakes_tests::CritUsesItsOwnRng` |
| F6 | WHEN a party unit previews a hit on a gated boss body THEN `critDamage` SHALL follow the cap: `2 * damage` when the cap allows it, the cap value when the cap lies between the damage and twice the damage, and 0 when the cap is at or below the damage. | `boss_goals_tests::GatedBodyCritFollowsCap` |
| F7 | WHEN the tempo tuning is loaded from the shipped `data/dungeon` THEN `critChancePercent` SHALL be 15 and `critMultiplierPercent` 200; IF `crit_chance_percent` is above 100 or `crit_multiplier_percent` below 100 THEN the loader SHALL return `nullopt`. | `combat_stakes_tests::CritTuningLoads` |
| F8 | WHEN the shipped `data/dungeon` is loaded THEN `LoadBattleObjectives` SHALL return two rows, `objective.unbroken` (`NoAllyDown`, reward 1) and `objective.swift` (`WinByRound`, rounds 5, reward 1); IF the document has no `battle_objectives` key THEN it SHALL return an empty table; IF the value is malformed (rules in §8) THEN it SHALL return `nullopt`. | `combat_stakes_tests::ObjectivesLoad` |
| F9 | WHEN `PickBattleObjective` is called twice with the same seed, depth and room THEN it SHALL return the same row; over rooms 0..2 of depths 1..8 with seed 12345 it SHALL return each of the two shipped rows at least once; IF the table is empty THEN it SHALL return `nullptr`. | `combat_stakes_tests::ObjectivePickIsSeeded` |
| F10 | WHEN `IsBattleObjectiveFailed` is evaluated THEN `NoAllyDown` SHALL fail iff `anyAllyDown`; `WinByRound` with rounds 5 SHALL fail iff `rounds > 5`. | `combat_stakes_tests::ObjectiveFailureRules` |
| F11 | WHEN a battle room is won IF the context holds an `FBattleObjective` and an active `FRoomBattleStats` and the objective is not failed THEN the run SHALL gain `room_materials + rewardMaterials`, the message SHALL end with `Objective met: <name> (+<n> material).`, the `FBattleObjective` SHALL have `settled == true, met == true`, and the `room_end` log line SHALL carry `objective: {id, met: true}`; IF it is failed THEN the run SHALL gain `room_materials` only, the message SHALL end with `Objective missed: <name>.`, and `met` SHALL be false in both places. | `combat_stakes_tests::VictoryPaysMetObjective` |
| F12 | WHEN a battle room is won IF the context has no `FBattleObjective`, or its `FRoomBattleStats` is absent or not active THEN the run SHALL gain exactly `room_materials`, the message SHALL not contain `Objective`, and the `room_end` line SHALL have no `objective` key. | `combat_stakes_tests::NoObjectiveNoBonus` |
| F13 | WHEN `UpdateFirstContact` runs IF a living party unit and a living non-party unit stand at grid distance 1 THEN `firstAdjacentRound` SHALL be set to `max(1, rounds)` once and never change; IF a party unit's `hpMin` is below its `hpStart`, or a non-party unit's HP is below its max, THEN `firstDamageRound` SHALL be set the same way. | `combat_stakes_tests::FirstContactRecordedOnce` |
| F14 | WHEN a `room_end` line is written THEN it SHALL contain integer `crits`, `first_damage_round`, `first_adjacent_round`. | `run_log_tests::RoomEndCarriesStakes` |
| F15 | WHEN the combat info snapshot is built for a party aim IF the preview target has `critDamage > 0` THEN its `FPreviewBadge` SHALL carry the same `critDamage` and the preview's `critChancePercent`. | `combat_info_ui_tests::PreviewBadgeCarriesCrit` |
| F16 | WHEN `ApplyBattleStakes` runs on a registry holding a dungeon run in the Battle phase, an `FCombatRuntime` and the ship assets THEN the runtime SHALL have `critChancePercent == 15`, `critMultiplierPercent == 200` and a non-zero `critRng.state` different from `rng.state`, the context SHALL hold the `FBattleObjective` that `PickBattleObjective` returns for the run's seed, depth and room, and the combat log SHALL contain a line starting `Objective: `; WHEN `StakesEnabled()` is evaluated with `GAME01P_STAKES=0` THEN it SHALL be false, otherwise true. | `combat_stakes_tests::StakesWireIntoBattle` |

## 4. Numbers
| Name | Value | Unit | Data path | Knob |
|---|---|---|---|---|
| Party crit chance | 15 | percent | `assets/data/dungeon.json` → `balance.crit_chance_percent` | `[0, 30]` — moves average party damage per turn and battle rounds |
| Crit multiplier | 200 | percent of damage | `balance.crit_multiplier_percent` | `[150, 250]` — same metric |
| Objective reward | 1 | materials | `battle_objectives[].reward_materials` | `[0, 3]` — moves materials per dungeon |
| Swift round limit | 5 | rounds | `battle_objectives[id=objective.swift].rounds` | `[3, 8]` — moves objective success rate |

Crit is a new stage appended to `docs/design/combat-math.md` §1 (exact text in §8). Owner: `ActionResolverSystem` (roll) and `PreviewSkill` (numbers).

## 5. File map
- Create: `src/gameplay/dungeon/FBattleObjective.h`, `src/gameplay/dungeon/FBattleObjective.cpp` — objective table, loader, pick, failure rule, battle wiring. Sources are globbed (`CMakeLists.txt:22`, `GLOB_RECURSE CONFIGURE_DEPENDS`); no CMake source edit.
- Create: `tests/features/combat_stakes_tests.cpp` — F1–F5, F7–F13, F16.
- Create: `docs/wp/WP-39/progress.md` — same format as `docs/wp/WP-36/progress.md`.
- Modify: `src/gameplay/combat/FCombatRules.h`, `FCombatRules.cpp` — add `ComputeCritDamage`.
- Modify: `src/gameplay/combat/FCombatTypes.h` — `FCombatRuntime` gains three fields.
- Modify: `src/gameplay/combat/FSkillApCap.h`, `FSkillApCap.cpp` — `FTempoTuning` gains two fields; `LoadTempoTuning` reads them.
- Modify: `src/gameplay/combat/FCombatSheet.h`, `FCombatSheet.cpp` — `FPreviewTarget::critDamage`, `FSkillPreview::critChancePercent`, computed in `PreviewSkill` (add includes `FCombatTypes.h`, `FCombatRules.h` if missing).
- Modify: `src/gameplay/combat/FBattleEvents.h` — `FBattleEvent::FTarget::crit`.
- Modify: `src/ecs/systems/ActionResolverSystem.h` — the hostile-damage branch of the target loop (the block starting `} else if (resolved->power > 0) {`, line 298).
- Modify: `src/gameplay/dungeon/FRunLog.h`, `FRunLog.cpp` — `FRoomBattleStats` fields, `UpdateFirstContact`, `BuildRoomEnd`. `FRunLog.cpp` includes the ECS components it needs (the same ones `CombatOutcomeSystem.h` includes for these types: `ecs/components/FHealth.h`, `ecs/components/FFaction.h`, `gameplay/factory/FEntityDefinitions.h`, the grid header that declares `FGridPosition`; plus `FCombatQuery.h` for `IsAlive` and `FCombatRules.h` for `GridDistance`); gameplay including ECS components is established (`FCombatSheet.cpp:6-13`). Update the comment at `FRunLog.h:22-24` to say the first-contact step lives in gameplay so a test can call it without the system.
- Modify: `src/ecs/systems/CombatOutcomeSystem.h` — `AccumulateRoomStats` calls `UpdateFirstContact`.
- Modify: `src/gameplay/dungeon/FRunFlow.cpp` — `ResolveRoomVictory` (include `FBattleObjective.h`, `FRunLog.h`).
- Modify: `src/states/FEmberBattleState.cpp` — `OnEnter` and `OnExit` (include `FBattleObjective.h`).
- Modify: `src/ecs/systems/EmberCombatInfo.h`, `EmberCombatInfo.cpp` — `FPreviewBadge` fields and the `snapshot.preview.push_back` line (278).
- Modify: `src/ecs/systems/EmberCombatHudView.cpp` — `TopBar` (the `TextDisabled("%s  /  seed %u", …)` line, 334), the preview plate loop (877), `DamagePopOverlays` (901).
- Modify: `src/presentation/FBattleAnim.h`, `FBattleAnim.cpp` — `FDamagePop::crit`, `React` (171).
- Modify: `assets/data/dungeon.json` — two balance keys, `battle_objectives`.
- Modify: `CMakeLists.txt` — test environment only (§8 "test switch").
- Modify: `docs/design/combat-math.md` — §1 stage 5.
- Modify: `tests/features/boss_goals_tests.cpp` (add F6), `run_log_tests.cpp` (add F14), `combat_info_ui_tests.cpp` (add F15), `compendium_tests.cpp` (one added width check, §8 display).
- Modify: `docs/wp/WP-39/features.json`.

Exact signatures:
```cpp
// FCombatRules.h (namespace as the existing ComputeDamage)
/// Crit damage of a hit that deals `damage`: integer floor of damage * multiplierPercent / 100.
[[nodiscard]] int ComputeCritDamage(int damage, int multiplierPercent);

// FCombatTypes.h — FCombatRuntime, appended directly after `FBattleRng rng;`
int critChancePercent{0};       // 0 = crits are off (the default for tests and probes)
int critMultiplierPercent{200};
FBattleRng critRng;             // crit rolls only, so status rolls keep their sequence

// FSkillApCap.h — FTempoTuning, appended
int critChancePercent{0};
int critMultiplierPercent{200};

// FCombatSheet.h
struct FPreviewTarget { /* existing fields */ int critDamage{0}; };   // 0 = a crit cannot raise this hit
struct FSkillPreview  { /* existing fields */ int critChancePercent{0}; };

// FBattleEvents.h — FBattleEvent::FTarget, appended
bool crit{false};

// FBattleAnim.h — FDamagePop, appended after `lifetime`
bool crit{false};

// EmberCombatInfo.h — FPreviewBadge, appended after `lethal`
int critDamage{0};
int critChancePercent{0};

// FBattleObjective.h (namespace game)
enum class EBattleObjectiveKind { NoAllyDown, WinByRound };
struct FBattleObjectiveDef {
    std::string id;
    std::string displayName;
    std::string text;
    EBattleObjectiveKind kind{EBattleObjectiveKind::NoAllyDown};
    int rounds{0};            // WinByRound only
    int rewardMaterials{0};
};
struct FBattleObjectiveTable { std::vector<FBattleObjectiveDef> rows; };
/// Registry context: the objective of the battle in progress. Absent = this battle has none.
/// `settled`/`met` are latched by ResolveRoomVictory so the HUD keeps the result after the stats reset.
struct FBattleObjective { FBattleObjectiveDef def; bool settled{false}; bool met{false}; };
/// Reads `battle_objectives` from `data/dungeon`. Missing document or key = empty table.
/// nullopt + LOG_ERROR when the present value is malformed.
[[nodiscard]] std::optional<FBattleObjectiveTable> LoadBattleObjectives(const FAssetManager& assets);
[[nodiscard]] const FBattleObjectiveDef* PickBattleObjective(
    const FBattleObjectiveTable& table, std::uint32_t seed, int depth, int room);
[[nodiscard]] bool IsBattleObjectiveFailed(const FBattleObjectiveDef& def, int rounds, bool anyAllyDown);
/// False only when the environment variable GAME01P_STAKES is exactly "0".
[[nodiscard]] bool StakesEnabled();
/// Battle start wiring: crit tuning + crit RNG on FCombatRuntime, and this battle's FBattleObjective.
void ApplyBattleStakes(entt::registry& registry);

// FRunLog.h — FRoomBattleStats, appended
int crits{0};
int firstDamageRound{0};     // 0 = not yet
int firstAdjacentRound{0};   // 0 = not yet
std::string objectiveId;     // set by ResolveRoomVictory when an objective was evaluated
bool objectiveMet{false};
/// Sets firstAdjacentRound / firstDamageRound to max(1, stats.rounds) the first time each is true.
void UpdateFirstContact(entt::registry& registry, FRoomBattleStats& stats);
```
- Do not touch: `src/gameplay/combat/FIntent.*`, `src/gameplay/ai/**` (enemy damage and AI scoring stay exact), `src/gameplay/combat/FStatusRules.*`, `src/gameplay/combat/FEffectHooks.*`, `assets/data/skills.json`, `runes.json`, `units.json`, `passive-tree.json`, `tests/fixtures/**` (the frozen fixture `tests/fixtures/assets/data/dungeon.json` stays without the new keys), the save format (`FDungeonSave.*`).

## 6. Tasks
1. **T1 Failing tests** — create `FBattleObjective.h` and `FBattleObjective.cpp`; add every new struct field and declaration from §5; give every new function a stub body (`ComputeCritDamage` returns `damage`; `LoadBattleObjectives` returns an empty table; `PickBattleObjective` returns `nullptr`; `IsBattleObjectiveFailed` returns `true`; `StakesEnabled` returns `true`; `ApplyBattleStakes` and `UpdateFirstContact` do nothing). Add `tests/features/combat_stakes_tests.cpp` (copy the harness — the name-to-function table and `main` — from `tests/features/pipeline_tests.cpp`) and register the three added cases in their files' tables. Run `cmake --preset windows-msvc-agent` once so the new test file is globbed. Done: `node .claude/scripts/build.mjs --preset windows-msvc-agent` → build OK; these cases FAIL: F2, F3, F5, F6, F7, F8, F9, F10, F11, F13, F14, F15, F16; F1, F4, F12 may already pass; every pre-existing case passes. Commit `WP-39: add failing tests`.
2. **T2 Crit rules** — `ComputeCritDamage`; `FTempoTuning` + `LoadTempoTuning`; `PreviewSkill`; the resolver branch; `FRoomBattleStats::crits`; `dungeon.json` balance keys. Done: build script → F1–F7 pass.
3. **T3 Objectives** — loader, pick, failure rule; `dungeon.json` `battle_objectives`; `ResolveRoomVictory`; the `objective` part of `BuildRoomEnd`. Done: build script → F8–F12 pass.
4. **T4 First contact and log** — `UpdateFirstContact`, the call in `AccumulateRoomStats`, the rest of `BuildRoomEnd`. Done: build script → F13, F14 pass.
5. **T5 Battle wiring, test switch and display** — `StakesEnabled`, `ApplyBattleStakes`, `FEmberBattleState`, `CMakeLists.txt`, `FPreviewBadge`, the preview plate, the damage pop, the top bar, `combat-math.md`. Done: build script → all tests pass; then run
   `$env:SDL_VIDEO_DRIVER="dummy"; $env:SDL_AUDIO_DRIVER="dummy"; $env:SDL_RENDER_DRIVER="software"; $env:GAME01P_SAVE_DIR="out/wp39"; $env:GAME01P_SEED="7"; $env:GAME01P_START="battle"; .\out\build\windows-msvc-agent\Debug\game01P.exe --ember --capture .\out\wp39-battle.png`
   → exit code 0 and `out/wp39-battle.png` exists. Read the image and confirm the line under "EMBER DUNGEON" reads `Unbroken +1: No ally goes down` or `Swift +1: Win by round 5`. If the capture cannot run under the dummy driver, report that instead of opening a window.

## 7. Tests
Fixtures and helpers (copy what you need into `combat_stakes_tests.cpp`; do not move helpers out of their files):
- Battles: as `tests/features/pipeline_tests.cpp` builds them (`CheckPreviewEqualsResolution`, lines 121–187: dungeon run → `BuildDungeonBattlefield` → `CreateBattlefield` → `SetupCombatUnits` → `ApplyRunParty`).
- Runs with the run log: as `tests/features/run_log_tests.cpp` does at lines 322–325 (`StartRun` then `BuildDungeonBattlefield`; the run is then in the Battle phase, which `ResolveRoomVictory` requires). `FGameplayServices::Initialize` installs the run-log listener, so `ResolveRoomVictory` always writes a `room_end` line into `FRunLog::events` and resets the stats before it returns.
- Temp documents: `FTempRoot`, `FixtureDungeon`, `WriteJsonFile`, `RootsWith` from `tests/features/tempo_skills_tests.cpp`.
- Asset roots: `TestAssetRoots()` puts the frozen fixture `tests/fixtures/assets` first, and its `data/dungeon.json` has none of the new keys. Cases that read shipped values (F7, F8, F9, F16) build their `FAssetManager` from `ShipAssetRoots()` (`tests/TestSupport.h:22`).

Cases:
- **ZeroChanceNeverCrits (F1)**: default `FCombatRuntime`; a party attacker's preview on an enemy has `critDamage == 0`; after resolving, HP lost `== preview damage`.
- **PreviewCarriesCrit (F2)**: runtime chance 15, multiplier 200; party attacker → `critDamage == 2 * damage`, `preview.critChancePercent == 15`; the same skill previewed with an enemy as actor against a party unit → `critDamage == 0`, `critChancePercent == 0`.
- **CritRollDealsCritDamage (F3)**: chance 100; register `FBattleEvents`, `FCombatLog` and an `FRoomBattleStats`; resolve a party attack; HP lost `== critDamage`; the pending event with `kind == Skill` has a target with `crit == true`; some log line contains `" / CRIT"`; `crits == 1`.
- **EnemiesNeverCrit (F4)**: chance 50; record `critRng.state`; resolve an enemy attack on a party unit; HP lost `== preview damage`; `critRng.state` unchanged.
- **CritUsesItsOwnRng (F5)**: chance 50; take the first party skill slot whose resolved skill has `power > 0` and `statuses.empty()` (iterate the party units' slots; fail the test if none exists); record both states; resolve; `rng.state` unchanged, `critRng.state` changed.
- **GatedBodyCritFollowsCap (F6)**: in `boss_goals_tests.cpp`, reuse the `FMiniBattle` fixture of `GatedBodyPreviewZeroDamage` (lines 1231–1254) with crit chance 15 on its runtime (the preview does not roll). Build it so the hero deals 19 base damage to the body — `FMiniBattle(110, 50, 2, 20)`: attack 20 + `skill.strike` power 4 − body defense 5. Three previews: body HP 110 → `critDamage == 38`; body HP `floor + 30` → `critDamage == 30`; body HP `floor` → `critDamage == 0`. If those constructor arguments do not give 19 base damage in that fixture, keep the three relations (cap ≥ 2×damage, damage < cap < 2×damage, cap ≤ damage) and write the numbers you used in the test comment.
- **CritTuningLoads (F7)**: ship roots → 15 / 200. Temp documents: `crit_chance_percent: 101` → `nullopt`; `crit_multiplier_percent: 99` → `nullopt`; a document without the two keys → 0 / 200.
- **ObjectivesLoad (F8)**: ship roots → the two rows with the stated fields. Temp documents → `nullopt` for each of: value not an array; a row that is not an object; unknown `kind`; empty `id`; repeated `id`; `win_by_round` without `rounds`; `rounds: 0`; `reward_materials: 11`; a row with an unknown key. A document without the key → empty table.
- **ObjectivePickIsSeeded (F9)**, **ObjectiveFailureRules (F10)**: as the behavior rows state.
- **VictoryPaysMetObjective (F11)**: boot a run with the run log; read `materials`; emplace `FBattleObjective{FBattleObjectiveDef{"objective.unbroken", "Unbroken", "No ally goes down", EBattleObjectiveKind::NoAllyDown, 0, 1}}` (a literal: the boot uses the fixture roots, which have no objectives; read the room reward with `DungeonBalance(registry, "room_materials")`) and `FRoomBattleStats{.rounds = 3, .ko = false, .active = true}` (use `insert_or_assign` on the context); `ResolveRoomVictory(registry, true)`; materials grew by `room_materials + 1`; message ends with `Objective met: Unbroken (+1 material).`; the context's `FBattleObjective` has `settled && met`; the last `room_end` line in `FRunLog::events` has `objective.id == "objective.unbroken"` and `objective.met == true`. Then, on a **fresh boot** (the first run is in the Reward phase), the same with `.ko = true` → `+ room_materials` only, `Objective missed: Unbroken.`, `settled && !met`, `objective.met == false`.
- **NoObjectiveNoBonus (F12)**: fresh boot, no `FBattleObjective` → `+ room_materials`, no `Objective` in the message, the `room_end` line has no `objective` key. Fresh boot with an objective and stats `active == false` → the same three results.
- **FirstContactRecordedOnce (F13)**: one party unit and one enemy at distance 3, both at full HP, stats `rounds = 0` with the party unit's `hpStart == hpMin` → both fields stay 0; move the enemy adjacent, `rounds = 2`, call → `firstAdjacentRound == 2`, `firstDamageRound == 0`; lower the enemy's HP below max, call → `firstDamageRound == 2`; set `rounds = 4`, call → both still 2.
- **RoomEndCarriesStakes (F14)**: in `run_log_tests.cpp`, next to the case that checks the `room_end` required keys (line ~310): after a won room the `room_end` line contains integer `crits`, `first_damage_round`, `first_adjacent_round`.
- **PreviewBadgeCarriesCrit (F15)**: in `combat_info_ui_tests.cpp`, reuse the fixture of `PreviewMatchesPipeline` (line 441); set chance 15 on the runtime (emplace an `FCombatRuntime` if the fixture has none); the hostile badge has `critDamage == 2 * damage` and `critChancePercent == 15`.
- **StakesWireIntoBattle (F16)**: boot a run from ship roots, in the Battle phase; emplace `FCombatRuntime` with `rng.state = 1234` and an `FCombatLog`; `ApplyBattleStakes(registry)`; assert the F16 row. For `StakesEnabled`: set the variable with the `_putenv_s` / `setenv` guard pattern used in `tests/features/camp_tests.cpp`, check `"0"` → false and `"1"` → true, then restore `"0"` (the value the test environment sets).

Existing assertions keep their meaning and no timeout changes. Pre-existing tests that enter `FEmberBattleState::OnEnter` with ship assets keep their results because every test executable runs with `GAME01P_STAKES=0` (§8 "test switch"). If a pre-existing case still changes, stop and report `BLOCKED:` with the test name instead of editing it.

## 8. Decisions
- Resolved — **crit numbers in the preview** (`PreviewSkill`, hostile branch, after the existing cap lines 182–186): when the actor has `FPartyMember`, `registry.ctx().find<FCombatRuntime>()` exists with `critChancePercent > 0`, and `terms.damage > 0`: `crit = BossBodyDamageCap(registry, target, ComputeCritDamage(terms.damage, runtime->critMultiplierPercent))`; `entry.critDamage = crit > entry.damage ? crit : 0`. Under the same actor/runtime condition set `preview.critChancePercent = runtime->critChancePercent` once. `PreviewSkill` still uses no RNG.
- Resolved — **the roll** (resolver, hostile branch): before `ApplyDamageToUnit`, `int dealt = target.damage; bool crit = false; if (target.critDamage > target.damage && combat != nullptr && combat->critRng.rollChance(combat->critChancePercent)) { dealt = target.critDamage; crit = true; }`. Use `dealt` for `ApplyDamageToUnit`, for the `-N HP` number in the log, and for both `FHookContext::amount` values. Append `" / CRIT"` to the log line right after the closing `]` of the arithmetic and before `" / KO"`. Set `reached.crit = crit`. When `crit`, increment `FRoomBattleStats::crits` if that context exists. The bracketed arithmetic stays the base terms (`= <target.damage>`). No roll is made when `critDamage` is 0.
- Resolved — **scope of crit**: only this branch. Hook damage, status ticks, traps, collisions, heals and status-only skills do not pass through it and are untouched.
- Resolved — **tuning** (`LoadTempoTuning`): read `crit_chance_percent` with `ReadOptionalBalanceInt(*balance, "crit_chance_percent", 0, tuning.critChancePercent)` and `crit_multiplier_percent` with minimum 100; directly after the reads, `if (tuning.critChancePercent > 100) { LOG_ERROR(...); return std::nullopt; }`.
- Resolved — **`ApplyBattleStakes`**: needs `FDungeonRun`, `FCombatRuntime` in the context; returns without change if either is missing. (1) `critRng.state = rng.state ^ 0x5bd1e995u; if (critRng.state == 0) critRng.state = 0x5bd1e995u;`. (2) With `registry.ctx().find<FAssetManager>()` (the lookup at `FEffectHooks.cpp:291`): if `LoadTempoTuning` returns a value copy its two crit fields into the runtime; on `nullopt` or a missing manager `LOG_ERROR` once and leave the chance at 0. (3) Erase any `FBattleObjective`; if `LoadBattleObjectives` returns a table and `PickBattleObjective(table, run.seed, run.depth, run.room)` returns a row, emplace `FBattleObjective{*row}` and, if an `FCombatLog` exists, push `Objective: <displayName> - <text> (+<rewardMaterials> material).`; a `nullopt` table logs `LOG_ERROR` and leaves the battle without an objective.
- Resolved — **battle state**: in `FEmberBattleState::OnEnter`, erase `FBattleObjective` next to the `FCombatRuntime` erase (line 131); after the `continuedAfterRoom` block (declared at line 144) add `if (!continuedAfterRoom && game::StakesEnabled()) game::ApplyBattleStakes(Registry);`. In `OnExit`, erase `FBattleObjective` next to the `FCombatRuntime` erase (line 187). A room re-entered at the reward step therefore has no objective and no crits.
- Resolved — **test switch**: `StakesEnabled()` reads `GAME01P_STAKES` with `std::getenv` (as `src/gameplay/dungeon/FRunSeed.h` reads its variable): SDL3 `SDL_getenv` reads a cached copy of the environment, so a value set by a test with `_putenv_s` would never be seen. In `CMakeLists.txt`, append `GAME01P_STAKES=0` to the `ENVIRONMENT` of every test whose command is a test executable: the feature-test loop (line 148, the string becomes `"GAME01P_SAVE_DIR=…;GAME01P_STAKES=0"`), `dungeon_rules` (line 85) and `ai_behavior` (line 90) via `set_property(TEST <name> APPEND PROPERTY ENVIRONMENT "GAME01P_STAKES=0")`. The smoke tests that launch `game01P` keep stakes on. Reason: several existing harnesses auto-play whole battles through `OnEnter` with ship assets (`content_a_tests.cpp:1458`, `headless_tools_tests.cpp:645`, `tempo_skills_tests.cpp:1022`, `run_modal_tests.cpp:118`, `battle_flow_tests.cpp:175`, `DungeonTests.cpp:72`, `AiTests.cpp:141`) and assert exact rounds or materials.
- Resolved — **loader rules** (`LoadBattleObjectives`): document or key missing → empty table. Otherwise the value must be an array of objects; each row has required non-empty strings `id` (unique in the table), `display_name`, `text`, `kind` (`"no_ally_down"` or `"win_by_round"`), required integer `reward_materials` in 0..10, and integer `rounds` in 1..60 that is required for `win_by_round` and forbidden for `no_ally_down`. Any violation, a wrong type or an unknown key → `LOG_ERROR` naming the row and `nullopt`.
- Resolved — **pick**: `std::uint32_t h = seed ^ (static_cast<std::uint32_t>(depth) * 0x9E3779B1u) ^ (static_cast<std::uint32_t>(room) * 0x85EBCA77u); h ^= h >> 16; h *= 0x7FEB352Du; h ^= h >> 15; return &table.rows[h % table.rows.size()];` (empty table → `nullptr`). It depends only on the run seed and position, so Continue shows the same objective and no save field is needed. Checked: seed 12345 over depths 1..8 × rooms 0..2 yields 14 and 10 picks of the two rows.
- Resolved — **victory** (`ResolveRoomVictory`, after `run->message` gets its level-up sentence, before `RollRoomOffer`): `objective = ctx.find<FBattleObjective>()`, `stats = ctx.find<FRoomBattleStats>()`; when both exist and `stats->active`: `met = !IsBattleObjectiveFailed(objective->def, stats->rounds, stats->ko)`; set `objective->settled = true; objective->met = met; stats->objectiveId = objective->def.id; stats->objectiveMet = met;`; if met add `rewardMaterials` to `run->materials` and append ` Objective met: <displayName> (+<n> material).`, else append ` Objective missed: <displayName>.` `FRoomBattleStats::ko` means "a party unit was at 0 HP at some frame"; the game has no revive, so it equals "an ally went down".
- Resolved — **`UpdateFirstContact`**: "party" = entities in `view<FPartyMember, FUnitData, FHealth>`; "others" = entities in `view<FUnitData, FHealth>` without `FPartyMember`. Adjacent = a living (`IsAlive`) party unit and a living other whose `FGridPosition`s have `GridDistance == 1`. Damage seen = some id with `hpMin < hpStart` in the stats, or some other (alive or not) with `FHealth::current < FHealth::max`. Called from `AccumulateRoomStats` right after the loop that updates `hpMin` (line 138).
- Resolved — **`BuildRoomEnd`**: write `crits`, `first_damage_round`, `first_adjacent_round` always; write `objective: {"id", "met"}` only when `objectiveId` is not empty; reset all five new fields together with the existing ones.
- Resolved — **data** (`assets/data/dungeon.json`): in `balance` add `"crit_chance_percent": 15, "crit_multiplier_percent": 200`; add top-level
  `"battle_objectives": [ {"id": "objective.unbroken", "display_name": "Unbroken", "text": "No ally goes down", "kind": "no_ally_down", "reward_materials": 1}, {"id": "objective.swift", "display_name": "Swift", "text": "Win by round 5", "kind": "win_by_round", "rounds": 5, "reward_materials": 1} ]`. The dungeon loader checks required keys only, so the new keys need no loader change.
- Resolved — **display**:
  - `EmberCombatInfo.cpp:278`: `FPreviewBadge{*cell, target.damage, target.heal, target.lethal, target.critDamage, preview->critChancePercent}`.
  - Preview plate: when `badge.critDamage > 0` the text is `"-%d  %d%% -%d"` (damage, chance, critDamage) in a `char text[32]`; otherwise unchanged.
  - `React`: build the pop as a named value, `FDamagePop pop{glm::vec2{…}, target.healthBefore - target.healthAfter, 0.f}; pop.crit = target.crit; effects->pops.push_back(pop);`.
  - `DamagePopOverlays`: a crit pop prints `"CRIT -%d"` in a `char text[24]` and uses `size * 1.35f` for both `LabelWidth` and the draw call, so it stays centred.
  - `TopBar`, the line at 334: with `objective = ctx.find<FBattleObjective>()` absent, the existing `"%s  /  seed %u"` line. Otherwise the base text is `"%s +%d: %s"` (displayName, rewardMaterials, text) and: `settled && met` → base + `" (met)"` with `ImGui::TextColored(pal::kGoldV, …)`; `settled && !met` → base + `" (lost)"` with `ImGui::TextDisabled`; not settled and failed (`IsBattleObjectiveFailed` with the current `FRoomBattleStats` `rounds` and `ko`, when those stats exist and are active) → base + `" (lost)"` with `TextDisabled`; else base in gold. The longest line, `Unbroken +1: No ally goes down (lost)` (37 characters), is shorter than the 38-character seed line the layout guard already measures; `tests/features/compendium_tests.cpp` gets one added guard directly after the seed-line check (lines 493–494), in the same form: `const float objectiveLine = lineWidth("Unbroken +1: No ally goes down (lost)", 12.f * u) + 8.f * u; CHECK(layout.zones.title.x + objectiveLine < layout.zones.turnOrder.x);`. The preview plate keeps one colour: the crit part is deliberately not lethal-coloured when only a crit would kill.
- Resolved — **`combat-math.md` §1**: after list item 4 add `5. 적용 시 굴림 (치명타) — 아군 스킬의 직접 피해에만, 대상별 1회. 최종 수치 × crit_multiplier_percent / 100, 보스 피해 상한은 그 뒤에 적용. 적은 굴리지 않는다. (WP-39)`; replace the line `Owner per stage: TBD (filled by WPs that add modifiers)` with `Owner per stage: 1–4 TBD (filled by WPs that add modifiers); 5 = ActionResolverSystem (roll), PreviewSkill (numbers).`
- Alternatives considered: enemy crits — rejected, they break `expectedAtExecution == actualDamage` (FIntent.h). A damage range — rejected, ±10% of single-digit damage is under 1. Rolling crit on the battle RNG (as `expansion-plan.md` §3.1 words it) — replaced by a dedicated `critRng` seeded from it, so existing status roll sequences do not shift. The plan's preview wording `8 dmg · crit 16 (15%)` — shortened to `-8  15% -16` to fit the plate, same three numbers. An EXP penalty for KO'd units — rejected, a level gap accumulates and the ruling forbids that. Storing the objective in the save — rejected, the pick is a pure function of seed and position. Editing each auto-play harness to switch stakes off — rejected for one environment switch.
- Defaults for anything unspecified: follow the nearest existing code in the same file (naming, logging, error style); strings are English.
- Out of scope: nodes or runes that change crit chance; two-sided collision damage; any change to enemy AI, intents, deployment or move range; the route strip and "ROOM n / 3" text (WP-41).

## 9. Pitfalls
- A new `tests/features/*_tests.cpp` is picked up by a CMake glob: reconfigure once (T1) or the test never builds.
- One build/ctest at a time; an MSB3073/MSB3075 DLL-copy failure is a transient lock — rerun once.
- ImGui math operators are disabled: build `ImVec2` component-wise.
- Player-facing strings are English and ASCII; the HUD font has no Hangul.
- Game runs from the agent stay windowless with `GAME01P_SAVE_DIR` under `out/`.
- `PreviewSkill` is also the AI's and the intent system's source of numbers: the `FPartyMember` condition is what keeps enemy previews at `critDamage == 0`.
- `FBattleRng::rollChance` returns early for 0 and 100 without advancing the state: RNG-state assertions need a chance strictly between.
- `FRoomBattleStats` is zeroed by `BuildRoomEnd` when the RoomCleared event is appended inside `ResolveRoomVictory`; tests read the objective result from the `room_end` line or from `FBattleObjective`, never from the stats.
- Tests inherit `GAME01P_STAKES=0` from ctest; a test that needs stakes calls `ApplyBattleStakes` directly.

## 10. Open questions
None.
