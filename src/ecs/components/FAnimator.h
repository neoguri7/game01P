#pragma once

#include "core/AssetHandles.h"

namespace game {

/// Runtime playback state. The immutable clip and its frames are owned by FAnimationLibrary.
struct FAnimator {
    FAnimClipId clip{};
    double frameTime{};
    std::uint32_t frame{};
    bool playing{};
    bool loop{true};
    bool finished{};
};

} // namespace game
