#include "states/FEmberMenuRequest.h"

#include "gameplay/dungeon/FDungeonRun.h"
#include "states/FEmberBattleState.h"
#include "states/FEmberPreparationState.h"

std::optional<FStateTransition> ResolveEmberMenuRequest(entt::registry& registry) {
    auto* request = registry.ctx().find<game::FMenuRequest>();
    if (request == nullptr) return std::nullopt;
    switch (request->value) {
    case game::EMenuRequest::Quit:
        return FStateTransition{EStateChangeKind::Quit, {}};
    case game::EMenuRequest::Start: {
        // The title never starts a run: it hands the fresh flag to the preparation step through the carrier.
        registry.ctx().insert_or_assign<game::FRunStartRequest>(game::FRunStartRequest{request->fresh});
        *request = {};
        return FStateTransition{EStateChangeKind::Root, std::string(kEmberPrepareId)};
    }
    case game::EMenuRequest::Continue: {
        const bool resumed = game::ContinueDungeonRun(registry);
        *request = {};
        if (resumed) return FStateTransition{EStateChangeKind::Root, std::string(kEmberBattleId)};
        return std::nullopt;
    }
    case game::EMenuRequest::None:
        break;
    }
    return std::nullopt;
}
