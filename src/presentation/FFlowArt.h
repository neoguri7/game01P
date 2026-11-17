#pragma once

#include <array>
#include <string>
#include <string_view>

#include <entt/entt.hpp>

#include "gameplay/dungeon/FRunBriefing.h"
#include "presentation/FUiTheme.h"

namespace game {

/// The pictures this WP draws (asset ids, PNG under assets/): seven route icons 48x48, two theme banners
/// 384x160, the boss plate 512x96 (docs/wp/WP-50/flow-art.json).
inline constexpr std::array<std::string_view, 10> kFlowArtIds{
    "art/ui/node_start", "art/ui/node_battle", "art/ui/node_elite", "art/ui/node_event", "art/ui/node_camp",
    "art/ui/node_boss", "art/ui/node_unknown", "art/ui/theme_ember_crypt", "art/ui/theme_ashen_reach",
    "art/ui/boss_plate"};
inline constexpr std::string_view kBossPlateId = "art/ui/boss_plate";

/// Entrance -> "art/ui/node_start", Battle -> node_battle, Elite -> node_elite, Event -> node_event,
/// Mystery -> node_unknown, Boss -> node_boss, Camp -> node_camp.
[[nodiscard]] std::string_view NodeIconId(EBriefingNodeKind kind);
/// "art/ui/theme_" + the theme id without its "theme." prefix ("theme.ember_crypt" -> "art/ui/theme_ember_crypt");
/// an id without the prefix is appended whole.
[[nodiscard]] std::string ThemeBannerId(std::string_view themeId);
/// A picture by asset id as an ImGui texture with its native size. Empty (invalid) piece without
/// FAssetManager or FResourceManager in the context, for an empty id, or when the picture cannot be loaded.
[[nodiscard]] ui::FSkinPiece FlowPicture(entt::registry& registry, std::string_view assetId);

} // namespace game
