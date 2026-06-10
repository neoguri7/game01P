#include "core/EngineAbilityPipeline.h"

#include "core/EngineState.h"
#include "core/events/FEventPublishing.h"

#include <imgui.h>
#include "imgui/backends/imgui_impl_sdl3.h"
#include "imgui/backends/imgui_impl_sdlrenderer3.h"

namespace game {

namespace {

void SetEngineTag(entt::registry& registry, EEngineTag tag, bool enabled) {
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
        return { .canActivate = false, .blockedReason = EEngineSkipReason::RendererNotReady };
    }

    if (auto* runtime = registry.ctx().find<FEngineRuntimeState>();
        runtime && !runtime->rendererReady) {
        return { .canActivate = false, .blockedReason = EEngineSkipReason::RendererNotReady };
    }

    // Hint: window-minimized can become a blocking tag later. Leaving it
    // non-blocking preserves the current render behavior for this first pass.
    return {};
}

} // namespace

std::uint64_t CurrentEngineFrameIndex(entt::registry& registry) {
    if (auto* frame = registry.ctx().find<FEngineFrameState>()) {
        return frame->frameIndex;
    }
    return 0;
}

void BeginEngineFrameAbility(entt::registry& registry) {
    auto* frame = registry.ctx().find<FEngineFrameState>();
    if (!frame) return;

    // GAS shape: BeginFrame ability applies the frame-state effect.
    ++frame->frameIndex;
    frame->frameActive = true;
    SetEngineTag(registry, EEngineTag::FrameActive, true);
}

void EndEngineFrameEffects(entt::registry& registry) {
    auto* frame = registry.ctx().find<FEngineFrameState>();
    if (!frame) return;

    frame->frameActive = false;
    SetEngineTag(registry, EEngineTag::FrameActive, false);
}

FInputState* BeginEngineInputAbility(entt::registry& registry, const FEngineAbilityRequest& request) {
    auto* input = registry.ctx().find<FInputState>();
    if (!input) {
        PublishAndQueueFrameEvent(registry, FEngineInputSkippedEvent{
            .frameIndex = request.frameIndex,
            .reason = EEngineSkipReason::InputNotReady
        });
        SetEngineTag(registry, EEngineTag::InputReady, false);
        return nullptr;
    }

    const ImGuiIO& io = ImGui::GetIO();
    input->beginFrame(io.WantCaptureKeyboard, io.WantCaptureMouse);

    if (auto* runtime = registry.ctx().find<FEngineRuntimeState>()) {
        runtime->inputReady = true;
        runtime->keyboardCapturedByUi = io.WantCaptureKeyboard;
        runtime->mouseCapturedByUi = io.WantCaptureMouse;
        runtime->tags.add(EEngineTag::InputReady);

        if (io.WantCaptureKeyboard || io.WantCaptureMouse) {
            runtime->tags.add(EEngineTag::InputUiCaptured);
        } else {
            runtime->tags.remove(EEngineTag::InputUiCaptured);
        }
    }

    if (auto* frame = registry.ctx().find<FEngineFrameState>()) {
        frame->inputFrameActive = true;
        SetEngineTag(registry, EEngineTag::InputFrameActive, true);
    }

    PublishAndQueueFrameEvent(registry, FEngineInputFrameBeganEvent{
        .frameIndex = request.frameIndex,
        .keyboardCapturedByUi = io.WantCaptureKeyboard,
        .mouseCapturedByUi = io.WantCaptureMouse
    });

    return input;
}

void EndEngineInputAbility(entt::registry& registry, const FEngineAbilityRequest& request, bool inputWasActive) {
    if (auto* frame = registry.ctx().find<FEngineFrameState>()) {
        frame->inputFrameActive = false;
        SetEngineTag(registry, EEngineTag::InputFrameActive, false);
    }

    if (!inputWasActive) return;

    PublishAndQueueFrameEvent(registry, FEngineInputFrameEndedEvent{
        .frameIndex = request.frameIndex
    });
}

void ApplyEngineWindowEventAbility(
    entt::registry& registry,
    const SDL_Event& event,
    const FEngineAbilityRequest& request,
    bool& running
) {
    auto* windowState = registry.ctx().find<FEngineWindowState>();

    switch (event.type) {
    case SDL_EVENT_QUIT:
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        // GAS shape: RequestQuit applies StopRunLoop before the cue is emitted.
        running = false;
        PublishAndQueueFrameEvent(registry, FEngineQuitRequestedEvent{ .frameIndex = request.frameIndex });
        return;
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
        if (windowState) windowState->minimized = true;
        SetEngineTag(registry, EEngineTag::WindowMinimized, true);
        return;
    case SDL_EVENT_WINDOW_RESTORED:
        if (windowState) windowState->minimized = false;
        SetEngineTag(registry, EEngineTag::WindowMinimized, false);
        return;
    default:
        return;
    }
}

bool BeginEngineRenderAbility(entt::registry& registry, const FEngineAbilityRequest& request, SDL_Renderer* renderer) {
    const FEngineAbilityCheck check = CanBeginEngineRenderFrame(registry, renderer);
    if (!check.canActivate) {
        PublishAndQueueFrameEvent(registry, FEngineRenderSkippedEvent{
            .frameIndex = request.frameIndex,
            .reason = check.blockedReason
        });
        return false;
    }

    if (auto* frame = registry.ctx().find<FEngineFrameState>()) {
        frame->renderFrameActive = true;
        SetEngineTag(registry, EEngineTag::RenderFrameActive, true);
    }

    const auto* windowState = registry.ctx().find<FEngineWindowState>();
    PublishAndQueueFrameEvent(registry, FEngineRenderFrameBeganEvent{
        .frameIndex = request.frameIndex,
        .width = windowState ? windowState->width : 0,
        .height = windowState ? windowState->height : 0
    });

    return true;
}

void EndEngineRenderAbility(entt::registry& registry, const FEngineAbilityRequest& request) {
    if (auto* frame = registry.ctx().find<FEngineFrameState>()) {
        frame->renderFrameActive = false;
        SetEngineTag(registry, EEngineTag::RenderFrameActive, false);
    }

    PublishAndQueueFrameEvent(registry, FEngineRenderFramePresentedEvent{
        .frameIndex = request.frameIndex
    });
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

    SDL_RenderPresent(renderer);

    return { .effect = EEngineEffect::PresentBackbuffer, .applied = true };
}

} // namespace game
