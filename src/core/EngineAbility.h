#pragma once

#include "core/events/EngineEvents.h"

#include <cstdint>

namespace game {

/// Engine-level translation of GAS "ability" concepts.
/// These are not gameplay abilities; they name engine phase capabilities.
enum class EEngineAbility {
    Initialize,
    Shutdown,
    BeginFrame,
    BeginInputFrame,
    PollInputEvents,
    ProcessInput,
    ApplyWindowEvent,
    RequestQuit,
    BeginRenderFrame,
    PresentRenderFrame
};

/// Engine-level translation of GAS "effect" concepts.
/// Hint: an ability may apply several effects, but each effect should mutate one
/// clear piece of engine state or perform one low-level operation.
enum class EEngineEffect {
    UpdateFrameState,
    InitializeSdl,
    CreateWindow,
    CreateRenderer,
    InitializeImGui,
    InitializeContextServices,
    MarkRunning,
    BeginInputFrame,
    ForwardInputToImGui,
    TranslateInputState,
    UpdateInputState,
    UpdateWindowState,
    RequestQuit,
    StopRunLoop,
    ShutdownEntities,
    ShutdownContextServices,
    ShutdownImGui,
    DestroyRenderer,
    DestroyWindow,
    ShutdownSdl,
    BeginImGuiFrame,
    ClearBackbuffer,
    RenderWorldSystems,
    RenderOverlay,
    RenderImGuiDrawData,
    PresentBackbuffer
};

/// Engine-level translation of GAS "condition" concepts.
/// Conditions are guard facts checked before activating an ability or applying
/// an effect. Keep this list finite and engine-owned; gameplay requirements do
/// not belong here.
enum class EEngineCondition {
    None,
    WindowOpen,
    RendererReady,
    InputReady,
    FrameActive,
    WindowNotMinimized,
    InputNotUiCaptured
};

/// EngineAction: a request to activate one engine ability during the current
/// frame. The compatibility alias below keeps the older request naming usable.
/// Hint: add required/blocked tag fields here only when a phase needs them.
struct FEngineAction {
    EEngineAbility ability{EEngineAbility::BeginFrame};
    std::uint64_t  frameIndex{0};
};

using FEngineAbilityRequest = FEngineAction;

/// Result of the GAS-style CanActivate step.
/// Hint: keep failed guards explicit; they become useful debug overlay data.
struct FEngineAbilityCheck {
    bool                 canActivate{true};
    EEngineSkipReason    blockedReason{EEngineSkipReason::None};
    EEngineCondition     failedCondition{EEngineCondition::None};
};

/// Result of applying one engine effect.
/// Hint: future debugging can queue these for an engine effect timeline.
struct FEngineEffectResult {
    EEngineEffect effect{EEngineEffect::UpdateFrameState};
    bool          applied{false};
};

} // namespace game
