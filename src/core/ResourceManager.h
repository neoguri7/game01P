#pragma once

#include "core/AssetHandles.h"
#include "core/SDLDeleter.h"

#include <SDL3/SDL_render.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace game {

struct FAssetManager;

struct FTextureSize {
    int width{};
    int height{};
};

struct FTextureInfo {
    SDL_Texture* texture{};
    int width{};
    int height{};
};

/// Owns renderer-bound textures for the renderer's lifetime. Asset names remain on the CPU-side boundary.
class FResourceManager {
public:
    FResourceManager() = default;
    FResourceManager(const FResourceManager&) = delete;
    FResourceManager& operator=(const FResourceManager&) = delete;
    ~FResourceManager() = default;

    void init(SDL_Renderer* renderer);

    [[nodiscard]] std::optional<FTextureId> loadTexture(
        const FAssetManager& assets,
        std::string_view assetId,
        SDL_ScaleMode scaleMode = SDL_SCALEMODE_NEAREST);
    [[nodiscard]] const FTextureInfo* find(FTextureId id) const noexcept;
    [[nodiscard]] std::optional<FTextureSize> findSize(FTextureId id) const noexcept;

    void clear();

private:
    struct FTextureEntry {
        FTexturePtr texture;
        FTextureInfo info;
    };

    [[nodiscard]] static std::string cacheKey(std::string_view assetId, SDL_ScaleMode scaleMode);

    SDL_Renderer* renderer_{};
    // Handles are never reused, including after clear(), so cached handles cannot alias new textures.
    std::uint64_t nextTextureId_{1};
    std::unordered_map<std::uint32_t, FTextureEntry> textures_;
    std::unordered_map<std::string, FTextureId> textureIds_;
    std::unordered_set<std::string> failedTextures_;
};

} // namespace game
