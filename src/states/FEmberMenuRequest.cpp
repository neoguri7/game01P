#include "states/FEmberMenuRequest.h"

#include <SDL3/SDL.h>

#include "gameplay/dungeon/FDungeonRun.h"
#include "states/FEmberBattleState.h"

std::optional<FStateTransition> ResolveEmberMenuRequest(entt::registry& registry) {
    auto* request = registry.ctx().find<game::FMenuRequest>();
    if (request == nullptr) return std::nullopt;
    switch (request->value) {
    case game::EMenuRequest::Quit:
        return FStateTransition{EStateChangeKind::Quit, {}};
    case game::EMenuRequest::Start:
        game::StartDungeonRun(registry, static_cast<std::uint32_t>(SDL_GetTicks()), request->fresh);
        *request = {};
        return FStateTransition{EStateChangeKind::ReplaceTop, std::string(kEmberBattleId)};
    case game::EMenuRequest::Continue: {
        const bool resumed = game::ContinueDungeonRun(registry);
        *request = {};
        if (resumed) return FStateTransition{EStateChangeKind::ReplaceTop, std::string(kEmberBattleId)};
        return std::nullopt;
    }
    case game::EMenuRequest::None:
        break;
    }
    return std::nullopt;
}
