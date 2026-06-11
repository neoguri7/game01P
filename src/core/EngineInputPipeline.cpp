#include "core/EngineAbilityPipeline.h"

#include "core/EngineState.h"
#include "core/events/FEventPublishing.h"

#include <imgui.h>
#include "imgui/backends/imgui_impl_sdl3.h"

namespace game {

namespace {

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

        runtime->keyboardCapturedByUi = keyboardCapturedByUi;
        runtime->mouseCapturedByUi = mouseCapturedByUi;

        if (keyboardCapturedByUi || mouseCapturedByUi) {
            runtime->tags.add(EEngineTag::InputUiCaptured);
        } else {
            runtime->tags.remove(EEngineTag::InputUiCaptured);
        }
    }
    SetEngineInputReadyState(registry, true);

    input.beginFrame(keyboardCapturedByUi, mouseCapturedByUi);

    SetEngineInputFrameActiveState(registry, true);

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
    if (request.ability != EEngineAbility::BeginInputFrame) return nullptr;

    auto* input = registry.ctx().find<FInputState>();
    if (!input) {
        PublishAndQueueFrameEvent(registry, FEngineInputSkippedEvent{
            .frameIndex = request.frameIndex,
            .reason = EEngineSkipReason::InputNotReady
        });
        SetEngineInputReadyState(registry, false);
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

bool CanPollEngineInputAbility(entt::registry& registry, const FEngineAbilityRequest& request, bool running) {
    if (request.ability != EEngineAbility::PollInputEvents) return false;
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
    if (request.ability != EEngineAbility::RequestQuit) {
        return { .effect = EEngineEffect::RequestQuit, .applied = false };
    }

    if (!IsEngineQuitInputEvent(event)) {
        return { .effect = EEngineEffect::RequestQuit, .applied = false };
    }

    running = false;
    SetEngineRunningState(registry, false);
    SetEngineWindowOpenState(registry, false);

    PublishAndQueueFrameEvent(registry, FEngineQuitRequestedEvent{ .frameIndex = request.frameIndex });
    return { .effect = EEngineEffect::RequestQuit, .applied = true };
}

void EndEngineInputAbility(entt::registry& registry, const FEngineAbilityRequest& request, bool inputWasActive) {
    if (request.ability != EEngineAbility::BeginInputFrame) return;

    SetEngineInputFrameActiveState(registry, false);

    if (!inputWasActive) return;

    PublishAndQueueFrameEvent(registry, FEngineInputFrameEndedEvent{
        .frameIndex = request.frameIndex
    });
}

} // namespace game
