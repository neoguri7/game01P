# WP-41: Dungeon paths — path strip, one fork per dungeon, elite battles, "?" events, six events

Size: L (new system) · Player-facing: yes · Certainty: settled · Base commit: the commit that adds this file · Branch: wp/WP-41

## 1. Goal [S]
Each dungeon shows its rooms as a path strip and has one fork: just before one room slot the player picks the standard room or its riskier variant — a battle or an **elite** battle (one affix, a certain boosted rune offer, +2 materials), a named event or a hidden **"?"** event (drawn from the slot's pool plus the `mystery` pool). Six data-driven events and four event effects give the event slots something to decide. Why: a choice of the next room with its reward in view makes the player reason from the party's current state (`docs/design/expansion-plan.md` §5).

- Decisions it rests on: `docs/design/expansion-plan.md` §5.1–5.5, §6; `docs/design/decisions.md` — room composition and count per dungeon are fixed (확정), "경로 선택: 위험한 경로는 보상 트레이드오프를 가진다" (확정), node points come from run EXP only (확정). WP-40 supplies `rarity_tables`, `FindRarityTable` and `FRuneOffer::rarityTable`.

## 2. Player experience [player-facing]
- **Decision added**: at the fork, standard vs variant.
  - Battle slot: `Battle` (normal rewards) vs `Elite: <affix>` (enemies +30% max HP, or +1 attack; reward: a certain rune offer from the `boosted` table and +2 materials). Elite is right when the party is healthy and wants runes; standard when HP is low or the build is thin.
  - Event slot: `Event: <title>` (its choices are listed) vs `? Unknown event` (hidden; drawn from the common and the mystery events together — mystery events pay more, a common one may be no better than the named event; nothing forces a loss, every event has a free choice). `?` is right when the named event offers nothing the party can use now; the named event is right when it is exactly what the party needs (a heal at low HP, `Ember Shrine` with 3 materials in hand).
  - Inside events: pay materials for a rune offer, trade HP for materials, stake 2 materials for 0/0/2/7, or leave.
- **What the player sees before committing**:
  - Path strip (HUD, one text line under the status zone, in every run phase): `Battle > Event > [Battle] > Battle / Elite: Hardened > Boss: Ember Warden > Camp` (a boss room shows the boss's name from the start) — done rooms muted, the current room in brackets and gold, the undecided fork as `standard / variant`.
  - Fork panel (in the reward modal or on the event result screen that precedes the fork slot): `Party HP a/b | Materials n`, then two buttons, each with a detail line (affix effect and reward; or the named event's choice texts).
  - Event choices: cost and reward range are in the choice text; a choice the party cannot pay is disabled with `Need N materials (have M)`; a stat gate shows the party's best value (existing).
  - The HUD text `ROOM n / 3` is removed; the status zone's first row reads `DEPTH 01`.
- **Playtest hypothesis (fork)**: expect the player to name party HP or the build as the reason at a fork ("can't take the elite now", "no materials, so ?") · observe the run log `fork` records (`kind`, `choice`, `party_hp`, `party_hp_max`); a session of two dungeons yields two forks · kill-if, judged per fork kind (battle forks and event forks separately): every fork of that kind in the session gets the same branch without a stated reason, or a wipe right after an elite is noted as unfair.
- **Playtest hypothesis (events)**: expect the player to weigh a cost against a shown range at least once ("3 materials is my upgrade") · observe the run log `event` records · kill-if every event is resolved by the same choice index without comment.

## 3. Behaviors [S]
| ID | Behavior | Proving test |
|---|---|---|
| F1 | WHEN `dungeon.json` has an `elite` block THEN `FRunSequence::Load` SHALL store `rewardMaterials` and the affixes; IF an affix lacks `id`/`display_name`/`text`, has `health_percent < 0`, `attack_bonus < 0`, both zero, or a duplicate id, or `reward_materials < 0` THEN load SHALL fail. WHEN the block is absent THEN the affix list SHALL be empty. | `run_path_tests::EliteBlockLoadsAndValidates` |
| F2 | WHEN the path state is first asked for in a dungeon THEN the fork slot SHALL be `eligible[EventPickHash(seed, depth, 1001) % eligible.size()]`, where a slot index ≥ 1 is eligible IF it is a non-boss battle and at least one affix exists, or an event and the catalog has at least one `mystery` event; with no eligible slot there SHALL be no fork. | `run_path_tests::ForkSlotIsSeededAndEligible` |
| F3 | WHEN the fork slot is a battle THEN its affix SHALL be `affixes[EventPickHash(seed, depth, 1002) % affixes.size()]`. | `run_path_tests::AffixIsSeeded` |
| F4 | WHEN `ContinueReward` or `ContinueEvent` leaves the slot before an undecided fork THEN the choice SHALL become 0; WHEN `TakeFork` is applied there THEN the choice SHALL become 1 and the flow SHALL advance exactly as the matching continue does; IF no fork is pending, an offer is open, or the event is unresolved THEN `TakeFork` SHALL fail without changing state. | `run_path_tests::ContinueTakesStandardTakeForkTakesVariant` |
| F5 | WHEN the chosen variant is an elite battle THEN the room plan SHALL carry `elite = true` and the affix id, and each enemy's sheet SHALL get `max HP += max HP * health_percent / 100` and `attack`/`magicAttack += attack_bonus` after the cycle scale; party units and non-elite rooms SHALL be unchanged. | `run_path_tests::EliteAffixScalesEnemies` |
| F6 | WHEN an elite battle is won THEN materials SHALL rise by `room_materials + elite.reward_materials`, a rune offer SHALL open without a chance roll from the `boosted` table (when at least the minimum legal candidates exist), and pity SHALL be 0. | `run_path_tests::EliteVictoryPaysOfferAndMaterials` |
| F7 | WHEN the chosen variant is `?` THEN the event SHALL be picked, with the existing unseen-first rule, from the catalog events that are in the slot's own pool or in `mystery`; with choice 0 it SHALL be picked from the slot's own pool alone. | `run_path_tests::MysteryForkPicksMysteryPool` |
| F8 | WHEN a run is saved and loaded THEN fork slot, kind, affix and choice SHALL be the saved ones (not re-derived), and the room plan, elite payout and picked event SHALL follow them; IF the saved fork no longer matches the content (slot out of range, slot type differs, boss slot, unknown affix) THEN the dungeon SHALL have no fork; a malformed `path` section SHALL load as the default state; a new run (same seed too) SHALL start undecided. | `run_path_tests::ForkSurvivesSaveAndContentChange` |
| F9 | WHEN the path strip is built THEN it SHALL hold one node per room slot plus `Camp`, with kinds, labels, `done`, `current` and the fork alternative as §5 defines for each phase. | `run_path_tests::StripMirrorsSlotsAndPhase` |
| F10 | WHEN the strip is drawn THEN it SHALL print every node label as ImGui text, the current node as `[label]`, and the undecided fork as `standard / variant`. | `run_path_tests::StripDrawsLabels` |
| F11 | WHEN a fork is pending THEN the reward modal and the event result screen SHALL show `CHOOSE A PATH`, the party line, and both option titles and details instead of the single `Continue`. | `run_path_tests::ForkPanelShowsBothOptions` |
| F12 | WHEN `gain_materials_range` applies THEN materials SHALL rise by `amount + roll % (magnitude - amount + 1)` with `roll = EventPickHash(seed, depth, 2000 + room*16 + min(rollIndex, 15))`; IF `amount < 0`, `magnitude < amount` or `magnitude > 999` THEN boot SHALL fail. | `event_framework_tests::MaterialsRangeIsSeededAndBounded` |
| F13 | WHEN `gain_materials_gamble` applies THEN the gain SHALL be `{0, 0, magnitude, amount}[roll % 4]` with the same `roll`. | `event_framework_tests::MaterialsGambleUsesFourOutcomes` |
| F14 | WHEN a choice has `pay_materials` effects THEN its gate SHALL be closed with `Need N materials (have M)` while `materials < N` (N = their sum), and open with `Costs N materials` otherwise; applying SHALL subtract N; such a choice counts as gated in the "one choice stays ungated" boot rule. | `event_framework_tests::PayMaterialsGatesAndCharges` |
| F15 | WHEN `open_rune_offer` applies THEN an offer SHALL open from the table named by `pool` and pity SHALL be 0; IF no offer can open THEN materials SHALL rise by `amount`; IF `pool` is not `normal`/`boosted`/`boss` THEN boot SHALL fail. WHILE that offer is open `ContinueEvent` SHALL fail, the legacy one-step choice SHALL stop on the result screen, and a resume past a vanished event SHALL skip the offer first. | `event_framework_tests::OpenRuneOfferOpensOrRefunds` |
| F16 | WHEN the shipped `events.json` loads THEN it SHALL hold the six events of §4.3 with their pools, choices, gates and effect parameters, and a ship-root boot SHALL pass. | `content_a_tests::ShippedEventsMatchTheTable` |
| F19 | WHEN the reward modal or the event modal posts `TakeFork` through the modal host THEN the host SHALL apply it (the run advances on the variant branch). | `run_path_tests::HostAcceptsTakeFork` |
| F17 | WHEN a fork is decided THEN the run log SHALL get a `fork` record with `depth`, `room`, `kind`, `choice`, `party_hp`, `party_hp_max`. | `run_log_tests::ForkRecordCarriesChoiceAndHp` |
| F18 | WHEN the autopilot reaches a resolved event with an open rune offer THEN it SHALL resolve the offer before continuing; `ParseAutopilotPolicy` SHALL accept `fork=standard` / `fork=variant` and reject other values. | `headless_tools_tests::AutopilotResolvesEventOffer` |
| F20 | WHEN the autopilot's policy says `fork=variant` and a fork is pending THEN it SHALL post `TakeFork`; with the default policy it SHALL post the plain continue (standard branch). | `run_path_tests::AutopilotForkPolicy` |

## 4. Numbers
### 4.1 Elite — `assets/data/dungeon.json` → top-level `elite`
```json
"elite": {
  "reward_materials": 2,
  "affixes": [
    {"id": "affix.hardened", "display_name": "Hardened", "text": "Enemy max HP +30%", "health_percent": 30, "attack_bonus": 0},
    {"id": "affix.fierce", "display_name": "Fierce", "text": "Enemy attack +1", "health_percent": 0, "attack_bonus": 1}
  ]
}
```
`reward_materials` knob [0, 4] (metric: materials gained per dungeon); `health_percent` knob [10, 60], `attack_bonus` knob [1, 3] (metric: elite-room win rate and HP loss, measured with the autopilot policy `fork=variant`). Elite offer table: `boosted` (WP-40). These first values are untuned.

### 4.2 Stage in `docs/design/combat-math.md`
Add a new paragraph at the end of §1 (after the `Owner per stage` line; §1's list is the party skill pipeline and is not edited): `Enemy sheet (separate from the skill pipeline): base → depth and room scale → cycle scale → elite affix (WP-41). Owner: RefreshCombatSheet block ③.`

### 4.3 Events — `assets/data/events.json` (`schema_version` 1, array `events`, in this order)
Effect parameters use the existing effect keys `amount`, `magnitude`, `pool`. `range a..b` means `amount = a, magnitude = b`. `Leave` choices have no `effects` key (the key is optional in the choice schema).
| id / title | tags | Choices: text → effects → result_text |
|---|---|---|
| `event.ember_shrine` / Ember Shrine | `common` | 1. `Offer 3 materials: rune offer (boosted)` → `pay_materials amount=3`, `open_rune_offer pool=boosted amount=3` → `The embers answer.` · 2. `Leave` → none → `You leave the shrine cold.` |
| `event.sealed_chest` / Sealed Chest | `common` | 1. `Force the lid: 3-5 materials`, check `stat=attack difficulty=10` → `gain_materials_range 3..5` → `The lid gives way.` · 2. `Pry it open: each ally loses 20% max HP, 2-4 materials` → `damage_party_pct amount=20`, `gain_materials_range 2..4` → `The trap bites, the chest opens.` · 3. `Leave` → none → `The chest stays sealed.` |
| `event.ash_gambler` / Ash Gambler | `common` | 1. `Stake 2 materials: win 0, 0, 2 or 7` → `pay_materials amount=2`, `gain_materials_gamble amount=7 magnitude=2` → `The ash settles.` · 2. `Leave` → none → `You keep your materials.` |
| `event.rune_cache` / Rune Cache | `mystery` | 1. `Take a rune: rune offer (boosted)` → `open_rune_offer pool=boosted amount=4` → `Runes glow in the dust.` · 2. `Take the materials: 4-6 materials` → `gain_materials_range 4..6` → `You fill your pack.` |
| `event.blood_altar` / Blood Altar | `mystery` | 1. `Bleed: each ally loses 30% max HP, rune offer (boss)` → `damage_party_pct amount=30`, `open_rune_offer pool=boss amount=4` → `The altar drinks.` · 2. `Take the offerings: 2 materials` → `gain_materials amount=2` → `You take what was left.` |
| `event.forgotten_armory` / Forgotten Armory | `mystery` | 1. `Break in: rune offer (boosted) and 2 materials`, check `stat=defense difficulty=8` → `open_rune_offer pool=boosted amount=2`, `gain_materials amount=2` → `The racks are yours.` · 2. `Scavenge: 2-3 materials` → `gain_materials_range 2..3` → `You find scraps.` |

Bodies: Ember Shrine `A shrine of banked embers asks for fuel.` · Sealed Chest `An iron chest, its lock fused by heat.` · Ash Gambler `A figure of ash rattles a cup of cinders.` · Rune Cache `A cracked wall hides a cache.` · Blood Altar `A black altar, still warm.` · Forgotten Armory `A barred door; racks glint behind it.`
All amounts are knobs [0, 10] (percentages [10, 50]); metric: materials gained per dungeon (target +0…3 over today's ~9) and rune offers per dungeon; tuned by the simulation, not by hand. The built-in `event.quiet_shrine` stays in `common`. Shipped `dungeon.json` room slots are unchanged.

## 5. File map [S]
### Create `src/gameplay/dungeon/FRunPath.h` / `FRunPath.cpp` (namespace `game`)
```cpp
inline constexpr std::string_view kRunPathSectionId = "path";
inline constexpr std::string_view kEventPoolMystery = "mystery";
inline constexpr int kForkSlotSalt = 1001;
inline constexpr int kForkAffixSalt = 1002;

/// The current dungeon's fork and its decision (ctx). Derived once per (seed, depth), then only read; saved
/// whole through the `path` section, so a load never re-derives it.
struct FRunPathState {
    std::uint32_t seed{0};
    int depth{0};             // 0 = nothing derived yet
    bool hasFork{false};
    int slot{0};              // index into FRunDungeonDef::rooms
    bool battle{false};       // true: battle vs elite; false: event vs "?"
    int battleOrdinal{0};     // battle fork: the slot's battle ordinal; event fork: the ordinal of the battle before it
    std::string affixId;      // battle forks only
    int choice{-1};           // -1 undecided, 0 standard, 1 variant
};

enum class EPathNodeKind { Battle, Elite, Event, Mystery, Boss, Camp };

struct FPathNode {
    EPathNodeKind kind{EPathNodeKind::Battle};
    std::string label;        // "Battle" | "Elite: <display_name>" | "Event" | "?" | "Boss: <name>" | "Boss" | "Camp"
    bool done{false};
    bool current{false};
};

struct FPathStrip {
    std::vector<FPathNode> nodes;      // one per room slot, then Camp
    int forkNode{-1};                  // index into nodes, -1 = no fork
    std::optional<FPathNode> forkAlt;  // the variant node while the fork is undecided
};

struct FForkOption {
    std::string title;
    std::string detail;
};

/// Registers the `path` save section and emplaces the ctx state. The state is never erased afterwards, so
/// EnsureRunPath never inserts into the context (pointers and references into ctx stay valid across it).
void RegisterRunPathSection(entt::registry& registry);
/// Resets the state to default (depth 0), so the next EnsureRunPath derives again. Called at run start.
void ResetRunPath(entt::registry& registry);
/// The state for the installed run. Derives the fork when the state's seed or depth differs from the run's;
/// otherwise validates the stored fork against the sequence and drops it (hasFork = false) on a mismatch.
[[nodiscard]] FRunPathState& EnsureRunPath(entt::registry& registry);
/// The room-slot index the run is in: Battle/Reward → the slot of battle `run.room`; Event → that slot + 1;
/// Camp → rooms.size(). nullopt without run or sequence, or in Defeat.
[[nodiscard]] std::optional<int> CurrentSlotIndex(const entt::registry& registry);
/// True when a fork exists, is undecided, the phase is Reward or Event, and state.slot == CurrentSlotIndex + 1.
[[nodiscard]] bool IsForkPending(entt::registry& registry);
/// Decides the pending fork (0 or 1) and appends the ForkChosen run event. False when nothing is pending.
bool DecideFork(entt::registry& registry, int choice);
[[nodiscard]] bool IsEliteBattle(entt::registry& registry, int battleOrdinal);
[[nodiscard]] const FEliteAffix* EliteAffixFor(entt::registry& registry, int battleOrdinal);  // nullptr = not elite
/// The pools the gap event after battle `battleOrdinal` draws from: {slot pool, "mystery"} for a chosen "?"
/// fork, else {slot pool}; empty when there is no gap slot.
[[nodiscard]] std::vector<std::string> GapEventPools(entt::registry& registry, int battleOrdinal);
[[nodiscard]] FPathStrip BuildPathStrip(entt::registry& registry);
/// [0] standard, [1] variant; empty when no fork is pending.
[[nodiscard]] std::vector<FForkOption> ForkOptions(entt::registry& registry);
```
Rules:
- `RegisterRunPathSection`: `registry.ctx().emplace<FRunPathState>()` if absent, then `RegisterSaveSection` (template `FEventSection.cpp:53–135`). Pack: `{"seed","depth","has_fork","slot","battle","battle_ordinal","affix_id","choice"}`. Unpack assigns into the existing ctx object in place: `{}`, a missing key, or any malformed value (wrong type, `choice` outside −1..1, negative `depth`/`slot`/`battle_ordinal`) → `FRunPathState{}`; the `section_version` key the framework leaves in the object is ignored. It never reads `FDungeonRun`.
- `EnsureRunPath`: no run or no sequence → return the state as is. `state.seed != run.seed || state.depth != run.depth` → derive: `rooms = DungeonAt(PositionOf(run.depth - 1)).rooms`; eligible indices `i` in `1..rooms.size()-1`, ascending: `type == "battle" && boss.empty() && !sequence.elite.affixes.empty()`, or `type != "battle"` and the catalog (`EventCatalog(registry)`, called once here) holds an event with tag `mystery` (`EventInPool`). Write `{run.seed, run.depth, hasFork, slot, battle, battleOrdinal, affixId, -1}` with `slot = eligible[EventPickHash(seed, depth, kForkSlotSalt) % eligible.size()]`; `battleOrdinal` = count of battle slots before `slot` (for an event slot minus 1); `affixId = affixes[EventPickHash(seed, depth, kForkAffixSalt) % affixes.size()].id` for battle forks, empty otherwise. No eligible slot → `hasFork = false`. Otherwise (same seed and depth) validate when `hasFork`: `slot < rooms.size()`, `(rooms[slot].type == "battle") == battle`, a battle slot has an empty `boss` and `affixId` names an affix of the sequence; any failure → `hasFork = false`, `LOG_WARN("Content changed under this save: the fork of depth {} is dropped.")`. This path does not touch the catalog (it runs per frame and per enemy sheet).
- Absent state: `EnsureRunPath` asserts the ctx state exists (registered at boot). The query functions `IsForkPending`, `IsEliteBattle`, `EliteAffixFor`, `GapEventPools`, `BuildPathStrip`, `ForkOptions` first check `registry.ctx().find<FRunPathState>()` and mean "no fork" when it is absent (a registry built without `FGameplayServices::Initialize`).
- `IsForkPending`: `EnsureRunPath` → `hasFork && choice == -1`, phase Reward or Event, `CurrentSlotIndex + 1 == slot`.
- `DecideFork`: `IsForkPending` else false; store 0 or 1 (any non-zero input is 1); `AppendRunEvent(registry, ERunEventKind::ForkChosen, kind, choice)` with `kind` = the branch taken: `"battle"`, `"elite:<affixId>"`, `"event"` or `"mystery"`; return true. It does not save (the caller's transition saves).
- `IsEliteBattle`: `hasFork && battle && choice == 1 && state.battleOrdinal == battleOrdinal`. `EliteAffixFor`: that, then the affix with `affixId` in `FindRunSequence(registry)->elite.affixes` (nullptr when missing).
- `GapEventPools`: `GapEvent(position, battleOrdinal)`: null → `{}`; `hasFork && !battle && choice == 1 && state.battleOrdinal == battleOrdinal` → `{slot->eventPool, "mystery"}`; else `{slot->eventPool}`.
- `BuildPathStrip`: no run/sequence → empty strip. One node per slot: battle with non-empty `boss` → `Boss`, label `"Boss: " + def->name` for `def = FindBossCatalog(registry)->Resolve(themes[position.theme].id, slot.boss)`, plain `"Boss"` when the catalog or the mapping is missing; battle → `Battle`/`"Battle"`; other → `Event`/`"Event"`. Fork node (`hasFork`): choice 1 → the node becomes `Elite`/`"Elite: <display_name>"` or `Mystery`/`"?"`; choice −1 → `forkAlt` = that variant node; choice 0 → standard node, no `forkAlt`. `forkNode = slot`. Append `Camp`/`"Camp"`. `c = CurrentSlotIndex` (nullopt → no node current): nodes `< c` are `done`; node `c` is `current`; in phase Reward node `c` is also `done`.
- `ForkOptions` (only when `IsForkPending`, else empty): battle fork → `{"Battle", "Standard rewards"}`, `{"Elite: <display_name>", "<text>. Reward: rune offer (boosted), +<reward_materials> materials"}`. Event fork → `{"Event: <title>", <the event's choice texts joined with " / ">}` for the event `PickEventId(catalog, rooms[slot].eventPool, EventSeen(registry), seed, depth, state.battleOrdinal)` names (`{"Event", "A known event"}` when the pick is empty), and `{"? Unknown event", "Hidden: any common or mystery event. Mystery events pay more; every event has a free choice"}`. (Called only while a fork panel is on screen; the catalog copy is accepted there.)

Dependencies (all `.cpp`-level): `FRunPath.cpp` includes `FRunSequence.h`, `FDungeonRun.h`, `FEventContent.h`, `FEventSection.h`, `FRunEventLog.h`, `FSaveSections.h`. `FEventSection.cpp`, `FCombatSheet.cpp`, `FRunFlow.cpp`, `FDungeonRun.cpp` include `FRunPath.h`. `FRunSequence.cpp` does not (it stays a leaf).

### Modify `src/gameplay/dungeon/FRunSequence.h` / `.cpp`
```cpp
struct FEliteAffix {
    std::string id;
    std::string displayName;
    std::string text;
    int healthPercent{0};
    int attackBonus{0};
};
struct FEliteRules {
    int rewardMaterials{0};
    std::vector<FEliteAffix> affixes;
};
// in FRunSequence:
    FEliteRules elite;
```
`Load`: optional top-level `elite` object (F1 rules; error strings `elite.affixes[<i>]: <reason>` / `elite.reward_materials: <reason>`). `FrameRoomPlan` is not changed.

### Modify `src/gameplay/dungeon/FDungeonRun.cpp`
`BuildDungeonBattlefield`: change `const FRoomPlan plan = FrameRoomPlan(registry, run.room);` (l.880) to a non-const `FRoomPlan plan`, and directly after it: `if (const FEliteAffix* affix = EliteAffixFor(registry, run.room)) { plan.elite = true; plan.affixId = affix->id; }`.

### Modify `src/gameplay/combat/FCombatSheet.cpp`
Block ③, after the cycle scale lines, inside the same `if`: `if (const FEliteAffix* affix = EliteAffixFor(registry, run->room)) { health->max += health->max * affix->healthPercent / 100; stats->attack += affix->attackBonus; stats->magicAttack += affix->attackBonus; }`.

### Modify `src/gameplay/dungeon/FRewards.h` / `.cpp`
```cpp
/// Rolls candidates from `rarityTable` (RollRuneCandidates) and opens the offer: active, candidates,
/// rarityTable = FindRarityTable's name or empty, pity = 0, bossExtraPending = false. False when an offer is
/// already open (nothing consumed) or fewer than max(1, min_legal_candidates) candidates came up (the draw
/// index is consumed, nothing else changes).
bool OpenRuneOffer(entt::registry& registry, std::string_view rarityTable);
void RollRoomOffer(entt::registry& registry, bool bossRoom, bool elite = false);
```
`RollRoomOffer`: after the `offer.active` early return, `if (elite && !bossRoom) { if (!OpenRuneOffer(registry, kRarityTableBoosted)) LOG_INFO("Elite rune offer withheld: too few legal candidates."); return; }`. The monster and boss paths keep their code.

### Modify `src/gameplay/dungeon/FRunFlow.h` / `.cpp`
- `ERunIntent`: append `TakeFork`. `ApplyRunIntent`: `case ERunIntent::TakeFork: return TakeFork(registry);`.
- `TakeFork(registry)` (file-local, declared with the other handlers): no run → `NoRun()`; `FindRunSequence(registry) == nullptr` → `std::unexpected("The dungeon sequence is missing.")`; `!IsForkPending(registry)` → `std::unexpected("There is no path to choose.")`; `EnsureRewardsForRun(registry)`, then an open offer → `std::unexpected("Resolve the rune offer before continuing.")`; phase Event with no resolved `FEventInstance` → `std::unexpected("Choose an option first.")`. Then `DecideFork(registry, 1)` and `return run->phase == EDungeonPhase::Reward ? ContinueReward(registry) : ContinueEvent(registry);`.
- `ContinueReward`: after the sequence null check, `if (IsForkPending(registry)) DecideFork(registry, 0);`.
- `ContinueEvent`: after the resolved check: `EnsureRewardsForRun(registry);` and refuse with `"Resolve the rune offer before continuing."` while `FRewards.offer.active`; then `if (IsForkPending(registry)) DecideFork(registry, 0);`.
- `ChooseEventLegacy`: after a successful `ChooseEventOption`, `if (offer is active) return resolved;` (the result screen stays up for the offer), else `ContinueEvent` as today.
- `NormalizeResumedRunPhase`, vanished-event path (before `ApplyRunIntent(registry, ERunIntent::ContinueEvent)` at l.412): `if (offer is active) (void)ResolveRuneOffer(registry, -1);`.
- `ResolveRoomVictory`: `const bool elite = IsEliteBattle(registry, run->room);` before the materials line; after `run->message` is assigned (l.479–480): `if (elite) { const int bonus = sequence elite.rewardMaterials (0 without a sequence); run->materials += bonus; run->message += " Elite defeated: +" + std::to_string(bonus) + " materials."; }`; the call becomes `RollRoomOffer(registry, bossRoom, elite);`. Re-fetch `run` after `IsEliteBattle` is not needed (no ctx insert, see `RegisterRunPathSection`).
- `StartRunAt`: next to `ResetRewardsForRun(registry);` (l.370) call `ResetRunPath(registry);`.

### Modify `src/states/FEmberModalHostState.cpp`
`AcceptsIntent` (l.16–26): the reward modal also accepts `ERunIntent::TakeFork`; the event modal also accepts `ERunIntent::TakeFork`. Without this the host logs and drops the intent the fork buttons post.

### Modify `src/gameplay/dungeon/FEventSection.cpp`
`CurrentEventInstance`: `pools = GapEventPools(registry, run->room)`. One pool → `PickEventId(catalog, pools[0], ...)` exactly as today. Two pools → build `std::vector<FEventDef> mixed` = the catalog events (catalog order) for which `EventInPool(event, pool)` holds for any of the pools, and call `PickEventId(mixed, "", EventSeen(registry), run->seed, run->depth, run->room)` (the empty pool matches every event of `mixed`). Empty `pools` → nullopt as today.

### Modify `src/gameplay/dungeon/FEventEffects.h` / `.cpp`, `FEventContent.cpp`
- Kind constants in the header: `kEventEffectGainMaterialsRange = "gain_materials_range"`, `kEventEffectGainMaterialsGamble = "gain_materials_gamble"`, `kEventEffectPayMaterials = "pay_materials"`, `kEventEffectOpenRuneOffer = "open_rune_offer"`; `inline constexpr int kEventRollSaltBase = 2000;`.
- `FEventOutcome`: add `bool offerOpened{false};`. `FEventOutcomeAccumulator` (file-local): add `int rollIndex{0};`.
- Roll helper (file-local): `EventPickHash(run.seed, run.depth, kEventRollSaltBase + run.room * 16 + std::min(rollIndex, 15))`, where `rollIndex` is the accumulator's counter, post-incremented; without an accumulator (an effect applied outside `ApplyEventChoice`) it is 0. The key ignores event id and choice index: two choices of one event share a roll sequence (accepted — only one of them is ever applied).
- Appliers: range → `gain = amount + roll % (magnitude - amount + 1)`; gamble → `gain = std::array<int, 4>{0, 0, magnitude, amount}[roll % 4]`; both add to `run->materials` and `outcome->materials`. Pay → `run->materials = std::max(0, run->materials - amount)`, `outcome->materials -= amount`. Open offer → `if (OpenRuneOffer(registry, spec.pool)) outcome->offerOpened = true; else { run->materials += spec.amount; outcome->materials += spec.amount; }`.
- Validators: range `0 <= amount && amount <= magnitude && magnitude <= kEventEffectAmountMax`; gamble `0 <= magnitude && magnitude <= amount && amount <= kEventEffectAmountMax`; pay → the existing `ValidatePositiveAmount`; open offer (new) `pool` ∈ {`normal`, `boosted`, `boss`} and `0 <= amount <= kEventEffectAmountMax`. Register the four in `RegisterEventEffects` (Run scope). `gain_rune_random` keeps `ValidateEmptyRunePool`.
- `DescribeEventOutcome`: materials `> 0` as today; `< 0` → `"-<N> materials"`; `offerOpened` → `"rune offer"` (after the materials part).
- `EvaluateEventGate` (const registry; it only needs `ctx().find<FDungeonRun>()`): compute the stat gate as today. `cost` = sum of `amount` over the choice's effects of kind `kEventEffectPayMaterials`. When `cost > 0` and the stat gate is open or absent: `materials < cost` → `open = false`, reason `Need <cost> materials (have <materials>)`; else open, reason = the stat reason (if any) + `"; "` + `Costs <cost> materials` (no separator when there is no stat reason). A closed stat gate keeps its own reason. No run → closed, reason `Materials unknown`.
- `FEventContent.cpp` `ValidateEventContent` ②: a choice counts as gated when it has a `check` or any `pay_materials` effect.

### Modify `src/gameplay/dungeon/FRunEventLog.h`, `FRunLog.cpp`
`ERunEventKind`: append `ForkChosen`. `FRunLog.cpp` handler: `LogRunEvent(registry, {{"t","fork"},{"depth",event.depth},{"room",event.room},{"kind",event.id},{"choice",event.amount},{"party_hp", Σ DungeonEffectiveHealth(registry, member)},{"party_hp_max", Σ DungeonMemberMaxHealth(registry, member)}})`; add `{"fork", {"t","depth","room","kind","choice","party_hp","party_hp_max"}}` to `kRules`. The other `switch (event.kind)` is `src/gameplay/meta/FMetaProgress.cpp:578`: add `case ERunEventKind::ForkChosen: break;` there if it does not compile or warns without it.

### Modify `src/gameplay/GameplayServices.cpp`
`RegisterRunPathSection(registry);` directly after the event section registration (l.70).

### Create `src/presentation/FPathStripView.h` / `.cpp` (namespace `game`)
```cpp
/// The path strip as one line of ImGui text in a borderless window whose top-right corner is (rightX, topY).
/// `scale` is the HUD scale (font size 12 * scale). Nothing is drawn for an empty strip.
void DrawPathStrip(entt::registry& registry, float rightX, float topY, float scale);
/// The fork panel: SeparatorText("CHOOSE A PATH"), the line "Party HP <a>/<b> | Materials <n>", then per
/// option a ui::Button(title, {-1, 38}) and a TextWrapped detail. Returns 0 or 1 for the clicked option,
/// -1 otherwise. `disabled` greys both buttons. Draws nothing and returns -1 when no fork is pending.
[[nodiscard]] int DrawForkPanel(entt::registry& registry, bool disabled);
```
- `DrawPathStrip`: `ImGui::SetNextWindowPos({rightX, topY}, ImGuiCond_Always, {1.f, 0.f})`; window `"##path-strip"` with `NoDecoration | AlwaysAutoResize | NoSavedSettings | NoMove | NoInputs | NoFocusOnAppearing | NoNav | NoBackground`; font via `ui::FBodyFont body(12.f * scale)`. Nodes left to right, each one `ImGui::TextColored` item, joined by `ImGui::SameLine()` + `ImGui::TextDisabled(">")` + `ImGui::SameLine()`. Node text: label; the undecided fork node → `label + " / " + forkAlt->label`; the current node → `"[" + text + "]"`. Colours: current `pal::kGoldBrightV`, done (not current) `pal::kMutedV`, other `pal::kTextV`.
- `DrawForkPanel`: party line sums `DungeonEffectiveHealth` / `DungeonMemberMaxHealth` over `run.party` and `run.materials`.

### Modify `src/ecs/systems/EmberCombatHudView.cpp`
`TopBar`: the `depth` string becomes `"DEPTH %02d"` (`run.depth`); after `ui::EndZone()` call `DrawPathStrip(registry, zones.status.right(), zones.status.bottom() + 8.f * u, u);` (`u` is the function's existing scale variable).

### Modify `src/presentation/FDungeonRewardView.cpp`, `FDungeonEventView.cpp`
- Reward: when `IsForkPending(registry)`, the `Continue` button (`if (ui::Button("Continue", {-1, 42}))` with its body, near l.486–494 after WP-40) is replaced by `const int pick = DrawForkPanel(registry, offerOpen);`; `pick == 0` → the existing Continue body; `pick == 1` → the same body with `ERunIntent::TakeFork`.
- Event result stage: fetch `auto* rewards = registry.ctx().find<FRewards>();` and `const auto* content = registry.ctx().find<gameplay::FContentRegistry>();` as the reward view does. `offerOpen = rewards != nullptr && rewards->offer.active`; when open, `DrawRuneOffers(registry, *rewards, content)` under the result text and `ImGui::TextDisabled("Pick a rune or skip the offer to continue.")`. When `IsForkPending`, `DrawForkPanel(registry, offerOpen)` replaces `Continue` (0 → `ContinueEvent`, 1 → `TakeFork`; both `CloseCurrentPopup`) and the Confirm key does nothing; otherwise `Continue` is disabled while `offerOpen` and the Confirm key posts `ContinueEvent` only when `!offerOpen`. The popup keeps its 580 px width (the same width the reward modal shows the cards in).

### Modify `src/dev/FRunAutopilot.h` / `.cpp`
- `FAutopilotPolicy`: add `bool forkVariant{false};`; `ParseAutopilotPolicy`: key `fork`, values `standard` (false) / `variant` (true), parsed like the `event` key (l.267); an unknown value is the same error the `event` key returns.
- Reward branch: `PostRunIntent(registry, policy_.forkVariant && IsForkPending(registry) ? ERunIntent::TakeFork : ERunIntent::ContinueReward);`.
- Event branch, `instance->resolved`: first `ResolveOpenOffers(registry, policy_, *run, EOfferContext::Reward);`, then post `TakeFork` under the same condition, else `ContinueEvent`.

### Data and docs
- Modify `assets/data/dungeon.json` (§4.1), `assets/data/events.json` (§4.3), `docs/design/combat-math.md` (§4.2).
- Fixtures are not touched: the fixture `dungeon.json` has no `elite` block and no fixture event has the `mystery` tag, so fixture boots have no fork.

### Tests
- Create `tests/features/run_path_tests.cpp` (harness copied from `event_framework_tests.cpp`: `FBoot`, `FTempAssets`, `StartFresh`, `WinRoom`, `SkipOfferIfActive`, `ContinueRewardStep`, `ForgetRunInMemory`; `RenderModalLogs` copied from `reward_offer_tests.cpp:229`).
- Modify `tests/features/event_framework_tests.cpp`, `content_a_tests.cpp`, `run_log_tests.cpp`, `headless_tools_tests.cpp` (new cases only; in `run_log_tests.cpp` the file's `FBoot` may gain a defaulted `std::vector<std::string> roots = TestAssetRoots()` constructor parameter, and `FTempAssets`/`FixtureDoc` may be copied in from `event_framework_tests.cpp:167`) and `tests/features/compendium_tests.cpp` (l.481: the pinned string becomes `"DEPTH 01"`).
- Create `docs/wp/WP-41/progress.md`.

Do not touch: `tests/fixtures/**`, `src/gameplay/dungeon/FDungeonSave.cpp` (`EDungeonPhase` and the core schema stay), `FRoomGenerator.*`, `FEncounterContent.*`, `assets/data/rewards.json`, `assets/data/ui_layout.json`.

## 6. Tasks [S]
Every task ends with `node .claude/scripts/build.mjs --preset windows-msvc-agent` → exit 0, no failing test. A new `tests/features/*_tests.cpp` needs a reconfigure first (`cmake --preset windows-msvc-agent`).
1. **T1 Elite data + path state** — `FRunSequence` elite block; `FRunPath.h/.cpp` (state, section, `EnsureRunPath`, `CurrentSlotIndex`, `IsForkPending`, `DecideFork`, `IsEliteBattle`, `EliteAffixFor`, `GapEventPools`); `GameplayServices.cpp`; `ERunEventKind::ForkChosen` with its run-log record; tests F1–F3, F17.
2. **T2 Flow** — `OpenRuneOffer`, `RollRoomOffer(elite)`, `TakeFork`, `AcceptsIntent`, continue defaults, `ResetRunPath`, `CurrentEventInstance` pool, plan stamp, `FCombatSheet`, `ResolveRoomVictory`; tests F4–F8, F19.
3. **T3 Event effects** — four effects, gate, outcome text, dominance rule, `ContinueEvent` refusal, `ChooseEventLegacy`, resume path; tests F12–F15.
4. **T4 Content** — `dungeon.json`, `events.json`, `combat-math.md`; test F16.
5. **T5 Strip and panels** — `BuildPathStrip`, `ForkOptions`, `FPathStripView`, HUD, reward and event views, the compendium string; tests F9–F11.
6. **T6 Autopilot** — policy flag, event offer; tests F18, F20.

## 7. Tests
Temp-root documents (`FTempAssets`): start from the fixture `dungeon.json` (`FixtureDoc`), add the `elite` block of §4.1, replace the first dungeon's `rooms` with the list the case states, and write an `events.json` with two events tagged `common` and two tagged `mystery` (each: two ungated choices with `gain_materials amount=1`) unless the case says otherwise. "Pick a seed" means: loop seeds from 1 until `EnsureRunPath` gives the wanted slot/affix, and `CHECK` one was found below 200.
- **F1 EliteBlockLoadsAndValidates**: `FRunSequence::Load(document, error)` → `elite.rewardMaterials == 2`, two affixes with the §4.1 values. Eight broken copies (missing `id`, missing `display_name`, missing `text`, `health_percent: -1`, `attack_bonus: -1`, both 0, duplicate id, `reward_materials: -1`) → nullopt and a non-empty error. No `elite` key → loads, `affixes.empty()`.
- **F2 ForkSlotIsSeededAndEligible**: rooms `[battle, event, battle, battle(boss mid)]`: for seeds 1..40 (`StartRun` each) the slot is 1 or 2, both occur, and equals `std::array{1, 2}[EventPickHash(seed, 1, 1001) % 2]`; `battle`/`battleOrdinal` are `false/0` for slot 1 and `true/1` for slot 2. Without the `elite` block → always slot 1. Without `mystery` events and without `elite` → `hasFork == false`.
- **F3 AffixIsSeeded**: rooms `[battle, battle, battle]`, seeds 1..40: `affixId == affixes[EventPickHash(seed, 1, 1002) % 2].id`; both ids occur.
- **F4 ContinueTakesStandardTakeForkTakesVariant**: rooms `[battle, battle, battle]`, a seed with slot 1. After `WinRoom` + `SkipOfferIfActive`: `IsForkPending`. `ApplyRunIntent(ContinueReward)` → choice 0, `run.room == 1`, phase Battle. Second boot: `ApplyRunIntent(TakeFork)` → choice 1, `run.room == 1`. Third boot, offer still open after `WinRoom` (the fixture's `chance` is 1.0; `CHECK(offer.active)`): `TakeFork` fails, choice −1. Fourth: `TakeFork` right after `StartFresh` fails. Fifth, rooms `[battle, event, battle]` with slot 2 (battle after the event): in the event before a choice `TakeFork` fails; after `ChooseEventOption(0)` it succeeds and `run.room == 1`.
- **F5 EliteAffixScalesEnemies**: battle harness copied from `dungeon_sequence_tests.cpp:265` (`FBattleHarness`: constructor enters battle 0; `playBattle()`; `nextRoom()`). Rooms `[battle, battle, battle]`, a seed with slot 1 and `affix.hardened`. Steps: `ResolveRoomVictory(registry, true)`, `ResolveRuneOffer(registry, -1)` if an offer is active, `ApplyRunIntent(registry, ERunIntent::TakeFork)`, then leave and re-enter the battle state the way `nextRoom()` does (`OnExit` / `OnEnter`). Assert `game::FindRoomPlan(registry)->elite`, `affixId == "affix.hardened"`. Collect max HP per non-party unit (in entity creation order); a second harness with the same seed and `ERunIntent::ContinueReward` in that step gives the standard values; `elite[i] == standard[i] + standard[i] * 30 / 100`; party max HP equal in both. With a seed giving `affix.fierce`: enemy `attack` and `magicAttack` are standard + 1 and max HP equal.
- **F6 EliteVictoryPaysOfferAndMaterials**: `FBoot` flow (no battle): reach battle 1 through `TakeFork`; set `rewards.tables.chance = 0`, `rewards.pity = 1`, and WP-40 tables in place (`normal`, `boosted`, `boss`); erase the ctx `FBattleObjective` if present; `materials` before → `ResolveRoomVictory(true)` → delta `== DungeonBalance("room_materials") + 2`, `offer.active`, `offer.rarityTable == "boosted"`, `pity == 0`, message contains `Elite defeated: +2 materials`. A second boot on the standard branch under the same settings: delta `== room_materials`, no offer.
- **F7 MysteryForkPicksMysteryPool**: rooms `[battle, event, battle]`, no `elite` block (slot 1). Over seeds 1..40 with `TakeFork`: every picked event carries tag `common` or `mystery`, both tags occur, and each pick equals `PickEventId(mixed, "", {}, seed, 1, 0)` for `mixed` = the temp catalog's `common` and `mystery` events in catalog order (the built-in `event.quiet_shrine` is `common` and counts). Second boot, `ContinueReward` → tag `common`, and before it `ForkOptions()[0].title == "Event: " + <that event's title>`, `ForkOptions()[0].detail` contains both of its choice texts, `ForkOptions()[1].title == "? Unknown event"`.
- **F8 ForkSurvivesSaveAndContentChange**: (a) decide 1 on a battle fork, win the elite room so the phase is Reward, then `ForgetRunInMemory` and also assign `registry.ctx().get<game::FRunPathState>() = {}`; continue the run → state equals the saved one (slot, battle, affixId, choice 1), `IsEliteBattle(1)`, `game::FindRoomPlan(registry)->elite`. (b) event fork decided 1: the event id after continue equals the id before. (c) content change: after saving with a battle fork, boot a second temp root whose `dungeon.json` has no `elite` block and continue → `hasFork == false`, `IsEliteBattle(1) == false`. (d) set `ext.path` in the save file to `{"section_version": 1, "choice": 5}` (a section without a positive `section_version` fails the whole load, `FSaveSections.cpp:130`) → the run loads, the state is the default and re-derives (choice −1). (e) `StartRun` with the same seed after a decided fork → choice −1.
- **F9 StripMirrorsSlotsAndPhase**: rooms `[battle, event, battle, battle(boss final)]`, a seed with slot 2 and `affix.hardened`. At start labels are `Battle, Event, Battle, <boss label>, Camp` (the boss label starts with `Boss`; with a temp `bosses.json` mapping the slot it is `Boss: <name>`), node 0 current, `forkNode == 2`, `forkAlt->label == "Elite: Hardened"`. After `WinRoom`: node 0 done and current. In the event: node 1 current, node 0 done. After the event `TakeFork`: node 2 label `Elite: Hardened`, kind `Elite`, current, `forkAlt` empty. After `ContinueReward` on another boot: node 2 label `Battle`, `forkAlt` empty. In Camp: `Camp` current, all others done.
- **F10 StripDrawsLabels**: an ImGui context as in `RenderModalLogs` (surface, renderer, fonts built, `InitializeDungeonUi`); frames 1–3, on frame 3 `ImGui::LogToFile` around `game::DrawPathStrip(registry, 1268.f, 44.f, 1.f)`. With the F9 setup at start the log contains `[Battle]`, `Event`, `Battle / Elite: Hardened`, `Boss`, `Camp`, and `>` between nodes.
- **F11 ForkPanelShowsBothOptions**: reward modal capture with a pending battle fork (offer skipped) → contains `CHOOSE A PATH`, `Party HP `, `Materials `, `Elite: `, `Reward: rune offer (boosted), +2 materials`, `Standard rewards`; the text after `CHOOSE A PATH` does not contain `Continue`. Event modal capture: rooms `[battle, event, battle]`, a seed with slot 2 (the battle after the event), the event resolved → contains `CHOOSE A PATH` and `Elite: `. Reward modal capture before an event fork (rooms `[battle, event, battle]`, no `elite`) → contains `? Unknown event` and `Event: `.
- **F12 MaterialsRangeIsSeededAndBounded** (`event_framework_tests`): temp `events.json` with a `test`-pool event (the pool the fixture dungeon's helper `TestPoolDungeon` uses) whose choice 0 has `gain_materials_range amount=3 magnitude=5` twice: over seeds 1..60 the two gains are each in 3..5, all three values occur, and equal the formula with `rollIndex` 0 and 1. `magnitude < amount` → `BootsGameplay`-style boot false.
- **F13 MaterialsGambleUsesFourOutcomes**: `amount=7 magnitude=2`: over seeds 1..80 gains ⊆ {0, 2, 7}, each occurs, and each equals `{0,0,2,7}[roll % 4]`.
- **F14 PayMaterialsGatesAndCharges**: choice 0 `pay_materials amount=3` + `gain_materials amount=1`, choice 1 free. Materials 2 → `EvaluateEventGate` closed, reason `Need 3 materials (have 2)`, `ChooseEventOption(0)` fails, materials unchanged. Materials 3 → open, reason `Costs 3 materials`; after the choice materials == 1 and `run.message` contains `-2 materials`. An event whose two choices both pay → boot false.
- **F15 OpenRuneOfferOpensOrRefunds**: choice 0 `open_rune_offer pool=boosted amount=4`. After `ChooseEventOption(0)`: `offer.active`, `pity == 0`, message contains `rune offer`; `ApplyRunIntent(ContinueEvent)` fails; after `ResolveRuneOffer(-1)` it succeeds. Second boot with `tables.minLegalCandidates = tables.count = 99` → no offer, materials +4. Third boot: `ApplyRunIntent(ChooseEventRest)` (legacy, option 0) → succeeds, phase still Event, offer active. Fourth: `pool: "epic"` → boot false. Fifth (resume): resolve choice 0 (offer open), save, boot a root whose `events.json` lacks that event, continue the run → the offer is inactive and the phase left Event.
- **F16 ShippedEventsMatchTheTable** (`content_a_tests`): `ReadJsonFile("assets/data/events.json")`; a literal table of §4.3 — ids in order, tags, choice count, each `check` (stat, difficulty), each effect's `kind`/`amount`/`magnitude`/`pool`; a ship-root boot (`ShipAssetRoots()`) succeeds.
- **F19 HostAcceptsTakeFork** (`run_path_tests`, own temp-root harness): the host pattern of `run_modal_tests.cpp:266–268` — `game::PostRunIntent(registry, game::ERunIntent::TakeFork); game::FEmberModalHostState host(std::string(game::kRunModalReward), <the EHostMode::RunPhase value those lines pass>); host.Update(registry, 0.f);`. Rooms `[battle, battle, battle]`, a seed with slot 1, room 0 won and the offer skipped → after the update `EnsureRunPath().choice == 1` and `run.room == 1`. Rooms `[battle, event, battle]`, a seed with slot 2, the event resolved, host built with `kRunModalEvent` → `choice == 1`, `run.room == 1`.
- **F17 ForkRecordCarriesChoiceAndHp** (`run_log_tests`): that file's `FBoot` booted with a temp root (the roots parameter above; rooms `[battle, battle, battle]` plus the `elite` block, a seed with slot 1), room 0 won and the offer skipped, then `TakeFork` → exactly one `fork` record: `kind` starts with `elite:`, `choice == 1`, `0 < party_hp <= party_hp_max`. A `ContinueReward` boot → `kind == "battle"`, `choice == 0`.
- **F20 AutopilotForkPolicy** (`run_path_tests`): temp root, rooms `[battle, battle, battle]`, a seed with slot 1, room 0 won. Build the pilot and tick it the way `DriveAutopilot` does (`headless_tools_tests.cpp:757–793`; copy the construction and the tick-plus-host-update loop) with `kRunModalReward` on top, at most 10 ticks: policy from `ParseAutopilotPolicy("fork=variant")` → `EnsureRunPath().choice == 1`; default policy on a second boot → `choice == 0`; both end with `run.room == 1`.
- **F18 AutopilotResolvesEventOffer** (`headless_tools_tests`): `ParseAutopilotPolicy` accepts `fork=variant` (→ `forkVariant`) and `fork=standard`, rejects `fork=x`. With `FHeadlessRunHarness` (`l.606`; its constructor has already started the run), drive by direct calls to a resolved event (`ResolveRoomVictory(true)`, `ResolveRuneOffer(-1)` if active, `ApplyRunIntent(ContinueReward)`, `ApplyRunIntent(ChooseEventOption, 0)`), call `game::OpenRuneOffer(registry, "normal")` and `CHECK` it returned true, then tick the pilot with `kRunModalEvent` as `DriveAutopilot` (`l.757`) ticks it, at most 10 ticks, applying posted intents the way that helper does → the offer is inactive, the phase is no longer Event, no tick returned `Stuck`.
- **compendium_tests l.481** (existing assertion, edited): the status row string is `"DEPTH 01"`; the check keeps its meaning (the fixed HUD string fits the zone).

Existing assertions keep their meaning otherwise; no timeout changes; no check is dropped.

## 8. Decisions
- Resolved: no new run phase — the fork is decided in the modal that precedes the fork slot, and plain Continue means the standard branch. `EDungeonPhase`, the core save schema, the state catalog and every existing caller of `ContinueReward`/`ContinueEvent` stay as they are.
- Resolved: slot 0 is never a fork (it follows the camp, which has its own modal and intent).
- Resolved: a slot is eligible only when its variant exists (affix list / `mystery` pool), so fixture boots have no fork.
- Resolved: the fork is derived once per dungeon and saved whole (slot, kind, affix, choice) — a content change under a save can then only drop the fork, never move it onto a room the player did not choose.
- Resolved: the plan's `elite`/`affixId` are stamped in `BuildDungeonBattlefield`, not in `FrameRoomPlan` — `FRunSequence` stays a leaf module.
- Resolved: `gain_materials_gamble` is a fourth effect — the content schema has no number-array field, and the 0/0/2/7 jackpot shape is the point of `Ash Gambler` (criterion: anticipation and variance).
- Resolved: `open_rune_offer.amount` is the fallback materials when no offer can open — a paid or bled-for offer never pays nothing.
- Resolved: the path strip is one text line in its own window under the status zone — the 260 px status zone cannot hold six rooms, ImGui text is capturable in tests, and no layout-schema change is needed.
- Resolved: branch rewards are written in the fork panel, not on the strip (plan §5.1 put them on the strip) — the panel is where the choice is made and the strip is one line over the board.
- Resolved: the strip may overlap the board's top-right corner (it has no background and takes no input); no overlap test.
- Resolved: the Confirm key is inert while a fork panel is up, so every UI fork record is a deliberate click.
- Resolved: the autopilot takes the standard branch by default (`fork=variant` is opt-in), so existing simulation baselines stay comparable while elite numbers can be measured.
- Resolved (user ruling 2026-11-11, recorded in `decisions.md`): "?" draws from the slot's pool and `mystery` together, not from `mystery` alone as plan §5.3 said — with `mystery` alone "?" beat the named event in nearly every state. The kill condition is still judged per fork kind, and the named branch lists its choices.
- Alternatives considered: a `Fork` phase with its own modal — rejected (save validation, state catalog, modal registry, autopilot key for one click). Saving only the choice and re-deriving — rejected (content change moves the fork). Stamping in `FrameRoomPlan` — rejected (upward dependency, const change). A layout zone for the strip — rejected (schema change for a position). Stacked draw-list chips — rejected (not capturable by the text log tests).
- Defaults for anything unspecified: follow the nearest existing code in the same file; player-facing text is English ASCII.
- Follow-ups (not this WP): a per-dungeon materials metric in the simulation (plan §6); an autopilot policy for event forks beyond `fork=variant`.
- Out of scope: ambush/treasure rooms, enemy-count or speed affixes, node points or random rune grants from events, changing shipped room lists, chip art, balance tuning, affix display inside the battle HUD beyond the strip label.

## 9. Risks and pitfalls
| Risk | Mitigation |
|---|---|
| Content changes under a save move the fork | The fork is saved whole and validated; a mismatch drops it (F8c) |
| Per-frame / per-enemy cost of fork queries | State is cached; only the derivation (once per dungeon) and the fork panel read the catalog |
| An open event offer strands a flow that continues in one step | `ChooseEventLegacy` and the resume path are specified and tested (F15) |
| Elite and event numbers ship untuned | `fork=variant` autopilot policy; the wipe-after-elite kill condition |
| Materials inflation from events and elites | Simulation metric "materials per dungeon"; values are knobs |
- `run.room` is the battle ordinal, not a slot index; an event borrows the ordinal of the battle before it. Convert through `CurrentSlotIndex`.
- The registry context is a dense map: any `emplace`/`insert_or_assign` invalidates held ctx pointers. `EnsureRunPath` never inserts (the state is emplaced at registration); keep it that way.
- Save sections unpack before the run is installed: the `path` unpack must not read `FDungeonRun`.
- `DungeonBalance` throws on a missing key — elite numbers live in `FRunSequence::elite`, never in `balance`.
- `BuildDungeonBattlefield` also runs on Reward/Event/Camp continues; the elite stamp must depend only on the saved state.
- The modal log capture records frame 3 only and only ImGui widgets (not `ImDrawList::AddText`); ImGui math operators are disabled: build `ImVec2` component-wise.
- A new feature-test file needs a reconfigure; one build/ctest at a time; MSB3073/MSB3075 is a transient lock: rerun once.
- `features.json` `allowCheckDrop` is a list.

## 10. Notes for features.json
List F1–F20 in numeric order. `files` = every path of §5, including `src/states/FEmberModalHostState.cpp`, `src/dev/FRunAutopilot.h`, `src/dev/FRunAutopilot.cpp`, `docs/design/combat-math.md`, `docs/wp/WP-41/progress.md`, `docs/wp/WP-41/features.json`.

## 11. Open questions
None.
