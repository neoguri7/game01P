#pragma once

#include <span>
#include <vector>
#include <SDL3/SDL.h>
#include "ecs/systems/ISystem.h"

namespace game::ecs {

struct EmberGridRenderSystem : ISystem {
    void update(entt::registry&, float) override {}
    void render(entt::registry& registry) override;
    std::string name() const override { return "EmberGridRenderSystem"; }
};

/// WP-51: triangle indices for a columns x rows cell grid over (columns+1)*(rows+1) row-major vertices: first
/// every cell's upper-right triangle, then every cell's lower-left one, so no two consecutive triangles share
/// two vertices. SDL 3.4's software renderer turns a consecutive pair forming a one-colour axis-aligned rectangle
/// into SDL_RenderFillRect with SDL_BLENDMODE_BLEND (SDL_render.c:5230), which painted the MOD light mesh opaque.
[[nodiscard]] std::vector<int> LightMeshIndices(int columns, int rows);
/// WP-51: multiplies a columns x rows grid spanning (0,0)-(width,height) over what is drawn (SDL_BLENDMODE_MOD,
/// draw blend mode restored to BLEND after). `colors` are the (columns+1)*(rows+1) vertex colours, row-major.
/// DrawLightMesh builds its colours with LightAt and calls this; the test calls it directly.
void DrawModulateMesh(SDL_Renderer* renderer, std::span<const SDL_FColor> colors, int columns, int rows, float width,
                      float height);

} // namespace game::ecs
