# WP-45: Rune skeleton — runewords with real effects, catalyst label, power runes retired

Size: L · Player-facing: yes · Certainty: settled · Base commit: the commit that adds this spec · Branch: wp/WP-45

## 1. Goal
Most runes today only add numbers, so equipping them never changes what the player does on a turn. This WP builds the skeleton of the new rune design (`docs/design/build-foundation.md`): the four existing rune pairs become **runewords** — a named effect that switches on when one skill holds both runes and changes which enemy the player wants to hit. The seven runes that are runeword ingredients are labelled **Catalyst**, and the three runes that only add power leave the offers.

- Decisions it rests on (`docs/design/decisions.md`, section "난이도·빌드 다양성"):
  - 룬 설계 개편 (WP-45 방향): runes must change playstyle; completing a runeword makes the skill different (user, 2026-11-11).
  - 수치 룬의 처리: `rune.power`, `rune.inferno`, `rune.glacial` leave the offers; status runes stay as catalysts.
  - 룬워드 조합법 공개: a recipe is visible once the player owns one of its runes.
  - 확정 91 (runes change behaviour, numbers belong to passive nodes and stat upgrades), 확정 12 (re-socketing is free).
- Principles applied (`build-foundation.md`): R1 (action runes and catalysts), R3 (visible before equipping), R4 (a runeword changes how the skill is used), R5 (ingredients are common or uncommon), R6 (one runeword per skill, order-free).
- Not in this WP: costs on action runes and on `rune.swift` (rulings 256 and "행동 횟수"). Review showed that a power cost cannot price them: damage is attack + power − defense, power is 0–5, and a skill whose power falls to 0 stops hitting at all. They need a "less damage" percentage in the damage pipeline. That is balance work and waits for the balance phase (user, 2026-11-11: systems first, balancing later).
- Terms: "engine" is the existing code and data name of a rune pair (`engine_ignition.json`, `FEngineContent`, `FEngineIgnition`); it is kept, and the player-facing word is "Runeword". "Ignition" is the existing once-per-run journal line of an engine; unchanged. "Slot" is one skill of a unit with its socketed runes (`FSkillSlot{skillId, runeIds}`). "Owned" means in the run's inventory or socketed in any party skill (`IsRuneOwned`). "Adjacent" is grid distance 1 (`GridDistance`, Manhattan). "Shocked" is the status `status.paralyze` (display name "Shock"); "burning" is `status.burn`; "frozen" is `status.freeze`. "Feed" is the battle log (`FCombatLog`). "Ship data" is `assets/data/`; the "frozen fixture" is `tests/fixtures/assets/data/`, which tests load before the ship data. "The gate" is `.claude/scripts/wp-gate.mjs`.

## 2. Player experience
- **Decision added**: which two runes to put together in one skill, and then whom to hit.
  - **Ember Engine** (`rune.fire` + `rune.ember`): hitting a burning enemy deals half of the burn's remaining damage to every enemy next to it. Favoured when enemies stand together; the player hits the burning enemy in the middle of the group.
  - **Frost Lock** (`rune.cold` + `rune.frostbite`): hitting a frozen enemy freezes one enemy next to it for one turn. Favoured for control parties; the player keeps hitting the frozen enemy instead of switching targets.
  - **Arc** (`rune.lightning` + `rune.shock`): hitting a shocked enemy deals half of that hit to every other shocked enemy, at any distance. Favoured when several enemies are shocked first; the party sets up, one unit cashes in.
  - **Kindled** (`rune.fire` + `rune.duration`): killing a burning enemy with this skill moves its burn to the nearest enemy that is not burning. Favoured for finishing weak enemies first.
  - Trade-off: a runeword takes two sockets of one skill, and both runes must fit that skill's tags. The same sockets could hold a shape or reach rune. A skill has one runeword at a time: the first in the list above that is complete. A runeword acts at most once per skill use, also for area skills.
  - The setup status counts no matter which party member applied it.
- **What the player sees before committing**:
  - Rune offer card: the effect summary of a catalyst starts with `Catalyst. `. Under the summary, one line: `Runeword: completes <name>: <effect text>` when the run already owns the other rune, else `Runeword ingredient` when the rune belongs to a runeword.
  - Camp, under each skill name: `Runeword: <name>: <effect text>` when active, or `Runeword 1/2: <name> needs <missing rune name>` when the skill holds one of the two.
  - Camp, a "RUNEWORDS" block: every runeword of which the run owns at least one rune, as `<name>: <rune A> + <rune B>: <effect text>`.
  - In battle: the skill button's tooltip (where the player picks the skill before a target) ends with the same line as the camp, `Runeword: <name>: <effect text>` or the progress line; and the feed gets one line each time a runeword acts.
  - "Next to" in every text means the four orthogonal tiles, the same adjacency the rest of the game uses.
- **Playtest hypothesis**: expect the player to pick a rune because it starts or completes a runeword and then to choose targets for the runeword · observe a remark of that kind in the playtest notes, at least one runeword completed in most runs (`engine_ignition` lines in the run log; in `runlog-report.mjs` the pick rates of the seven ingredient runes above the others), and no offer of a retired rune · kill-if the player completes a runeword and cannot say what it changed, or never completes one in three runs, or one runeword is taken every run (Frost Lock is the suspect).

