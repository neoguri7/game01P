#pragma once

#include <SDL3/SDL.h>

namespace game {

/// Sprite frame definition within a sprite sheet.
struct FSprFrame {
    int srcX{0};
    int srcY{0};
    int srcW{32};
    int srcH{32};
    float duration{0.1f}; ///< How long this frame is shown
};

} // namespace game
