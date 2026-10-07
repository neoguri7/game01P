#include "FEmberTitleState.h"
#include "core/InputState.h"
#include "gameplay/dungeon/FDungeonRun.h"
#include <SDL3/SDL.h>

EStateTransition FEmberTitleState::Update(entt::registry& Registry, float DeltaTime)
{
    (void)DeltaTime;
    if (auto* request = Registry.ctx().find<game::FMenuRequest>()) {
        if (request->value == game::EMenuRequest::Quit) return EStateTransition::Quit;
        if (request->value == game::EMenuRequest::Start) {
            game::StartDungeonRun(Registry, static_cast<std::uint32_t>(SDL_GetTicks()), request->fresh);
            *request = {};
            return EStateTransition::ToBattle;
        }
    }
    const auto* input = Registry.ctx().find<game::FInputState>();
    if (input && input->isActionPressed(game::EInputAction::Confirm)) {
        return EStateTransition::ToHub;
    }
    return EStateTransition::None;
}
