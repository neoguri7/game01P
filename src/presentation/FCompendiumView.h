#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <entt/entt.hpp>

#include "states/FEmberModalHostState.h"

namespace game {

namespace gameplay {
struct FContentRegistry;
struct FUnitContent;
} // namespace gameplay

/// The modal view id. The catalog host id lives in FEmberModalHostState.h (WP-02b); this alias keeps the
/// title registration line self-describing and cannot drift from the state catalog.
inline constexpr std::string_view kCompendiumModalId = kEmberCompendiumId;

/// Four tabs, all fully public: the compendium is a learning library (StS card-library model), so nothing
/// is gated by encounters and the enemy tab shows no appearance depth.
enum class ECompendiumTab { Skills, Runes, Statuses, Enemies };

inline constexpr int kCompendiumTabCount = 4;

/// One row of a tab: an id for stable selection, a title, a free-text body and label/value facts.
struct FCompendiumEntry {
    std::string id;
    std::string title;
    std::string body;
    std::vector<std::pair<std::string, std::string>> facts;
};

/// Builds a tab from FContentRegistry — never from hand-authored rows, so new content appears automatically.
[[nodiscard]] std::vector<FCompendiumEntry> BuildCompendiumTab(entt::registry& registry, ECompendiumTab tab);

/// The tab's English display name (the body font has no Hangul glyphs).
[[nodiscard]] const char* CompendiumTabName(ECompendiumTab tab);

/// Enemy = a unit template with an empty `class` field. Playable units always carry warrior/archer/mage;
/// the healer is a mage-class playable unit, not an enemy.
[[nodiscard]] bool IsEnemyUnit(const gameplay::FUnitContent& unit);

/// Role from the unit's skill profile, first match wins:
/// Support (any Heal or Support skill tag) > Zone (any zone-shaped skill) > Ranged (max range >= 2) >
/// Melee. Documented priority; the enemy tab shows this role, never an appearance depth.
[[nodiscard]] std::string DeriveUnitRole(const gameplay::FContentRegistry& content,
                                         const gameplay::FUnitContent& unit);

/// The modal view (registered next to the settings view in FTitleMenuView.cpp).
void RenderCompendiumView(entt::registry& registry);

} // namespace game
