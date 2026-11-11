# WP-44: Rule-based elites — four build-testing rules, tougher enemies, uncommon-or-better rune reward

Size: L · Player-facing: yes · Certainty: settled · Base commit: the commit that adds this spec · Branch: wp/WP-44

## 1. Goal
An elite today is a normal battle with one stat bump (HP +30% or attack +1). It gives no reason to change a build or a route. This WP replaces the two stat affixes with four rules, each punishing one way of playing, shown on the fork before the player commits and during the battle. Every elite also has enemies with 20% more max HP, so an elite whose rule misses the party is still a harder fight than the standard branch. The elite reward becomes a rune offer whose cards are all uncommon or better; the 2 bonus materials are removed.

- Decisions it rests on (`docs/design/decisions.md`, section "난이도·빌드 다양성"):
  - 엘리트 규칙 (WP-44): four rules, stat affixes removed (user, 2026-11-11, WP-44 questions 1 and 2).
  - 엘리트 보상의 형태 (WP-44): all cards uncommon or better (uncommon 60 / rare 40), 2 materials removed (user, WP-44 question 3).
  - WP-44 changes elites only; the boss is untouched (user, WP-44 question 4).
  - 엘리트 기본 대가 (WP-44): every elite has enemy max HP +20% regardless of its rule (user, 2026-11-11, review question).
  - 확정 "무작위성만으로 실패를 만들면 부당한 난이도": every rule is deterministic and announced.
- Source: Slay the Spire elites punish one deck type each (Gremlin Nob: skill use raises its strength), `docs/design/research-2026-11-11.md`.
- Terms: "ship data" is `assets/data/`; "elite" is the variant branch of a battle fork (WP-41), one per dungeon at most; "affix" is the existing code and data name of the elite's modifier and is kept for the rules (`elite.affixes[]`, `FEliteAffix`, `affixId`); "sheet" is the derived combat stats of a unit (`RefreshCombatSheet`); "feed" is the battle log the HUD shows (`FCombatLog`); a "battle fork" is the reward-screen choice between the standard battle and its elite variant (`ForkOptions`, WP-41); the "path strip" is the row of room labels at the top right of the battle HUD (`DrawPathStrip`); "intent" is the action an enemy commits at round start and shows on the board; `features.json` and "the gate" are the WP checklist and `.claude/scripts/wp-gate.mjs`.

## 2. Player experience
- **Decision changed**: at a battle fork the player chooses "Battle" (standard rewards) or "Elite: <rule name>". The rule decides which party pays:
  - **Spell-fed** — every Spell-tagged skill the party uses gives all enemies +1 attack and magic attack from the next round, up to +4. Bad for spell-heavy parties; free for weapon parties.
  - **Thorned** — a party member who damages an enemy with a Melee-tagged skill loses 2 HP per enemy hit, never below 1 HP. Bad for melee parties and melee area skills; free for ranged and spell parties.
  - **Escalating** — from round 3 on, all enemies gain +1 attack and magic attack at the start of each round, up to +4. Bad for slow, defensive parties; cheap for burst parties.
  - **Unyielding** — enemies cannot be frozen or paralyzed. Bad for control builds; free for the rest.
  - All four also give every enemy +20% max HP, and all four pay the same reward: a rune offer with only uncommon and rare cards. The choice is whether this party, at this HP, can afford a longer fight under this rule for that offer. A rule that misses the party makes the elite cheaper, never free.
  - "Attack" in every rule text means both attack and magic attack; the texts say "attack" to stay short, and the HUD suffix and feed lines follow them.
- **What the player sees before committing**: the fork panel's elite button shows the rule name, and under it (existing panel, `DrawForkPanel`) one detail text: `Enemy max HP +20%. <rule text>. <affected skills>Reward: rune offer (uncommon or better)`. For Spell-fed and Thorned, `<affected skills>` names the party's skills the rule hits, e.g. `Your Melee skills: Cleave, Smite. ` or `Your party has no Melee skills. `, because skill tags are not shown anywhere on the battle HUD or the fork panel.
- **What the player sees in the battle**:
  - A line under the path strip (top right of the battle HUD), shown only while the run is in its Battle phase and wrapped at a fixed width: `<rule name>: <rule text>`, followed for Spell-fed and Thorned by ` <affected skills>` (the same sentence as on the fork), for Spell-fed and Escalating by `  |  enemy attack +N`, and for Spell-fed with a queued bonus by ` (+M next round)`.
  - A feed line whenever a rule acts: the bonus applied at round start, the thorn damage, a blocked status.
  - Attack bonuses change only at round start, before the enemies commit their intents, so the damage numbers on enemy intents are always true.
- **Playtest hypothesis**: expect the player to read the rule and pick or skip the elite because of the party's build, or to play the elite battle differently (holding spells, attacking from range, rushing) · observe a remark of that kind in the playtest notes, the elite branch taken at 30–80% of forks (`FORKS` block of `runlog-report.mjs`, rows `elite:<affix id>`), and a visible spread of pick rate between the four rules · kill-if the player takes or skips every elite regardless of the rule, or cannot say after the battle what the rule did.

