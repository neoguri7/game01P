#include "core/ResourceManager.h"

#include "SDL3_image/SDL_image.h"
#include <tracy/Tracy.hpp>

#include "core/Logger.h"

void FResourceManager::init(SDL_Renderer* r) {
    ZoneScopedN("ResourceManager::init");
    renderer = r;
    LOG_INFO("ResourceManager ready.");
}

SDL_Texture* FResourceManager::tryLoadTexture(const std::string& filePath) {
    ZoneScopedN("ResourceManager::tryLoadTexture");
    if (filePath.empty()) return nullptr;
    if (auto it = textures.find(filePath); it != textures.end()) {
        return it->second;
    }
    if (failedTextures.contains(filePath)) return nullptr;

    ZoneNamedN(loadZone, "LoadFromDiskAndGPU", true);
    SDL_Surface* surface = IMG_Load(filePath.c_str());
    if (!surface) {
        LOG_ERROR("IMG_Load failed: {} ({})", filePath, SDL_GetError());
        failedTextures.insert(filePath);
        return nullptr;
    }
    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surface);
    SDL_DestroySurface(surface);

    if (!tex) {
        LOG_ERROR("Texture creation failed for {} ({})", filePath, SDL_GetError());
        failedTextures.insert(filePath);
        return nullptr;
    }

    textures[filePath] = tex;
    failedTextures.erase(filePath);
    return tex;
}

SDL_Texture* FResourceManager::getTexture(const std::string& filePath) const {
    if (auto it = textures.find(filePath); it != textures.end()) {
        return it->second;
    }
    return nullptr;
}

void FResourceManager::clear() {
    ZoneScoped;
    for (auto& [k, t] : textures) {
        SDL_DestroyTexture(t);
    }
    textures.clear();
    failedTextures.clear();
}
