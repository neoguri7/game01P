#pragma once

#include <entt/entt.hpp>

namespace game::gameplay {

/// The one place that decides which gameplay services exist in the registry context (R21/R5). Called
/// from the composition root (`src/main.cpp`), never from `core/` or `ecs/` — those layers must not
/// know that a gameplay layer exists (R7a).
///
/// Content is loaded here so that a broken asset stops the boot instead of surfacing as a missing skill
/// mid-fight (R19/R22). Systems read `FContentRegistry` from the context; they never see JSON.
struct FGameplayServices {
    /// False means "the gameplay layer cannot run" (content unusable or asset boundary missing). The
    /// reason is already logged.
    [[nodiscard]] static bool Initialize(entt::registry& registry);

    static void Shutdown(entt::registry& registry);
};

} // namespace game::gameplay
