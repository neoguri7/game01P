#pragma once
#include <string>
#include <vector>
#include "ecs/components/FSprFrame.h"

namespace game {

/// Animation component — attached alongside FSprite to animate it.
struct FAnimation {
    std::string           sheetPath;   ///< Sprite sheet texture path (overrides FSprite::texturePath when active)
    std::vector<FSprFrame> frames;
    float                 timer{0.f};
    int                   currentFrame{0};
    bool                  loop{true};
    bool                  playing{true};
};

} // namespace game
