#pragma once

#include <optional>

#include <entt/entt.hpp>

#include "states/FBaseState.h"

/// The title and hub screens share one set of menu buttons; this turns the pressed one into a state transition.
/// Continue loads the saved expedition. When the save is unusable it stays on the menu, where
/// FDungeonSave::error explains why.
[[nodiscard]] std::optional<EStateTransition> ResolveEmberMenuRequest(entt::registry& registry);
