# WP-40: Rune rarity — tiers, three odds tables, before/after numbers on the offer card

Size: L · Player-facing: yes · Certainty: settled · Base commit: the commit that adds this file · Branch: wp/WP-40

## 1. Goal [S]
Every rune gets a rarity (common / uncommon / rare). Each card of a rune offer first rolls a rarity from a named odds table, then draws a legal rune of that rarity. The offer screen shows each card's rarity, the odds that produced the offer, and — for every party skill the rune fits — the skill's numbers before and after. Why: a visible tier turns "a rare came up" into an event of the run (`docs/design/expansion-plan.md` §4).

- Decisions it rests on: `docs/design/expansion-plan.md` §4.1–4.3 (hypothesis, tables, fallback rule, what is shown); `docs/design/decisions.md` lines 12 (free rune swap), 78 (strict tags), 80 (sockets + tags + no duplicates), 81 (runes drop during the run). Pity, duplicate refund, legality and the unlock gate keep their current rules.

## 2. Player experience [player-facing]
- **Decision added or changed**: the pick among three cards now has a tier dimension. A rare card changes how a skill behaves (shape, chain, on-kill, push) and usually needs a specific tag (Projectile, Fire, Cold, Melee; `Counter Guard` is the exception); a common card is a plain number gain, mostly without a tag requirement. Rare wins when the party has a skill that uses the behavior; common/uncommon wins when the rare fits only a skill the player does not use. Skip (+materials) stays the third option.
- **What the player sees before committing** (reward modal and camp offer, same cards):
  - Under `CHOOSE A RUNE`: `Odds (<table>): Common 60% / Uncommon 30% / Rare 10%`.
  - Cards are shown in ascending rarity, so a rare card is always the last card.
  - Each card: a rarity line (`Common rune` / `Uncommon rune` / `Rare rune`) in the rarity colour and a border in that colour around the card.
  - `Fits:` becomes one line per fitting skill: `Strike: Power 4 -> 6`, `Shove: Power 1 -> 2, AP 2 -> 1 (sockets full)`, `Bolt: no change`.
- **Playtest hypothesis**: expect the player to react out loud when a rare card appears and to re-fit skills around it · observe the run log `offer` records (`table`, `rarities`, `picked_rarity`) · kill-if a rare appears in the session and the player passes it without mentioning the tier, or a rare is dropped twice or more because no used skill fits it.

## 3. Behaviors [S]
| ID | Behavior | Proving test |
|---|---|---|
| F1 | WHEN the shipped `assets/data/runes.json` is read THEN every rune SHALL carry the rarity listed in §4.1 (8 common, 8 uncommon, 8 rare). | `content_a_tests::ShippedRunesHaveRarityTable` |
| F2 | WHEN a rune row has a `rarity` other than `common`/`uncommon`/`rare` THEN boot SHALL fail naming the rune and the value; WHEN `rarity` is absent THEN the rune SHALL be common. | `content_a_tests::UnknownRuneRarityFailsBoot` |
| F3 | WHEN `rewards.json` has `rarity_tables` THEN the loader SHALL store each table; IF a table is not an object, a weight is missing/negative, a table's weights sum to 0, or `normal` is missing THEN boot SHALL fail. WHEN `rarity_tables` is absent THEN boot SHALL succeed with tiering off. The shipped file SHALL hold the three tables of §4.2. | `reward_offer_tests::RarityTablesLoadAndValidate` |
| F4 | WHEN a card is drawn with a table whose only non-zero weight is rare IF at least `count` unowned legal rare runes exist THEN every card SHALL be rare. | `reward_offer_tests::TierTableDrivesCardRarity` |
| F5 | WHEN the rolled rarity has no unowned legal rune left THEN the card SHALL come from the next rarity in the order rare→uncommon→common, uncommon→common→rare, common→uncommon→rare. | `reward_offer_tests::EmptyTierFallsDownThenUp` |
| F6 | WHEN all legal runes share one rarity THEN the candidates SHALL be identical with tiering off and with any table, for the same (seed, depth, room, draw index). | `reward_offer_tests::UniformRarityMatchesUntieredDraw` |
| F7 | WHEN the same tiered draw is repeated THEN it SHALL return the same list, and a list SHALL never contain a rune twice. | `reward_offer_tests::TieredOfferIsDeterministicAndUnique` |
| F8 | WHEN an offer opens THEN it SHALL record the name of the table it resolved to: `normal` for a monster room, `boss` for a boss room (first and `extra` offer), empty with tiering off. | `reward_offer_tests::OfferRecordsItsRarityTable` |
| F9 | WHEN a run with an open offer is saved and loaded THEN the offer's table name SHALL be restored; IF the saved value is not a string THEN the offer SHALL be dropped (existing corrupt-offer rule); IF the key is absent THEN the name SHALL be empty. | `reward_offer_tests::SaveRestoresRarityTable` |
| F10 | WHEN the offer cards are laid out THEN the display order SHALL be a stable ascending sort by rarity over the stored candidates, and each card SHALL resolve its stored index. | `reward_offer_tests::DisplayOrderPutsRareLast` |
| F11 | WHEN the reward modal renders an open offer THEN it SHALL print one rarity line per card, and the odds line when the offer has a table. | `reward_offer_tests::CardShowsRarityAndOdds` |
| F12 | WHEN a preview is computed for a rune and a fitting skill THEN `before` SHALL be the skill resolved with the runes fitted in the party's first slot holding that skill and `after` the same plus the rune; IF that combination is tag-illegal or the slot's sockets are full THEN both SHALL be computed on the bare skill (no fitted runes); `socketsFull` and `alreadyFitted` SHALL always describe that slot. | `reward_offer_tests::FitPreviewUsesFittedRunes` |
| F13 | WHEN a preview is formatted THEN the line SHALL list every changed number as `Label B -> A` in the fixed order of §5, `no change` when nothing differs, and the suffixes `(sockets full)` / `already fitted`. | `reward_offer_tests::FitPreviewLineFormat` |
| F14 | WHEN the reward modal renders a card THEN it SHALL print `Fits:` followed by one preview line per fitting skill (or `Fits: no current party skill`). | `reward_offer_tests::CardShowsFitPreview` |
| F16 | WHEN a camp rune offer is purchased THEN the opened offer SHALL record `normal`. | `camp_tests::CampOfferRecordsNormalTable` |
| F15 | WHEN an offer is resolved THEN the run log `offer` record SHALL carry `table`, `rarities` (one per stored candidate, same order) and `picked_rarity` (empty on skip). | `run_log_tests::OfferRecordCarriesRarities` |

