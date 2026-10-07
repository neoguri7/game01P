#pragma once

#include <entt/entt.hpp>
#include <string>

#include <SDL3/SDL.h>

#include "core/AssetManager.h"
#include "core/EngineState.h"
#include "core/ResourceManager.h"
#include "ecs/components/FCamera.h"
#include "ecs/components/FFaction.h"
#include "ecs/components/FHealth.h"
#include "ecs/components/FGridPosition.h"
#include "ecs/components/FPosition.h"
#include "ecs/components/FSprite.h"
#include "ecs/components/FTag.h"
#include "ecs/systems/ISystem.h"
#include "ecs/systems/TurnSystem.h"
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

        SDL_SetRenderDrawColor(renderer, 19, 35, 31, 255);
        SDL_RenderFillRect(renderer, nullptr);
        const auto* assets = registry.ctx().find<FAssetManager>();
        auto* resources = registry.ctx().find<FResourceManager>();
        const auto texture = [&](const char* id) -> const FTextureInfo* {
            if (!assets || !resources) return nullptr;
            const auto handle = resources->loadTexture(*assets, id);
            return handle ? resources->find(*handle) : nullptr;
        };
        const auto* ground = texture("generated/forest-local/ground");
        for (int y = -3; y < metrics.height + 3; ++y) {
            for (int x = -5; x < metrics.width + 5; ++x) {
                const auto rect = CellRect(metrics, {x, y}, camera, viewport);
                const bool path = (x == 3 || x == 4) && y >= -1 && y <= metrics.height;
                const int variant = path ? 4 + ((x + y + 32) % 2) : ((x * 7 + y * 11 + 256) % 4);
                const SDL_FRect source{static_cast<float>(variant * 32), 0.f, 32.f, 32.f};
                if (ground) SDL_RenderTexture(renderer, ground->texture, &source, &rect);
                if (!IsInsideGrid(metrics, x, y)) {
                    SDL_SetRenderDrawColor(renderer, 10, 26, 24, 95);
                    SDL_RenderFillRect(renderer, &rect);
                }
            }
        }
        const auto prop = [&](const FTextureInfo* image, float x, float y, float size) {
            if (!image) return;
            const auto screen = WorldToScreen(metrics.origin + glm::vec2{x, y} * metrics.tileSize, camera, viewport);
            const SDL_FRect rect{screen.x, screen.y, size * metrics.tileSize * camera.zoom, size * metrics.tileSize * camera.zoom};
            SDL_RenderTexture(renderer, image->texture, nullptr, &rect);
        };
        const auto* oak = texture("generated/forest-local/oak");
        for (int y = -2; y <= metrics.height; y += 2) {
            prop(oak, -2.1f, static_cast<float>(y), 2.f);
            prop(oak, static_cast<float>(metrics.width) + 0.1f, static_cast<float>(y) - 0.5f, 2.f);
        }
        for (int x = 0; x < metrics.width; x += 2) {
            prop(oak, static_cast<float>(x), -2.2f, 2.f);
            prop(oak, static_cast<float>(x), static_cast<float>(metrics.height) + 0.3f, 2.f);
        }
        const auto* stone = texture("generated/forest-local/waystone");
        prop(stone, -0.9f, 3.f, 0.8f);
        prop(stone, static_cast<float>(metrics.width) + 0.1f, 4.5f, 0.8f);

        if (const auto* selection = registry.ctx().find<FGridSelection>()) {
            SDL_SetRenderDrawColor(renderer, 220, 185, 100, 18);
            for (const auto cell : selection->targetable) {
                if (IsInsideGrid(metrics, cell.x, cell.y)) DrawCellFill(renderer, metrics, cell, camera, viewport);
            }
            SDL_SetRenderDrawColor(renderer, 0, 200, 255, 55);
            for (const auto cell : selection->reachable) {
                if (!IsInsideGrid(metrics, cell.x, cell.y)) continue;
                DrawCellFill(renderer, metrics, cell, camera, viewport);
            }
            SDL_SetRenderDrawColor(renderer, 230, 170, 80, 75);
            for (const auto cell : selection->preview) {
                if (IsInsideGrid(metrics, cell.x, cell.y)) DrawCellFill(renderer, metrics, cell, camera, viewport);
            }
            if (selection->hovered) {
                SDL_SetRenderDrawColor(renderer, 235, 218, 166, 230);
                const auto rect = CellRect(metrics, *selection->hovered, camera, viewport);
                SDL_RenderRect(renderer, &rect);
            }
        }

        SDL_SetRenderDrawColor(renderer, 167, 186, 133, 40);
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
        const auto* turn = registry.ctx().find<FTurnRuntime>();
        const auto snapshot = turn ? turn->controller.snapshot() : FTurnSnapshot{};
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
            if (const auto* health = registry.try_get<FHealth>(entity); health && health->current <= 0) {
                red = 65; green = 65; blue = 65;
            }

            if (!registry.all_of<FSprite>(entity)) {
                SDL_SetRenderDrawColor(renderer, red, green, blue, 255);
                DrawUnitFill(renderer, metrics, cell, camera, viewport);
            }
            SDL_SetRenderDrawColor(renderer, red, green, blue, 180);
            auto base = UnitRect(metrics, cell, camera, viewport);
            base.y += base.h - 4.f;
            base.h = 4.f;
            SDL_RenderFillRect(renderer, &base);
            if ((snapshot.currentTurn && snapshot.currentTurn->actor == entity)
                || (selection && selection->selected == entity)) {
                SDL_SetRenderDrawColor(renderer, 255, 220, 60, 255);
                DrawUnitOutline(renderer, metrics, cell, camera, viewport);
            }
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
        const float inset = kUnitHitboxInset * camera.zoom;
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
