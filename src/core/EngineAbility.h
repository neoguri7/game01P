#pragma once

#include "core/events/EngineEvents.h"

#include <cstdint>

namespace game {

/// Engine-level translation of GAS "ability" concepts.
/// These are not gameplay abilities; they name engine phase capabilities.
enum class EEngineAbility {
    BeginFrame,
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
    UpdateInputState,
    UpdateWindowState,
    StopRunLoop,
    BeginImGuiFrame,
    ClearBackbuffer,
    RenderImGuiDrawData,
    PresentBackbuffer
};

/// A request to activate one engine ability during the current frame.
/// Hint: add required/blocked tag fields here only when a phase needs them.
struct FEngineAbilityRequest {
    EEngineAbility ability{EEngineAbility::BeginFrame};
    std::uint64_t  frameIndex{0};
};

/// Result of the GAS-style CanActivate step.
/// Hint: keep failed guards explicit; they become useful debug overlay data.
struct FEngineAbilityCheck {
    bool              canActivate{true};
    EEngineSkipReason blockedReason{EEngineSkipReason::None};
};

/// Result of applying one engine effect.
/// Hint: future debugging can queue these for an engine effect timeline.
struct FEngineEffectResult {
    EEngineEffect effect{EEngineEffect::UpdateFrameState};
    bool          applied{false};
};

} // namespace game
