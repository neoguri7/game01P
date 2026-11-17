#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <entt/entt.hpp>
#include <nlohmann/json.hpp>

#include "gameplay/dungeon/FRoomGenerator.h"

namespace game {

struct FAssetManager;
struct FRunSequence;
struct FRunDungeonDef;

namespace gameplay {
struct FContentRegistry;
}

/// WP-14a: encounter composition (`assets/data/encounters.json` — an OPTIONAL file). Absent = the legacy
/// `generation.enemies` path (nothing in ctx); present = strict validation, a bad file stops the boot; an
/// empty `encounters` array is a valid "no composition" file (the frozen test fixture ships one so the
/// fixture keeps the legacy path although the ship root is an overlay fallback). Boss slots never use
/// encounters (WP-15 keeps `generation.boss_unit`).

/// The closed role set (code, not data — an unknown role is a boot error). A role is authored as a skill
/// combination, never as an AI profile: every unit of a role slot's pool must carry a skill that satisfies
/// the role predicate (FEncounterContent.cpp).
inline constexpr std::string_view kEncounterRoleMelee = "melee";
inline constexpr std::string_view kEncounterRoleRanged = "ranged";
inline constexpr std::string_view kEncounterRoleSupport = "support";
inline constexpr std::string_view kEncounterRoleArtillery = "artillery";

/// One weighted unit choice of a pool (`slots[].units[]`, `fill.units[]`).
struct FEncounterUnit {
    std::string unit;
    int weight{1};
};

/// One role slot: the generator places exactly one unit of `units` (weighted pick) for it.
struct FEncounterSlot {
    std::string role;
    std::vector<FEncounterUnit> units;
};

/// One authored encounter (`encounters[]`).
struct FEncounterDef {
    std::string id;
    int introducedAtDepth{1};          ///< Eligible from this run depth on (1-based, the dynamic pool gate).
    std::vector<std::string> tags;     ///< Theme ids it may appear in; empty = every theme.
    gameplay::FIntRange count;         ///< Total enemies; min >= slots.size(), the rest come from `fill`.
    std::vector<FEncounterSlot> slots; ///< Role slots, in placement order (slot 0 = the leader, `enemy.1`).
    std::vector<FEncounterUnit> fill;  ///< Pool for enemies beyond the role slots (required if max > slots).
    int intentCommitMix{1};            ///< Minimum count of `artillery` slots (>= 1, P14-5 / D-16).
};

/// The installed table (ctx). `schemaVersion` is the file's `schema_version`.
struct FEncounterContent {
    int schemaVersion{1};
    std::vector<FEncounterDef> encounters;

    [[nodiscard]] const FEncounterDef* Find(std::string_view id) const;
};

/// P14-6 — the SINGLE definition of the room plan (ctx, never saved). Emplaced only by the real room-entry
/// path (BuildDungeonBattlefield) right after the document is built; the boot probe never touches it.
/// WP-19b reads `encounterId` / `family` / `elite`. 14a fills `encounterId` ("" = legacy pool or boss slot)
/// and `family` (the slot's effective family); `elite`/`affixId`/`leaderId` stay default until WP-14b.
struct FRoomPlan {
    std::string encounterId;
    std::string family;
    bool elite{false};
    std::string affixId;
    std::string leaderId;
};

/// The plan of the room being fought (nullptr before the first real battlefield build).
[[nodiscard]] const FRoomPlan* FindRoomPlan(const entt::registry& registry);

/// The installed encounter table (nullptr = file absent → legacy generation path).
[[nodiscard]] const FEncounterContent* FindEncounterContent(const entt::registry& registry);

/// Parses one encounters document and checks its STRUCTURE (schema_version, required keys and types, known
/// roles, weights >= 1, count range sane and >= slot count, fill present when needed, unique ids,
/// introduced_at_depth >= 1, intent_commit_mix >= 1 and <= artillery slot count). No content lookups.
/// `error` names the offending encounter/row.
[[nodiscard]] std::optional<FEncounterContent> ParseEncounterContent(const nlohmann::json& document,
                                                                     std::string& error);

/// Cross-checks a parsed table against the unit/skill content: every unit id exists, every pool unit
/// satisfies its slot's role predicate (artillery = carries a tile-commit skill by the FIntent mode rule),
/// and fill units exist. `error` names the encounter, slot and unit.
[[nodiscard]] bool CheckEncounterContent(const FEncounterContent& table, const gameplay::FContentRegistry& content,
                                         std::string& error);

/// [WP-14a] boot anchor, step 1: loads the optional `data/encounters` asset into ctx. Absent = true and
/// nothing installed; present but unparsable = false (errors logged).
[[nodiscard]] bool LoadEncounterContent(entt::registry& registry, const FAssetManager& assets);

/// [WP-14a] boot anchor, step 2: CheckEncounterContent against the ctx FContentRegistry (true when absent).
[[nodiscard]] bool ValidateEncounterContent(entt::registry& registry);

/// WP-50: the dynamic pool gate, exported (was file-local): introducedAtDepth <= depth AND (tags empty OR
/// tags contain themeId).
[[nodiscard]] bool EncounterEligible(const FEncounterDef& encounter, std::string_view themeId, int depth);

/// WP-50: the enemy unit ids `dungeon` can field at `depth` in `themeId`, unique, in first-seen order. Walks
/// the non-boss battle slots in order: a slot naming an `encounter` contributes that encounter whatever its
/// eligibility (boot validation already requires it eligible; an unknown id contributes nothing); an unnamed
/// slot contributes every eligible encounter in table order. Per encounter: the slot pools in slot order
/// (units in pool order), then `fill`. Boss slots contribute nothing.
[[nodiscard]] std::vector<std::string> DungeonThreatUnits(const FEncounterContent& table,
                                                          const FRunDungeonDef& dungeon,
                                                          std::string_view themeId, int depth);

/// Deterministic encounter choice. A non-empty `encounterId` selects that encounter (nullptr if unknown).
/// Otherwise: the eligible pool (introducedAtDepth <= depth AND (tags empty OR tags contains themeId)), in
/// file order, one uniform pick from an FRandom stream mixed from (seed, depth, room) with its own salt —
/// independent of the room generator's stream, so terrain stays identical. nullptr = nothing eligible.
[[nodiscard]] const FEncounterDef* ResolveEncounter(const FEncounterContent& table, std::string_view encounterId,
                                                    std::string_view themeId, std::uint32_t seed, int depth,
                                                    int room);

/// Dungeon-init check (non-empty tables only): every non-boss battle slot of every dungeon in the sequence
/// (depth = dungeon index + 1, its theme) resolves — a named `encounter` must exist and be eligible there;
/// an unnamed slot needs >= 1 eligible encounter. `error` names the dungeon and slot.
[[nodiscard]] bool ValidateEncounterCoverage(const FEncounterContent& table, const FRunSequence& sequence,
                                             std::string& error);

/// Writes `encounter`'s composition into a room-config COPY: `roleSlots` (one per slot, display names from
/// the unit content), `fill`, and `enemyCount = encounter.count`. Placement stays the generator's job.
void ApplyEncounter(gameplay::FRoomGenConfig& config, const FEncounterDef& encounter,
                    const gameplay::FContentRegistry& content);

} // namespace game
