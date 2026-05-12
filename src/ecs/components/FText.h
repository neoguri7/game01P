#pragma once
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <string>
#include <cstdint>

namespace game {

/// Text rendering component — paired with a position for on-screen text.
struct FText {
    std::string     content;
    std::string     fontPath;
    int             fontSize{24};
    SDL_Color       color{255, 255, 255, 255};
    SDL_Surface*    cachedSurface{nullptr};
    TTF_Font*       cachedFont{nullptr};
};

inline const char* SDL_GetErrorSafe() {
    return SDL_GetError();
}

} // namespace game
