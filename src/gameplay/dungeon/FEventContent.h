#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <entt/entt.hpp>

#include "gameplay/data/FEffectContent.h"

namespace game {

struct FAssetManager;

/// `data/events` schema version (WP-09 §1).
inline constexpr int kEventSchemaVersion = 1;
/// Choices per event: 2..4 (R3 P14 — every event keeps a real decision; the loader refuses 1 or 5+).
inline constexpr std::size_t kEventMinChoices = 2;
inline constexpr std::size_t kEventMaxChoices = 4;
/// `check.difficulty` bounds.
inline constexpr int kEventCheckDifficultyMin = 1;
inline constexpr int kEventCheckDifficultyMax = 30;
/// The gate stat vocabulary = FStatUpgrade::stat (FDungeonRun.h) — the six stats the camp reinforces.
inline constexpr std::string_view kEventCheckStats[] = {
    "health", "attack", "magic_attack", "defense", "magic_defense", "speed"};

/// The code-built seed (WP-09 §6): the former hard-coded shrine/cache choice, now one event of the same
/// pipeline. Its id/pool are constants so the boot validation can see them before data/dungeon is loaded.
inline constexpr std::string_view kLegacySeedEventId = "event.quiet_shrine";
inline constexpr std::string_view kLegacySeedPool = "common";

/// Requirement gate (WP-09 §2): the choice is selectable only when the party's best `stat` >= difficulty.
/// There is no failure branch — a gate never produces a "known to fail" option.
struct FEventCheck {
    std::string stat;
    int difficulty{0};
};

struct FEventChoice {
    std::string text;
    std::optional<FEventCheck> check;
    std::vector<gameplay::FEffectSpec> effects;  // Run-scope kinds only (FEventEffects.h)
    std::string resultText;                      // single Text (no success/failure union)
};

struct FEventDef {
    std::string id;
    std::string title;
    std::string body;
    std::vector<std::string> tags;  // pools this event belongs to (a slot's event_pool matches one tag)
    std::vector<FEventChoice> choices;
};

/// The authored events (`data/events`), ctx-owned, loaded once at boot. The seed is NOT stored here: it is
/// rebuilt from `data/dungeon balance`/`recovery` on every catalog read, so the numbers have one canonical
/// source (D-3) and a re-initialized dungeon is reflected immediately.
struct FEventTable {
    std::vector<FEventDef> events;
};

/// Structural load (FAssetManager boundary + recursive unknown-key rejection, WP-26): schema_version == 1,
/// unique non-empty ids, title/body present, 2..4 choices, every effect `kind` non-empty, `check.stat` in
/// kEventCheckStats, `check.difficulty` in [min, max], unknown keys refused (also inside `check`/`effects`).
/// **Zero events is valid** (`{"schema_version":1,"events":[]}` is the W9 shipped file). nullopt = unusable
/// (the reason is logged). A missing file reads as zero authored events + one LOG_WARN (the optional-table
/// precedent of the engine ignition data): the seed alone is the W9 event content, and content-only test roots
/// that predate WP-09 carry no events file. A present-but-broken file is unusable.
[[nodiscard]] std::optional<std::vector<FEventDef>> LoadEventDefs(const FAssetManager& assets);

/// LoadEventDefs + installs (replaces) FEventTable. False = boot stops.
[[nodiscard]] bool LoadEventContent(entt::registry& registry, const FAssetManager& assets);

/// The seed event (one entry): `event.quiet_shrine`, pool `common`. Needs the dungeon definition
/// (InitializeDungeon): choice 0 = rest (`restore_party_hp event_heal`), choice 1 = cache
/// (`gain_materials event_materials` + `damage_party_pct round(recovery.event_chest_hp_cost_fraction × 100)`
/// when the fraction is > 0). Texts are generated from the same values (the pre-WP-09 view strings).
[[nodiscard]] std::vector<FEventDef> BuildLegacySeedEvents(entt::registry& registry);

/// Seed first, then FEventTable in file order — the pool registration order the pick indexes into.
[[nodiscard]] std::vector<FEventDef> EventCatalog(entt::registry& registry);

/// A copy of one catalog event, or nullopt when the id is not in the catalog.
[[nodiscard]] std::optional<FEventDef> FindEventDef(entt::registry& registry, std::string_view id);

/// True when `pool` is empty (= every event) or one of the event's tags equals it.
[[nodiscard]] bool EventInPool(const FEventDef& event, std::string_view pool);

/// Cross validation (WP-09 §1 ②), the last statement of the [WP-09] boot block: every effect kind is
/// registered with Run scope in FEffectRegistry and its registered validator accepts the spec; gated choices
/// per event <= choices - 1; and — reading `data/dungeon` straight through the asset boundary (like
/// ValidateSkillApCap) — every slot `type` is battle or a registered room type, every slot of a registered
/// type carries that type's requiredSlotKey, and every referenced event_pool intersects (seed ∪ authored).
/// A content-only boot without `data/dungeon` skips the slot checks. False = boot stops (all offenders logged).
[[nodiscard]] bool ValidateEventContent(entt::registry& registry, const FAssetManager& assets);

} // namespace game
