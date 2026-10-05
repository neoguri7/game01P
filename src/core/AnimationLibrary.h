#pragma once

#include "core/AssetHandles.h"

#include <entt/entt.hpp>

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace game {

struct FAnimFrame {
    FSourceRect source{};
    double duration{};
};

struct FAnimClip {
    FTextureId texture{};
    std::vector<FAnimFrame> frames;
    double totalDuration{};
    bool loop{true};
};

/// Loads validated clip rows once; texture lifetime remains owned by FResourceManager.
class FAnimationLibrary {
public:
    [[nodiscard]] static bool Initialize(entt::registry& registry, std::string_view assetId = "anim/clips");
    static void Shutdown(entt::registry& registry);

    [[nodiscard]] static const FAnimationLibrary* Find(const entt::registry& registry) noexcept;
    [[nodiscard]] std::optional<FAnimClipId> findClip(std::string_view id) const;
    [[nodiscard]] const FAnimClip* get(FAnimClipId id) const noexcept;

private:
    struct FStringHash {
        using is_transparent = void;

        [[nodiscard]] std::size_t operator()(std::string_view value) const noexcept {
            return std::hash<std::string_view>{}(value);
        }
    };

    std::string assetId_;
    std::vector<FAnimClip> clips_;
    std::unordered_map<std::string, FAnimClipId, FStringHash, std::equal_to<>> ids_;
};

} // namespace game
