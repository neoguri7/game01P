#pragma once

#include <cstddef>
#include <string>

#include <entt/entt.hpp>

#include "gameplay/dungeon/FDungeonRun.h"

namespace game {

/// The shared helpers and view entry points for the dungeon run modals. The four views are registered in
/// FModalViewRegistry.cpp from InitializeDungeonUi; a later WP rewrites the bodies in place.

struct FRewards;
namespace gameplay {
struct FContentRegistry;
struct FRuneContent;
}

[[nodiscard]] const std::string& MemberName(const FRunMember& member);

/// WP-27: one rule for reading a content row's display name (id fallback) — the reward modal and the camp
/// loadout list must name skills and runes the same way. The row pointer may be null.
template <typename TRow>
[[nodiscard]] std::string ContentDisplayName(const TRow* row, const std::string& id) {
    return row != nullptr && !row->displayName.empty() ? row->displayName : id;
}

/// WP-27: the card's effect line — the rune's deltas plus its data-authored hooks ("On hit: 2 damage to 1
/// other enemy within 1 tile of the target", WP-27h). Status ids resolve to display names through `content`
/// (id fallback; `content` may be null).
[[nodiscard]] std::string RuneEffectSummary(
    const gameplay::FRuneContent& rune, const gameplay::FContentRegistry* content);

/// The growth readout (level, EXP, tree bonuses and passive tree) for one party member. Reward and camp
/// both draw it; the definition lives in FDungeonRewardView.cpp so there is one copy.
void DrawGrowthPanel(entt::registry& registry, const FDungeonGrowth& growth, FDungeonRun& run, std::size_t memberIndex);

/// The shared leave/abandon confirm. `allowLeave` shows "Save and leave" (the battle offers it); the camp
/// offers only the abandon. `viaIntent` posts the run intent instead of setting the run's flags (the camp
/// host owns its phase; the battle owns its flags). The confirm owns the "keep exploring" escape and clears
/// the flag itself.
void DrawAbandonConfirm(entt::registry& registry, bool allowLeave, bool viaIntent = false);

/// WP-17: the one-line offer forecast ("next battle room rune offer: guaranteed (pity) / chance 70%"). The
/// definition lives in FDungeonRewardView.cpp so the reward and camp screens show the same sentence.
void DrawRuneOfferForecast(entt::registry& registry);

/// WP-17/WP-10: the offer cards plus the skip button (the definition lives in FDungeonRewardView.cpp). The
/// camp reuses it when a camp-purchased offer is open — one decision UI, not two. `content` may be null
/// (the card then shows the raw ids); a pick or a skip resolves the offer in place.
void DrawRuneOffers(entt::registry& registry, FRewards& rewards, const gameplay::FContentRegistry* content);

void RenderDungeonRewardView(entt::registry& registry);
void RenderDungeonEventView(entt::registry& registry);
void RenderDungeonCampView(entt::registry& registry);
void RenderDungeonDefeatView(entt::registry& registry);

} // namespace game
