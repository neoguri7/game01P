# WP-43: Recovery structure — free healing removed, camp is rest or reinforce

Size: L · Player-facing: yes · Certainty: settled · Base commit: the commit that adds this spec · Branch: wp/WP-43

## 1. Goal
The playtest of 2026-11-11 found the game too easy: the autoplay baseline lost 0 of 144 battles and reached camps at 84% HP. HP comes back for free in three places (after every room, on camp arrival, after going down), so damage taken never becomes a decision.
This WP removes the free healing and makes each camp one exclusive choice: **rest** (heal) or **reinforce** (buy stat upgrades). HP lost in a dungeon now has to be paid for with a camp's worth of growth.

- Decisions it rests on (`docs/design/decisions.md`, section "난이도·빌드 다양성"):
  - 회복 구조: free healing removed, camp is heal-or-upgrade (user, 2026-11-11).
  - 캠프 휴식의 값: rest is free, heals 30% of max HP, paid heal removed (user, WP-43 question 1).
  - 휴식이 잠그는 범위: rest locks stat upgrades only (user, WP-43 question 2).
  - 쓰러진 유닛: post-battle HP floor 25% → 10% (user, WP-43 question 3).
  - 확정 9: no injury or death; unchanged.
- Terms: "ship data" is `assets/data/`; the "frozen fixture" is the copy under `tests/fixtures/assets/data/` that most tests load first. "Node reallocation" and "respec" are the same camp service (`RespecDungeonNodes`). "확정" marks a ruling the user confirmed; "확정 9" is line 9 of `decisions.md`.
- Source for the structure: Slay the Spire rest site, "Heal for 30% of max health" or upgrade a card (`docs/design/research-2026-11-11.md`).

## 2. Player experience
- **Decision added**: at every camp the player takes at most one of two options.
  - **Rest**: every party member heals 30% of their max HP, free. Stat upgrades are closed for this camp. Favoured when the party is hurt and the next dungeon is the risk.
  - **Reinforce**: stat upgrades stay purchasable with materials (same price curve as today). The first purchase closes Rest for this camp. Favoured when the party is healthy, or when materials have piled up and permanent stats are worth more than HP.
  - Neither: allowed (the player may descend without choosing). Rune offers, node reallocation and the free loadout (rune fitting, passive nodes, skill swap) are open in every case.
  - The choice cannot be undone within the camp. It resets at the next camp.
- **What the player sees before committing** (camp modal, new section "REST OR REINFORCE" above "CAMP SERVICES"):
  - The rule in one sentence.
  - The Rest button with the heal size and the party HP before → after: `Rest  /  free  /  +30% max HP each  /  party HP 112 -> 184 of 240`.
  - A line under it: the first stat purchase gives up Rest at this camp.
  - After the choice: one status line saying which option was taken and what is closed. A closed stat row is disabled and its tooltip gives the reason.
- **Playtest hypothesis**: expect the player to arrive at camps hurt and hesitate over the choice · observe mean party HP fraction at camp entry below 0.70 (was 0.84), Rest taken at 30–70% of camp visits (`spend/rest` count ÷ `visits` in `runlog-report.mjs`), and at least one "do I rest or reinforce" remark in the playtest notes · kill-if Rest is taken at more than 90% or fewer than 10% of visits (one option dominates), or the player reports that going down feels like a dead run (the 10% floor is too low).