## 3. Behaviors
| ID | Behavior | Proving test |
|---|---|---|
| F1 | WHEN engine data is loaded THEN an optional `effect` (`kind`, `percent`, `turns`) and `text` SHALL be parsed; an unknown effect kind, an effect without `text`, a `detonate` or `arc` percent outside 1..1000, a `spread` turns outside 1..10, or an effect on an engine with fewer than two runes SHALL fail the boot. | `engine_ignition_tests::RunewordEffectSchema` |
| F2 | WHEN the ship data is loaded THEN the four engines SHALL carry the names, effects and texts of §4, the seven ingredient runes SHALL be catalysts, the three power runes SHALL be retired, and no unlock row SHALL sell a retired rune. | `engine_ignition_tests::ShipRunewordData` |
| F3 | WHEN a slot's runes include every rune of one or more effect-bearing engines THEN the slot's runeword SHALL be the first such engine in data order; otherwise none. | `engine_ignition_tests::ActiveRunewordIsFirstComplete` |
| F4 | WHEN a party unit hits an enemy that already burns with a slot whose runeword is `detonate` THEN every living enemy adjacent to the target SHALL take `magnitude × turnsRemaining × percent / 100` damage of that burn and the feed SHALL get the line of §5; without an adjacent enemy, without the burn, or with an enemy as attacker nothing SHALL happen. | `engine_ignition_tests::EmberEngineBurnsNeighbours` |
| F5 | WHEN a party unit hits an already frozen enemy with a slot whose runeword is `spread` THEN exactly one living adjacent enemy that is neither frozen nor immune SHALL become frozen for `turns` turns (lowest entity id on ties) and the feed SHALL get the line of §5. | `engine_ignition_tests::FrostLockSpreadsFreeze` |
| F6 | WHEN a party unit hits an already shocked enemy with a slot whose runeword is `arc` THEN every other living shocked enemy SHALL take `max(1, damage dealt × percent / 100)`, whatever the distance, enemies without the status SHALL take nothing, and the feed SHALL get the line of §5. | `engine_ignition_tests::ArcHitsOtherShocked` |
| F7 | WHEN a party unit kills a burning enemy with a slot whose runeword is `carry` THEN the nearest living enemy that is not burning (lowest entity id on ties) SHALL burn with the slain enemy's remaining turns and magnitude, and the feed SHALL get the line of §5. | `engine_ignition_tests::KindledCarriesBurn` |
| F8 | WHEN a runeword has acted during a skill use THEN no runeword SHALL act again until the next skill use begins. | `engine_ignition_tests::RunewordActsOncePerSkillUse` |
| F9 | WHEN a rune has kind `retired` THEN it SHALL never be a rune offer candidate; other runes SHALL be offered as before. | `engine_ignition_tests::RetiredRuneNeverOffered` |
| F10 | WHEN the card line of a rune is built THEN it SHALL be `Runeword: completes …` IF the run owns every other rune of an effect-bearing engine containing it, `Runeword ingredient` IF it only belongs to such an engine, and empty otherwise. | `engine_ignition_tests::RunewordCardLine` |
| F12 | WHEN two skill uses in one turn each meet a runeword's condition, driven through the real resolver, THEN the runeword SHALL act once in each. | `engine_ignition_tests::RunewordActsOncePerSkillUse` |
| F11 | WHEN the slot line and the known-runeword list are built THEN they SHALL follow §5: active line, progress line, or empty; the list holds exactly the effect-bearing engines of which the run owns at least one rune, socketed runes included. | `engine_ignition_tests::RunewordSlotAndRecipeLines` |

## 4. Numbers
`assets/data/engine_ignition.json` (the `requires_runes`, `trigger` and `log` of each row are unchanged; every row authors its whole `effect` object as shown):

| `id` | `name` | `effect` | Knob | `text` | Metric it moves |
|---|---|---|---|---|---|
| `engine.ember` | Ember Engine | `{"kind": "detonate", "percent": 50}` | percent [25, 100] | Hitting a burning enemy deals half the burn's remaining damage to every enemy next to it | damage per round of runeword skills |
| `engine.frost` | Frost Lock | `{"kind": "spread", "turns": 1}` | turns [1, 2] | Hitting a frozen enemy freezes one enemy next to it for 1 turn | enemy turns skipped per battle |
| `engine.shock` | Arc | `{"kind": "arc", "percent": 50}` | percent [25, 100] | Hitting a shocked enemy deals half that hit to every other shocked enemy | damage per round of runeword skills |
| `engine.ember_kindled` | Kindled | `{"kind": "carry"}` | none | Killing a burning enemy moves its burn to the nearest enemy that is not burning | burn uptime |

The `text` states the number, so a knob change edits the text in the same commit; `ShipRunewordData` pins both.
Why these values: a burn is 3 damage for 3 turns (5 with `rune.ember`), so its remaining damage is 3–15; common enemies have 30–55 max HP; the existing Chain rune deals 2 to one extra enemy. At 50% Ember Engine deals 1–7 to each neighbour, about one ordinary hit; 100% would exceed the hit that triggers it. Arc at 50% follows the same reasoning. Path of Exile's area and chaining supports pay 11–40% less damage for extra targets (`build-foundation.md` §3). Frost Lock spreads a 1-turn freeze where the skill itself applies 3 turns (2 from the status, +1 from `rune.frostbite`): a frozen unit always loses its turn, so the spread starts at the minimum. Review flagged Frost Lock as the likely strongest runeword and noted that the knob cannot go below 1; that is recorded for the balance phase and not acted on here.

