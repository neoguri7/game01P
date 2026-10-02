#pragma once

#include <entt/entt.hpp>
#include <string>

#include <SDL3/SDL.h>

#include "core/EngineState.h"
#include "ecs/components/FCamera.h"
#include "ecs/components/FFaction.h"
#include "ecs/components/FGridPosition.h"
#include "ecs/components/FPosition.h"
#include "ecs/components/FTag.h"
#include "ecs/systems/ISystem.h"
#include "gameplay/grid/FCameraMath.h"
#include "gameplay/grid/FGridSelection.h"
#include "gameplay/grid/FGridTypes.h"

namespace game::ecs {

struct GridRenderSystem : ISystem {
    void update(entt::registry&, float) override {}

    void render(entt::registry& registry) override {
        auto* rendererContext = registry.ctx().find<SDL_Renderer*>();
        if (!rendererContext || !*rendererContext) return;
        SDL_Renderer* renderer = *rendererContext;
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

        const auto* gridContext = registry.ctx().find<FGridMetrics>();
        const FGridMetrics defaultMetrics{};
        const auto& metrics = gridContext ? *gridContext : defaultMetrics;
        if (metrics.width <= 0 || metrics.height <= 0 || metrics.tileSize <= 0.f) return;

        FViewport viewport{};
        if (const auto* window = registry.ctx().find<FEngineWindowState>();
            window && window->width > 0 && window->height > 0) {
            viewport.width = static_cast<float>(window->width);
            viewport.height = static_cast<float>(window->height);
        }

        FCamera camera{};
        for (const auto entity : registry.view<const FCamera, const FTag>()) {
            if (registry.get<const FTag>(entity).value == "battle_camera") {
                camera = registry.get<const FCamera>(entity);
                break;
            }
        }

        if (const auto* selection = registry.ctx().find<FGridSelection>()) {
            SDL_SetRenderDrawColor(renderer, 0, 200, 255, 90);
            for (const auto cell : selection->reachable) {
                if (!IsInsideGrid(metrics, cell.x, cell.y)) continue;
                DrawCellFill(renderer, metrics, cell, camera, viewport);
            }
        }

        SDL_SetRenderDrawColor(renderer, 70, 70, 90, 255);
        for (int x = 0; x <= metrics.width; ++x) {
            const auto top = WorldToScreen(GridCellTopLeftToWorld(metrics, x, 0), camera, viewport);
            const auto bottom = WorldToScreen(GridCellTopLeftToWorld(metrics, x, metrics.height), camera, viewport);
            SDL_RenderLine(renderer, top.x, top.y, bottom.x, bottom.y);
        }
        for (int y = 0; y <= metrics.height; ++y) {
            const auto left = WorldToScreen(GridCellTopLeftToWorld(metrics, 0, y), camera, viewport);
            const auto right = WorldToScreen(GridCellTopLeftToWorld(metrics, metrics.width, y), camera, viewport);
            SDL_RenderLine(renderer, left.x, left.y, right.x, right.y);
        }

        const auto* selection = registry.ctx().find<FGridSelection>();
        auto units = registry.view<const FGridPosition, const FPosition>();
        for (const auto entity : units) {
            const auto cell = units.get<const FGridPosition>(entity);
            if (!IsInsideGrid(metrics, cell.x, cell.y)) continue;

            Uint8 red = 150;
            Uint8 green = 150;
            Uint8 blue = 150;
            if (registry.all_of<FPartyMember>(entity)) {
                red = 80; green = 140; blue = 255;
            } else if (registry.all_of<FEnemy>(entity)) {
                red = 230; green = 80; blue = 80;
            }

            SDL_SetRenderDrawColor(renderer, red, green, blue, 255);
            DrawUnitFill(renderer, metrics, cell, camera, viewport);
            if (selection && selection->selected == entity) {
                SDL_SetRenderDrawColor(renderer, 255, 220, 60, 255);
            } else {
                SDL_SetRenderDrawColor(renderer, 20, 20, 30, 255);
            }
            DrawUnitOutline(renderer, metrics, cell, camera, viewport);
        }
    }

    std::string name() const override { return "GridRenderSystem"; }

private:
    static SDL_FRect CellRect(const FGridMetrics& metrics, FGridPosition cell, const FCamera& camera, const FViewport& viewport) {
        const auto worldTopLeft = GridCellTopLeftToWorld(metrics, cell.x, cell.y);
        const auto worldBottomRight = worldTopLeft + glm::vec2{metrics.tileSize};
        const auto topLeft = WorldToScreen(worldTopLeft, camera, viewport);
        const auto bottomRight = WorldToScreen(worldBottomRight, camera, viewport);
        return {topLeft.x, topLeft.y, bottomRight.x - topLeft.x, bottomRight.y - topLeft.y};
    }

    static void DrawCellFill(SDL_Renderer* renderer, const FGridMetrics& metrics, FGridPosition cell, const FCamera& camera, const FViewport& viewport) {
        const auto rect = CellRect(metrics, cell, camera, viewport);
        SDL_RenderFillRect(renderer, &rect);
    }

    static SDL_FRect UnitRect(const FGridMetrics& metrics, FGridPosition cell, const FCamera& camera, const FViewport& viewport) {
        auto rect = CellRect(metrics, cell, camera, viewport);
        constexpr float inset = 6.f;
        rect.x += inset;
        rect.y += inset;
        rect.w -= inset * 2.f;
        rect.h -= inset * 2.f;
        return rect;
    }

    static void DrawUnitFill(SDL_Renderer* renderer, const FGridMetrics& metrics, FGridPosition cell, const FCamera& camera, const FViewport& viewport) {
        const auto rect = UnitRect(metrics, cell, camera, viewport);
        SDL_RenderFillRect(renderer, &rect);
    }

    static void DrawUnitOutline(SDL_Renderer* renderer, const FGridMetrics& metrics, FGridPosition cell, const FCamera& camera, const FViewport& viewport) {
        const auto rect = UnitRect(metrics, cell, camera, viewport);
        SDL_RenderRect(renderer, &rect);
    }
};

} // namespace game::ecs
