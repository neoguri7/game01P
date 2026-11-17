#pragma once

#include <cstdint>

namespace game {

struct FTextureId {
    std::uint32_t value{};

    [[nodiscard]] explicit operator bool() const noexcept { return value != 0; }
    friend bool operator==(FTextureId, FTextureId) = default;
};

struct FAnimClipId {
    std::uint32_t value{};

    [[nodiscard]] explicit operator bool() const noexcept { return value != 0; }
    friend bool operator==(FAnimClipId, FAnimClipId) = default;
};

struct FSourceRect {
    // A non-positive width or height means "use the full texture".
    float x{};
    float y{};
    float w{};
    float h{};
};

} // namespace game
