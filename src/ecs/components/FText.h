#pragma once
#include <cstdint>
#include <string>

namespace game {

/// Text rendering component — paired with a position for on-screen text.
struct FText {
    std::string content;
    std::string fontPath;
    int         fontSize{24};
    std::uint8_t colorR{255};
    std::uint8_t colorG{255};
    std::uint8_t colorB{255};
    std::uint8_t colorA{255};
};

} // namespace game
