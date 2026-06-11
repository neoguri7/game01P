#pragma once

#include "core/EngineAbility.h"
#include "core/InputState.h"

#include <SDL3/SDL.h>
#include <entt/entt.hpp>

#include <cstdint>
#include <functional>

namespace game {

class SystemManager;
class Time;

[[nodiscard]] std::uint64_t CurrentEngineFrameIndex(entt::registry& registry);

void BeginEngineFrameAbility(entt::registry& registry);
void EndEngineFrameEffects(entt::registry& registry);

[[nodiscard]] FInputState* BeginEngineInputFrameAbility(
    entt::registry& registry,
    const FEngineAbilityRequest& request
);
[[nodiscard]] FInputState* BeginEngineInputAbility(entt::registry& registry, const FEngineAbilityRequest& request);
[[nodiscard]] bool CanPollEngineInputAbility(entt::registry& registry, bool running);
FEngineEffectResult ApplyEngineImGuiInputEffect(const SDL_Event& event);
FEngineEffectResult ApplyEngineInputTranslateEffect(FInputState* input, const SDL_Event& event);
FEngineEffectResult ApplyEngineQuitInputEffect(
    entt::registry& registry,
    const SDL_Event& event,
    const FEngineAbilityRequest& request,
    bool& running
);
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
FEngineEffectResult ApplyEngineWorldRenderEffect(entt::registry& registry, SystemManager& systemManager);
FEngineEffectResult ApplyEngineOverlayRenderEffect(
    entt::registry& registry,
    const Time& time,
    const SystemManager& systemManager,
    const std::function<void(entt::registry&, const Time&, const SystemManager&)>& overlayRenderer
);
FEngineEffectResult ApplyEngineRenderImGuiDrawDataEffect(SDL_Renderer* renderer);
FEngineEffectResult ApplyEnginePresentBackbufferEffect(SDL_Renderer* renderer);

} // namespace game
