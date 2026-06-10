#pragma once

#include <cstdint>

namespace game {

/// Why an engine phase declined to perform its normal effect this frame.
enum class EEngineSkipReason {
    None,
    WindowNotReady,
    RendererNotReady,
    InputNotReady,
    WindowMinimized,
    UiCaptured
};

/// Engine cues are typed events carried by FEventBus after meaningful effects.
/// Hint: publish cues after state mutation, not before.

/// SDL quit/window-close translated into the StopRunLoop engine effect.
struct FEngineQuitRequestedEvent {
    std::uint64_t frameIndex{0};
};

/// Window-size effect updated FEngineWindowState.
struct FEngineWindowResizedEvent {
    std::uint64_t frameIndex{0};
    int           width{0};
    int           height{0};
};

/// FInputState::beginFrame and UI capture facts were set.
struct FEngineInputFrameBeganEvent {
    std::uint64_t frameIndex{0};
    bool          keyboardCapturedByUi{false};
    bool          mouseCapturedByUi{false};
};

/// SDL input polling finished for this frame.
struct FEngineInputFrameEndedEvent {
    std::uint64_t frameIndex{0};
};

/// Input processing was intentionally skipped, not merely an ignored SDL event.
struct FEngineInputSkippedEvent {
    std::uint64_t     frameIndex{0};
    EEngineSkipReason reason{EEngineSkipReason::InputNotReady};
};

/// Render ability passed its guards, before render systems run.
struct FEngineRenderFrameBeganEvent {
    std::uint64_t frameIndex{0};
    int           width{0};
    int           height{0};
};

/// PresentBackbuffer effect was applied for the frame.
struct FEngineRenderFramePresentedEvent {
    std::uint64_t frameIndex{0};
};

/// Render phase was intentionally skipped.
struct FEngineRenderSkippedEvent {
    std::uint64_t     frameIndex{0};
    EEngineSkipReason reason{EEngineSkipReason::RendererNotReady};
};

} // namespace game
