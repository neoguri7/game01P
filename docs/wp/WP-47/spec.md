# WP-47: Boss content — three bosses for the empty boss slots

Size: L · Player-facing: yes · Certainty: settled · Base commit: the commit that adds this spec · Branch: wp/WP-47

## 1. Goal
The run has four boss slots (a mid boss and a final boss in each of the two themes), but only Ember Crypt's mid slot holds a real boss, the Stone Guardian. The other three fall back to the same guardian unit with a single phase and no goal, so the theme's final fight is weaker than its mid fight and the second theme repeats the first. This WP authors three bosses as data, each built on a different archetype, so every boss slot has its own multi-phase fight.

- Decisions it rests on (`docs/design/decisions.md`): 확정 29 (the third dungeon holds the mid boss, the last dungeon of a theme the real boss), 확정 63 (a boss is a multi-stage goal), 확정 28 (difficulty rises per theme), 작업 순서 (flow completion first; WP-47 is the empty boss slots), 시스템 먼저 (systems before balance: the stats here are starting values).
- Sources (`docs/design/research` of this WP, summarised in §8): Darkest Dungeon and Slay the Spire bosses each test one thing in the party. The three archetypes used: a telegraphed area attack that punishes clumping (Darkest Dungeon's Prophet, Into the Breach leaders), a lockdown the party must play around (Drowned Crew's anchor, Hag's pot), and "kill the enablers first" (Brigand Pounder's Matchman, Slay the Spire's Collector).
- Terms: a "boss slot" is a battle room marked `"boss": "mid"` or `"final"` in `dungeon.json`; `bosses.json` → `map` assigns a boss to a (theme, slot) pair, and an unmapped slot uses the fallback guardian. A boss "phase" is an HP band (`hp_above` percent); a "parts gate" keeps the body's HP from falling below the band's floor while the boss's parts are alive. The "action phase" the AI sees (`BossPhaseAtLeast`) follows the HP phase one round later. An "AI profile" is a behaviour tree in `ai.json` that a unit selects with its `ai` key. "The gate" (script) is `.claude/scripts/wp-gate.mjs`.

## 2. Player experience
- **Decisions the bosses ask for** (each differs from the Stone Guardian's "break the hearts, then survive the cleave"):
  - **Cinder Warden** (Ember Crypt, final): it drops a burning field on a tile and its neighbours whenever that pays (two party members close together) and at least every third turn; below half HP it does so every turn and can no longer be burned or frozen. The party has to stand apart and finish the fight before the fire outpaces its healing. Favoured builds: ranged and spread-out; punished: a party that fights shoulder to shoulder.
  - **Chain Warden** (Ashen Reach, mid): it pulls a party member toward itself and shackles them in place; below half HP it commits to charging through the line and can no longer be shackled or shocked. The party has to protect or free whoever is caught. Favoured: mobile parties and control used early; punished: a fragile backline.
  - **Ash Hierophant** (Ashen Reach, final): three chanters strengthen it and keep its HP above half while they live; it weakens the party meanwhile and cannot be weakened itself. Once the chanters are dead it commits to raining fire over an area. The party has to choose between killing the chanters first or racing the growing damage. Favoured: area damage and kill priority; punished: single-target parties that ignore the chanters.
- **What the player sees before committing**: the existing boss surfaces — the boss's name, the feed line when a phase starts (`<name>: <phase note>`), the "Break <part> xN first!" guidance for a gated boss, and the committed intents of the boss and its parts on the board each round.
- **Playtest hypothesis**: expect the player to change how the party stands or whom it targets in each boss fight · observe remarks naming what each boss did, and different parties or runes doing well against different bosses · kill-if two of the three fights are played like the Stone Guardian's, or the player cannot say what a boss was doing.

## 3. Behaviors
| ID | Behavior | Proving test |
|---|---|---|
| F1 | WHEN the ship data is loaded THEN every boss slot of every theme SHALL resolve to a boss in `bosses.json`, and each boss's unit, part units and AI profile SHALL exist. | `boss_goals_tests::ShipBossesCoverEverySlot` |
| F2 | WHEN a battle is built for each of the three new bosses THEN the body SHALL carry its boss goals with the phases of §4, and the Ash Hierophant SHALL have three living parts. | `boss_goals_tests::NewBossesBuildWithGoals` |
| F3 | WHEN a new boss's action phase is 2 and a party member is in reach THEN its AI SHALL pick the phase skill of §4 (`skill.flame_field`, `skill.charge`, `skill.imp_firebomb`); in action phase 1 that rule SHALL NOT force it. | `boss_goals_tests::NewBossPhaseSkills` |
| F4 | WHEN the Ash Hierophant's chanters are alive THEN its body HP SHALL NOT fall below 50% of max; after the last chanter dies it SHALL. | `boss_goals_tests::HierophantGateHoldsWhileChantersLive` |

## 4. Numbers
All values are starting values (systems first); every stat is a knob for the balance phase.

`assets/data/units.json`, three new rows (same keys as `enemy.guardian`; `sprite` reuses `art/guardian`, which needs no code — distinct art is a later asset task):

| `id` | `display_name` | max_health | speed | move_ap | skill_ap | attack | magic_attack | defense | magic_defense | `ai` | `skills` |
|---|---|---|---|---|---|---|---|---|---|---|---|
| `enemy.cinder_warden` | Cinder Warden | 130 | 4 | 1 | 2 | 3 | 9 | 3 | 5 | `ai.boss_cinder` | `skill.flame_field`, `skill.firebolt`, `skill.imp_firebomb` |
| `enemy.chain_warden` | Chain Warden | 120 | 5 | 2 | 2 | 9 | 2 | 4 | 2 | `ai.boss_chain` | `skill.hook`, `skill.shackle`, `skill.charge`, `skill.strike` |
| `enemy.ash_hierophant` | Ash Hierophant | 150 | 3 | 1 | 2 | 2 | 8 | 3 | 6 | `ai.boss_hierophant` | `skill.debuff`, `skill.bolt`, `skill.imp_firebomb` |

Reference point: the Stone Guardian is 110 HP, attack 10, defense 5. The two final bosses sit above it; the second theme's enemies are also scaled by depth by the existing rule.

`assets/data/bosses.json` — three new `bosses` rows and three new `map` rows:
```json
{ "id": "boss.cinder_warden", "name": "Cinder Warden", "unit": "enemy.cinder_warden",
  "phases": [ { "hp_above": 50, "notes": "Kindling" },
              { "hp_above": 0, "body_immunities": ["status.burn", "status.freeze"], "notes": "Wildfire" } ] },
{ "id": "boss.chain_warden", "name": "Chain Warden", "unit": "enemy.chain_warden",
  "phases": [ { "hp_above": 50, "notes": "Hunting" },
              { "hp_above": 0, "body_immunities": ["status.locked_move", "status.paralyze"], "notes": "Unchained" } ] },
{ "id": "boss.ash_hierophant", "name": "Ash Hierophant", "unit": "enemy.ash_hierophant",
  "phases": [ { "hp_above": 50, "gate": "parts", "body_immunities": ["status.debuff"], "notes": "Chanted ward" },
              { "hp_above": 0, "notes": "Ward broken" } ],
  "parts": [ { "unit": "enemy.chanter", "count": 3, "name": "Ash chanter" } ] }
```
```json
{ "theme": "theme.ember_crypt", "slot": "final", "boss": "boss.cinder_warden" },
{ "theme": "theme.ashen_reach", "slot": "mid", "boss": "boss.chain_warden" },
{ "theme": "theme.ashen_reach", "slot": "final", "boss": "boss.ash_hierophant" }
```
Use the theme ids exactly as `dungeon.json` → `themes[].id` spells them. `enemy.chanter` is an existing unit (30 HP, skills War Cry buff and bolt, default AI); as a part it buffs the Hierophant and shoots.

`assets/data/ai.json` — four new fragments appended after `fragment.boss_phase` (which stays first among the boss fragments and unchanged) and three new profiles appended after `ai.part`:
```json
{ "id": "fragment.cinder_phase", "node": { "type": "Sequence", "children": [
    {"type": "BossPhaseAtLeast", "phase": 2},
    {"type": "Succeeder", "children": [{"type": "Engage", "skills": ["skill.flame_field"]}]},
    {"type": "UseSkill", "skills": ["skill.flame_field"]} ] } },
{ "id": "fragment.cinder_cycle", "node": { "type": "Sequence", "children": [
    {"type": "TurnIs", "every": 3, "offset": 2},
    {"type": "Succeeder", "children": [{"type": "Engage", "skills": ["skill.flame_field"]}]},
    {"type": "UseSkill", "skills": ["skill.flame_field"]} ] } },
{ "id": "fragment.chain_phase", "node": { "type": "Sequence", "children": [
    {"type": "BossPhaseAtLeast", "phase": 2},
    {"type": "Succeeder", "children": [{"type": "Engage", "skills": ["skill.charge"]}]},
    {"type": "UseSkill", "skills": ["skill.charge"]} ] } },
{ "id": "fragment.hierophant_phase", "node": { "type": "Sequence", "children": [
    {"type": "BossPhaseAtLeast", "phase": 2},
    {"type": "Succeeder", "children": [{"type": "Engage", "skills": ["skill.imp_firebomb"]}]},
    {"type": "UseSkill", "skills": ["skill.imp_firebomb"]} ] } }
```
```json
{ "id": "ai.boss_cinder", "intent_mode": "hybrid", "tree": { "type": "ActiveSelector", "children": [
    {"type": "Ref", "id": "fragment.cinder_phase"}, {"type": "Ref", "id": "fragment.cinder_cycle"},
    {"type": "Ref", "id": "fragment.fight"}, {"type": "Ref", "id": "fragment.tend_allies"} ] } },
{ "id": "ai.boss_chain", "intent_mode": "hybrid", "tree": { "type": "ActiveSelector", "children": [
    {"type": "Ref", "id": "fragment.chain_phase"},
    {"type": "Ref", "id": "fragment.fight"}, {"type": "Ref", "id": "fragment.tend_allies"} ] } },
{ "id": "ai.boss_hierophant", "intent_mode": "hybrid", "tree": { "type": "ActiveSelector", "children": [
    {"type": "Ref", "id": "fragment.hierophant_phase"},
    {"type": "Ref", "id": "fragment.fight"}, {"type": "Ref", "id": "fragment.tend_allies"} ] } }
```
Each gets a `_comment` in the file's style. `TurnIs every 3 offset 2` is true on the unit's 3rd, 6th, 9th… turn. In action phase 1 every boss also plays by `fragment.fight`, which picks whichever skill of its loadout is worth the most per AP; the phase skill is in that pool, so it can appear in phase 1 when it scores best. The phase rule forces it in phase 2. The Hierophant's phase skill is `skill.imp_firebomb` (magical, area) because its damage follows magic attack; `skill.storm` is physical and would do minimum damage from this unit.

`assets/data/battle_anim.json`: under `units`, add a row for each of the three new units that copies the `enemy.guardian` row (same clips and impact frame).

No combat modifier is added; `combat-math.md` is unaffected.

## 5. File map
Modify:
- `assets/data/units.json`, `assets/data/bosses.json`, `assets/data/ai.json`, `assets/data/battle_anim.json` — §4.
- `tests/features/boss_goals_tests.cpp` — §7.

No source file changes: the boss catalog, phases, parts gate, immunities, AI node types and the boss feed lines exist and are data-driven.

Do not touch: `src/**`, `assets/data/dungeon.json` (boss slots and the fallback `generation.boss_unit` stay), `assets/data/skills.json`, `assets/data/statuses.json`, `tests/fixtures/**` (the fixture `bosses.json` is empty on purpose), `.claude/scripts/**`, `CMakeLists.txt`, `docs/design/**`.

## 6. Tasks
Work in the developer worktree named in the handoff, on branch `wp/WP-47`. Build time is the scarce resource: for the red and green steps build and run only this test, `cmake --build --preset windows-msvc-agent --target boss_goals_tests` then `ctest --preset windows-msvc-agent -R boss_goals_tests --output-on-failure`; run the full `node .claude/scripts/build.mjs --preset windows-msvc-agent` once, at the final HEAD, so `out/evidence.json` carries that SHA. `docs/wp/WP-47/progress.md` is created by the implementer; the gate allows it. The implementer never edits `spec.md` or `features.json`.

1. **T1 Tests first (red)** — add the four cases of §7. Done: the targeted build succeeds and `boss_goals_tests` fails in exactly those four cases. Commit.
2. **T2 Data** — the four data files of §4. Done: `boss_goals_tests` passes. Commit.
3. **T3 Full build** — `build.mjs` at the final HEAD. Done: 0 failing tests. Stop conditions, reported as `BLOCKED:` instead of being worked around: the game refuses to boot on the new data (report the loader's error line); an existing `boss_goals_tests` case fails (for example `PhaseGuardSelectsSkill`, which derives its expectation from the shipped `ai.json`); any test outside the file map fails. In each case report the test name and the assertion.
4. **T4 Evidence** — PowerShell, timeout 600000 ms: `node .claude/scripts/baseline.mjs --label wp47-boss --seeds 1-4 --policy "reward=card0,event=first,chest=open,camp=offer+1,rest=60,continue" --max-rooms 40 | Out-File -Encoding utf8 out/baseline-wp47-boss.txt` (40 rooms reach the Ember Crypt final boss and the second theme's bosses when the party survives). Done: exit code 0 and the file is non-empty; copy the report's battle rows into `docs/wp/WP-47/progress.md`, and list the boss ids of the `"t":"boss_defeat"` lines found in the run logs under `out/baseline/wp47-boss/` (search the log files for that text). The numbers are a record, not a pass condition; a party wipe at a boss is a balance observation, not a failure of this WP.

## 7. Tests
All in `tests/features/boss_goals_tests.cpp`, registered in its `RunTests` list. Helpers of that file: `ReadJsonFile`, `FTempAssets` (`CopyGameplayData`, `Write`), `BossCatalog(theme, slot, boss)`, `FBattle(stem, const json* bosses, slot, seed)`, `FBossRun` (boots shipped roots; has `Body()`), `FAiProbe`, `Decide`, `SkillOf`, `FreeCellNear`, `MappedBossId`. `FBattle` copies the frozen fixture data into its temp root, and the fixture has none of the new units, skills or AI fragments, so the new cases need the shipped data: give `FBattle` a defaulted constructor parameter `bool copyFixture = true`; when false it skips `CopyGameplayData()` and writes only the given `bosses.json`, so every other file falls through to the shipped root. Existing cases are unchanged. In the new cases the body is the first entity of `registry.view<game::FBossGoals>()`. `PhaseGuardSelectsSkill` (line 1942) is the template for an AI decision test; `TransitionOnLastPartDeath` and `PartsGateCapsBodyAtThreshold` for the gate.

To put a new boss into a test battle, build a catalog document from the shipped rows: read `assets/data/bosses.json`, take the boss's row from `bosses`, and map it to the first theme's `"mid"` slot (`{"schema_version": 1, "map": [{"theme": <first theme id>, "slot": "mid", "boss": <id>}], "bosses": [<row>]}`), then `FBattle(stem, &document, "mid", seed, false)`. Put the document building in one file-local helper, `ShippedBossAsMid(bossId)`; the first theme id is read from the shipped `dungeon.json`.

- **ShipBossesCoverEverySlot** (F1): read the shipped `dungeon.json`, `bosses.json`, `units.json` and `ai.json`. For every theme and every room whose `boss` is `"mid"` or `"final"`, `map` has exactly one row with that theme id and slot. For every row of `bosses`: its `unit` and each part `unit` is the id of a `units.json` row; the body unit's `ai` value is the id of an `ai.json` profile. There are exactly four bosses and four map rows.
- **NewBossesBuildWithGoals** (F2): for each of `boss.cinder_warden`, `boss.chain_warden`, `boss.ash_hierophant`: `FBattle` with `ShippedBossAsMid(id)`; the body entity exists and has `game::FBossGoals` with `bossId == id` and two phases whose `hpAbove` are 50 and 0; `phases[0].partsGate` is true only for the Hierophant; the number of living entities with `game::FBossPart` is 3 for the Hierophant and 0 for the other two; the body's `FActiveStatuses::immune` holds `"status.debuff"` for the Hierophant at battle start and is empty for the other two.
- **NewBossPhaseSkills** (F3): for each new boss with its phase skill (`skill.flame_field`, `skill.charge`, `skill.imp_firebomb`), follow `PhaseGuardSelectsSkill` from "Clear the field down to the body vs. one adjacent hero" on: kill every non-party unit except the body, place one hero on a free cell next to the body, clear the body's statuses, set `points.move = 0` and `points.skill = points.skillMax`. With `goals.behaviorPhase = 1`: `SkillOf(Decide(registry, body, 2, true))` equals the phase skill. With `goals.behaviorPhase = 0`, for the Chain Warden only: the decision's skill is not `skill.charge` (against one adjacent hero the 1-AP hook is worth more per AP). The other two are checked in phase 2 only, because their phase skill may also win on value in phase 1.
- **HierophantGateHoldsWhileChantersLive** (F4): `FBattle` with the Hierophant; with all three parts alive, `game::ApplyDamageToUnit(registry, body, 100000)` leaves the body's HP at `BossPhaseFloor` of its 50% band (the value `PartsGateCapsBodyAtThreshold` computes for its own boss) and the body alive; after killing the three parts with `ApplyDamageToUnit`, the same call kills the body.

Timeouts are unchanged. `features.json` exempts no file from the gate's CHECK-count rule.

## 8. Decisions
- Resolved: three archetypes, one per empty slot — telegraphed area fire (Darkest Dungeon's Prophet marks ranks before the rubble falls; Into the Breach leaders attack whole lines), lockdown (Drowned Crew's "All Hands on Deck!" pulls and anchors a hero; the Hag's pot removes one), and enablers that must die first (Brigand Pounder's Matchman, Slay the Spire's Collector and its Torch Heads, Donu and Deca buffing each other). Sources: darkestdungeon.wiki.gg and slaythespire.wiki.gg pages of those bosses, read through summaries; numbers on those pages were not used.
- Resolved: data only — the catalog, phases, the parts gate, per-phase immunities and the AI node types cover all three; mid-battle summons and skill-placed traps do not exist in the engine and are not added (systems first, and neither is needed for these archetypes).
- Resolved: the Ash Hierophant's parts are the existing chanter unit — it already buffs an ally and shoots, which is the enabler role; a stationary turret (`ai.part`) would not buff.
- Resolved: sprites reuse `art/guardian` — a new art id needs an enum value, two tables and a picture; the candidate art made on 2026-11-11 is not yet wired to an asset pipeline. The bosses are told apart by name, phases and behaviour until the art task.
- Resolved: `fragment.boss_phase` and `ai.boss` are not edited — the Stone Guardian and the fallback guardian keep their behaviour, and an existing test derives its expectation from that fragment.
- Resolved: stats are set by analogy to the Stone Guardian and are not tuned here (user, 2026-11-11: systems first, balancing later).
- Alternatives considered: one shared "boss" AI profile with per-boss skills picked by role — rejected: the phase rule would again be the same for every boss. A second parts-gated boss with new turret units — rejected: two of four bosses would play like the Stone Guardian.
- Defaults for anything unspecified: copy the Stone Guardian's rows and the style of the neighbouring entries in each file.
- Out of scope: boss art and animation, a boss HUD (phase and goal are feed lines today), new skills, summons, boss-room escorts (two units from the generation pool, as today), rewards, balance.

## 9. Pitfalls
- The boss loader validates at boot: phases must be strictly descending and end at 0; only one phase may have a gate, it needs `hp_above` above 0 and at least one part; immunities must be known status ids; at most 6 parts; map rows need a known theme id and no duplicate (theme, slot). A wrong row stops every test that boots ship data.
- Phase notes appear in a feed line with a 34-character budget (`<name>: <notes>`); keep the names and notes of §4 as written.
- An unknown `ai` profile id or an unknown node parameter fails the boot; `TurnIs` takes `every` 1..20 and `offset` smaller than `every`.
- Tests that copy fixture data get the fixture's empty `bosses.json`; tests that do not copy it fall through to the shipped file and now see four bosses.
- The existing case `GateGuidanceFitsOneLine` checks every shipped boss's guidance and phase lines against the 34-character budget; the names and notes of §4 fit ("Shielded! Break Ash chanter x3" is 30).
- `enemy.chanter` is also in the escort pools, so the Hierophant's room may hold more chanters than the three parts; only the three claimed as parts hold the gate.
- JSON files in `assets/data/` use CRLF line endings; keep them.
- One build at a time per worktree; an MSB3073/MSB3075 DLL-copy failure is a transient lock: rerun once.

## 10. Open questions
None.
