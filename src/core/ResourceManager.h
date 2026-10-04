#pragma once

#include "core/AssetHandles.h"
#include "core/SDLDeleter.h"

#include <SDL3/SDL_render.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace game {

struct FAssetManager;

struct FTextureInfo {
    SDL_Texture* texture{};
    int width{};
    int height{};
};

/// Owns renderer-bound textures for the renderer's lifetime. Asset names remain on the CPU-side boundary.
class FResourceManager {
public:
    FResourceManager() = default;
    ~FResourceManager() = default;

    void init(SDL_Renderer* renderer);

    [[nodiscard]] std::optional<FTextureId> loadTexture(
        const FAssetManager& assets,
        std::string_view assetId,
        SDL_ScaleMode scaleMode = SDL_SCALEMODE_NEAREST);
    [[nodiscard]] const FTextureInfo* find(FTextureId id) const noexcept;

    void clear();

private:
    struct FTextureEntry {
        // EnTT context storage requires copyable service values; copied contexts share texture ownership.
        std::shared_ptr<SDL_Texture> texture;
        FTextureInfo info;
    };

    [[nodiscard]] static std::string cacheKey(std::string_view assetId, SDL_ScaleMode scaleMode);

    SDL_Renderer* renderer_{};
    std::vector<FTextureEntry> textures_;
    std::unordered_map<std::string, FTextureId> textureIds_;
    std::unordered_set<std::string> failedTextures_;
};

} // namespace game
