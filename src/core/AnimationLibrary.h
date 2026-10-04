#pragma once

#include "core/AssetHandles.h"

#include <entt/entt.hpp>

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
    std::vector<FAnimClip> clips_;
    std::unordered_map<std::string, FAnimClipId> ids_;
};

} // namespace game
