#pragma once

#include <entt/entt.hpp>

namespace game {

/// The settings modal (WP-24a): battle speed 1x/2x and skip battle animations. Registered under
/// `kEmberSettingsId` in FTitleMenuView.cpp and drawn by FEmberModalHostState (Menu mode); Close asks
/// the host to pop. Every change saves immediately through the FSettings API.
void RenderSettingsView(entt::registry& registry);

} // namespace game
