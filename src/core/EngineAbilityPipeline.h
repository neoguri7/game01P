#pragma once

#include "core/EngineAbility.h"
#include "core/InputState.h"

#include <SDL3/SDL.h>
#include <entt/entt.hpp>

#include <cstdint>

namespace game {

[[nodiscard]] std::uint64_t CurrentEngineFrameIndex(entt::registry& registry);

void BeginEngineFrameAbility(entt::registry& registry);
void EndEngineFrameEffects(entt::registry& registry);

[[nodiscard]] FInputState* BeginEngineInputAbility(entt::registry& registry, const FEngineAbilityRequest& request);
void EndEngineInputAbility(entt::registry& registry, const FEngineAbilityRequest& request, bool inputWasActive);

void ApplyEngineWindowEventAbility(
    entt::registry& registry,
    const SDL_Event& event,
    const FEngineAbilityRequest& request,
    bool& running
);

[[nodiscard]] bool BeginEngineRenderAbility(
    entt::registry& registry,
    const FEngineAbilityRequest& request,
    SDL_Renderer* renderer
);
void EndEngineRenderAbility(entt::registry& registry, const FEngineAbilityRequest& request);

FEngineEffectResult ApplyEngineBeginImGuiFrameEffect();
FEngineEffectResult ApplyEngineClearBackbufferEffect(SDL_Renderer* renderer);
FEngineEffectResult ApplyEngineRenderImGuiDrawDataEffect(SDL_Renderer* renderer);
FEngineEffectResult ApplyEnginePresentBackbufferEffect(SDL_Renderer* renderer);

} // namespace game