## 3. Behaviors
| ID | Behavior | Proving test |
|---|---|---|
| F1 | WHEN the dungeon data is loaded THEN each `elite.affixes[]` row SHALL parse into a rule with its parameters, and a row with an unknown rule, a missing or out-of-range parameter, or a duplicate id SHALL fail the load with a message naming the row. | `run_path_tests::EliteBlockLoadsAndValidates` |
| F2 | WHEN the ship data is loaded THEN `elite.affixes` SHALL hold exactly the four rules of §4 with those values, `elite.reward_materials` SHALL be absent, and every status id of the Unyielding rule SHALL exist in the ship `statuses.json`. | `run_path_tests::ShipEliteRules` |
| F3 | WHEN an elite battle is built THEN the elite battle state SHALL exist with that affix and zero bonuses, every enemy's max HP SHALL be the standard branch's plus `elite.health_percent` percent of it (integer division), and attack and magic attack SHALL equal the standard branch's; in a standard battle the state SHALL be absent. | `run_path_tests::EliteBattleInstallsState` |
| F4 | WHEN a party unit uses a Spell-tagged skill in a Spell-fed battle THEN the queued bonus SHALL grow by `amount` and enemy stats SHALL NOT change until the next round starts; at that round start every enemy's attack and magic attack SHALL rise by the queued bonus, the total never above `max_bonus`. A skill without the Spell tag, or a skill used by an enemy, SHALL queue nothing. | `run_path_tests::SpellFedQueuesUntilNextRound` |
| F5 | WHEN a round starts in an Escalating battle IF the round number is at least `from_round` THEN every enemy's attack and magic attack SHALL rise by `amount`, once per round, the total never above `max_bonus`. | `run_path_tests::EscalatingFromRound` |
| F6 | WHEN a party unit damages an enemy with a Melee-tagged skill in a Thorned battle THEN the attacker SHALL lose `min(amount, current HP − 1)` HP per enemy hit; a non-Melee skill, a hit that dealt 0, an enemy attacker or a non-Thorned battle SHALL cost nothing. | `run_path_tests::ThornedHurtsMeleeAttacker` |
| F7 | WHEN an Unyielding battle starts THEN every enemy SHALL be immune to each status id of the rule; in other battles enemies SHALL NOT gain those immunities. | `run_path_tests::UnyieldingBlocksControl` |
| F8 | WHEN the HUD line is built THEN it SHALL be empty without an elite state or when the run is not in its Battle phase, and otherwise `<name>: <text>` plus the affected-skills sentence and the bonus suffixes of §2. | `run_path_tests::EliteHudLine` |
| F9 | WHEN an elite battle is won THEN a rune offer SHALL open from the `boss` rarity table in strict mode: no candidate of a rarity whose table weight is 0, also when the uncommon and rare pool runs short (then fewer cards, and an owned duplicate only of an allowed rarity); the materials gained SHALL equal those of a standard win. | `run_path_tests::EliteVictoryPaysOffer` |
| F10 | WHEN the fork panel shows an elite option THEN its detail SHALL be `Enemy max HP +<P>%. <rule text>. <affected skills>Reward: rune offer (uncommon or better)`. | `run_path_tests::ForkPanelShowsBothOptions` |
| F11 | WHEN a rule acts THEN the feed SHALL get the exact line of §5 (round-start bonus, thorn damage). | `run_path_tests::EliteFeedLines` |

## 4. Numbers
All in `assets/data/dungeon.json` → `elite` (`health_percent`) and `elite.affixes[]`. Attack values of enemy units range from 1 to 10 today, so +1 is a large step. The Knob column is the tuning range for the simulator; the loader accepts the wider ranges of §5.

| Rule (`id`) | Parameter | Unit | Value | JSON key | Knob | Metric it moves |
|---|---|---|---|---|---|---|
| all elites | enemy max HP | % of max HP | 20 | `elite.health_percent` | [0, 40] | mean rounds and party HP loss in elite battles |
| `affix.spell_fed` | bonus per Spell use | attack and magic attack | 1 | `amount` | [1, 2] | elite battle loss rate, Spell use per elite battle |
| `affix.spell_fed` | bonus cap | attack and magic attack | 4 | `max_bonus` | [2, 6] | elite battle loss rate |
| `affix.thorned` | HP lost per enemy hit | HP | 2 | `amount` | [1, 4] | party HP loss in elite battles |
| `affix.escalating` | first round | round number | 3 | `from_round` | [2, 5] | mean rounds of elite battles |
| `affix.escalating` | bonus per round | attack and magic attack | 1 | `amount` | [1, 2] | elite battle loss rate |
| `affix.escalating` | bonus cap | attack and magic attack | 4 | `max_bonus` | [2, 6] | elite battle loss rate |
| `affix.unyielding` | immune statuses | status ids | `status.freeze`, `status.paralyze` | `statuses` | none | none |

Removed: `elite.reward_materials` (was 2), `health_percent`, `attack_bonus`, the rows `affix.hardened` and `affix.fierce`.
Elite rune offer: rarity table `boss` of `assets/data/rewards.json` (common 0 / uncommon 60 / rare 40), unchanged; the elite now shares this table with the boss offer, and reads it in strict mode (§5). The +20% is the old Hardened value (+30%) reduced, since it now applies to every elite on top of a rule; Slay the Spire elites are tougher than normal fights whatever the deck.

The ship block (exact text):
```json
"elite": {
  "health_percent": 20,
  "affixes": [
    {"id": "affix.spell_fed", "display_name": "Spell-fed", "text": "Each Spell your party uses gives enemies +1 attack from the next round (up to +4)", "rule": "spell_fed", "amount": 1, "max_bonus": 4},
    {"id": "affix.thorned", "display_name": "Thorned", "text": "Melee skills cost the attacker 2 HP per enemy hit (never below 1 HP)", "rule": "thorned", "amount": 2},
    {"id": "affix.escalating", "display_name": "Escalating", "text": "From round 3, enemies gain +1 attack each round (up to +4)", "rule": "escalating", "amount": 1, "from_round": 3, "max_bonus": 4},
    {"id": "affix.unyielding", "display_name": "Unyielding", "text": "Enemies cannot be frozen or paralyzed", "rule": "unyielding", "statuses": ["status.freeze", "status.paralyze"]}
  ]
}
```