Pity is not a behavior of this WP: no code path of pity changes, and the existing `reward_offer_tests::PityForcesOffer` / `ChanceZeroStillPities` must stay green untouched.

## 4. Numbers
### 4.1 Rarity per rune — `assets/data/runes.json` → `runes[].rarity`
| Rarity | Runes (id without `rune.`) |
|---|---|
| common | fire, cold, lightning, power, swift, leech, inferno, glacial |
| uncommon | area, concentrated, range, duration, ember, frostbite, shock, precision |
| rare | multishot, pierce, zone, chain, frostpath, combust, guard, impact |

`drop_weight` values stay as they are; they now bias the draw inside one rarity only.

### 4.2 Odds tables — `assets/data/rewards.json` → `rarity_tables.<name>.{common,uncommon,rare}` (integer weights)
| Table | common | uncommon | rare | Used by (this WP) |
|---|---|---|---|---|
| `normal` | 60 | 30 | 10 | monster-room offer, camp purchase |
| `boosted` | 30 | 50 | 20 | nothing yet (WP-41: elite, events) |
| `boss` | 0 | 60 | 40 | boss-room offer |

Weights are relative, not percentages: any integer ≥ 0 loads and the range is not enforced (the tuning range is `knob: [0, 100]`); metric: share of offers with at least one rare card, and the fallback rate below.

### 4.3 Expected fallback (default party, 22 legal runes = 7 common / 8 uncommon / 7 rare; `rune.inferno`, `rune.chain` unlock-gated)
Monte Carlo, `normal` table, one random pick per offer, 20,000 runs: share of card slots whose rolled rarity was empty — offers 1–5: 0%; 6: 0.6%; 7: 2.6%; 8: 6.5%; 9: 12%; 10: 19%; 11: 26%; 12: 33%. About 27% of `normal` offers contain a rare card. Fallback only appears once the player owns most commons; it is accepted (the card is then a higher tier than rolled).

### 4.4 Rarity colours (card border and rarity line) — constants in `FDungeonRewardView.cpp`
common `IM_COL32(140, 128, 108, 255)` border 1 px · uncommon `IM_COL32(80, 140, 255, 255)` border 2 px · rare `IM_COL32(240, 206, 128, 255)` border 3 px. Border rounding 4 px, padding 4 px outside the card's item rect.

No combat modifier is added; `docs/design/combat-math.md` is unchanged.

