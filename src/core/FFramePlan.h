#pragma once

namespace game {

/// WP-42: the fixed step fast-forward feeds the run loop (equals kScriptFixedDeltaSeconds, asserted in Engine.h).
inline constexpr float kFastForwardDeltaSeconds = 1.f / 60.f;

/// What one engine frame does: the simulation step and whether the frame is rasterized.
struct FFramePlan {
    float dt{0.f};
    bool rasterize{true};
};

[[nodiscard]] constexpr FFramePlan PlanEngineFrame(bool fixedTimestep, bool fastForward, bool captureDue, float wallDelta) {
    return FFramePlan{(fixedTimestep || fastForward) ? kFastForwardDeltaSeconds : wallDelta, !fastForward || captureDue};
}

} // namespace game