Modifier stage (`docs/design/combat-math.md`, enemy sheet): base → depth and room scale → cycle scale → **elite stage**: max HP +`health_percent`%, then the rule attack bonus (flat, added to attack and magic attack). It replaces the WP-41 elite affix stage. Owner: `RefreshCombatSheet`. The design session updates `combat-math.md` in the spec commit.

## 5. File map
Create:
- `src/gameplay/combat/FEliteRules.h`, `src/gameplay/combat/FEliteRules.cpp` — the elite battle state and the four rules. (`CMakeLists.txt` globs `src/*.cpp`; no CMake edit.)

Modify:
- `assets/data/dungeon.json` — the `elite` block of §4.
- `src/gameplay/dungeon/FRunSequence.h`, `FRunSequence.cpp` — `FEliteAffix`, `FEliteRules`, the loader (lines 218–265).
- `src/gameplay/dungeon/FRunPath.cpp` — the fork option detail (lines 329–331).
- `src/gameplay/dungeon/FDungeonRun.cpp` — install the state at the room build (lines 881–885) and apply the battle start in `ApplyRunParty` (lines 928–935).
- `src/gameplay/dungeon/FRunFlow.cpp` — elite victory message without materials (lines 524–529).
- `src/gameplay/dungeon/FRewards.cpp`, `FRewards.h` — elite offer table (cpp lines 401–406; h line 84 comment).
- `src/gameplay/combat/FCombatSheet.cpp` — the elite block (lines 86–91).
- `src/ecs/systems/ActionResolverSystem.h` — Spell-fed call (before line 389) and the immune feed line (line 376).
- `src/ecs/systems/TurnFlowSystem.h` — round-start call (before line 69).
- `src/gameplay/GameplayServices.cpp` — register the listener (after line 93).
- `src/presentation/FPathStripView.cpp` — the HUD line in `DrawPathStrip`.
- `tests/features/run_path_tests.cpp`, `tests/features/run_log_tests.cpp` — §7.

`FRunSequence.h`:
```cpp
/// The rule of an elite battle (dungeon.json `elite.affixes[].rule`, WP-44).
enum class EEliteRule : std::uint8_t { SpellFed, Thorned, Escalating, Unyielding };

/// One elite rule row. Which parameters are used depends on `rule` (see the loader).
struct FEliteAffix {
    std::string id;
    std::string displayName;
    std::string text;
    EEliteRule rule{EEliteRule::SpellFed};
    int amount{0};                      // SpellFed, Escalating: attack bonus per trigger; Thorned: HP per enemy hit
    int fromRound{0};                   // Escalating: first round that adds the bonus
    int maxBonus{0};                    // SpellFed, Escalating: cap of the total attack bonus
    std::vector<std::string> statuses;  // Unyielding: status ids the enemies are immune to
};

struct FEliteRules {
    int healthPercent{0};               // every elite: enemy max HP +healthPercent% (elite.health_percent)
    std::vector<FEliteAffix> affixes;
};
```
Loader (`FRunSequence.cpp`): `elite.health_percent` is optional, default 0; a non-integer fails with `"elite.health_percent: must be an integer"`, a value outside 0..1000 with `"elite.health_percent: must be 0..1000"`. Then per affix row, after the three string fields (unchanged):
- `rule`: required string, one of `spell_fed`, `thorned`, `escalating`, `unyielding`; else `Fail(error, where + "`rule` must be spell_fed, thorned, escalating or unyielding")`.
- Integer parameters: a non-integer fails with `where + "`<key>` must be an integer"`, a value outside 1..1000 with `where + "`<key>` must be 1..1000"`. They are required as follows; a missing required key fails with `where + "`<key>` is required for rule <rule>"`:
  - `spell_fed`: `amount`, `max_bonus`.
  - `thorned`: `amount`.
  - `escalating`: `amount`, `from_round`, `max_bonus`.
  - For `spell_fed` and `escalating`, `max_bonus < amount` fails with `where + "`max_bonus` must be at least `amount`"`.
- `unyielding`: `statuses` must be a non-empty array of non-empty strings; else `where + "`statuses` must be a non-empty array of status ids"`.
- Keys a rule does not use are not read. `health_percent`, `attack_bonus` and `elite.reward_materials` are no longer read and cause no error. Replace the `reward_materials` block (lines 220–229) with the `health_percent` read and delete the "needs health_percent or attack_bonus" check. Per-row `health_percent` is not read. The duplicate-id check stays.