## 5. File map [S]
**Modify `src/gameplay/data/FRuneContent.h`**
```cpp
enum class ERuneRarity : std::uint8_t { Common = 0, Uncommon = 1, Rare = 2 };
[[nodiscard]] std::optional<ERuneRarity> ParseRuneRarity(std::string_view name); // "common"|"uncommon"|"rare"
[[nodiscard]] std::string_view RuneRarityName(ERuneRarity rarity);              // "common"|"uncommon"|"rare"
// in FRuneContent, after dropWeight:
    std::string rarityName{"common"};            // as authored; validated at boot
    ERuneRarity rarity{ERuneRarity::Common};     // parsed; an unknown name reads as Common until boot rejects it
```
Append `{"rarity", EContentFieldType::Text, false}` as the last entry of `kRuneFields`, add `kRuneFieldRarity = 18`, change the `static_assert` to 19 and move the "last field" comment. Add `#include <cstdint>`, `<optional>`, `<string_view>`.

**Modify `src/gameplay/data/FRuneContent.cpp`** — `decodeRune`: `rarityName = row.text(kRuneFieldRarity)`; empty → `"common"`; `rarity = ParseRuneRarity(rarityName).value_or(ERuneRarity::Common)`. Define the two helpers.

**Modify `src/gameplay/data/FContentRegistry.cpp`** — in the rune loop that rejects an unknown `shape_override` (line ~123): `if (!ParseRuneRarity(rune.rarityName))` → `LOG_ERROR("content: rune '{}' has unknown rarity '{}'.", rune.id, rune.rarityName)` and fail the same way that block fails.

**Modify `assets/data/runes.json`** — add `"rarity": "<value>"` as the last key of each of the 24 rows per §4.1. `tests/fixtures/assets/data/runes.json` is not touched (all fixture runes read as common).

**Modify `assets/data/rewards.json`** — add top-level `"rarity_tables": {"normal": {"common": 60, "uncommon": 30, "rare": 10}, "boosted": {"common": 30, "uncommon": 50, "rare": 20}, "boss": {"common": 0, "uncommon": 60, "rare": 40}}`. The fixture `rewards.json` is not touched (tiering off in fixture boots).

**Modify `src/gameplay/dungeon/FRewards.h`** (add `#include "gameplay/combat/FSkillResolver.h"`)
```cpp
struct FRarityWeights {
    std::string name;
    int common{0};
    int uncommon{0};
    int rare{0};
};
// in FRewardTables:
    std::vector<FRarityWeights> rarityTables;  // empty = tiering off (every card draws from the whole pool)

inline constexpr std::string_view kRarityTableNormal = "normal";
inline constexpr std::string_view kRarityTableBoosted = "boosted";
inline constexpr std::string_view kRarityTableBoss = "boss";

// in FRuneOffer:
    std::string rarityTable;  // the table name this offer was rolled with; empty when tiering is off

/// Pure lookup, never logs: the named table, else the table named `normal`, else nullptr (tiering off).
[[nodiscard]] const FRarityWeights* FindRarityTable(const FRewardTables& tables, std::string_view name);

[[nodiscard]] std::vector<std::string> RollRuneCandidates(
    entt::registry& registry, std::string_view rarityTable = kRarityTableNormal);
[[nodiscard]] std::vector<std::string> PeekRuneCandidates(
    entt::registry& registry, std::string_view rarityTable = kRarityTableNormal);

struct FRuneFitPreview {
    std::string skillId;
    std::string skillName;      // display name, id when the name is empty
    FResolvedSkill before;
    FResolvedSkill after;
    bool alreadyFitted{false};  // the slot already holds this rune: before and after are the same resolve
    bool socketsFull{false};    // the slot's fitted runes fill every socket
};
/// One entry per party skill id the rune fits, in PartySkills order (ids are unique there; two skills that
/// share a display name give two entries — intended).
[[nodiscard]] std::vector<FRuneFitPreview> RuneFitPreviews(entt::registry& registry, std::string_view runeId);
```

