#pragma once

#include <cstddef>
#include <string>

#include <entt/entt.hpp>

#include "gameplay/dungeon/FDungeonRun.h"

namespace game {

/// The shared helpers and view entry points for the dungeon run modals. The four views are registered in
/// FModalViewRegistry.cpp from InitializeDungeonUi; a later WP rewrites the bodies in place.

[[nodiscard]] const std::string& MemberName(const FRunMember& member);

/// The growth readout (level, EXP, tree bonuses and passive tree) for one party member. Reward and camp
/// both draw it; the definition lives in FDungeonRewardView.cpp so there is one copy.
void DrawGrowthPanel(entt::registry& registry, const FDungeonGrowth& growth, FDungeonRun& run, std::size_t memberIndex);

/// The shared leave/abandon confirm. `allowLeave` shows "Save and leave" (the battle offers it); the camp
/// offers only the abandon. `viaIntent` posts the run intent instead of setting the run's flags (the camp
/// host owns its phase; the battle owns its flags). The confirm owns the "keep exploring" escape and clears
/// the flag itself.
void DrawAbandonConfirm(entt::registry& registry, bool allowLeave, bool viaIntent = false);

void RenderDungeonRewardView(entt::registry& registry);
void RenderDungeonEventView(entt::registry& registry);
void RenderDungeonCampView(entt::registry& registry);
void RenderDungeonDefeatView(entt::registry& registry);

} // namespace game
