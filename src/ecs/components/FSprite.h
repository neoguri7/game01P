#pragma once

#include <string>

namespace game {

/**
 * Simple sprite component.
 * Storing only texture path + dimensions for the scaffold.
 * In future you can store more (atlas rect, tint, layer, shader id ...).
 */
struct FSprite {
    std::string texturePath;

    // default 32x32 — override at emplace time if needed
    int width  = 32;
    int height = 32;

    // optional flip flags
    bool flipX = false;
    bool flipY = false;

    // TODO: add srcRect, tint, etc. later
};

} // namespace game
