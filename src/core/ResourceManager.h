#pragma once
#include <SDL3/SDL_render.h>
#include <string>
#include <unordered_map>
#include <unordered_set>

/// Holds GPU textures loaded from disk. Best used as registry.ctx() service (lifetime tied to registry).
class FResourceManager
{
public:
    FResourceManager() = default;
    ~FResourceManager() { clear(); }

    void init(SDL_Renderer* r);

    // safe load: returns nullptr or use std::expected
    SDL_Texture* tryLoadTexture(const std::string& filePath);
    SDL_Texture* getTexture(const std::string& filePath) const;

    [[deprecated("Use clear()")]] void CLear() { clear(); }
    void clear();

private:
    std::unordered_map<std::string, SDL_Texture*> textures;
    std::unordered_set<std::string> failedTextures;
    SDL_Renderer* renderer = nullptr;
};
