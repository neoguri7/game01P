#include "core/EngineAbilityPipeline.h"

#include "core/EngineState.h"
#include "core/SystemManager.h"
#include "core/Time.h"
#include "core/events/FEventPublishing.h"

#include <imgui.h>
#include "imgui/backends/imgui_impl_sdlrenderer3.h"
#include "imgui/backends/imgui_impl_sdl3.h"
#include <tracy/Tracy.hpp>

namespace game {

namespace {

void SetEngineRuntimeTag(entt::registry& registry, EEngineTag tag, bool enabled) {
    auto* runtime = registry.ctx().find<FEngineRuntimeState>();
    if (!runtime) return;

    if (enabled) {
        runtime->tags.add(tag);
    } else {
        runtime->tags.remove(tag);
    }
}

FEngineAbilityCheck CanBeginEngineRenderFrame(entt::registry& registry, SDL_Renderer* renderer) {
    if (!renderer) {
        return {
            .canActivate = false,
            .blockedReason = EEngineSkipReason::RendererNotReady,
            .failedCondition = EEngineCondition::RendererReady
        };
    }

    if (auto* runtime = registry.ctx().find<FEngineRuntimeState>();
        runtime && !runtime->rendererReady) {
        return {
            .canActivate = false,
            .blockedReason = EEngineSkipReason::RendererNotReady,
            .failedCondition = EEngineCondition::RendererReady
        };
    }

    // Hint: window-minimized can become a blocking tag later. Leaving it
    // non-blocking preserves the current render behavior for this first pass.
    return {};
}

} // namespace

void SetEngineRunningState(entt::registry& registry, bool enabled) {
    auto* runtime = registry.ctx().find<FEngineRuntimeState>();
    if (!runtime) return;

    runtime->running = enabled;
    SetEngineRuntimeTag(registry, EEngineTag::Running, enabled);
}

void SetEngineInputReadyState(entt::registry& registry, bool enabled) {
    auto* runtime = registry.ctx().find<FEngineRuntimeState>();
    if (!runtime) return;

    runtime->inputReady = enabled;
    SetEngineRuntimeTag(registry, EEngineTag::InputReady, enabled);
}

void SetEngineFrameActiveState(entt::registry& registry, bool enabled) {
    auto* frame = registry.ctx().find<FEngineFrameState>();
    if (!frame) return;

    frame->frameActive = enabled;
    SetEngineRuntimeTag(registry, EEngineTag::FrameActive, enabled);
}

void SetEngineInputFrameActiveState(entt::registry& registry, bool enabled) {
    auto* frame = registry.ctx().find<FEngineFrameState>();
    if (!frame) return;

    frame->inputFrameActive = enabled;
    SetEngineRuntimeTag(registry, EEngineTag::InputFrameActive, enabled);
}

void SetEngineRenderFrameActiveState(entt::registry& registry, bool enabled) {
    auto* frame = registry.ctx().find<FEngineFrameState>();
    if (!frame) return;

    frame->renderFrameActive = enabled;
    SetEngineRuntimeTag(registry, EEngineTag::RenderFrameActive, enabled);
}

void SetEngineWindowOpenState(entt::registry& registry, bool enabled) {
    auto* windowState = registry.ctx().find<FEngineWindowState>();
    if (!windowState) return;

    windowState->windowOpen = enabled;
    SetEngineRuntimeTag(registry, EEngineTag::WindowOpen, enabled);
}

void SetEngineWindowFocusedState(entt::registry& registry, bool enabled) {
    auto* windowState = registry.ctx().find<FEngineWindowState>();
    if (!windowState) return;

    windowState->focused = enabled;
    SetEngineRuntimeTag(registry, EEngineTag::WindowFocused, enabled);
}

void SetEngineWindowMinimizedState(entt::registry& registry, bool enabled) {
    auto* windowState = registry.ctx().find<FEngineWindowState>();
    if (!windowState) return;

    windowState->minimized = enabled;
    SetEngineRuntimeTag(registry, EEngineTag::WindowMinimized, enabled);
}

std::uint64_t CurrentEngineFrameIndex(entt::registry& registry) {
    if (auto* frame = registry.ctx().find<FEngineFrameState>()) {
        return frame->frameIndex;
    }
    return 0;
}

void BeginEngineFrameAbility(entt::registry& registry, const FEngineAbilityRequest& request) {
    if (request.ability != EEngineAbility::BeginFrame) return;

    auto* frame = registry.ctx().find<FEngineFrameState>();
    if (!frame) return;

    // GAS shape: BeginFrame ability applies the frame-state effect.
    ++frame->frameIndex;
    SetEngineFrameActiveState(registry, true);
}

void EndEngineFrameEffects(entt::registry& registry) {
    SetEngineFrameActiveState(registry, false);
}

void ApplyEngineWindowEventAbility(
    entt::registry& registry,
    const SDL_Event& event,
    const FEngineAbilityRequest& request,
    bool& running
) {
    (void)running;
    if (request.ability != EEngineAbility::ApplyWindowEvent) return;

    auto* windowState = registry.ctx().find<FEngineWindowState>();

    switch (event.type) {
    case SDL_EVENT_WINDOW_RESIZED:
        if (windowState) {
            windowState->width = event.window.data1;
            windowState->height = event.window.data2;
        }
        PublishAndQueueFrameEvent(registry, FEngineWindowResizedEvent{
            .frameIndex = request.frameIndex,
            .width = event.window.data1,
            .height = event.window.data2
        });
        return;
    case SDL_EVENT_WINDOW_MINIMIZED:
        SetEngineWindowMinimizedState(registry, true);
        PublishAndQueueFrameEvent(registry, FEngineWindowMinimizedEvent{
            .frameIndex = request.frameIndex
        });
        return;
    case SDL_EVENT_WINDOW_RESTORED:
        SetEngineWindowMinimizedState(registry, false);
        PublishAndQueueFrameEvent(registry, FEngineWindowRestoredEvent{
            .frameIndex = request.frameIndex
        });
        return;
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
        SetEngineWindowFocusedState(registry, true);
        PublishAndQueueFrameEvent(registry, FEngineWindowFocusChangedEvent{
            .frameIndex = request.frameIndex,
            .focused = true
        });
        return;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        SetEngineWindowFocusedState(registry, false);
        PublishAndQueueFrameEvent(registry, FEngineWindowFocusChangedEvent{
            .frameIndex = request.frameIndex,
            .focused = false
        });
        return;
    default:
        return;
    }
}

bool BeginEngineRenderAbility(entt::registry& registry, const FEngineAbilityRequest& request, SDL_Renderer* renderer) {
    if (request.ability != EEngineAbility::BeginRenderFrame) return false;

    const FEngineAbilityCheck check = CanBeginEngineRenderFrame(registry, renderer);
    if (!check.canActivate) {
        PublishAndQueueFrameEvent(registry, FEngineRenderSkippedEvent{
            .frameIndex = request.frameIndex,
            .reason = check.blockedReason
        });
        return false;
    }

    SetEngineRenderFrameActiveState(registry, true);

    const auto* windowState = registry.ctx().find<FEngineWindowState>();
    PublishAndQueueFrameEvent(registry, FEngineRenderFrameBeganEvent{
        .frameIndex = request.frameIndex,
        .width = windowState ? windowState->width : 0,
        .height = windowState ? windowState->height : 0
    });

    return true;
}

void EndEngineRenderAbility(entt::registry& registry, const FEngineAbilityRequest& request, bool presented) {
    if (request.ability != EEngineAbility::PresentRenderFrame) return;

    SetEngineRenderFrameActiveState(registry, false);

    if (presented) {
        PublishAndQueueFrameEvent(registry, FEngineRenderFramePresentedEvent{
            .frameIndex = request.frameIndex
        });
    }
}

FEngineEffectResult ApplyEngineBeginImGuiFrameEffect() {
    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    return { .effect = EEngineEffect::BeginImGuiFrame, .applied = true };
}

FEngineEffectResult ApplyEngineClearBackbufferEffect(SDL_Renderer* renderer) {
    if (!renderer) {
        return { .effect = EEngineEffect::ClearBackbuffer, .applied = false };
    }

    SDL_SetRenderDrawColor(renderer, 30, 30, 40, 255);
    SDL_RenderClear(renderer);

    return { .effect = EEngineEffect::ClearBackbuffer, .applied = true };
}

FEngineEffectResult ApplyEngineWorldRenderEffect(entt::registry& registry, SystemManager& systemManager) {
    systemManager.renderAll(registry);

    return { .effect = EEngineEffect::RenderWorldSystems, .applied = true };
}

FEngineEffectResult ApplyEngineOverlayRenderEffect(
    entt::registry& registry,
    const Time& time,
    const SystemManager& systemManager,
    const std::function<void(entt::registry&, const Time&, const SystemManager&)>& overlayRenderer
) {
    if (!overlayRenderer) {
        return { .effect = EEngineEffect::RenderOverlay, .applied = false };
    }

    ZoneScopedN("Engine::debugOverlayRender");
    overlayRenderer(registry, time, systemManager);

    return { .effect = EEngineEffect::RenderOverlay, .applied = true };
}

FEngineEffectResult ApplyEngineRenderImGuiDrawDataEffect(SDL_Renderer* renderer) {
    if (!renderer) {
        return { .effect = EEngineEffect::RenderImGuiDrawData, .applied = false };
    }

    ImGui::Render();
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);

    return { .effect = EEngineEffect::RenderImGuiDrawData, .applied = true };
}

FEngineEffectResult ApplyEnginePresentBackbufferEffect(SDL_Renderer* renderer) {
    if (!renderer) {
        return { .effect = EEngineEffect::PresentBackbuffer, .applied = false };
    }

    const bool presented = SDL_RenderPresent(renderer);

    return { .effect = EEngineEffect::PresentBackbuffer, .applied = presented };
}

} // namespace game
