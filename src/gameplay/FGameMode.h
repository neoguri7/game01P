#pragma once

#include <entt/entt.hpp>

namespace game {

enum class EGameMode { Forest, Ember };

struct FGameMode {
    EGameMode value{EGameMode::Forest};
};

[[nodiscard]] inline bool IsEmberMode(const entt::registry& registry) {
    const auto* mode = registry.ctx().find<FGameMode>();
    return mode && mode->value == EGameMode::Ember;
}

} // namespace game
