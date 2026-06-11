#include "core/EngineAbilityPipeline.h"

#include "core/EngineState.h"
#include "core/events/FEventPublishing.h"

#include <imgui.h>
#include "imgui/backends/imgui_impl_sdl3.h"

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

void SetEngineRunningState(entt::registry& registry, bool enabled) {
    auto* runtime = registry.ctx().find<FEngineRuntimeState>();
    if (!runtime) return;

    runtime->running = enabled;
    if (enabled) {
        runtime->tags.add(EEngineTag::Running);
    } else {
        runtime->tags.remove(EEngineTag::Running);
    }
}

bool IsEngineQuitInputEvent(const SDL_Event& event) {
    return event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED;
}

FEngineEffectResult ApplyEngineInputBeginFrameEffect(
    entt::registry& registry,
    FInputState& input,
    const FEngineAbilityRequest& request,
    bool keyboardCapturedByUi,
    bool mouseCapturedByUi
) {
    bool captureChanged = false;
    bool previousKeyboardCapturedByUi = false;
    bool previousMouseCapturedByUi = false;

    if (auto* runtime = registry.ctx().find<FEngineRuntimeState>()) {
        previousKeyboardCapturedByUi = runtime->keyboardCapturedByUi;
        previousMouseCapturedByUi = runtime->mouseCapturedByUi;
        captureChanged = previousKeyboardCapturedByUi != keyboardCapturedByUi
            || previousMouseCapturedByUi != mouseCapturedByUi;

        runtime->inputReady = true;
        runtime->keyboardCapturedByUi = keyboardCapturedByUi;
        runtime->mouseCapturedByUi = mouseCapturedByUi;
        runtime->tags.add(EEngineTag::InputReady);

        if (keyboardCapturedByUi || mouseCapturedByUi) {
            runtime->tags.add(EEngineTag::InputUiCaptured);
        } else {
            runtime->tags.remove(EEngineTag::InputUiCaptured);
        }
    }

    input.beginFrame(keyboardCapturedByUi, mouseCapturedByUi);

    if (auto* frame = registry.ctx().find<FEngineFrameState>()) {
        frame->inputFrameActive = true;
        SetEngineTag(registry, EEngineTag::InputFrameActive, true);
    }

    PublishAndQueueFrameEvent(registry, FEngineInputFrameBeganEvent{
        .frameIndex = request.frameIndex,
        .keyboardCapturedByUi = keyboardCapturedByUi,
        .mouseCapturedByUi = mouseCapturedByUi
    });

    if (captureChanged) {
        PublishAndQueueFrameEvent(registry, FEngineInputCaptureChangedEvent{
            .frameIndex = request.frameIndex,
            .previousKeyboardCapturedByUi = previousKeyboardCapturedByUi,
            .previousMouseCapturedByUi = previousMouseCapturedByUi,
            .keyboardCapturedByUi = keyboardCapturedByUi,
            .mouseCapturedByUi = mouseCapturedByUi
        });
    }

    return { .effect = EEngineEffect::BeginInputFrame, .applied = true };
}

} // namespace

FInputState* BeginEngineInputFrameAbility(entt::registry& registry, const FEngineAbilityRequest& request) {
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
    (void)ApplyEngineInputBeginFrameEffect(
        registry,
        *input,
        request,
        io.WantCaptureKeyboard,
        io.WantCaptureMouse
    );

    return input;
}

FInputState* BeginEngineInputAbility(entt::registry& registry, const FEngineAbilityRequest& request) {
    return BeginEngineInputFrameAbility(registry, request);
}

bool CanPollEngineInputAbility(entt::registry& registry, bool running) {
    if (!running) return false;

    if (auto* windowState = registry.ctx().find<FEngineWindowState>()) {
        return windowState->windowOpen;
    }

    return true;
}

FEngineEffectResult ApplyEngineImGuiInputEffect(const SDL_Event& event) {
    ImGui_ImplSDL3_ProcessEvent(&event);
    return { .effect = EEngineEffect::ForwardInputToImGui, .applied = true };
}

FEngineEffectResult ApplyEngineInputTranslateEffect(FInputState* input, const SDL_Event& event) {
    if (!input) {
        return { .effect = EEngineEffect::TranslateInputState, .applied = false };
    }

    return {
        .effect = EEngineEffect::TranslateInputState,
        .applied = input->processEvent(event)
    };
}

FEngineEffectResult ApplyEngineQuitInputEffect(
    entt::registry& registry,
    const SDL_Event& event,
    const FEngineAbilityRequest& request,
    bool& running
) {
    if (!IsEngineQuitInputEvent(event)) {
        return { .effect = EEngineEffect::RequestQuit, .applied = false };
    }

    running = false;
    SetEngineRunningState(registry, false);
    if (auto* windowState = registry.ctx().find<FEngineWindowState>()) {
        windowState->windowOpen = false;
    }
    SetEngineTag(registry, EEngineTag::WindowOpen, false);

    PublishAndQueueFrameEvent(registry, FEngineQuitRequestedEvent{ .frameIndex = request.frameIndex });
    return { .effect = EEngineEffect::RequestQuit, .applied = true };
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

} // namespace game