`FEliteRules.h` (namespace `game`, like the other files of `gameplay/combat`; includes `<entt/entt.hpp>`, `gameplay/dungeon/FRunSequence.h`, `gameplay/combat/FEffectHooks.h`, `gameplay/data/FSkillContent.h`, and forward-declares or includes `gameplay::FContentRegistry` the way `FCombatSheet.h` does). `FEliteRules.cpp` additionally includes what it uses: `gameplay/combat/FCombatApply.h`, `FCombatQuery.h`, `FCombatTypes.h`, `FCombatSheet.h`, `FStatusRules.h` (same folder), `ecs/components/FFaction.h`, `FActiveStatuses.h`, `FHealth.h`, `gameplay/data/FContentRegistry.h`, `gameplay/dungeon/FDungeonRun.h`, `gameplay/dungeon/FRewards.h`. Every file that calls into it adds `#include "gameplay/combat/FEliteRules.h"`: `FCombatSheet.cpp`, `FDungeonRun.cpp`, `FRunPath.cpp`, `ActionResolverSystem.h`, `TurnFlowSystem.h`, `GameplayServices.cpp`, `FPathStripView.cpp`, `run_path_tests.cpp`.
```cpp
/// The rule of the battle being fought. Registry context, never saved: the room build installs it.
struct FEliteBattleState {
    FEliteAffix affix;                      // a copy of the rule row
    int healthPercent{0};                   // a copy of FEliteRules::healthPercent
    int attackBonus{0};                     // applied to every enemy's attack and magic attack by the sheet
    int pendingBonus{0};                    // SpellFed: queued, applied at the next round start
    std::uint64_t lastRound{0};             // the last round number EliteOnRoundStarted processed
};

/// Erases the state; when affix is non-null, emplaces a fresh one (bonuses 0, lastRound 0).
void InstallEliteBattle(entt::registry& registry, const FEliteAffix* affix, int healthPercent);
[[nodiscard]] const FEliteBattleState* FindEliteBattle(const entt::registry& registry);
/// No state: nothing. Else: bonuses and lastRound back to 0; for Unyielding, adds each status id of the
/// rule to `FActiveStatuses::immune` of every entity with `FEnemy` (get_or_emplace; no duplicate entries).
void ApplyEliteBattleStart(entt::registry& registry);
/// SpellFed only: when actor has FPartyMember and skill.tags contains "Spell",
/// pendingBonus = min(pendingBonus + amount, maxBonus - attackBonus). Nothing else changes.
void EliteOnSkillUsed(entt::registry& registry, entt::entity actor, const gameplay::FSkillContent& skill);
/// Once per round number (roundNumber == 0 or <= lastRound: nothing). SpellFed: attackBonus += pendingBonus,
/// pendingBonus = 0. Escalating: when roundNumber >= fromRound, attackBonus = min(attackBonus + amount, maxBonus).
/// When attackBonus changed: RefreshCombatSheet for every FEnemy entity (only if the registry has an
/// FDungeonRun) and one feed line.
void EliteOnRoundStarted(entt::registry& registry, const gameplay::FContentRegistry& content, std::uint64_t roundNumber);
/// Thorned only (the OnHit rule of F6). Reads the skill row from the FContentRegistry in the registry context.
void EliteOnHit(entt::registry& registry, const FHookContext& context);
/// Registers EliteOnHit as an OnHit listener (boot, once).
void RegisterEliteRuleListener(entt::registry& registry);
/// "" for Escalating and Unyielding. For SpellFed ("Spell") and Thorned ("Melee"): the display names of the
/// party's skills whose base tags contain the tag, in PartySkillRows order, as
/// "Your <Tag> skills: A, B. " or "Your party has no <Tag> skills. " (note the trailing space).
[[nodiscard]] std::string EliteAffectedSkills(entt::registry& registry, const FEliteAffix& affix);
/// "" without a state, or when the registry has an FDungeonRun whose phase is not Battle; else the line of §2.
[[nodiscard]] std::string EliteRuleHudLine(entt::registry& registry);
```
Rule details:
- Tags are the base tags of the skill row (`gameplay::FSkillContent::tags`, exact strings `"Spell"` and `"Melee"`); tags added by runes do not count.
- OnHit facts (`ActionResolverSystem.h:338-340`): the resolver fires OnHit once per hostile target that took damage from a skill with power above 0, with `owner` = the attacker, `other` = that target, `amount` = the damage dealt, `skillId` = the skill; registered listeners run for every such hit, whatever hooks the units own. A hit that kills the target also fires it, so Thorned charges on a killing hit too.
- Thorn damage goes through `ApplyDamageToUnit` only: it fires no hooks and is not a skill hit. The room statistics see it as ordinary HP loss.
- `ApplyRunParty` has one caller (`FEmberBattleState.cpp:121`), after `SetupCombatUnits` has created the enemies, once per battle; that is why `ApplyEliteBattleStart` may reset the bonuses there.
- `EliteOnHit`: requires a state with rule Thorned, `context.amount > 0`, `context.owner` with `FPartyMember`, `context.other` with `FEnemy`, a skill row for `context.skillId` whose tags contain `"Melee"`, and the owner's `FHealth`. Loss = `min(affix.amount, health.current - 1)`; when the loss is above 0: feed line, then `(void)ApplyDamageToUnit(registry, context.owner, loss)`.
- Feed lines (`registry.ctx().find<FCombatLog>()`, skipped when null), exact formats:
  - round start with a changed bonus: `"<displayName>: enemies +<delta> attack (now +<attackBonus>)"`
  - thorn: `"<UnitDisplayName(owner)> takes <loss> damage (Thorned)"`
  - blocked status (resolver): `"<targetName> is immune to <status displayName>"`. `TryApplyStatus` already refuses an immune status silently; the resolver pre-check exists only to tell the player, and must stay.
- `EliteRuleHudLine`: `affix.displayName + ": " + affix.text`; for SpellFed and Thorned append `" "` + `EliteAffectedSkills` with its trailing space removed; for SpellFed and Escalating append `"  |  enemy attack +" + attackBonus`; for SpellFed with `pendingBonus > 0` append `" (+" + pendingBonus + " next round)"`.

