#pragma once

#include <bitset>
#include <cstddef>
#include <cstdint>

namespace game {

/// Small shared vocabulary for engine ability guards.
/// This mirrors GAS tags, but stays core-only and deliberately finite for now.
enum class EEngineTag {
    WindowOpen,
    RendererReady,
    Running,
    FrameActive,
    InputReady,
    InputFrameActive,
    RenderFrameActive,
    InputUiCaptured,
    WindowMinimized,
    Count
};

struct FEngineTagSet {
    std::bitset<static_cast<std::size_t>(EEngineTag::Count)> tags{};

    [[nodiscard]] bool has(EEngineTag tag) const {
        return tags.test(static_cast<std::size_t>(tag));
    }

    void add(EEngineTag tag) {
        tags.set(static_cast<std::size_t>(tag));
    }

    void remove(EEngineTag tag) {
        tags.reset(static_cast<std::size_t>(tag));
    }
};

/// Durable window facts owned by the engine and safe to mirror in registry.ctx().
struct FEngineWindowState {
    int  width{0};
    int  height{0};
    bool windowOpen{false};
    bool minimized{false};
};

/// Durable frame facts that later input/render effects can update once per tick.
struct FEngineFrameState {
    std::uint64_t frameIndex{0};
    bool          frameActive{false};
    bool          inputFrameActive{false};
    bool          renderFrameActive{false};
};

/// Durable service readiness and UI capture facts for phase guard checks.
struct FEngineRuntimeState {
    FEngineTagSet tags{};

    bool running{false};
    bool rendererReady{false};
    bool inputReady{false};
    bool keyboardCapturedByUi{false};
    bool mouseCapturedByUi{false};
};

} // namespace game
