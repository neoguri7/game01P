#include "core/ResourceManager.h"

#include "SDL3_image/SDL_image.h"
#include "core/AssetManager.h"
#include "core/Logger.h"

#include <tracy/Tracy.hpp>

#include <limits>
#include <utility>

namespace game {

void FResourceManager::init(SDL_Renderer* renderer) {
    ZoneScopedN("FResourceManager::init");
    renderer_ = renderer;
    if (renderer_ == nullptr) {
        LOG_ERROR("ResourceManager cannot load textures without an SDL renderer.");
        return;
    }
    LOG_INFO("ResourceManager ready.");
}

std::optional<FTextureId> FResourceManager::loadTexture(
    const FAssetManager& assets,
    std::string_view assetId,
    SDL_ScaleMode scaleMode) {
    ZoneScopedN("FResourceManager::loadTexture");
    if (assetId.empty()) {
        LOG_ERROR("Cannot load a texture with an empty asset id.");
        return std::nullopt;
    }
    if (renderer_ == nullptr) {
        LOG_ERROR("Cannot load texture '{}' because ResourceManager has no renderer.", assetId);
        return std::nullopt;
    }

    const std::string key = cacheKey(assetId, scaleMode);
    if (const auto found = textureIds_.find(key); found != textureIds_.end()) {
        return found->second;
    }
    if (failedTextures_.contains(key)) {
        return std::nullopt;
    }

    const std::optional<std::string> path = assets.resolvePath(assetId, ".png");
    if (!path) {
        LOG_ERROR("PNG texture '{}' was not found in any asset root.", assetId);
        failedTextures_.insert(key);
        return std::nullopt;
    }

    ZoneNamedN(loadZone, "LoadPngToGpu", true);
    SDL_Surface* surface = IMG_Load(path->c_str());
    if (surface == nullptr) {
        LOG_ERROR("IMG_Load failed for '{}' ({}).", *path, SDL_GetError());
        failedTextures_.insert(key);
        return std::nullopt;
    }

    const int width = surface->w;
    const int height = surface->h;
    SDL_Texture* rawTexture = SDL_CreateTextureFromSurface(renderer_, surface);
    SDL_DestroySurface(surface);
    if (rawTexture == nullptr) {
        LOG_ERROR("Texture creation failed for '{}' ({}).", *path, SDL_GetError());
        failedTextures_.insert(key);
        return std::nullopt;
    }

    FTexturePtr texture{rawTexture};
    if (!SDL_SetTextureScaleMode(texture.get(), scaleMode)) {
        LOG_ERROR("Setting scale mode failed for '{}' ({}).", *path, SDL_GetError());
        failedTextures_.insert(key);
        return std::nullopt;
    }

    if (textures_.size() >= static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
        LOG_ERROR("Texture handle capacity exhausted while loading '{}'.", assetId);
        return std::nullopt;
    }

    const FTextureId id{static_cast<std::uint32_t>(textures_.size() + 1)};
    std::shared_ptr<SDL_Texture> ownedTexture{texture.release(), FTextureDeleter{}};
    const FTextureInfo info{ownedTexture.get(), width, height};
    textures_.push_back(FTextureEntry{std::move(ownedTexture), info});
    textureIds_.emplace(key, id);
    failedTextures_.erase(key);
    return id;
}

const FTextureInfo* FResourceManager::find(FTextureId id) const noexcept {
    if (!id || id.value > textures_.size()) {
        return nullptr;
    }
    return &textures_[id.value - 1].info;
}

void FResourceManager::clear() {
    ZoneScopedN("FResourceManager::clear");
    textures_.clear();
    textureIds_.clear();
    failedTextures_.clear();
}

std::string FResourceManager::cacheKey(std::string_view assetId, SDL_ScaleMode scaleMode) {
    std::string key{assetId};
    key.push_back('\0');
    key.push_back(static_cast<char>(scaleMode));
    return key;
}

} // namespace game