Call sites:
- `FDungeonRun.cpp:882-885`: keep the `plan.elite` / `plan.affixId` stamp; directly before the `if` add a call that installs the state: `InstallEliteBattle(registry, EliteAffixFor(registry, run.room), <healthPercent>)`, where `<healthPercent>` is `FindRunSequence(registry)->elite.healthPercent` (0 when the sequence is null). It erases the state for a non-elite room.
- `FDungeonRun.cpp` `ApplyRunParty`: call `ApplyEliteBattleStart(registry);` as the first statement, before the sheets are refreshed.
- `FCombatSheet.cpp:86-91`: replace the WP-41 block with
  `if (const FEliteBattleState* elite = FindEliteBattle(registry)) { health->max += health->max * elite->healthPercent / 100; stats->attack += elite->attackBonus; stats->magicAttack += elite->attackBonus; }` and a comment naming the stage of §4. The enclosing non-party condition is unchanged; the HP line is the old affix line with the percent taken from the state.
- `ActionResolverSystem.h`, directly before `EmitAt(registry, std::move(event), eventBase);` (line 389): `if (skillRow != nullptr) EliteOnSkillUsed(registry, actor, *skillRow);`
- `ActionResolverSystem.h`, in the status loop, directly before the `TryApplyStatus` call (line 376): `if (IsStatusImmune(*active, definition->id)) { if (log) log->push(targetName + " is immune to " + definition->displayName); continue; }`. This also covers the boss's existing immunities.
- `TurnFlowSystem.h`, directly before `CommitEnemyIntents(registry, *content);` (line 69): `EliteOnRoundStarted(registry, *content, runtime->controller.snapshot().roundNumber);` — the bonus must be on the sheets before the intents are committed.
- `GameplayServices.cpp`, after `RegisterEngineIgnitionListener(registry);`: `RegisterEliteRuleListener(registry);` with a `// [WP-44]` marker like its neighbours.
- `FPathStripView.cpp` `DrawPathStrip`, after the node loop and inside the `ImGui::Begin` block: `const std::string rule = EliteRuleHudLine(registry); if (!rule.empty()) { ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 460.f * scale); ImGui::TextColored(ui::palette::kGoldBrightV, "%s", rule.c_str()); ImGui::PopTextWrapPos(); }` (a new line, no `SameLine`; the wrap keeps the right-anchored window from growing across the HUD).
- `FRunPath.cpp:329-331`: detail = `"Enemy max HP +" + std::to_string(sequence->elite.healthPercent) + "%. " + affix->text + ". " + EliteAffectedSkills(registry, *affix) + "Reward: rune offer (uncommon or better)"`. When `healthPercent` is 0 the first sentence is left out.
- `FRunFlow.cpp:524-529`: the block becomes `if (elite) run->message += " Elite defeated.";`. No materials are added.
- `FRewards.h` / `FRewards.cpp`, strict mode. New signatures (the added parameter is defaulted, so every existing caller is unchanged):
  ```cpp
  bool OpenRuneOffer(entt::registry& registry, std::string_view rarityTable, bool strictTiers = false);
  [[nodiscard]] std::vector<std::string> RollRuneCandidates(entt::registry& registry, std::string_view rarityTable = kRarityTableNormal, bool strictTiers = false);
  /// The distinct skills of the run party (the file-local PartySkills, made public for the elite texts).
  [[nodiscard]] std::vector<const gameplay::FSkillContent*> PartySkillRows(entt::registry& registry);
  ```
  `CandidatesFor` (file-local) gets the same `bool strictTiers` and both public functions pass it through; `PeekRuneCandidates` passes false. In strict mode with a non-null weights table, directly after the `unowned` / `owned` lists are built (line 186), remove from both lists every rune whose rarity has weight 0 in that table. Nothing else in the function changes: the slot loop, the fallback walk and the duplicate rule (`fromUnowned < requested`) then run on allowed runes only, so a short pool gives fewer cards plus at most one owned duplicate of an allowed rarity, and one tier roll per slot is still consumed. `PartySkillRows` returns `PartySkills(registry, content, run)` for the registry's content and run, or an empty vector when either is missing.
- `FRewards.cpp:401-406` (elite win): `if (!OpenRuneOffer(registry, kRarityTableBoss, true)) LOG_INFO("Elite rune offer withheld: too few legal candidates.");` — there is no non-strict retry: a common card would break the ruling "every card uncommon or better". The offer is withheld only when fewer than `min_legal_candidates` (ship 2) allowed cards exist, counting the owned duplicate. Update the comment there and at `FRewards.h:84`. `kRarityTableBoosted` stays (events use that table).

Do not touch: `assets/data/rewards.json`, `assets/data/events.json`, `assets/data/statuses.json`, `assets/data/bosses.json`, boss code (`FBossGoals.h`, `FBossGoalSystem.h`, `FBossContent.*`), `src/gameplay/dungeon/FRunLog.cpp`, `.claude/scripts/**`, `tests/fixtures/**`, `CMakeLists.txt`, `docs/design/**`.

## 6. Tasks
`node .claude/scripts/build.mjs --preset windows-msvc-agent` configures if needed (a new source file is picked up by the glob), builds and runs every ctest entry; it prints only errors and failing tests. A case "passes" when it is absent from that output. `docs/wp/WP-44/progress.md` is created by the implementer; the gate allows it. The implementer never edits `spec.md` or `features.json`. A build takes about 10 minutes; tasks may share one build when each task's commit is separate and the shared build's result is checked against the later task's Done line.

