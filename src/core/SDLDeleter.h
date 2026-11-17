#pragma once

#include <SDL3/SDL.h>
#include <memory>

struct FWindowDeleter
{
	void operator()(SDL_Window* window){ SDL_DestroyWindow(window); }
};

struct FRendererDeleter
{
	void operator()(SDL_Renderer* renderer) { SDL_DestroyRenderer(renderer); }
};

struct FTextureDeleter
{
	void operator()(SDL_Texture* texture) { SDL_DestroyTexture(texture); }
};

using FWindowPtr = std::unique_ptr<SDL_Window, FWindowDeleter>;
using FRendererPtr = std::unique_ptr<SDL_Renderer, FRendererDeleter>;
using FTexturePtr = std::unique_ptr<SDL_Texture, FTextureDeleter>;

