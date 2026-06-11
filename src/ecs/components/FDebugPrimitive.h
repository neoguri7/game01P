#pragma once

#include <cstdint>

namespace game {

struct FDebugPrimitive {
    float width{48.f};
    float height{48.f};

    std::uint8_t fillR{80};
    std::uint8_t fillG{160};
    std::uint8_t fillB{240};
    std::uint8_t fillA{180};

    std::uint8_t outlineR{255};
    std::uint8_t outlineG{255};
    std::uint8_t outlineB{255};
    std::uint8_t outlineA{255};

    bool filled{true};
    bool drawCollider{true};
};

} // namespace game