1. **T1 Data model, loader, ship data, reward** — everything that must change together for the build and the existing tests to stay green:
   - `FRunSequence.*` as in §5, and the ship `elite` block of §4 in `assets/data/dungeon.json` (the new loader rejects the old rows, and many tests load the ship file).
   - Create `FEliteRules.h/.cpp` with `FEliteBattleState`, `InstallEliteBattle` and `FindEliteBattle` implemented; `ApplyEliteBattleStart`, `EliteOnSkillUsed`, `EliteOnRoundStarted`, `EliteOnHit`, `RegisterEliteRuleListener` as empty stubs; `EliteAffectedSkills` and `EliteRuleHudLine` returning `""`.
   - `FCombatSheet.cpp`, `FRunPath.cpp`, `FRunFlow.cpp`, `FRewards.*` as in §5 (these are the only other users of the removed fields; the `rewardMaterials` in `FBattleObjective.*`, `EmberCombatHudView.cpp` and `combat_stakes_tests.cpp` belongs to battle objectives and is not touched).
   - Tests: the fixture blocks, the rewritten and renamed existing cases, and all new cases of §7.

   Done: build.mjs → the only failing cases are `EliteBattleInstallsState`, `SpellFedQueuesUntilNextRound`, `EscalatingFromRound`, `ThornedHurtsMeleeAttacker`, `UnyieldingBlocksControl`, `EliteHudLine`, `EliteFeedLines` (`ForkPanelShowsBothOptions` and `EliteVictoryPaysOffer` already pass: the fork text is complete except for the affected-skills sentence, which the test builds with the same stubbed function). Stop condition, reported as `BLOCKED:` instead of being worked around: a test outside the file map fails on the new ship data or loader (report the test name and the assertion). Commit.