**Modify `src/gameplay/dungeon/FRewards.cpp`**
- `LoadRewards`: read optional `rarity_tables` (F3). `rarity_tables` must be an object; iterate it; each value must be an object whose `common`, `uncommon`, `rare` are present, `is_number_integer()`, ≥ 0, with sum > 0 (anything else fails boot); `normal` must exist when the key is present. Error text: `rewards.json rarity_tables.<name> ...`. Append `rarity tables {}` (count) to the ready log line.
- `CandidatesFor(registry, drawIndex, rarityTable)`: add `constexpr std::uint32_t kTierSalt = 0x6b43a9b5u;` and a second `FRewardRng tierRng(StreamSeed(run->seed, run->depth, run->room, drawIndex, kTierSalt))`. `const FRarityWeights* weights = FindRarityTable(rewards->tables, rarityTable)`. In the unowned loop, per slot:
  1. `weights == nullptr` → `sub = pool` (today's behavior).
  2. else `roll = tierRng.Next() % (common + uncommon + rare)`; rolled = Common if `roll < common`, Uncommon if `roll < common + uncommon`, else Rare. `tierRng` is consumed exactly once per slot. Walk the fallback order of F5 and take the first rarity for which `sub` = the runes of `pool` with that rarity (pool order kept) is non-empty.
  3. `picked = PickWeighted(rng, sub)`; push `sub[picked]->id`; `std::erase(pool, sub[picked])` (erase by pointer identity).
  The pick stream `rng` (`kDrawSalt`) is used exactly as today, so a single-rarity pool yields today's list (F6). The owned-duplicate card rule is unchanged and ignores rarity.
- `RollRuneCandidates` / `PeekRuneCandidates`: pass the table through.
- `RollRoomOffer`: table = `bossRoom ? kRarityTableBoss : kRarityTableNormal`; pass it to `RollRuneCandidates`; on open set `offer.rarityTable` to `FindRarityTable(tables, name)->name` (so a missing `boss` table records `normal` and the odds line is never mislabelled), or to empty when it returns nullptr.
- `ResolveRuneOffer`: the `extra` boss offer rolls with `kRarityTableBoss` and sets `offer.rarityTable` the same way; the cleared offer resets `rarityTable` (assign `FRuneOffer{}` semantics: `active=false`, `candidates.clear()`, `rarityTable.clear()`), before the extra offer is opened.
- `RuneFitPreviews`: for each skill from `PartySkills` that passes the same two checks as `RuneFittingSkillNames`: find the first `run.party[m].skills[s]` with that `skillId` (party order, then slot order); `fitted` = its `runeIds` resolved through `content.runes.find` (unknown ids skipped); none found → `fitted` empty. `alreadyFitted` = the rune id is in that slot. `socketsFull` = `fitted.size() >= skill.runeSockets` (in every branch below). If `alreadyFitted`: `before = after = ResolveSkill(skill, fitted)`. Else if `!socketsFull` and `CheckRuneTagRequirements(skill, fitted + rune)` passes: `before = ResolveSkill(skill, fitted)`, `after = ResolveSkill(skill, fitted + rune)` (rune appended last). Else: `before = ResolveSkill(skill, {})`, `after = ResolveSkill(skill, {rune})`.

**Modify `src/gameplay/dungeon/FRewardSection.cpp`** — pack `offer.rarity_table` (string) next to `candidates`; unpack: absent → empty; present and not a string → `corrupt = true`. `kRewardSectionVersion` is unchanged (an absent key is the default, as for the other keys).

**Modify `src/gameplay/dungeon/FDungeonCamp.cpp`** — `PurchaseCampRuneOffer`: after `rewards.offer.candidates = std::move(candidates);` set `rewards.offer.rarityTable` to `FindRarityTable(rewards.tables, kRarityTableNormal)->name`, or empty when it returns nullptr. The `PeekRuneCandidates`/`RollRuneCandidates` calls keep the default argument.

**Modify `src/presentation/FDungeonViews.h`** (add `#include <vector>`; forward-declare `struct FRuneFitPreview;` in namespace `game`; `<cstddef>` and `struct FRewards;` are already there)
```cpp
/// Indices into `candidates`, stable-sorted ascending by rarity (unknown rune or null content = common).
[[nodiscard]] std::vector<std::size_t> RuneOfferDisplayOrder(
    const std::vector<std::string>& candidates, const gameplay::FContentRegistry* content);
/// "Odds (normal): Common 60% / Uncommon 30% / Rare 10%"; empty when the offer has no table or tiering is off.
[[nodiscard]] std::string RuneOfferOddsLine(const FRewards& rewards);
/// "Strike: Power 4 -> 6, AP 2 -> 1 (sockets full)" — see the format rules in the spec.
[[nodiscard]] std::string RuneFitPreviewLine(
    const FRuneFitPreview& preview, const gameplay::FRuneContent& rune, const gameplay::FContentRegistry* content);
```

**Modify `src/presentation/FDungeonRewardView.cpp`**
- `RuneOfferOddsLine`: table = `FindRarityTable(rewards.tables, rewards.offer.rarityTable)` when `offer.rarityTable` is non-empty; return `""` when the name is empty or the lookup is nullptr. Percent = `std::lround(100.0 * weight / sum)`. Text exactly `Odds (<name>): Common <c>% / Uncommon <u>% / Rare <r>%` where `<name>` is the found table's `name`.
- `RuneFitPreviewLine`: `<skillName>: ` then, when `alreadyFitted`, `already fitted`; otherwise the changes joined with `, ` in this order, each only when before ≠ after: `Power B -> A`, `AP B -> A`, `Range B -> A`, `Area B -> A`, `Shape b -> a`, `Life on hit B -> A`; then per status of `after.statuses` (in order): absent from `before.statuses` (by `statusId`) → `+<StatusName>`; present with a different `durationDelta` → `<StatusName> duration <Signed(diff)>`; with a different `magnitudeDelta` → `<StatusName> magnitude <Signed(diff)>` (`diff` = after − before; both differ → two entries, duration first; a status of `before` is never missing from `after`, so that case is not handled); then `New effect` when `rune.hooks` is non-empty (it counts as an entry). No entry at all → `no change`. Append ` (sockets full)` when `socketsFull && !alreadyFitted`. `StatusName` and `Signed` are the existing helpers of this file. ASCII `->` only.
- `DrawRuneOfferCard`: order is `PushID` → `BeginGroup` → body (the pick result stays in the existing `resolved` bool) → `EndGroup` → border → `Dummy` → `PopID` → `return resolved`. Inside the group, after `SeparatorText(name)` print the rarity line with `ImGui::TextColored` (`Common rune` / `Uncommon rune` / `Rare rune`, colour §4.4, unknown rune → common). Replace the single `Fits:` line: no fitting skill → `ImGui::TextWrapped("Fits: no current party skill")`; else `ImGui::TextWrapped("Fits:")` and one `ImGui::TextWrapped("%s", RuneFitPreviewLine(...).c_str())` per `RuneFitPreviews` entry. After `EndGroup()` draw the border with `ImGui::GetWindowDrawList()->AddRect(min, max, colour, 4.f, 0, thickness)` where `min`/`max` are `GetItemRectMin()`/`GetItemRectMax()` moved 4 px outward, built component-wise, then `ImGui::Dummy({0, 6})`. The `Pick` button, duplicate line and `ui.pickedRuneId` logic are unchanged and stay inside the group.
- `DrawRuneOffers`: after `SeparatorText("CHOOSE A RUNE")` print the odds line with `ImGui::TextDisabled` when non-empty. Iterate `RuneOfferDisplayOrder(candidates, content)` and call `DrawRuneOfferCard(..., storedIndex, candidates[storedIndex])`, so `PushID` and `ResolveRuneOffer` receive the stored index.

**Modify `src/gameplay/dungeon/FRunLog.cpp`** — `case ERunEventKind::RewardPicked`: read `registry.ctx().find<FRewards>()` (the offer is still open while the event is delivered); add `"table"` (`offer.rarityTable`, `""` without the module), `"rarities"` (array of `RuneRarityName` per `offer.candidates`, unknown rune → `"common"`) and `"picked_rarity"` (`RuneRarityName` of `event.id`, `""` on skip or unknown). The `kRules` required-key list for `offer` is unchanged (the new keys are optional for old logs). Add the includes it needs (`gameplay/dungeon/FRewards.h`, `gameplay/data/FContentRegistry.h`) if absent.

**Namespaces**: `ERuneRarity`, `ParseRuneRarity`, `RuneRarityName` live in `game::gameplay` (callers in `game` write `gameplay::`); `FRarityWeights`, `FRuneFitPreview`, `FindRarityTable`, `RuneFitPreviews` live in `game`.

**Modify tests** `tests/features/reward_offer_tests.cpp`, `tests/features/content_a_tests.cpp`, `tests/features/run_log_tests.cpp`, `tests/features/camp_tests.cpp` — add the cases of §7 and register them in each file's case table. No existing case in these files is edited.

**Modify `tests/features/reward_equip_tests.cpp`** — `MeasureFitsWrap` (the `fitsLine` lambda and the probe window, l.416–434) and `FitsLineWraps` (l.964–971) pin the old single `Fits: A, B, C` line; rewrite them as §7 "FitsLineWraps" says. The CHECK count of the file must not go down.

**Create** `docs/wp/WP-40/progress.md` — implementer notes.

Do not touch: `tests/fixtures/**`, `src/gameplay/dungeon/FEventEffects.cpp` (the event rune grant keeps the default `normal` draw and the first stored candidate), `src/gameplay/combat/**`, `src/dev/**` (autopilot picks by stored index, unchanged), `FCompendiumView`, `FLoadoutPanel`, `FDungeonCampView.cpp`, `docs/design/**`.

## 6. Tasks [S]
Every task ends with `node .claude/scripts/build.mjs --preset windows-msvc-agent` → exit 0, no failing test.
1. **T1 Rune rarity content** — `FRuneContent.h/.cpp`, `FContentRegistry.cpp`, `assets/data/runes.json`; tests F1, F2 first (red), then green.
2. **T2 Odds tables** — `FRewards.h` (`FRarityWeights`, `rarityTables`, constants, `FindRarityTable`), `LoadRewards`, `assets/data/rewards.json`; test F3.
3. **T3 Tiered draw** — `CandidatesFor`, `Roll/PeekRuneCandidates`, `FRuneOffer::rarityTable`, `RollRoomOffer`, `ResolveRuneOffer`, `FDungeonCamp.cpp`, `FRewardSection.cpp`; tests F4–F9. Every pre-existing test case stays green unedited. Fixture boots are untiered by construction. A case that boots the shipped root (`ShipAssetRoots()`, `{scratch, "assets"}`) now draws tiered; those cases assert properties of the pool, not card ids, and are expected to stay green. If one fails, do not edit it: report `BLOCKED:` with the case name and the failing assertion. The offer-economy arithmetic in `content_a_tests` reads the JSON and stays rarity-blind (accepted).
4. **T4 Fit preview** — `FRuneFitPreview`, `RuneFitPreviews`, `RuneFitPreviewLine`; tests F12, F13.
5. **T5 Offer screen** — `RuneOfferDisplayOrder`, `RuneOfferOddsLine`, `DrawRuneOfferCard`, `DrawRuneOffers`; tests F10, F11, F14; the `reward_equip_tests::FitsLineWraps` rewrite. Every other `reward_equip_tests` case stays green unedited.
6. **T6 Run log** — `FRunLog.cpp`; test F15.

## 7. Tests
Every case that needs N legal runes of some rarity states it first as `CHECK(LegalRuneIds(boot).size() >= N)` (the fixture party has well over 6). New `reward_offer_tests` cases use that file's `FBoot` harness (`StartFresh`, `WinRoom`, `LegalRuneIds`, `OwnLegalRunes`, `RenderModalLogs`). The fixture has no rarities and no tables, so a test sets them in place: `boot.registry.ctx().get<game::gameplay::FContentRegistry>().runes.rows[i].rarity` (and `rarityName`) and `boot.rewards().tables.rarityTables`. Helper to add: `SetRarity(boot, runeId, rarity)` and `SetTables(boot, {{"normal",c,u,r}, ...})`.
- **F1 ShippedRunesHaveRarityTable** (`content_a_tests`): `ReadJsonFile("assets/data/runes.json")`; a literal id→rarity map of §4.1; assert all 24 ids are present with that value and the counts are 8/8/8.
- **F2 UnknownRuneRarityFailsBoot** (`content_a_tests`): same temp-document injection as `UnknownRequireTagFailsBoot`: `rarity: "epic"` on one rune → boot false; the unmodified shipped file → boot true; the frozen fixture (no `rarity` key) → boot true and a fixture rune reads `ERuneRarity::Common`.
- **F3 RarityTablesLoadAndValidate**: temp root holding the shipped file (`assets.Write("rewards.json", ReadJsonFile("assets/data/rewards.json"))`; `Copy` copies fixture files only), booted through the `FBoot` constructor that takes roots, with the temp root in front of `TestAssetRoots()` → after boot `tables.rarityTables` holds `normal` 60/30/10, `boosted` 30/50/20, `boss` 0/60/40. Six broken copies (`rarity_tables` is an array; a table is a number; weight −1; weight 1.5; all three weights 0; `normal` removed) → `BootsGameplay` false each. A copy without `rarity_tables` → boot true and `rarityTables.empty()`.
- **F4 TierTableDrivesCardRarity**: mark the first `tables.count + 1` legal runes rare, the rest common; `SetTables` `normal` = 0/0/100; `PeekRuneCandidates` → `tables.count` cards, all rare. Then `normal` = 100/0/0 → none rare.
- **F5 EmptyTierFallsDownThenUp**: (a) 1 rare, 1 uncommon, rest common, table 0/0/100 → the three cards are exactly: the rare, the uncommon, one common (order of the stored list: rare first). (b) no common at all (all legal runes uncommon except 2 rare), table 100/0/0 → 3 uncommon cards. (c) only rare and common runes, table 0/100/0 → all cards common while 3 commons exist (down before up).
- **F6 UniformRarityMatchesUntieredDraw**: all legal runes common. For draw indices 0..4 (advance with `RollRuneCandidates`): list with `rarityTables` empty == list with `normal` 60/30/10 == list with `normal` 0/0/100. Compare by running three boots with the same seed.
- **F7 TieredOfferIsDeterministicAndUnique**: mixed rarities (cycle common/uncommon/rare over legal runes), shipped tables; two boots with the same seed return equal lists for 5 consecutive draws; every list has no repeated id and only legal ids.
- **F8 OfferRecordsItsRarityTable**: with tables set — `WinRoom` (monster room) → `offer.rarityTable == "normal"`; `game::RollRoomOffer(boot.registry, true)` on a fresh boot → `"boss"`; with `boss_offer = "extra"`, after resolving the first offer the second is `"boss"` too; with tables holding only `normal`, a boss-room offer records `"normal"`. With `rarityTables` empty → `""`.
- **F16 CampOfferRecordsNormalTable** (`camp_tests`): the setup of the successful purchase at `camp_tests.cpp:429`, with `rarityTables` set in place to the three tables of §4.2 before the purchase → `rewards.offer.rarityTable == "normal"`.
- **F9 SaveRestoresRarityTable**: as `SaveRestoresOffer`; after reload `offer.rarityTable == "normal"` and candidates equal. Edit the saved JSON so `offer.rarity_table` is `7` → offer inactive after load, pity and draw cursor kept. Remove the key → offer active, name empty.
- **F10 DisplayOrderPutsRareLast**: candidates with rarities [rare, common, uncommon, common] → order `[1, 3, 2, 0]`; null content → identity order. Then render the reward modal with a stored list whose first candidate is rare and assert in the captured log, searching only the text after `CHOOSE A RUNE`, that the rare rune's display name first appears after the first appearance of both other names.
- **F11 CardShowsRarityAndOdds**: tables set, offer open with one rune of each rarity (set rarities on the three stored candidates) → the captured reward modal contains `Odds (normal): Common 60% / Uncommon 30% / Rare 10%`, `Common rune`, `Uncommon rune`, `Rare rune`. With `rarityTables` empty and `offer.rarityTable` empty → no `Odds (` text, and `Common rune` still printed.
- **F12 FitPreviewUsesFittedRunes**: fixture ids: S = `skill.strike` (Melee/Attack, 3 sockets), R = `rune.power`, Q = `rune.leech`. Setup after `StartFresh`: if `boot.run().party` is empty push one member; replace `party[0].skills` with one slot whose `skillId` is S and whose `runeIds` the step sets (clear the other members' `skills`), so the party's skills are exactly S. (a) `runeIds` empty → `before == ResolveSkill(S, {})`, `after == ResolveSkill(S, {R})` (compare `power`, `apCost`, `range`). (b) `runeIds = {Q}` → `before.runeIds == {Q}`, `after.runeIds == {Q, R}`. (c) `runeIds = {rune.leech, rune.swift, rune.range}` → `socketsFull`, `before.runeIds.empty()`, `after.runeIds == {R}` (a full slot never shows a loadout the player cannot equip). (d) `runeIds = {R}` → `alreadyFitted` and `before.power == after.power`. (e) `rune.multishot` (requires Projectile) → empty vector. (f) tag-illegal fallback: set `excludesTags = {"Fire"}` on the content row of `rune.power` in place and `runeIds = {rune.fire}` → `before.runeIds.empty()`, `after.runeIds == {rune.power}`, `socketsFull == false`; with `runeIds = {rune.fire, rune.swift, rune.range}` → same numbers and `socketsFull == true`. `FResolvedSkill` has no `operator==`: compare fields.
- **F13 FitPreviewLineFormat**: hand-built `FRuneFitPreview` values (no boot needed beyond content for status names): power 4→6 and AP 2→1 → `Strike: Power 4 -> 6, AP 2 -> 1`; identical before/after → `Strike: no change`; `socketsFull` → suffix ` (sockets full)`; `alreadyFitted` → `Strike: already fitted`; a status only in `after` → the entry is exactly `+<display name>`; a status in both with `durationDelta` 0→1 and `magnitudeDelta` 0→2 → `<name> duration +1, <name> magnitude +2`; shape single→spread → `Shape single -> spread`; a rune with one hook and identical numbers → `Strike: New effect` (no `no change`).
- **F14 CardShowsFitPreview**: reward modal capture contains `Fits:` and, for the first stored candidate, the exact `RuneFitPreviewLine` text of each `RuneFitPreviews` entry. The existing `DuplicateCardShowsRefund` (which checks `Fits:`) stays green unedited.
- **F15 OfferRecordCarriesRarities** (`run_log_tests`): with the harness that file uses for its `offer` record case, set rarities on the open offer's candidates and `offer.rarityTable = "normal"`, resolve with index 0 → the logged `offer` record has `table == "normal"`, `rarities` equal to the candidates' rarity names in stored order, `picked_rarity` equal to the picked rune's; a skip → `picked_rarity == ""`.

- **FitsLineWraps** (`reward_equip_tests`, existing case, rewritten because the Fits item it measures is replaced): its meaning stays "the whole Fits content reaches the frame and the modal grows by exactly its height". In `MeasureFitsWrap`, replace the `fitsLine` lambda by a `drawFits(runeId)` lambda that issues the same items as the view — `TextWrapped("Fits: no current party skill")` when `RuneFitPreviews` is empty, else `TextWrapped("Fits:")` and one `TextWrapped` per `RuneFitPreviewLine` — between `ImGui::BeginGroup()` and `EndGroup()`, and read `GetItemRectSize().y` of the group into `longFitsItemHeight` / `shortFitsItemHeight`. In `FitsLineWraps`, keep `CHECK(fitting.size() >= 8u)`; replace the `expected` string and its CHECK (l.966–971) by one CHECK that every `RuneFitPreviewLine` of `rune.power` is found in `probe.longLog`. The three remaining CHECKs (`Shape: spread`, `fitsDelta > lineHeight`, the ±1 px equality) are unchanged.

Existing assertions keep their meaning; no timeout changes; no check is dropped.

## 8. Decisions
- Resolved (taste, reported to the user 2026-11-11 with this recommendation; data-only, reversible in `runes.json`): `Added Fire/Cold/Lightning Damage` are **common**, not uncommon — the plan's principle put status-applying runes at uncommon, which leaves 5 commons (4 ungated) for a 60% slot and makes fallback start at the second offer. They are the +1 power staples that open element tags (the Path of Exile "Added X Damage" supports are the plainest supports); status *amplifiers* (ember, frostbite, shock, duration) stay uncommon. Result: 8/8/8.
- Resolved: "rare is revealed last" is a display order (ascending rarity), not a timed animation — deterministic, testable through the ImGui log capture, and the stored candidate order (save, autopilot, run log indices) is untouched.
- Resolved: the rarity roll uses its own RNG stream (`kTierSalt`) — so the pick stream is unchanged and every existing fixture-based draw test keeps its result.
- Resolved: fixtures stay frozen; a missing `rarity` is common and a missing `rarity_tables` turns tiering off — the fixture has neither, and tests set both in place.
- Resolved: with full sockets the preview uses the bare skill and says `(sockets full)` — showing fitted + rune would promise a loadout that cannot be equipped, and which rune the player would swap out is unknown.
- Resolved: the preview compares the skill as fitted now with the same plus the rune — that is the number the player has on screen in the loadout; when the fitted runes make the rune illegal the preview falls back to the bare skill, the same basis offer legality uses.
- Resolved: the preview shows resolved skill-sheet numbers (`Power`, not final damage) — damage needs an attacker and a target; `ResolveSkill` already applies the clamps, so a capped gain shows as capped.
- Alternatives considered [L]: sort the stored candidates by rarity — rejected: it would make the event rune grant (`candidates.front()`) favour commons and change saved/autopilot indices. A `rune_rarity_tests` file of its own — rejected: it would duplicate the 160-line `FBoot` harness. Rarity inside `drop_weight` only — rejected in the plan (invisible).
- Defaults for anything unspecified: follow the nearest existing code in the same file (naming, logging, error handling); player-facing text is English ASCII.
- Out of scope: timed card reveal; rarity in the compendium, loadout panel or camp inventory; using `boosted` anywhere (WP-41); passing a table to the event rune effect (WP-41); rarity affecting pity or refunds; simulation baselines; the report-only ignition sim in `content_a_tests` (it mirrors the untiered draw until the sim/optimizer pass).

## 9. Pitfalls
- `kRuneFields`, the `kRuneField*` indices, `decodeRune` and the `static_assert` must change together (the header's invariant).
- ImGui math operators are disabled (C2676): build the border's `ImVec2` corners component-wise.
- The HUD font has no Hangul and `->` must stay ASCII (no arrow glyph).
- `RenderModalLogs` captures frame 3 only; nothing on the card may depend on elapsed time.
- `AppendRunEvent(RewardPicked)` runs before `ResolveRuneOffer` clears the offer; the run-log handler relies on that order — do not move the clear above the append.
- One build/ctest at a time; an MSB3073/MSB3075 DLL-copy failure is a transient lock: rerun once.
- `features.json` `allowCheckDrop` is a list.

## 10. Open questions
None.
