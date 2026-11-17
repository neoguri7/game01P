#pragma once

#include "core/ResourceManager.h"
#include "core/Logger.h"
#include "core/EngineState.h"
#include "ecs/components/FCamera.h"
#include "ecs/components/FGridPosition.h"
#include "ecs/components/FHealth.h"
#include "ecs/components/FTag.h"
#include "ecs/components/FLayer.h"
#include "ecs/components/FPosition.h"
#include "ecs/components/FSprite.h"
#include "ecs/systems/ISystem.h"
#include "gameplay/grid/FCameraMath.h"

#include <entt/entt.hpp>
#include <SDL3/SDL.h>
#include <tracy/Tracy.hpp>

#include <algorithm>
#include <string>
#include <unordered_set>
#include <vector>

namespace game::ecs {

struct SpriteRenderSystem : public ISystem {
    void update(entt::registry&, float) override {}

    void render(entt::registry& registry) override {
        ZoneScopedN("SpriteRenderSystem");

        FResourceManager* resources = registry.ctx().find<FResourceManager>();
        SDL_Renderer** rendererContext = registry.ctx().find<SDL_Renderer*>();
        if (resources == nullptr || rendererContext == nullptr || *rendererContext == nullptr) {
            if (!missingServiceLogged_) {
                LOG_ERROR("SpriteRenderSystem requires FResourceManager and SDL_Renderer services.");
                missingServiceLogged_ = true;
            }
            return;
        }

        SDL_Renderer* renderer = *rendererContext;
        FViewport viewport{};
        if (const auto* window = registry.ctx().find<FEngineWindowState>()) {
            viewport = {static_cast<float>(window->width), static_cast<float>(window->height)};
        }
        FCamera camera{};
        bool battleCamera = false;
        for (const auto entity : registry.view<const FCamera, const FTag>()) {
            if (registry.get<const FTag>(entity).value == "battle_camera") {
                camera = registry.get<const FCamera>(entity);
                battleCamera = true;
                break;
            }
        }
        auto view = registry.view<const FPosition, const FSprite>();
        std::vector<entt::entity> entities(view.begin(), view.end());
        std::ranges::stable_sort(entities, [&registry](entt::entity lhs, entt::entity rhs) {
            const int lhsLayer = registry.all_of<FLayer>(lhs) ? registry.get<FLayer>(lhs).depth : 0;
            const int rhsLayer = registry.all_of<FLayer>(rhs) ? registry.get<FLayer>(rhs).depth : 0;
            return lhsLayer < rhsLayer;
        });

        for (const entt::entity entity : entities) {
            const FPosition& position = view.get<const FPosition>(entity);
            const FSprite& sprite = view.get<const FSprite>(entity);
            if (!sprite.texture) {
                continue;
            }

            const FTextureInfo* texture = resources->find(sprite.texture);
            if (texture == nullptr) {
                if (invalidTextureIds_.insert(sprite.texture.value).second) {
                    LOG_ERROR("Entity {} references invalid texture handle {}.",
                              entt::to_integral(entity), sprite.texture.value);
                }
                continue;
            }

            const SDL_FRect* source = nullptr;
            SDL_FRect sourceRect{sprite.source.x, sprite.source.y, sprite.source.w, sprite.source.h};
            if (sprite.source.w > 0.0f && sprite.source.h > 0.0f) {
                source = &sourceRect;
            }

            SDL_FRect destination{
                position.x + sprite.offsetX,
                position.y + sprite.offsetY,
                sprite.width,
                sprite.height};
            if (battleCamera && registry.all_of<FGridPosition>(entity)) {
                const auto screen = WorldToScreen({destination.x, destination.y}, camera, viewport);
                destination = {screen.x, screen.y, destination.w * camera.zoom, destination.h * camera.zoom};
            }
            const auto* health = registry.try_get<FHealth>(entity);
            SDL_SetTextureAlphaMod(texture->texture, health && health->current <= 0 ? 65 : 255);
            SDL_FlipMode flip = SDL_FLIP_NONE;
            if (sprite.flipX) {
                flip = static_cast<SDL_FlipMode>(flip | SDL_FLIP_HORIZONTAL);
            }
            if (sprite.flipY) {
                flip = static_cast<SDL_FlipMode>(flip | SDL_FLIP_VERTICAL);
            }

            const bool rendered = flip == SDL_FLIP_NONE
                ? SDL_RenderTexture(renderer, texture->texture, source, &destination)
                : SDL_RenderTextureRotated(renderer, texture->texture, source, &destination, 0.0, nullptr, flip);
            SDL_SetTextureAlphaMod(texture->texture, 255);
            if (!rendered && renderFailures_.insert(sprite.texture.value).second) {
                LOG_ERROR("Rendering texture {} for entity {} failed ({}).",
                          sprite.texture.value, entt::to_integral(entity), SDL_GetError());
            }
        }
    }

    std::string name() const override { return "SpriteRenderSystem"; }

private:
    bool missingServiceLogged_{};
    std::unordered_set<std::uint32_t> invalidTextureIds_;
    std::unordered_set<std::uint32_t> renderFailures_;
};

} // namespace game::ecs
