#pragma once

#include "core/AssetHandles.h"

namespace game {

struct FSprite {
    FTextureId texture{};
    FSourceRect source{};
    float width{32.0f};
    float height{32.0f};
    bool flipX{};
    bool flipY{};
};

} // namespace game