## 3. Behaviors
| ID | Behavior | Proving test |
|---|---|---|
| F1 | WHEN the ship data is loaded THEN `balance.room_heal` SHALL be 0, `recovery.camp_auto_heal` 0, `recovery.ko_recover_fraction` 0.10, `recovery.camp_heal_percent` 30, and `recovery.camp_heal_cost` SHALL be absent. | `camp_tests::ShipRecoveryValues` |
| F2 | WHEN the player rests at camp IF no choice was made at this camp and a member is below max HP THEN each member SHALL gain `maxHp × camp_heal_percent / 100` HP (integer division, clamped to max; a member whose stored health is -1, the "full health" sentinel, is skipped) and materials SHALL NOT change. IF every member is at max HP or is the sentinel THEN the rest SHALL be refused. | `camp_tests::RestHealsPercentFree` |
| F3 | WHEN the player has rested at this camp THEN a second rest SHALL be refused with a reason and change nothing. | `camp_tests::RestOncePerCamp` |
| F4 | WHEN the player has rested at this camp THEN every stat upgrade SHALL be refused with a reason, and materials and stats SHALL NOT change. | `camp_tests::RestClosesStatUpgrades` |
| F5 | WHEN the player has bought a stat upgrade at this camp THEN rest SHALL be refused with a reason, and further stat upgrades SHALL stay purchasable. | `camp_tests::UpgradeClosesRest` |
| F6 | WHEN either choice was made THEN the camp rune offer and the node reallocation SHALL validate exactly as before the choice. | `camp_tests::ServicesStayOpenAfterChoice` |
| F7 | WHEN the run's depth differs from the depth the choice was made at, or the stored value is above 2, THEN the choice SHALL read as none. | `camp_tests::ChoiceResetsAtNextCamp` |
| F8 | WHEN a run is saved at camp after a choice and continued THEN the choice SHALL be restored and a rest SHALL still be refused. | `camp_tests::CampChoiceRoundTrip` |
| F9 | WHEN the rest preview is computed THEN its party HP after SHALL equal the sum of `DungeonEffectiveHealth` over the party after an actual rest. | `camp_tests::RestPreviewMatchesOutcome` |
| F10 | WHEN the player rests THEN the run log SHALL get one `CampSpent` event with id `rest`, amount 0, member -1. | `camp_tests::RestLogged` |
| F11 | WHEN the camp modal is rendered THEN it SHALL show the Rest button with the percent and the HP preview and the give-up line before a choice, and the matching status line after each choice; it SHALL NOT show a paid heal button. | `camp_tests::CampViewShowsChoice` |
| F12a | WHEN `CampCheapestTwoPurchasable` (the camp-entry "hoard" metric: materials held ÷ the two cheapest buyable services) counts services THEN a hurt member SHALL NOT add a service. | `tuning_tests::CampPurchasableCountsOnlyLiveServices` |
| F12b | WHEN the party has rested at this camp THEN `CampCheapestTwoPurchasable` SHALL count no stat upgrade. | `tuning_tests::CampPurchasableDropsUpgradesAfterRest` |
| F13a | WHEN the autopilot policy has `rest=<P>` THEN it SHALL parse P in 0..100; without the token P SHALL be 0. | `headless_tools_tests::RestTokenParses` |
| F13b | WHEN `AutopilotShouldRest` is asked THEN it SHALL be true only IF P > 0, the rest validates and the party HP percent is below P. | `headless_tools_tests::AutopilotRestsBelowThreshold` |
| F13c | WHEN the autopilot acts at camp IF `AutopilotShouldRest` is true THEN it SHALL rest and buy no stat upgrade; otherwise it SHALL buy stat upgrades as before. | `headless_tools_tests::AutopilotCampRestSkipsUpgrades` |

## 4. Numbers
| Name | Unit | Old | New | Data path (`assets/data/dungeon.json`) | Knob | Metric it moves |
|---|---|---|---|---|---|---|
| Room heal | HP per member | 4 | 0 | `balance.room_heal` | [0, 4] | mean HP fraction at camp entry |
| Camp arrival heal | HP per member | 22 | 0 | `recovery.camp_auto_heal` | [0, 22] | mean HP fraction at camp entry |
| Post-battle HP floor | fraction of max HP | 0.25 | 0.10 | `recovery.ko_recover_fraction` | [0.0, 0.25] | battle loss rate after a member went down |
| Rest heal | % of max HP per member | 25 (paid) | 30 | `recovery.camp_heal_percent` | [20, 50] | Rest rate per camp visit |
| Paid heal price | materials | 3 | key removed | `recovery.camp_heal_cost` | none | none |

