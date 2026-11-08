#include "states/FEmberPreparationState.h"

#include <SDL3/SDL.h>

#include <cstdint>

#include "core/Logger.h"
#include "gameplay/dungeon/FDungeonRun.h"
#include "gameplay/dungeon/FRunFlow.h"
#include "gameplay/dungeon/FRunSeed.h"
#include "states/FEmberBattleState.h"
#include "states/FEmberTitleState.h"

FStateTransition FEmberPreparationState::Update(entt::registry& Registry, float DeltaTime)
{
    (void)DeltaTime;
    // WP-21 rewrites this stub into the real preparation screen. The carrier is consumed exactly once and
    // a missing carrier is "start from the checkpoint" (fresh=false), which is the safe default.
    bool fresh = false;
    if (auto* request = Registry.ctx().find<game::FRunStartRequest>()) {
        fresh = request->fresh;
        Registry.ctx().erase<game::FRunStartRequest>();
    } else {
        LOG_WARN("Preparation started without a run-start request; starting from the checkpoint.");
    }

    if (Registry.ctx().contains<game::FDungeonRun>()) {
        LOG_ERROR("Preparation found a run already installed; returning to the title instead of starting a second run.");
        return {EStateChangeKind::Root, std::string(kEmberTitleId)};
    }
    // WP-28 A3: GAME01P_SEED overrides the ticks seed; a retry-from-checkpoint in the same process
    // advances by one (see FRunSeed.h).
    game::StartRun(Registry, game::ResolveRunSeed(static_cast<std::uint32_t>(SDL_GetTicks())), fresh);
    return {EStateChangeKind::Root, std::string(kEmberBattleId)};
}