2. **T2 Rules** — implement `ApplyEliteBattleStart`, `EliteOnSkillUsed`, `EliteOnRoundStarted`, `EliteOnHit`, `RegisterEliteRuleListener`; the call sites in `FDungeonRun.cpp`, `ActionResolverSystem.h`, `TurnFlowSystem.h`, `GameplayServices.cpp`. Done: build.mjs → the only failing case is `EliteHudLine`.
3. **T3 Texts** — `EliteAffectedSkills`, `EliteRuleHudLine`, `FPathStripView.cpp`. Done: build.mjs → 0 failing tests.
4. **T4 Evidence run** — PowerShell, timeout 600000 ms: `node .claude/scripts/baseline.mjs --label wp44-elite --seeds 1-12 --policy "reward=card0,event=first,chest=open,fork=variant,camp=offer+1,rest=60,continue" | Out-File -Encoding utf8 out/baseline-wp44-elite.txt` (`out/` exists; the report is the script's stdout). Done: exit code 0 and the file is non-empty; copy into `docs/wp/WP-44/progress.md` the report's battle rows whose label contains `elite` and every row under its `FORKS` heading. The numbers are a record, not a pass condition.

## 7. Tests
All in `tests/features/run_path_tests.cpp` (helpers of that file: `FPathBoot`, `PickSeed`, `kNoSeed`, `FBattleHarness` with `toRoomOne(game::ERunIntent)`, `.registry`, `.enemies()`, `.partyMaxHp()`; `FBattleHarness` boots `FGameplayServices`, so the OnHit listener is registered). The content registry is `registry.ctx().get<game::gameplay::FContentRegistry>()`. `FBattleHarness` declares its own nested `FEnemy` struct, so the components are always written in full: `game::FEnemy`, `game::FPartyMember`. If `registry.ctx().find<game::FCombatLog>()` is null in a harness, the case emplaces one before it asserts feed lines. The elite harness of every new case is built like the old `EliteAffixScalesEnemies` (lines 479–497): `FPathBoot probe(name, {"battle", "battle", "battle"})`, `PickSeed` for the wanted affix, `FBattleHarness elite(seed, probe.assets.Roots())`, `elite.toRoomOne(game::ERunIntent::TakeFork)`. Every new case that reads the elite state starts with `const auto* state = game::FindEliteBattle(registry); CHECK(state != nullptr); if (state == nullptr) return;` — in T1 the state is not installed yet, and a null dereference would abort the whole `run_path_tests` executable instead of failing one case. Register new cases in the file's `RunTests` list.

Fixture: the test-local elite block (lines 147–150) becomes the four rows of §4 without `reward_materials`. The fixture block also gets `"health_percent": 20`. Existing cases that pick a seed by `affix.hardened` (lines 754 and 860) pick `affix.unyielding` instead (it changes nothing for the party); their label assertions follow: `"Elite: Hardened"` becomes `"Elite: Unyielding"` at lines 768 and 786, and `"Battle / Elite: Hardened"` becomes `"Battle / Elite: Unyielding"` at line 867. `AffixIsSeeded` (lines 328 and 331) hard-codes two affixes: use `% affixes.size()` and `seen.size() == affixes.size()` (the fixture's row count). The comments at lines 239, 243 and 522 are updated to the new block. `run_log_tests.cpp:338-343`: the fixture `elite` object becomes `{"affixes": [<the affix.thorned row of §4>]}`; if a later assertion in that file names `affix.hardened`, it names `affix.thorned`.

To find skills for the tag cases: read `tests/fixtures/assets/data/skills.json` once and hard-code, as named constants at the top of the new cases, one skill id whose `tags` contain `"Spell"`, one whose tags contain `"Melee"`, and one with neither (the Spell constant must not also carry `"Melee"`, and the Melee constant must not carry `"Spell"`). Get the row with `content.skills.find(id)` and assert it is non-null and has the expected tag, so a fixture change fails loudly.

- **EliteBlockLoadsAndValidates** (F1, rewrite): the four rows load with `rule`, `amount`, `fromRound`, `maxBonus`, `statuses` as authored. `elite.healthPercent == 20`. Broken variants, each must fail the load (the file's `broken(name, mutate)` helper): unknown `rule`; `spell_fed` without `max_bonus`; `spell_fed` with `max_bonus` below `amount`; `thorned` with `amount` 0; `escalating` with `from_round` 0; `unyielding` with an empty `statuses`; duplicate id; `health_percent` −1. Remove the `reward -1` variant and the `rewardMaterials` assertion.
- **ShipEliteRules** (F2): parse `assets/data/dungeon.json` and `assets/data/statuses.json` from disk (`kShipAssetRoot`); `elite.health_percent == 20`; `elite.affixes` has exactly the four ids of §4 in that order with the values of §4; `!elite.contains("reward_materials")`; each id in the Unyielding row's `statuses` is the `id` of a row in `statuses.json`.
- **EliteBattleInstallsState** (F3, replaces `EliteAffixScalesEnemies`; remove the old case from the list): for each of the four ids: `PickSeed` a seed whose fork has that affix at slot 1, `CHECK(seed != kNoSeed)` (if `PickSeed` finds no seed for one of the four at slot 1, use the slot the existing cases use for that affix search, and if there is still none report `BLOCKED:` with the affix id); elite harness `toRoomOne(TakeFork)` → `FindEliteBattle` non-null, `affix.id == wanted`, `attackBonus == 0`, `pendingBonus == 0`; standard harness `toRoomOne(ContinueReward)` → `FindEliteBattle == nullptr`; enemy count equal; per enemy `hpMax == standard + standard * 20 / 100`, `attack` and `magic` equal between the two; `partyMaxHp()` equal.
- **SpellFedQueuesUntilNextRound** (F4): Spell-fed elite harness; `party` = the first entity with `FPartyMember`, `foe` = the first with `FEnemy`; record the enemies' attack and magic. `EliteOnSkillUsed(registry, party, spellRow)` twice → `pendingBonus == 2`, enemy stats unchanged. `EliteOnSkillUsed` with the no-tag row and with `foe` as actor → still 2. `EliteOnRoundStarted(registry, content, 2)` → `attackBonus == 2`, `pendingBonus == 0`, every enemy attack and magic == recorded + 2. Calling round 2 again → unchanged. Five more Spell uses → `pendingBonus == 2` (cap 4 − 2); `EliteOnRoundStarted(…, 3)` → `attackBonus == 4`, every enemy == recorded + 4. One more Spell use → `pendingBonus == 0`.
- **EscalatingFromRound** (F5): Escalating elite harness; record enemy attack. `EliteOnRoundStarted` for rounds 1 and 2 → `attackBonus == 0`; round 3 → 1; round 3 again → 1; rounds 4, 5, 6 → 2, 3, 4; round 7 → 4; every enemy attack and magic == recorded + 4. `EliteOnSkillUsed` with the Spell row → `pendingBonus == 0`.
- **ThornedHurtsMeleeAttacker** (F6): Thorned elite harness; `party`, `foe` as above; set the party unit's `FHealth::current` to 10. `EliteOnHit(registry, {.owner = party, .other = foe, .amount = 5, .skillId = meleeId})` → 8. Same with the Spell skill id → 8. With `.amount = 0` → 8. With owner and other swapped → both units' HP unchanged. Set current to 2 → after a melee hit 1. Again → 1, and the unit has no `FDead`. In a standard harness the same melee hit → HP unchanged.
- **UnyieldingBlocksControl** (F7): Unyielding elite harness → for every `FEnemy` entity, `IsStatusImmune(registry.get<FActiveStatuses>(entity), "status.freeze")` and `"status.paralyze"` are true, and each id appears once in `immune`. Standard harness of the same seed → both false for every enemy that has the component.
- **EliteHudLine** (F8): standard harness → `""`. Thorned → starts with `"Thorned: Melee skills cost the attacker 2 HP per enemy hit (never below 1 HP) "` and the rest equals `EliteAffectedSkills(registry, affix)` without its trailing space; that text starts with `"Your Melee skills: "` or equals `"Your party has no Melee skills."`, and when it lists skills each listed name is the `displayName` of a `PartySkillRows` row whose tags contain `"Melee"`, and every such row is listed. Escalating → `EliteAffectedSkills` is `""`. With the run's phase set to `game::EDungeonPhase::Reward` → `""`; set back to Battle → non-empty. Spell-fed at start → ends with `"  |  enemy attack +0"`; after one Spell use → ends with `"  |  enemy attack +0 (+1 next round)"`; after `EliteOnRoundStarted(…, 2)` → ends with `"  |  enemy attack +1"`.
- **EliteVictoryPaysOffer** (F9, rewrite of `EliteVictoryPaysOfferAndMaterials`; rename it in the list). The frozen fixture runes carry no `rarity` (all read as common), so the case needs its own tiers: extend `FTieredPathBoot` (lines 529–538) so that, in its constructor before `FBoot` loads content (the comma expression at lines 535–536), it also writes `runes.json` into the temp root with `FTempAssets::Write` = the fixture `runes.json` with `"rarity": "uncommon"` on every row except `rune.fire`, `rune.cold` and `rune.lightning`, which get `"rarity": "common"`. `TieredRewards()` already copies the ship rarity tables, so `boss` (0/60/40) exists. Then: after the elite win `rewards.offer.rarityTable == "boss"`; no candidate is one of the three common ids; the materials gained by the elite win equal those gained by a standard win of the same seed. Dry pool: build two more `FTieredPathBoot` boots of the same seed, each followed by `game::StartRun(registry, seed, true)`, and in both push every uncommon rune id of the fixture except one into `boot.boot.run().runes` (`FDungeonRun::runes`, the owned list). On the first, `game::RollRuneCandidates(registry, game::kRarityTableBoss, true)` returns no common id and at most 2 ids (the one unowned uncommon plus at most one owned duplicate). On the second, the same call with `false` returns a list that contains one of the three common ids. If that non-strict list contains none of them (the three are not legal for the fixture party), report `BLOCKED:` with the list.
- **ForkPanelShowsBothOptions** (F10): `game::ForkOptions(registry)[1].detail` equals `"Enemy max HP +20%. " + affix.text + ". " + EliteAffectedSkills(registry, affix) + "Reward: rune offer (uncommon or better)"` for the picked seed's affix. The captured panel text (the old assertion at line 884) is checked by fragments, because the panel wraps: it contains `"Enemy max HP +20%"` and `"uncommon or better"` and contains neither `"boosted"` nor `"+2 materials"`.
- **EliteFeedLines** (F11): Escalating harness: after `EliteOnRoundStarted(…, 3)` the last line of `FCombatLog::lines` is exactly `"Escalating: enemies +1 attack (now +1)"`; calling round 3 again adds no line. Thorned harness: after the melee `EliteOnHit` of F6 on a unit at 10 HP the last line is `UnitDisplayName(registry, party) + " takes 2 damage (Thorned)"`; a hit that costs nothing adds no line. The immune line of the resolver is checked by the diff review (no test builds a resolver action).

Timeouts are unchanged. `features.json` exempts no file from the CHECK-count rule.

## 8. Decisions
- Resolved: four rules, stat affixes removed, uncommon-or-better offer without materials, boss untouched — the user's answers of 2026-11-11 (`decisions.md`).
- Resolved: attack bonuses apply only at round start, before `CommitEnemyIntents` — an enemy's shown intent damage is computed when the intent is committed, so a mid-round bonus would make the shown numbers wrong.
- Resolved: both bonus rules are capped (`max_bonus` 4) — enemy attack is 1–10; an uncapped Gremlin-Nob bonus would turn the rule from a build test into a wall. The cap is a knob.
- Resolved: thorn damage never downs the attacker — the same floor as the chest cost (`ChestHpLoss`: "KO 불가·HP 1 바닥"), and it keeps the resolver's target loop from running with a dead actor.
- Resolved: tags come from the base skill row only — `FResolvedSkill` carries no tags, and "Spell" / "Melee" as printed on the skill is what the player can read.
- Resolved: the elite offer reuses the `boss` rarity table — it already is "uncommon 60 / rare 40"; a separate table is added when simulation needs to tune them apart.
- Resolved: strict mode is a parameter, not a change of the table walk for everyone — the boss offer and the fixture-based reward tests rely on the fallback to common. The elite never falls back to commons (ruling: every card uncommon or better); with 16 uncommon and rare runes in the ship data and the owned duplicate counting toward the minimum, a withheld offer needs a party that can legally fit fewer than two of them.
- Resolved: every elite has enemy max HP +20% (user, 2026-11-11) — a rule that misses the party would otherwise make the elite the standard battle with a better reward. The value is the old Hardened +30% reduced because it now stacks with a rule; knob [0, 40].
- Resolved: the fork and the HUD name the party's affected skills — skill tags are shown only on the preparation screen and in the compendium, so "Melee" and "Spell" would otherwise be unreadable at the moment of the choice.
- Resolved: the code and data keep the name "affix" — `affixId` is saved in the run path state and logged as `elite:<affixId>`; a rename would touch the save and the log schema for no player-visible gain. A saved run that holds a removed affix id is already handled by the path state's revalidation on load (`ForkSurvivesSaveAndContentChange`).
- Resolved: no test drives a full action through `ActionResolverSystem::Resolve` for the Spell-fed call — no existing test builds an `FActionRequest` directly; the one-line call site is verified by the diff review, the rule itself by F4.
- Alternatives considered: rules as data-driven hooks on enemy units — rejected: units cannot carry hooks today and three of the four rules react to the party's actions, which no hook event covers. Keeping the stat affixes in the pool — rejected by the user (question 2). Applying Spell-fed immediately — rejected for intent truthfulness (above).
- Defaults for anything unspecified: follow `FEngineIgnition.cpp` (a battle rule with a registered listener and feed lines) for code shape.
- Declined: naming the party's freeze and paralyze skills on the Unyielding fork text (review suggestion) — the rule text names the two statuses, which are printed on the skills and runes that apply them; Spell-fed and Thorned need the list because tags are printed nowhere in battle.
- Out of scope: boss rules, new enemy units or AI nodes, the difficulty ladder, showing thorn damage inside the skill target card, the "status applied" feed line that precedes an immune block for non-damage skills, runes with costs and keystones (WP-45), balance tuning.

## 9. Pitfalls
- `RefreshCombatSheet` recomputes from the base stats on every call (`*stats = base->stats`): a bonus written directly into `FStats` is lost. The bonus lives in `FEliteBattleState` and is added inside the sheet.
- The room build (`FDungeonRun.cpp:880-896`) also runs on Reward/Event/Camp continues; `InstallEliteBattle` there must be idempotent (erase, then emplace).
- Hook listeners are registered once at boot and are never cleared between battles; `EliteOnHit` must return early when there is no state.
- `EliteOnRoundStarted` is called every frame from `TurnFlowSystem`; the `lastRound` guard makes repeats free.
- `run_path_tests.cpp` carries its own elite fixture JSON; the frozen fixture under `tests/fixtures/` has no elite block and is not edited.
- Player-facing strings are English only. ImGui math operators are disabled: build `ImVec2` component-wise.
- One build at a time; an MSB3073/MSB3075 DLL-copy failure is a transient lock: rerun once.

## 10. Open questions
None.
