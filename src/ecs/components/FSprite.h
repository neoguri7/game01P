#pragma once

#include "core/AssetHandles.h"

namespace game {

struct FSprite {
    // AnimationSystem owns these fields while the entity has an FAnimator.
    FTextureId texture{};
    FSourceRect source{};
    float width{32.0f};
    float height{32.0f};
    bool flipX{};
    bool flipY{};
};

} // namespace game