`assets/data/runes.json`: `"kind": "catalyst"` on `rune.fire`, `rune.cold`, `rune.lightning`, `rune.ember`, `rune.frostbite`, `rune.shock`, `rune.duration`; `"kind": "retired"` on `rune.power`, `rune.inferno`, `rune.glacial`. No other value changes.
`assets/data/unlocks.json`: the `rune.inferno` row is removed (a retired rune must not be sold; a saved unlock whose id is no longer in the catalog is already ignored with a warning, `FMetaProgress.cpp`).

Modifier stage (`docs/design/combat-math.md`): runeword damage is outside the skill pipeline. It is flat damage applied with `ApplyDamageToUnit` after stage 5 of the triggering hit (Arc reads that hit's final damage, after crit and the boss cap); it takes no defense, cannot crit and fires no hooks. The design session adds this line to `combat-math.md` in the spec commit.

## 5. File map
Create:
- `src/gameplay/combat/FRuneword.h`, `FRuneword.cpp` — the active runeword of a slot, the once-per-skill-use guard and the four effects.
- `src/gameplay/dungeon/FRunewordInfo.h`, `FRunewordInfo.cpp` — the texts the UI shows.

Modify:
- `assets/data/engine_ignition.json`, `assets/data/runes.json`, `assets/data/unlocks.json` — §4.
- `src/gameplay/data/FEngineContent.h`, `FEngineContent.cpp` — the new fields.
- `src/gameplay/data/FRuneContent.h`, `FRuneContent.cpp` — the `kind` field.
- `src/gameplay/data/FContentRegistry.cpp` — validation of the new fields.
- `src/gameplay/dungeon/FRewards.cpp` — retired runes leave the candidate pool.
- `src/ecs/systems/ActionResolverSystem.h` — one call that starts a skill use for the guard.
- `src/gameplay/GameplayServices.cpp` — register the listeners.
- `src/presentation/FDungeonRewardView.cpp` — the two card additions.
- `src/presentation/FDungeonCampView.cpp` — the slot line and the RUNEWORDS block.
- `src/ecs/systems/EmberCombatHudView.cpp` — the runeword line in the skill tooltip.
- `tests/features/engine_ignition_tests.cpp` — §7.
- `tests/features/hub_shop_tests.cpp` — the unlock catalog count (line 978) goes from 9 to 8.

`FEngineContent.h`:
```cpp
inline constexpr std::string_view kEngineEffectDetonate = "detonate";
inline constexpr std::string_view kEngineEffectSpread = "spread";
inline constexpr std::string_view kEngineEffectArc = "arc";
inline constexpr std::string_view kEngineEffectCarry = "carry";

struct FEngineContent {
    // existing fields unchanged
    std::string text;         // player-facing effect sentence (English); required when effectKind is set
    std::string effectKind;   // "" = no battle effect (journal only), else one of the four constants
    int effectPercent{0};     // detonate, arc: percent of the base amount (authored; 0 when absent)
    int effectTurns{0};       // spread: duration of the spread status in turns (authored; 0 when absent)
};
```
- Schema: append to `kEngineFields`, after `log`, `{"text", EContentFieldType::Text, false}` (index `kEngineFieldText`) and `{"effect", EContentFieldType::Object, false, &kEngineEffectSchema}` (index `kEngineFieldEffect`), with new `kEngineEffectFields` = `{"kind", Text, true}`, `{"percent", Number, false}`, `{"turns", Number, false}` (indices `kEngineEffectFieldKind`, `kEngineEffectFieldPercent`, `kEngineEffectFieldTurns`), built exactly like `kEngineTriggerFields` / `kEngineTriggerSchema` (index constants, `static_assert`). Update the `static_assert` on `kEngineFields`.
- `decodeEngine`: the loader fills an absent optional object with a default row and an absent optional number with 0, so `row.object(kEngineFieldEffect)` is non-null in practice; keep the same defensive `if (const FContentRow* effect = row.object(kEngineFieldEffect))` form the trigger uses, and inside it read `effectKind = effect->text(kEngineEffectFieldKind)` (empty when the row has no `effect`), `effectPercent = effect->integer(kEngineEffectFieldPercent, 0)`, `effectTurns = effect->integer(kEngineEffectFieldTurns, 0)`. No in-code default other than 0.
- Validation (`FContentRegistry.cpp`, in the engine loop, same `LOG_ERROR` + `return std::nullopt` pattern), only when `effectKind` is non-empty: it must be one of the four constants; `text` must be non-empty; `requiresRunes.size()` must be at least 2; for `detonate` and `arc`, `effectPercent` must be 1..1000; for `spread`, `effectTurns` must be 1..10.

`FRuneContent.h`: `std::string kind;` on `FRuneContent` (`""` = an ordinary rune), constants `kRuneKindCatalyst = "catalyst"`, `kRuneKindRetired = "retired"`; append `{"kind", EContentFieldType::Text, false}` as the last entry of `kRuneFields` with its index constant `kRuneFieldKind`, and raise the literal in its `static_assert` from 19 to 20; `decodeRune` reads it. Validation in `FContentRegistry.cpp` next to the rune rarity check: a non-empty kind other than the two constants fails the boot with `content: rune '<id>' has unknown kind '<kind>'.`

`FRewards.cpp`, `CandidatesFor`, in the pool loop directly after the `IsUnlockGated` check: `if (rune.kind == gameplay::kRuneKindRetired) continue;` (events that grant a random rune draw through the same function).

`FRuneword.h` (namespace `game`; includes `<entt/entt.hpp>`, `ecs/components/FSkillLoadout.h`, `gameplay/combat/FEffectHooks.h`, `gameplay/data/FContentRegistry.h`):
```cpp
/// Registry context. `fired` is true once a runeword has acted in the current skill use.
struct FRunewordGuard { bool fired{false}; };

/// The runeword of a slot: the first engine in content.engines.rows order that has an effect
/// (effectKind non-empty) and whose requiresRunes are all in slot.runeIds. nullptr = none.
[[nodiscard]] const gameplay::FEngineContent* ActiveRuneword(
    const gameplay::FContentRegistry& content, const FSkillSlot& slot);
/// Starts a skill use: the guard exists afterwards with fired == false.
void BeginRunewordAction(entt::registry& registry);
/// detonate, spread, arc (see below). Safe to call for every OnHit of every unit.
void RunewordOnHit(entt::registry& registry, const FHookContext& context);
/// carry (see below). Safe to call for every OnKill.
void RunewordOnKill(entt::registry& registry, const FHookContext& context);
/// Registers the two functions with RegisterHookListener for EHookEvent::OnHit and OnKill (boot, once),
/// the way FEliteRules.cpp registers its listener.
void RegisterRunewordListeners(entt::registry& registry);
```
Common gate of `RunewordOnHit` and `RunewordOnKill`, in this order; any failure returns without effect:
1. `registry.ctx().find<gameplay::FContentRegistry>()` is non-null.
2. The guard, if it exists, has `fired == false`.
3. `context.owner` is valid and has `FPartyMember` and `FSkillLoadout`.
4. The acting slot is the first slot of the loadout whose `skillId == context.skillId` (the resolver's rule; `FEngineIgnition.cpp` has a file-local `FirstSkillSlot` doing the same — write the loop again in `FRuneword.cpp`, that file is not edited).
5. `engine = ActiveRuneword(content, slot)` is non-null and its `effectKind` belongs to the function (OnHit: detonate, spread, arc; OnKill: carry).
6. `context.other` is valid and has `FEnemy` and `FGridPosition`, and its `FActiveStatuses` holds an effect with `statusId == engine->triggerStatusId` and `magnitude >= engine->minMagnitude`. Call that effect `trigger`. Do not require `context.other` to be alive: the resolver applies the damage before it fires OnHit, so on a killing hit the target is already dead here and keeps its components; the hit's own statuses land after the hooks, so `trigger` is the state before the hit.
7. `row = content.statuses.find(engine->triggerStatusId)` is non-null.

"Other enemies" below are entities with `FEnemy` and `FGridPosition` for which `IsAlive` is true, excluding `context.other`. Damage is `ApplyDamageToUnit`; when it returns true the feed gets `UnitDisplayName(victim) + " falls!"` (the same handling as `ApplyChain` in `FEffectHooks.cpp`; the battle outcome system sees the death as it does for chain kills). Feed lines go to `registry.ctx().find<FCombatLog>()` and are skipped when it is null; `A` is `UnitDisplayName(registry, context.owner)`, `N` is `engine->name`. When an effect acts (reaches its feed line), mark the guard: `if (auto* guard = registry.ctx().find<FRunewordGuard>()) guard->fired = true; else registry.ctx().emplace<FRunewordGuard>().fired = true;` (the `EnsureDispatchState` pattern of `FEffectHooks.cpp`). An effect that finds no victim leaves the guard untouched. `BeginRunewordAction` is `registry.ctx().insert_or_assign(FRunewordGuard{});`. The amounts in the feed lines are the amounts requested; on a boss whose damage gate lowers them the line may overstate, which is accepted. Adjacency is measured from the target's cell at the moment the listener runs, i.e. after the hit's own data hooks (a push rune on the same skill moves the target first).
- **detonate**: `amount = trigger.magnitude * trigger.turnsRemaining * engine->effectPercent / 100`. Victims: other enemies at `GridDistance == 1` from the target. When `amount <= 0` or there is no victim, nothing. Else damage each; feed: `A + ": " + N + " scorches " + <victim count> + " nearby for " + amount`.
- **spread**: candidates: other enemies at `GridDistance == 1` that have `FActiveStatuses`, do not carry `row->id` and are not immune to it (`IsStatusImmune`). None: nothing. Pick the lowest `entt::to_integral(entity)`. `ApplyStatus(active, *row, engine->effectTurns - EffectiveStatusDuration(*row, 0), 0)`, so the spread status lasts `effectTurns`; when the registry has an `FDungeonRun`, `RefreshCombatSheet(registry, victim, content)` (freeze changes speed). Feed: `A + ": " + N + " spreads " + row->displayName + " to " + UnitDisplayName(victim)`.
- **arc**: when `context.amount <= 0`, nothing. `amount = max(1, context.amount * engine->effectPercent / 100)`. Victims: other enemies whose `FActiveStatuses` carries `row->id`. None: nothing. Else damage each; feed: `A + ": " + N + " arcs to " + <victim count> + " for " + amount`.
- **carry** (OnKill; `context.other` is the slain enemy): destination = the other enemy with the smallest `GridDistance` to the slain enemy's cell (lowest entity id on ties) that has `FActiveStatuses`, does not carry `row->id` and is not immune. None: nothing. `ApplyStatus(active, *row, trigger.turnsRemaining - EffectiveStatusDuration(*row, 0), trigger.magnitude - EffectiveStatusMagnitude(*row, 0))`, so the applied burn has the slain enemy's remaining turns and magnitude. Feed: `A + ": " + N + " carries " + row->displayName + " to " + UnitDisplayName(destination)`.
- Runeword effects fire on every skill use whose condition holds. `CheckEngineIgnition` (the once-per-run journal and run-log line) is not changed.

Call sites:
- `ActionResolverSystem.h`, `Resolve`, directly after `points->skill -= resolved->apCost;`: `BeginRunewordAction(registry);` (add `#include "gameplay/combat/FRuneword.h"`). One skill use hits each target once and fires OnHit per target; the guard makes an area skill, and a corpse hit again later in the same target list, trigger one runeword only.
- `GameplayServices.cpp`: after `RegisterEliteRuleListener(registry);` add `RegisterRunewordListeners(registry);` with a `// [WP-45]` marker.
- `EmberCombatHudView.cpp`, the skill bar tooltip (the `if (hot && resolved)` block that builds `tip`): directly before `ImGui::SetTooltip`, `if (const std::string word = RunewordSlotLine(content, slots[index]); !word.empty()) tip += "\n" + word;` (`content` is the content registry already used by `AppendControlChances` there; add `#include "gameplay/dungeon/FRunewordInfo.h"`).

`FRunewordInfo.h` (namespace `game`; includes `<entt/entt.hpp>`, `ecs/components/FSkillLoadout.h`, `gameplay/data/FContentRegistry.h`; the `.cpp` includes `gameplay/combat/FRuneword.h` and `gameplay/dungeon/FRewards.h`):
```cpp
struct FRunewordEntry {
    std::string id;
    std::string name;
    std::string text;
    std::vector<std::string> runeNames;  // display names, in requires_runes order
};
/// Effect-bearing engines of which the run owns at least one rune (IsRuneOwned: inventory or socketed), in data order.
[[nodiscard]] std::vector<FRunewordEntry> KnownRunewords(entt::registry& registry);
/// "<name>: <rune A> + <rune B>: <text>"
[[nodiscard]] std::string RunewordRecipeLine(const FRunewordEntry& entry);
/// For a rune offered on a card. Walks the effect-bearing engines that contain runeId, in data order:
/// the first whose other runes are all owned gives "Runeword: completes <name>: <text>";
/// if none completes but at least one contains the rune: "Runeword ingredient"; else "".
/// Whether runeId itself is already owned makes no difference.
[[nodiscard]] std::string RunewordCardLine(entt::registry& registry, std::string_view runeId);
/// For a skill slot. ActiveRuneword non-null: "Runeword: <name>: <text>". Else the first effect-bearing
/// engine with at least one rune in the slot: "Runeword <held>/<total>: <name> needs <missing display names, ", "-joined>".
/// Else "".
[[nodiscard]] std::string RunewordSlotLine(const gameplay::FContentRegistry& content, const FSkillSlot& slot);
```
A rune's display name is its `displayName`, or its id when the row is missing. `KnownRunewords` and `RunewordCardLine` return empty results when the registry has no content registry.

Views:
- `FDungeonRewardView.cpp`, `DrawRuneOfferCard` (it has `registry`, `runeId` and `rune` in scope): the summary line becomes `(rune->kind == gameplay::kRuneKindCatalyst ? "Catalyst. " : "") + RuneEffectSummary(*rune, content)` (no new widget on the title row, whose width is tuned for 1280x800). Directly after that line and before the "Fits:" block: `const std::string word = RunewordCardLine(registry, runeId); if (!word.empty()) ImGui::TextWrapped("%s", word.c_str());`
- `FDungeonCampView.cpp`, in the "Skills & runes (free)" tree, directly after the skill name `ImGui::Text` and its tooltip: `if (content != nullptr) { const std::string word = RunewordSlotLine(*content, slot); if (!word.empty()) ImGui::TextDisabled("%s", word.c_str()); }`.
- `FDungeonCampView.cpp`, `RenderDungeonCampView`, directly before `ImGui::SeparatorText("LOADOUT (free)")`: `ImGui::SeparatorText("RUNEWORDS");` then one `ImGui::TextWrapped` per `KnownRunewords` entry with `RunewordRecipeLine`, or, when the list is empty, `ImGui::TextDisabled("None found yet. Rune cards mark runeword ingredients.");`

Do not touch: `src/gameplay/combat/FEngineIgnition.*`, `src/dev/FEnginePolicy.*`, `src/gameplay/combat/FSkillResolver.*`, `src/gameplay/combat/FEliteRules.*`, the rest of `src/ecs/systems/**` (only `ActionResolverSystem.h` and `EmberCombatHudView.cpp` change), `assets/data/statuses.json`, `assets/data/skills.json`, `assets/data/dungeon.json`, `tests/fixtures/**`, `.claude/scripts/**`, `CMakeLists.txt`, `docs/design/**`.

## 6. Tasks
`node .claude/scripts/build.mjs --preset windows-msvc-agent` configures if needed (new source files are picked up by the glob), builds and runs every ctest entry; it prints only errors and failing tests. A case "passes" when it is absent from that output. `docs/wp/WP-45/progress.md` is created by the implementer; the gate allows it. The implementer never edits `spec.md` or `features.json`. A build takes about 10 minutes; consecutive tasks may share one build when each task has its own commit. The final build runs at the final HEAD so `out/evidence.json` carries that SHA.

1. **T1 Schema, stubs and tests (red)** — `FEngineContent.*`, `FRuneContent.*`, the validation in `FContentRegistry.cpp`; create `FRuneword.*` and `FRunewordInfo.*` with every function declared and stubbed (`ActiveRuneword` returns nullptr, `BeginRunewordAction` and the two listener bodies empty, the text functions return empty); add all cases of §7. Ship data is not changed yet. Done: build.mjs → the only failing cases are `ShipRunewordData`, `ActiveRunewordIsFirstComplete`, `EmberEngineBurnsNeighbours`, `FrostLockSpreadsFreeze`, `ArcHitsOtherShocked`, `KindledCarriesBurn`, `RunewordActsOncePerSkillUse`, `RetiredRuneNeverOffered`, `RunewordCardLine`, `RunewordSlotAndRecipeLines` (`RunewordEffectSchema` passes). Commit.
2. **T2 Effects, guard and offer filter** — `FRuneword.cpp`, the call in `ActionResolverSystem.h`, the listener registration in `GameplayServices.cpp`, the retired filter in `FRewards.cpp`. Done: build.mjs → the only failing cases are `ShipRunewordData`, `RunewordCardLine`, `RunewordSlotAndRecipeLines`.
3. **T3 Texts and views** — `FRunewordInfo.cpp`, `FDungeonRewardView.cpp`, `FDungeonCampView.cpp`, `EmberCombatHudView.cpp`. Done: build.mjs → the only failing case is `ShipRunewordData`.
4. **T4 Ship data** — the three data files of §4 and the count in `hub_shop_tests.cpp:978` (9 → 8, with a comment naming WP-45). Done: build.mjs → 0 failing tests. Stop conditions, reported as `BLOCKED:` instead of being worked around: `hub_shop_tests::PurchaseAddsRuneToOfferPool` fails (it buys the first rune unlock, which becomes `rune.chain`); or any other test outside the file map fails on the new ship data (report the test name and the assertion). The suites that play battles on ship data are `dungeon_sequence_tests`, `tempo_skills_tests`, `combat_stakes_tests`, `boss_goals_tests` and `headless_tools_tests`; rune values do not change in this WP, only the offer pool and runeword effects can move their outcomes.
5. **T5 Evidence run** — PowerShell, timeout 600000 ms: `node .claude/scripts/baseline.mjs --label wp45-runes --seeds 1-12 --policy "reward=engine,equip=engine,event=first,chest=open,fork=variant,camp=offer+1,rest=60,continue" | Out-File -Encoding utf8 out/baseline-wp45-runes.txt` (the policy makes the autopilot prefer cards that complete a pair and socket pairs together). Done: exit code 0 and the file is non-empty; copy into `docs/wp/WP-45/progress.md` the report's battle rows and its per-rune rows (offered, picked, rate), and state whether `rune.power`, `rune.inferno` or `rune.glacial` appears among the offered runes (none may). The numbers are a record, not a pass condition.

## 7. Tests
New cases go into `tests/features/engine_ignition_tests.cpp` and are registered in its `RunTests` list. Helpers of that file: `FIgnitionFixture(label, actorSlots)` (actor "Adam" at (5,5) with `FStats{10, 2, 2, 2}`, enemy `foe` "Bela" at (5,4) with `FStats{1, 1, 20, 20}` and 50 HP, a bare `FDungeonRun`, `FCombatLog` in the context; it boots `TestAssetRoots()` = frozen fixture, then ship), `MakeUnit(tag, x, y, party, slots)`, `Slot(skillId, runes)`, `GiveStatus(registry, unit, statusId, magnitude)` (3 turns), `HasStatus(registry, unit, statusId)`, `StartTurn()`, `Tackle(token, target, skillId)` (one real resolver action; a hit deals at least 1), `RefillSkillAp()`, `Log(registry)`, `CountLines(lines, needle)`, `FTempRoot`, `RootsWith(temp)` (temp root first), `ReadJsonFile`, `WriteJsonFile`. `kShipAssetRoot` is in `tests/TestSupport.h`. The existing cases use `"skill.strike"` (single target, range 1, 3 sockets).

The frozen fixture has its own `engine_ignition.json` (four rows without effects), so the existing battle cases never see the ship file. `BadTriggerFailsBoot` does read the ship document as the base of its variants; the T4 rows stay valid for it, so it is not edited. New cases that need effects use a temp root:
- `RunewordEngines()` (file-local): returns the fixture's `engine_ignition.json` document with the §4 `name`, `text` and `effect` set on each row.
- `FIgnitionFixture` gets a third constructor parameter, `std::vector<std::string> roots = TestAssetRoots()`, passed to `LoadTestRegistry`; existing cases keep constructing `FIgnitionFixture(label, slots)` unchanged.
- `FRunewordFixture`: a small struct holding an `FTempRoot temp` declared before an `FIgnitionFixture fixture`; its constructor writes `RunewordEngines()` to `<temp>/data/engine_ignition.json` and then constructs `fixture(label, slots, RootsWith(temp))`. Cases reach the helpers through `.fixture`.

Cases:
- **RunewordEffectSchema** (F1): boot with `RunewordEngines()` in a temp root via `gameplay::FContentRegistry::load(game::FAssetManager{RootsWith(temp)})` as `BadTriggerFailsBoot` does: it has a value; `engines.find("engine.ember")` has `effectKind == "detonate"`, `effectPercent == 50`, non-empty `text`; `engine.frost` has `effectTurns == 1`. The plain fixture document (no `effect`) loads with empty `effectKind`. Each variant makes the load return no value: effect kind `"boom"`; an effect with `text` removed; `detonate` with `percent` 0, with `percent` 1001, with `percent` omitted; `spread` with `turns` 0 and with `turns` 11; an effect on a row whose `requires_runes` has one id.
- **ShipRunewordData** (F2): parse the three ship files from disk. The four engines have the `name`, `effect` and `text` of §4 exactly. In `runes.json`: exactly the seven ids of §4 have `kind == "catalyst"`, exactly the three have `kind == "retired"`. No row of `unlocks.json` names a retired rune id.
- **ActiveRunewordIsFirstComplete** (F3), on `FRunewordFixture` content: slot runes `{fire, ember}` → `engine.ember`; `{fire, duration}` → `engine.ember_kindled`; `{fire, ember, duration}` → `engine.ember`; `{ember, fire}` → `engine.ember`; `{fire}` → nullptr; `{}` → nullptr. On plain `FIgnitionFixture` content (no effects) `{fire, ember}` → nullptr.
- **EmberEngineBurnsNeighbours** (F4): `FRunewordFixture` with actor slot `Slot("skill.strike", {"rune.fire", "rune.ember"})`; enemies `near` at (5,3) and `far` at (5,2); `GiveStatus(foe, "status.burn", 4)`. `Tackle(StartTurn(), foe, "skill.strike")` → `near` lost exactly `4 * 3 * 50 / 100 = 6` HP, `far` lost 0, and the feed has exactly one line containing `"Ember Engine scorches 1 nearby for 6"`. Negative checks, each on a fresh fixture: no burn on `foe` → `near` unchanged, no such line; no `near` unit → no such line; actor slot with only `rune.fire` → `near` unchanged. Enemy attacker: create a fourth enemy `raider` with `MakeUnit("Raid", 6, 3, false, {Slot("skill.strike", {"rune.fire", "rune.ember"})})`, burn `near`, put a third enemy next to `near`, call `game::RunewordOnHit(registry, {.owner = raider, .other = near, .amount = 5, .skillId = "skill.strike"})` → no enemy's HP changes.
- **FrostLockSpreadsFreeze** (F5): actor slot `{"rune.cold", "rune.frostbite"}`; enemies `a` at (5,3) and `b` at (4,4), both adjacent to `foe`; `GiveStatus(foe, "status.freeze", 0)`. After `Tackle` exactly one of `a`, `b` carries the freeze, the one with the lower `entt::to_integral`, with `turnsRemaining == 1`; the feed has one line containing `"Frost Lock spreads"`. With both already frozen before the hit → no such line. With the lower one immune (`immune` holds `"status.freeze"`) → the other one is frozen.
- **ArcHitsOtherShocked** (F6): actor slot `{"rune.lightning", "rune.shock"}`; set `foe`'s `FStats` to `{1, 1, 0, 0}` so the hit deals more than 1; enemies `p` at (9,9) with `GiveStatus(p, "status.paralyze", 50)` and `q` at (9,8) without it; `GiveStatus(foe, "status.paralyze", 50)`. Let `dealt` be the HP `foe` lost in `Tackle`: `CHECK(dealt >= 4)`; `p` lost `dealt * 50 / 100`; `q` lost 0; the feed has one line containing `"Arc arcs to 1 for "`.
- **KindledCarriesBurn** (F7): actor slot `{"rune.fire", "rune.duration"}`; set `foe`'s `FHealth::current` to 1 and push a burn `FStatusEffect{"status.burn", 2, 7, false}` onto it; enemies `n` at (5,2), `burning` at (5,3) already carrying a burn of magnitude 9, and `m` at (9,9). After `Tackle` `foe` has `game::FDead`; `n` carries `status.burn` with `turnsRemaining == 2` and `magnitude == 7`; `burning` still has magnitude 9; `m` does not burn; the feed has one line containing `"Kindled carries"`.
- **RunewordActsOncePerSkillUse** (F8): Ember Engine fixture as in F4. `game::BeginRunewordAction(registry)`; `RunewordOnHit(registry, {.owner = actor, .other = foe, .amount = 5, .skillId = "skill.strike"})` → `near` lost 6; the same call again → `near` lost nothing more; `BeginRunewordAction` and the call again → `near` lost 6 more. On a fresh fixture without `near`: `BeginRunewordAction`, then the call → `registry.ctx().find<game::FRunewordGuard>()` is non-null and its `fired` is false. Through the resolver (F12), on a fresh Ember Engine fixture with `near`: `Tackle(token, foe, "skill.strike")`, `RefillSkillAp()`, `Tackle` again in the same turn → the feed has exactly two lines containing `"Ember Engine scorches"`, and `near` lost 6 in the first use and 7 in the second (after the first hit `rune.fire` and `rune.ember` re-apply the burn at magnitude 5 for 3 turns: `5 * 3 * 50 / 100 = 7`).
- **RetiredRuneNeverOffered** (F9): `FTempRoot`; write `data/runes.json` = the fixture `runes.json` with `"kind": "retired"` on `rune.power` (it has no tag requirement, and the offer pool falls back to the dungeon definition's party when the run party is empty). Boot A = `FIgnitionFixture(label, {}, RootsWith(temp))`, boot B = `FIgnitionFixture(label, {})`. For `room` 0..199 set `run.room` and collect `game::PeekRuneCandidates(registry)`. A: `rune.power` never appears and the union is non-empty; B: `rune.power` appears at least once.
- **RunewordCardLine** (F10): `FRunewordFixture`; `run.runes = {}` → `RunewordCardLine(registry, "rune.fire") == "Runeword ingredient"`, `RunewordCardLine(registry, "rune.range") == ""`. `run.runes = {"rune.ember"}` → for `"rune.fire"`: exactly `"Runeword: completes Ember Engine: "` + the §4 text. `run.runes = {"rune.duration"}` → `"Runeword: completes Kindled: "` + its text. `run.runes = {"rune.ember", "rune.duration"}` → Ember Engine (data order). Inventory empty but `rune.ember` socketed in a party member's skill (`run.party` gets one member whose `skills` holds `Slot("skill.strike", {"rune.ember"})`) → completes Ember Engine.
- **RunewordSlotAndRecipeLines** (F11): slot `{fire, ember}` → `"Runeword: Ember Engine: "` + text; slot `{fire}` → `"Runeword 1/2: Ember Engine needs "` + the display name of `rune.ember`; slot `{rune.range}` → `""`. `run.runes = {}` and no party → `KnownRunewords` empty; `run.runes = {"rune.shock"}` → one entry, `engine.shock`, and `RunewordRecipeLine` equals `"Arc: "` + display name of `rune.lightning` + `" + "` + display name of `rune.shock` + `": "` + text; `run.runes = {"rune.fire"}` → two entries, `engine.ember` then `engine.ember_kindled`; inventory empty with `rune.cold` socketed in a party member's skill → one entry, `engine.frost`.

`hub_shop_tests.cpp:978`: `CHECK(catalog.size() == 8);`.
The tooltip line of `EmberCombatHudView.cpp` is the same `RunewordSlotLine` that F11 tests.
The card and camp wiring in the two view files and the one-line resolver call have no test of their own (the texts and the guard are tested through their functions; `EmberEngineBurnsNeighbours` goes through the real resolver); the diff review checks them. Timeouts are unchanged. `features.json` exempts no file from the gate's CHECK-count rule.

## 8. Decisions
- Resolved: runewords are the existing four pairs with effects added — the pairs, their ingredients (all common or uncommon), the "status first, payoff on the next hit" trigger and the once-per-run journal already exist and are tested.
- Resolved: all four triggers read the target's state before the hit — the existing engine rule; every runeword becomes a set-up-then-cash-in play that any party member can prepare.
- Resolved: Arc is "damage to every other shocked enemy", not the foundation draft's "ignore range" — ignoring range would change targeting and line-of-sight code; the chosen effect gives the same play (shock several, then hit one) with one listener.
- Resolved: Frost Lock spreads from a frozen target, for one turn — same play as "when this skill freezes", one trigger rule for all runewords, and the shortest duration because a frozen unit always loses its turn.
- Resolved: Ember Engine does not remove the burn — `rune.fire` re-applies it on the same hit, so removal would be invisible.
- Resolved: one runeword action per skill use — OnHit fires once per target, so an area skill on N burning enemies would otherwise detonate N times, and a corpse later in the target list would trigger again.
- Resolved: Kindled skips enemies that already burn — `ApplyStatus` overwrites magnitude with the latest value, which could weaken a stronger burn.
- Resolved: `rune.leech` is not a catalyst — it is in no runeword and applies no status; it stays unlabelled until the balance phase decides its class.
- Resolved: retired runes stay in `runes.json` with `kind: "retired"` — saves, tests and `dungeon.json`'s `rune_drops` list still name them; only the offer pool skips them.
- Resolved: the recipe list is per run (runes are run-scoped); remembering discovered runewords across runs needs the meta save and is out of scope.
- Declined for now: a data lever that can weaken Frost Lock below a 1-turn spread (review finding) — a strength question, recorded for the balance phase (user, 2026-11-11: systems first, balancing later).
- Declined: showing the runeword on the reward screen's "Equip on:" buttons (review suggestion) — the card already says which runeword the rune completes, and re-socketing at camp is free.
- Alternatives considered: a new data file for runewords — rejected: it would duplicate `requires_runes` and the trigger. Putting the effects into `FEngineIgnition.cpp` — rejected: its "once per run" bookkeeping and tests would be entangled with effects that fire on every use. Power costs on action runes in this WP — deferred to the balance phase (see §1).
- Defaults for anything unspecified: follow `ApplyChain` in `FEffectHooks.cpp` for iterating enemies and dealing flat damage, and `FEliteRules.cpp` for a listener with battle state in the registry context.
- Out of scope: rune costs and `rune.swift` (balance phase), keystones (WP-46), new rune axes (WP-47), 3-rune runewords, a compendium page, autopilot changes (`ScoreEngineCard` already prefers completing a pair), balance tuning.

## 9. Pitfalls
- Listeners are registered once at boot and run for every hit and kill of every unit, in every battle; both functions must return early and cheaply when the gate fails.
- `ApplyDamageToUnit` fires no hooks, so runeword damage cannot chain into another runeword. Never call `FireHooks` from an effect.
- The resolver applies the hit's damage before OnHit: on a killing hit `context.other` is already dead. Do not filter on `IsAlive(context.other)`; only the victims are filtered.
- `ApplyStatus` checks immunity itself; the candidate filters still exclude immune units so the feed never claims a spread or carry that did not happen.
- `kRuneFields` and `kEngineFields` are ordered projections with index constants and `static_assert`s (the rune one is the literal 19): append at the end and update them together.
- The content loader gives absent optional numbers the value 0 and absent optional objects a default row; there is no "absent" to test for inside `decodeEngine`.
- Player-facing strings are English only. ImGui math operators are disabled: build `ImVec2` component-wise.
- One build at a time; an MSB3073/MSB3075 DLL-copy failure is a transient lock: rerun once. A linker error about a corrupt `game01P_runtime.lib` after an interrupted build: delete that `.lib` under `out/build/windows-msvc-agent/Debug/` and rebuild.

## 10. Open questions
None.
