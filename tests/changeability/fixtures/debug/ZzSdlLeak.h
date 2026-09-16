#pragma once

#include <SDL3/SDL.h>

// V26 [R26] platform API used outside the platform/core boundary (DOOM 3 sys leak shape: every subsystem
// reaching into neo/sys/win32 directly instead of through idSys, neo/sys/sys_public.h:537).
inline void ZzDebugClear( SDL_Renderer * renderer ) {
	SDL_SetRenderDrawColor( renderer, 0, 0, 0, 255 );
	SDL_RenderClear( renderer );
}