`event_heal` (35, the shrine event's choice) and `free_heal_cap_fraction` (0.6) are unchanged. No combat modifier is added; `combat-math.md` is unaffected.
The frozen test fixture `tests/fixtures/assets/data/dungeon.json` keeps all its values; only its `recovery.camp_heal_cost` line is removed.

## 5. File map
Modify:
- `assets/data/dungeon.json` — the five values of §4.
- `tests/fixtures/assets/data/dungeon.json` — remove `recovery.camp_heal_cost` only.
- `src/gameplay/dungeon/FDungeonCamp.h`, `FDungeonCamp.cpp` — choice state, save keys, prices without heal, purchasable metric.
- `src/gameplay/dungeon/FDungeonRun.h`, `FDungeonRun.cpp` — recovery struct and loader, rest functions, upgrade gate.
- `src/gameplay/dungeon/FRunFlow.cpp` — camp entry message (line 147).
- `src/presentation/FDungeonCampView.cpp` — the new section, paid heal button removed.
- `src/dev/FRunAutopilot.h`, `FRunAutopilot.cpp` — `rest=<P>` token and the camp branch.
- `tests/features/camp_tests.cpp`, `tests/features/tuning_tests.cpp`, `tests/features/headless_tools_tests.cpp`, `tests/features/dungeon_sequence_tests.cpp`, `tests/DungeonTests.cpp` — §7.

Signatures. `FDungeonCamp.h`:
```cpp
/// The camp's one exclusive choice (WP-43). Stored as an integer in FCampProgress.
enum class ECampChoice : std::uint8_t { None = 0, Rest = 1, Reinforce = 2 };

struct FCampProgress {
    std::uint32_t upgradesTotal{0};
    std::uint32_t runeDrawsTotal{0};
    std::uint32_t campDepth{0};
    std::uint32_t runeDrawsThisCamp{0};
    std::uint32_t choiceDepth{0};  // the depth `choice` is valid for
    std::uint32_t choice{0};       // ECampChoice; valid only while choiceDepth == run.depth
};

/// None when runDepth < 0, progress.choiceDepth != runDepth, or progress.choice > 2; else the stored choice.
[[nodiscard]] ECampChoice CampChoiceThisCamp(const FCampProgress& progress, int runDepth);
/// Writes choice and choiceDepth = runDepth. Does not touch campDepth / runeDrawsThisCamp.
/// runDepth < 0 writes nothing (callers pass run.depth, which is never negative).
void SetCampChoice(FCampProgress& progress, int runDepth, ECampChoice choice);
```
- `FCampSession`: remove `paidHealCost`; rename `paidHealPercent` to `restHealPercent`; add `ECampChoice choice{ECampChoice::None};` (filled by `BuildCampSession` from `CampChoiceThisCamp(state, depth)`, its existing locals).
- `FCampPrices`: remove `heal`. `CampBasketTotal` sums the four remaining prices; `CampCheapestTwo` sorts the four. In `CampPricesOf` delete the `recovery` local (`FDungeonCamp.cpp:176`), which is then unused.
- `CampCheapestTwoPurchasable`: remove the heal entry; when `CampChoiceThisCamp(...) == ECampChoice::Rest`, push neither upgrade price.
- Camp save section: `PackCamp` writes `choice_depth` and `choice`; `UnpackCamp` reads both with `ReadCounter`. `kCampSectionVersion` stays 1 (`ReadCounter` keeps the default 0 for an absent key, so an older save reads as no choice).
- Comments that describe the paid heal or "five services" are rewritten to match, in English: `FDungeonCamp.h:75-77`, `FDungeonCamp.h:101` (the save-key list gains `choice_depth` / `choice`), `FDungeonCamp.cpp:224` and `:244-246`, `FDungeonRun.h:161-170` and `:212-213`; in the tests `camp_tests.cpp:239`, `:425`, `:534`, `:553`, `tuning_tests.cpp:400-401`, `:440`, and `dungeon_sequence_tests.cpp:1313-1314` (it says the content is the frozen fixture although line 1317 loads the ship `dungeon.json`).

`FDungeonRun.h` (the two paid-heal functions are replaced; no wrapper with the old names remains):
```cpp
struct FDungeonRecovery {
    double koRecoverFraction{};
    int campAutoHeal{};
    int campHealPercent{};              // camp rest heal = % of max HP, per member
    double eventChestHpCostFraction{};
    double freeHealCapFraction{};
};

/// Party totals for the Rest button. Members whose max HP is unknown (<= 0) are left out of all three sums.
struct FCampRestPreview {
    int partyHp{0};       // sum of DungeonEffectiveHealth
    int partyHpAfter{0};  // sum of min(effective + maxHp * campHealPercent / 100, maxHp)
    int partyHpMax{0};    // sum of DungeonMemberMaxHealth
};
[[nodiscard]] FCampRestPreview DungeonCampRestPreview(const entt::registry& registry);
[[nodiscard]] std::optional<std::string> ValidateDungeonCampRest(const entt::registry& registry);
[[nodiscard]] std::optional<std::string> RestAtDungeonCamp(entt::registry& registry);
```
- `InitializeDungeon`: stop reading `camp_heal_cost`; drop it from the validation condition. The other recovery checks are unchanged.
- `ValidateDungeonCampRest` checks, in this order, returning the quoted string:
  1. no run → `"Run state is unavailable."`
  2. phase is not Camp → `"The party can rest at camp only."`
  3. `registry.ctx().find<FCampProgress>()` is null → `"Camp progress is unavailable."`
  4. choice is Rest → `"The party already rested at this camp."`
  5. choice is Reinforce → `"Stats were reinforced at this camp; rest is closed until the next camp."`
  6. the existing `anyHealable` / `anyBelowMax` loop of `ValidateDungeonCampHeal`, with `"The rest would restore nothing."` and `"The party is already at full health."`
  There is no materials check.
- `RestAtDungeonCamp`: validate; apply the existing heal loop of `PayDungeonCampHeal` (unchanged rule); `SetCampChoice(progress, run.depth, ECampChoice::Rest)`; `AppendRunEvent(registry, ERunEventKind::CampSpent, "rest", 0, -1)`; `SaveDungeonRun`. Materials are not touched.
- `ValidateDungeonStatUpgrade`: directly after the phase check add: choice is Rest → `"The party rested at this camp; stat upgrades are closed until the next camp."`
- `UpgradeDungeonStat`: after `progress.upgradesTotal += 1` call `SetCampChoice(progress, run.depth, ECampChoice::Reinforce)`.

`FRunFlow.cpp:147` message: `"Dungeon cleared. Checkpoint reached. Rest or reinforce stats, fit runes, then descend."` The `RestoreParty(... campAutoHeal)` call and the `room_heal` call stay (they now restore 0 with the ship data; both remain knobs).

`FDungeonCampView.cpp`, `RenderDungeonCampView`:
- Replace the whole `ImGui::TextWrapped("Healed up to ...")` call (lines 223–226, including its sentence about paid services and the action limit) with: `if (recovery.campAutoHeal > 0) ImGui::TextWrapped("Healed up to %d HP on the way in.", recovery.campAutoHeal);`
- Insert before `ImGui::SeparatorText("CAMP SERVICES")`:
  - `ImGui::SeparatorText("REST OR REINFORCE");` The three cases below branch on `session.choice`.
  - choice None: `ImGui::TextWrapped("Take one at this camp: rest, or reinforce stats. Rune offers, node reallocation and the loadout stay open either way.");` then the Rest button, `ui::Button(label, {-1, 42})`, label `"Rest  /  free  /  +<P>% max HP each  /  party HP <partyHp> -> <partyHpAfter> of <partyHpMax>"`, disabled when `ValidateDungeonCampRest` returns a reason, the reason shown with `ImGui::TextDisabled` under it (same pattern as the other services). On click: error → `run.message = "Cannot rest: " + *error`, success → `run.message = "The party rests."`. Then `ImGui::TextDisabled("Reinforce: buy any stat upgrade below. The first purchase gives up Rest at this camp.");`
  - choice Rest: `ImGui::TextDisabled("Rested at this camp. Stat upgrades are closed until the next camp.");`
  - choice Reinforce: `ImGui::TextDisabled("Reinforcing at this camp. Rest is closed until the next camp.");`
- Delete the paid heal block (lines 255–269).
- `DrawCampUpgradeRow` needs no change: it already disables on the validator's reason and shows it as the tooltip.

`FRunAutopilot.h` / `.cpp` (namespace `game::dev`):
```cpp
struct FAutopilotPolicy { /* existing fields */ int campRestBelow{0}; };  // rest=<P>; 0 = never rest
/// True when policy.campRestBelow > 0, ValidateDungeonCampRest accepts, and
/// partyHp * 100 / partyHpMax (DungeonCampRestPreview, integer division; false when partyHpMax <= 0) < campRestBelow.
[[nodiscard]] bool AutopilotShouldRest(const entt::registry& registry, const FAutopilotPolicy& policy);
```
- `ParseAutopilotPolicy`: new top-level key `rest`, value parsed with `ParseCount(value, 100)`; a bad value → `BadValue("bad rest value", value)`; a second `rest` token → `"duplicate token 'rest'"`. Document it in the token comment block. `kDefaultAutopilotPolicy` is unchanged.
- Camp branch: after the consolidation pass and before the reinforcement loop, `if (AutopilotShouldRest(registry, policy_))` call `RestAtDungeonCamp`, log `LOG_INFO("[autopilot] camp rest materials={}", run->materials)` and skip the reinforcement loop (the `for (int bought = 0; bought < policy_.campUpgrades; ...)` block at lines 470–489); otherwise run that loop as today. The rest does not depend on `campUpgrades`: it also happens when `campUpgrades` is 0.

Do not touch: `src/gameplay/dungeon/FRunLog.cpp`, `FRunEventLog.h`, `.claude/scripts/**` (the log schema is unchanged: `rest` is one more `camp` id), `FEventEffects.*`, `FEventContent.*`, `assets/data/events.json`, `src/gameplay/combat/**`, `CMakeLists.txt`.

## 6. Tasks
`node .claude/scripts/build.mjs --preset windows-msvc-agent` configures if needed, builds and runs every ctest entry; it prints only errors and failing tests. A case named below "passes" when it is absent from that output. `dungeon_rules` is the ctest entry of `tests/DungeonTests.cpp`. `docs/wp/WP-43/progress.md` is created by the implementer; the gate allows it without a file-map entry. The gate script, not the implementer, sets `passes` in `features.json`.

1. **T1 Tests first (red)** — add the new cases of §7 to `camp_tests.cpp`, `tuning_tests.cpp`, `headless_tools_tests.cpp`, written against the §5 signatures. Add only these §5 additions, with stub bodies, so everything compiles: the enum `ECampChoice`, the fields `FCampProgress::choiceDepth` / `choice`, the struct `FCampRestPreview`, and the functions: `CampChoiceThisCamp` returns `None`, `SetCampChoice` is empty, `DungeonCampRestPreview` returns `{}`, `ValidateDungeonCampRest` / `RestAtDungeonCamp` forward to the old heal functions, `AutopilotShouldRest` returns false, `FAutopilotPolicy::campRestBelow` exists but `rest=` is not parsed. The removals and renames of §5 (`FCampSession`, `FCampPrices`, `FDungeonRecovery`, the heal functions) wait for T3; existing cases are untouched in this task. Done: build.mjs → the build succeeds; every new case fails except `ServicesStayOpenAfterChoice` (it passes against the stubs, which is expected); no existing case fails. `CampPurchasableCountsOnlyLiveServices` (F12a) is an existing case: it stays green until its T3 rewrite. Commit.
2. **T2 Choice state** — `FDungeonCamp.*`: `CampChoiceThisCamp`, `SetCampChoice`, the two save keys. Done: build.mjs → `ChoiceResetsAtNextCamp` passes.
3. **T3 Rest replaces the paid heal, in code and on screen** — one task, because removing the heal functions breaks the view and the label test until they change too.
   - `FDungeonRun.*`: recovery struct and loader, preview, validate, rest, upgrade gate; remove `ValidateDungeonCampHeal` / `PayDungeonCampHeal`.
   - `FDungeonCamp.*`: `FCampSession`, `FCampPrices`, `CampBasketTotal`, `CampCheapestTwo`, `CampCheapestTwoPurchasable`, comments.
   - `FDungeonCampView.cpp` and `FRunFlow.cpp:147` as in §5.
   - `tests/fixtures/assets/data/dungeon.json`: remove `camp_heal_cost`.
   - The "existing cases that change" of §7 in `camp_tests.cpp`, `tuning_tests.cpp`, `dungeon_sequence_tests.cpp`, `tests/DungeonTests.cpp`.

   Done: build.mjs → the only failing cases are `ShipRecoveryValues`, `RestTokenParses`, `AutopilotRestsBelowThreshold`, `AutopilotCampRestSkipsUpgrades`. The `dungeon_sequence_tests` stop condition of T5 applies here too (its scripted player changes in this task).
4. **T4 Autopilot** — `FRunAutopilot.*`. Done: build.mjs → the only failing case is `ShipRecoveryValues`.
5. **T5 Ship data** — `assets/data/dungeon.json` values of §4. Done: build.mjs → 0 failing tests. Two stop conditions, each reported as `BLOCKED:` to the design session instead of being worked around:
   - `dungeon_sequence_tests` reaches its `Defeat` branch (`CHECK(false)`, line 1408): report the seed and the depth. Whether three dungeons must stay survivable for that scripted player is a balance ruling.
   - a test outside the file map fails on the new ship values: report the test name and the assertion.
6. **T6 Evidence run** — PowerShell, timeout 600000 ms: `node .claude/scripts/baseline.mjs --label wp43-rest --seeds 1-12 --policy "reward=card0,event=first,chest=open,camp=offer+1,rest=60,continue" | Out-File -Encoding utf8 out/baseline-wp43-rest.txt`. Done: exit code 0 and the file is non-empty; copy its camp block (`visits`, `meanHpFractionBeforeHeal`, the `spend/...` lines) and its battle win/loss lines into `docs/wp/WP-43/progress.md`. The numbers are a record, not a pass condition.

## 7. Tests
New cases in `tests/features/camp_tests.cpp` (use the file's `FBoot`, `StartFresh`, `Member`, `RenderModalLogs` helpers; "effective HP" is `game::DungeonEffectiveHealth`; set `run.phase = game::EDungeonPhase::Camp` directly as `PaidHealIsOptional` does). Register each in the `RunTests("camp", {...})` list.
- **ShipRecoveryValues** (F1): parse `assets/data/dungeon.json` from disk with nlohmann (path `std::string(kShipAssetRoot) + "/data/dungeon.json"`); assert the four values of F1 and `!recovery.contains("camp_heal_cost")`.
- **RestHealsPercentFree** (F2): materials 50; all members at max → validate and rest are refused, materials 50; all sentinel (-1) → refused; member 1 at 1 HP → rest succeeds, materials still 50, member 1 HP == `min(1 + maxHp * campHealPercent / 100, maxHp)`, member 0 still -1.
- **RestOncePerCamp** (F3): all members at 1 HP; rest succeeds; record HP; second `RestAtDungeonCamp` returns exactly `"The party already rested at this camp."`; HP unchanged.
- **RestClosesStatUpgrades** (F4): materials 1000, members at 1 HP; `ValidateDungeonStatUpgrade(registry, 0, 0)` accepts before the rest; rest; `ValidateDungeonStatUpgrade(registry, 0, 0)` and `UpgradeDungeonStat(registry, 0, 0)` both return exactly `"The party rested at this camp; stat upgrades are closed until the next camp."`; materials 1000; `upgradesTotal` unchanged.
- **UpgradeClosesRest** (F5): materials 1000, members at 1 HP; `UpgradeDungeonStat(registry, 0, 0)` succeeds; `ValidateDungeonCampRest` returns exactly `"Stats were reinforced at this camp; rest is closed until the next camp."`; HP unchanged after a refused `RestAtDungeonCamp`; a second `UpgradeDungeonStat(registry, 0, 0)` succeeds.
- **ServicesStayOpenAfterChoice** (F6): two boots. Each: materials 1000, members at 1 HP, warrior `allocatedNodes = {"node.might"}`. Record `ValidateCampRuneOfferPurchase` and `ValidateDungeonRespec` results (both accept). Boot A rests, boot B buys an upgrade. Both validators still accept.
- **ChoiceResetsAtNextCamp** (F7): `SetCampChoice(progress, 3, ECampChoice::Rest)`; `CampChoiceThisCamp(progress, 3) == Rest`; `CampChoiceThisCamp(progress, 4) == None`; `CampChoiceThisCamp(progress, -1) == None`; `campDepth` and `runeDrawsThisCamp` unchanged by the set; then `progress.choice = 7` → `CampChoiceThisCamp(progress, 3) == None`; `SetCampChoice(progress, -1, ECampChoice::Rest)` leaves `choice` and `choiceDepth` as they were.
- **CampChoiceRoundTrip** (F8): same route as `CampSectionRoundTrip`, with depth 2, cleared 2, phase Camp, members at 1 HP: `RestAtDungeonCamp` succeeds (it saves), erase the `FDungeonRun` from the context, `ContinueDungeonRun` → `CampChoiceThisCamp(boot.progress(), boot.run().depth) == Rest`, `boot.progress().choiceDepth == 2u`, and `RestAtDungeonCamp` is refused.
- **RestPreviewMatchesOutcome** (F9): members at 1, 10, max, and -1; take `DungeonCampRestPreview`; assert `partyHp` equals the sum of `DungeonEffectiveHealth`; rest; the new sum of `DungeonEffectiveHealth` equals `partyHpAfter`; `partyHpAfter <= partyHpMax`.
- **RestLogged** (F10): as `CampSpentLogged` counts events: after one rest, exactly one `CampSpent` event, `id == "rest"`, `amount == 0`, `member == -1`.
- **CampViewShowsChoice** (F11): walk to camp phase, members at 1 HP; `RenderModalLogs(registry, {camp modal id})` (the id `CampAutoHealOnceOnEntry`'s phase maps to: `game::RunModalIdForPhase(game::EDungeonPhase::Camp)`). Assert the text contains `"REST OR REINFORCE"`, `"Rest  /  free  /  +"`, `"party HP "`, `"gives up Rest"` and does not contain `"Heal party"`. Rest, render again: contains `"Rested at this camp."`. In a second boot buy an upgrade, render: contains `"Reinforcing at this camp."`.

Existing cases that change (meaning kept, numbers follow the new rule):
- `camp_tests.cpp` `PricesAt`, `ReferenceCampWalk`: drop `prices.heal`, the `heal` flag of `kPath` and the heal spend line; drop the now unused `recovery` parameter from both and from their callers: `BudgetFromRules`, `CampBudgetForcesAllocation` and `ShrinePathTradesBuyingPower` (the call at line 445; delete its `recovery` local at line 440 if nothing else uses it). `BudgetFromRules` keeps its three CHECKs (with the fixture the basket still exceeds the held materials at all eight camps: 11>9, 19>15, 26>20, 30>22, 32>17, 32>15, 35>19, 35>20).
- `CampBudgetForcesAllocation`: replace the heal purchase with `RespecDungeonNodes`; the materials assertion becomes `before - session.respecCost - session.runeOfferCost`; the printed line names respec instead of heal. Every CHECK stays (camp 4 holds 22, respec 5 + offer 2 leaves 15, basket 30).
- `PaidHealIsOptional`: delete (replaced by `RestHealsPercentFree`); remove it from the registration list.
- `CampSpentLogged`: delete the heal block (three lines); the final count becomes 3.
- `MemberSentinelSkipsHeal` (the call at line 538) and `UnknownMemberIsNotClamped` (the call at line 557): call `RestAtDungeonCamp` / `ValidateDungeonCampRest` instead of the removed functions; assertions unchanged.
- `CampAutoHealOnceOnEntry`, `AttritionBudgetPerUnit`, `KoFractionFromData`: unchanged (they read the frozen fixture or compute from loaded data).
- `tuning_tests.cpp` (helpers `FCampBoot`, `Member`) `CampPurchasableCountsOnlyLiveServices` (F12a): keep `warrior.health = 1`, rename `withHeal` to `withHurt` and assert `withHurt == withRespec` (a hurt member adds no service); `capped == withRespec - 1`; `emptyPool == withRespec - 1`; update the doc comment. `CampPurchasableIgnoresAffordability`: `poor == 4`, comment `upgrade, next upgrade, respec, offer`. New **CampPurchasableDropsUpgradesAfterRest** (F12b): `FCampBoot`, camp phase, warrior at 1 HP, count `before`; `RestAtDungeonCamp` succeeds; count `after == before - 2`.
- `tests/DungeonTests.cpp:533-548`: rename `healLabel` to `restLabel` and build only the stable prefix, `"Rest  /  free  /  +" + std::to_string(session.restHealPercent) + "% max HP each"` (no HP preview, so the party state at capture time does not matter); the `CHECK(log.find(...))` at line 548 uses it. The `"Healed up to 61 HP"` assertion stays (the test sets `camp_auto_heal` to 61, so the line is shown).
- `dungeon_sequence_tests.cpp:1383-1401`: the scripted player rests when hurt, else reinforces. Replace line 1385 with `const auto preview = game::DungeonCampRestPreview(registry); const bool rested = preview.partyHp * 100 < preview.partyHpMax * 60 && !game::RestAtDungeonCamp(registry).has_value();` and wrap the existing `for (;;)` purchase loop in `if (!rested) { ... }`. Update the comment above it. This test loads the ship data (line 1317), so its survival assertion is first exercised with the new values in T5; see T5's stop condition.
- `headless_tools_tests.cpp`: in all three new cases never dereference a parse result directly. Follow the pattern at lines 973–975: `const auto policy = game::dev::ParseAutopilotPolicy(text); CHECK(policy.has_value()); if (!policy) return;` and only then use `*policy`. Before T4 `rest=` is an unknown token, and dereferencing an error-holding `std::expected` in the Debug build aborts the whole test executable instead of failing one case. **RestTokenParses** (F13a): `rest=60` → `campRestBelow == 60`; default policy → 0; `rest=101`, `rest=x`, `rest=` and a duplicate `rest` → no value. **AutopilotRestsBelowThreshold** (F13b): `FHeadlessRunHarness` + `SetupConstructedRun(harness, game::EDungeonPhase::Camp)` as `CampConsolidationMovesSplitPair` does; policy `rest=50`; members at max → `AutopilotShouldRest` false; members at 1 HP → true; policy `rest=0` with members at 1 HP → false; after `RestAtDungeonCamp` → false. **AutopilotCampRestSkipsUpgrades** (F13c): same harness, materials 1000, members at 1 HP. Pilot A is built from the checked policy `"camp=+2,rest=100"`, one `pilot.tick(registry, game::kRunModalCamp)`: `CampChoiceThisCamp == Rest`, `upgradesTotal` unchanged, the tick returns `EAutopilotStatus::Finished` (no `continue` token). A second harness with pilot B `"camp=+2"`: after one tick `CampChoiceThisCamp == Reinforce` and `upgradesTotal` grew by at least 1.

Timeouts are unchanged. `features.json` already exempts `camp_tests.cpp` and `tuning_tests.cpp` from the gate's CHECK-count rule (`allowCheckDrop`), because the paid-heal assertions are removed by design; the implementer does not edit `features.json`.

## 8. Decisions
- Resolved: Rest is free, 30%, locks stat upgrades only; post-battle floor 10% — the user's answers of 2026-11-11 (`decisions.md`).
- Resolved: no separate "Reinforce" button; the first stat purchase is the commitment — one click fewer, and existing callers of `UpgradeDungeonStat` keep working. The give-up line makes the commitment readable before the click.
- Resolved: the player may leave the camp without choosing — forcing a choice would make `LeaveCamp` refuse in every existing run-flow test, for no gain in the decision.
- Resolved: `room_heal`, `camp_auto_heal` and the post-battle floor stay as data keys at their new values instead of being deleted from code — the ruling leaves their final values to simulation, so they must remain knobs.
- Resolved: the post-battle floor keeps applying to every member, not only downed ones — a unit at 5% and a downed unit both leave at 10%, so going down is never better than surviving.
- Resolved: the rest log line carries amount 0 — `camp.amount` is materials spent in `runlog.mjs`; the count of `rest` lines gives the Rest rate.
- Resolved: the shrine event's heal stays — it is a choice inside an event (확정 "교환 지점: 이벤트 룸"), not free healing.
- Resolved: the Max health upgrade still raises current HP by its amount (5) — it is paid, and only available on the Reinforce side.
- Alternatives considered: Rest costs materials — rejected by the user (question 1); with ~19 materials held at camp the price would not be felt. Rest locks all material spending — rejected by the user (question 2). Camp action counter (`actions_per_camp`) — rejected: it limits the number of purchases, not the heal-versus-growth trade the ruling asks for.
- Defaults for anything unspecified: follow the nearest existing camp service (`RespecDungeonNodes` for the function shape, the respec button for the view).
- Out of scope: difficulty ladder, elite rules and rewards (WP-44), runes with costs and keystones (WP-45), re-tuning material income or prices, the simulator/optimizer, removing the unused `actions_per_camp` and `free_heal_cap_fraction` keys.

## 9. Pitfalls
- Tests load the frozen fixture first (`tests/TestSupport.h`: `TestAssetRoots`). Changing fixture values other than removing `camp_heal_cost` breaks unrelated tests; ship values are asserted only in `ShipRecoveryValues`.
- `choice` must not share `campDepth`: `PurchaseCampRuneOffer` rewrites `campDepth` on every purchase, which would carry an old choice into a new camp. Use `choiceDepth`.
- Player-facing strings are English only (the HUD font has no Hangul).
- ImGui math operators are disabled: build `ImVec2` component-wise.
- One build at a time; an MSB3073/MSB3075 DLL-copy failure is a transient lock: rerun once.
- Game runs from agents stay windowless with `GAME01P_SAVE_DIR` under `out/` (`baseline.mjs` already does this).
- `ValidateDungeonStatUpgrade` takes a const registry: read the progress with `registry.ctx().find<FCampProgress>()` (already done there).

## 10. Open questions
None.
