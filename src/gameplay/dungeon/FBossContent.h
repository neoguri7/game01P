#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <entt/entt.hpp>
#include <nlohmann/json.hpp>

#include "gameplay/data/FContentRegistry.h"

namespace game {

/// WP-15 bosses.json — the multi-stage boss catalog (schema_version 1). Data says WHAT (phases, gate, parts);
/// code interprets HOW (FBossGoals.h). No string condition DSL.
inline constexpr int kBossCatalogSchemaVersion = 1;
inline constexpr std::string_view kBossGateParts = "parts"; ///< the only gate kind
inline constexpr std::string_view kBossSlotMid = "mid";
inline constexpr std::string_view kBossSlotFinal = "final";
/// Upper bound on the parts one boss spawns (all part rows summed): they share the 3-row spawn band with the
/// boss and its escorts.
inline constexpr int kMaxBossParts = 6;

struct FBossPhaseDef {
    int hpAbove{0};       ///< 0..100, strictly descending across phases; the last phase is 0
    bool partsGate{false}; ///< `"gate": "parts"`; absent = no gate. At most one gated phase per boss
    std::vector<std::string> bodyImmunities; ///< known status ids
    std::string notes;
};

struct FBossPartDef {
    std::string unit;  ///< a units.json id
    std::string name;  ///< display name (English — the HUD font has no Hangul)
    int count{1};      ///< >= 1; the sum over all rows <= kMaxBossParts
};

struct FBossDef {
    std::string id;
    std::string name;
    std::string unit;  ///< the body, a units.json id
    std::vector<FBossPhaseDef> phases;
    std::vector<FBossPartDef> parts;
};

struct FBossMapEntry {
    std::string theme; ///< a dungeon.json theme id
    std::string slot;  ///< "mid" | "final"
    std::string boss;  ///< a bosses[] id (several slots may name the same boss)
};

/// Boot content (registry context). An empty catalog = every boss slot uses the single-phase fallback
/// (`generation.boss_unit`, the pre-WP-15 guardian path).
struct FBossCatalog {
    std::vector<FBossMapEntry> map;
    std::vector<FBossDef> bosses;

    [[nodiscard]] const FBossDef* Find(std::string_view id) const;
    /// The boss mapped to (theme, slot), or nullptr = unmapped (fallback).
    [[nodiscard]] const FBossDef* Resolve(std::string_view theme, std::string_view slot) const;
};

/// Parses and validates a bosses.json document. Rules: schema_version 1; map rows name a known theme
/// (`themeIds`), slot in {mid, final}, a known boss, no duplicate (theme, slot); boss ids unique; body and
/// part units exist in `content.units`; immunity ids exist in `content.statuses`; phases non-empty, hp_above
/// in 0..100 strictly descending with the last == 0; gate absent or "parts"; at most one gated phase; a gated
/// phase needs hp_above > 0 and at least one part; part count >= 1 and the sum <= kMaxBossParts.
/// On failure `error` names the first broken rule and the result is nullopt. Never throws.
[[nodiscard]] std::optional<FBossCatalog> LoadBossCatalog(
    const nlohmann::json& document,
    const gameplay::FContentRegistry& content,
    std::span<const std::string> themeIds,
    std::string& error);

/// The InitializeDungeon boot block: resolves the optional asset `data/bosses` through FAssetManager
/// (`resolvePath` — absence is not an error). Absent -> installs an empty catalog (all fallback) and returns
/// true. Present but unreadable/invalid -> logs the reason and returns false (the boot stops). Valid ->
/// installs the catalog. Needs FContentRegistry and FRunSequence (theme ids) in the context; re-init safe
/// (replaces any previous catalog).
[[nodiscard]] bool InstallBossCatalog(entt::registry& registry);

/// The installed catalog (nullptr before InitializeDungeon).
[[nodiscard]] const FBossCatalog* FindBossCatalog(const entt::registry& registry);

} // namespace game
