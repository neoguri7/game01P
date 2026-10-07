#include "ecs/systems/EmberGridRenderSystem.h"

#include <algorithm>
#include <cmath>
#include "core/EngineState.h"
#include "ecs/components/FCamera.h"
#include "ecs/components/FDead.h"
#include "ecs/components/FFaction.h"
#include "ecs/components/FHealth.h"
#include "ecs/components/FTag.h"
#include "gameplay/grid/FCameraMath.h"
#include "gameplay/grid/FGridSelection.h"
#include "gameplay/grid/FGridTerrain.h"
#include "presentation/FDungeonArt.h"

namespace game::ecs {

void EmberGridRenderSystem::render(entt::registry& registry) {
    const auto* rendererContext = registry.ctx().find<SDL_Renderer*>();
    const auto* grid = registry.ctx().find<FGridMetrics>();
    const auto* terrain = registry.ctx().find<FGridTerrain>();
    if (!rendererContext || !*rendererContext || !grid || !terrain) return;
    auto* renderer = *rendererContext;
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    const auto& window = registry.ctx().get<FEngineWindowState>();
    const FViewport viewport{static_cast<float>(window.width), static_cast<float>(window.height)};
    FCamera camera{};
    for (const auto entity : registry.view<FCamera, FTag>()) {
        if (registry.get<FTag>(entity).value == "battle_camera") camera = registry.get<FCamera>(entity);
    }
    const auto rectFor = [&](FGridPosition cell) {
        const auto point = WorldToScreen(GridCellTopLeftToWorld(*grid, cell.x, cell.y), camera, viewport);
        return SDL_FRect{point.x, point.y, grid->tileSize * camera.zoom, grid->tileSize * camera.zoom};
    };
    const auto first = rectFor({0, 0});
    const SDL_FRect screen{0.f, 0.f, viewport.width, viewport.height};
    for (float y = 0; y < screen.h; y += 64.f) {
        for (float x = 0; x < screen.w; x += 64.f) {
            DrawDungeonSprite(registry, EDungeonSprite::Backdrop, {x, y, 64.f, 64.f});
        }
    }
    SDL_SetRenderDrawColor(renderer, 6, 8, 16, 150);
    SDL_RenderFillRect(renderer, &screen);
    for (int ring = 0; ring < 14; ++ring) {
        const float inset = static_cast<float>(ring) * 6.f;
        const SDL_FRect edge{inset, inset, screen.w - inset * 2.f, screen.h - inset * 2.f};
        SDL_SetRenderDrawColor(renderer, 0, 0, 6, static_cast<Uint8>(90 - ring * 6));
        SDL_RenderRect(renderer, &edge);
        const SDL_FRect thick{inset + 1.f, inset + 1.f, edge.w - 2.f, edge.h - 2.f};
        SDL_RenderRect(renderer, &thick);
    }
    for (int glow = 4; glow >= 1; --glow) {
        const float pad = 8.f + static_cast<float>(glow) * 5.f;
        const SDL_FRect halo{first.x - pad, first.y - pad, first.w * 8 + pad * 2, first.h * 8 + pad * 2};
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, static_cast<Uint8>(34 + (4 - glow) * 12));
        SDL_RenderFillRect(renderer, &halo);
    }
    SDL_FRect frame{first.x - 8, first.y - 8, first.w * 8 + 16, first.h * 8 + 16};
    SDL_SetRenderDrawColor(renderer, 24, 22, 30, 255);
    SDL_RenderFillRect(renderer, &frame);
    SDL_SetRenderDrawColor(renderer, 120, 104, 78, 255);
    SDL_RenderRect(renderer, &frame);
    const SDL_FRect innerFrame{frame.x + 3, frame.y + 3, frame.w - 6, frame.h - 6};
    SDL_SetRenderDrawColor(renderer, 58, 50, 44, 255);
    SDL_RenderRect(renderer, &innerFrame);
    for (int y = 0; y < grid->height; ++y) {
        for (int x = 0; x < grid->width; ++x) {
            const auto rect = rectFor({x, y});
            DrawDungeonSprite(registry, terrain->at(x, y) == ETile::Wall ?
                              WallVariant(x, y) : FloorVariant(x, y), rect);
            if (terrain->at(x, y) == ETile::Trap) DrawDungeonSprite(registry, EDungeonSprite::Spikes, rect);
            if (terrain->at(x, y) != ETile::Wall) {
                SDL_SetRenderDrawColor(renderer, 6, 8, 18, 40);
                SDL_RenderFillRect(renderer, &rect);
                SDL_SetRenderDrawColor(renderer, 10, 10, 20, 28);
                SDL_RenderRect(renderer, &rect);
            }
        }
    }
    const auto* selection = registry.ctx().find<FGridSelection>();
    if (selection) {
        for (const auto cell : selection->reachable) {
            const auto rect = rectFor(cell);
            SDL_SetRenderDrawColor(renderer, 0, 200, 255, 35);
            SDL_RenderFillRect(renderer, &rect);
            SDL_SetRenderDrawColor(renderer, 0, 200, 255, 115);
            SDL_RenderRect(renderer, &rect);
        }
        for (const auto cell : selection->affected) {
            if (!IsInsideGrid(*grid, cell.x, cell.y)) continue;
            const auto rect = rectFor(cell);
            SDL_SetRenderDrawColor(renderer, 230, 80, 80, 65);
            SDL_RenderFillRect(renderer, &rect);
        }
        if (selection->hovered && IsInsideGrid(*grid, selection->hovered->x, selection->hovered->y)) {
            const auto rect = rectFor(*selection->hovered);
            SDL_SetRenderDrawColor(renderer, 255, 220, 60, 200);
            SDL_RenderRect(renderer, &rect);
        }
    }
    for (int y = 0; y < grid->height; ++y) {
        for (const auto entity : registry.view<FGridPosition, FHealth, FUnitArt>()) {
            const auto cell = registry.get<FGridPosition>(entity);
            if (cell.y != y) continue;
            auto rect = rectFor(cell);
            const bool dead = registry.all_of<FDead>(entity);
            const bool party = registry.all_of<FPartyMember>(entity);
            if (selection && selection->selected == entity && !dead) {
                SDL_SetRenderDrawColor(renderer, 255, 220, 60, 55);
                SDL_RenderFillRect(renderer, &rect);
                SDL_SetRenderDrawColor(renderer, 255, 220, 60, 255);
                SDL_RenderRect(renderer, &rect);
            }
            SDL_FRect marker{rect.x + rect.w * .25f, rect.y + rect.h * .87f, rect.w * .5f, 3.f};
            SDL_SetRenderDrawColor(renderer, party ? 80 : 230, party ? 140 : 80, party ? 255 : 80, dead ? 60 : 210);
            SDL_RenderFillRect(renderer, &marker);
            DrawDungeonSprite(registry, registry.get<FUnitArt>(entity).sprite, rect, dead ? 65 : 255);
            if (!dead) {
                const auto& health = registry.get<FHealth>(entity);
                SDL_FRect bar{rect.x + 9 * camera.zoom, rect.y + 3 * camera.zoom,
                              rect.w - 18 * camera.zoom, 4 * camera.zoom};
                SDL_SetRenderDrawColor(renderer, 20, 20, 30, 255);
                SDL_RenderFillRect(renderer, &bar);
                bar.w *= std::clamp(static_cast<float>(health.current) / std::max(1, health.max), 0.f, 1.f);
                SDL_SetRenderDrawColor(renderer, party ? 80 : 230, party ? 140 : 80, party ? 255 : 80, 255);
                SDL_RenderFillRect(renderer, &bar);
            }
        }
    }
}

} // namespace game::ecs
