#pragma once

#include <entt/entt.hpp>

namespace game {
class SystemManager;
} // namespace game

namespace game::gameplay {

/// The one place that decides which gameplay services exist in the registry context (R21/R5). Called
/// from the composition root (`src/main.cpp`), never from `core/` or `ecs/` — those layers must not
/// know that a gameplay layer exists (R7a).
///
/// Content is loaded here so that a broken asset stops the boot instead of surfacing as a missing skill
/// mid-fight (R19/R22). Systems read `FContentRegistry` from the context; they never see JSON.
struct FGameplayServices {
    /// False means "the gameplay layer cannot run" (content unusable, asset boundary missing, or the battle could
    /// not be spawned). The reason is already logged.
    [[nodiscard]] static bool Initialize(entt::registry& registry);

    /// Registers the battle systems in pipeline order. why the order is explicit and commented: an event produced
    /// by one system is consumed by the next ones *in the same frame* (FEventBus::queueFrame), so the order is
    /// part of the turn loop's contract, not an implementation detail (R21/R16④). Adding an AI slice means
    /// replacing FEnemyTurnSystem's entry, not reordering this list.
    static void RegisterSystems(game::SystemManager& systems);

    static void Shutdown(entt::registry& registry);
};

} // namespace game::gameplay
