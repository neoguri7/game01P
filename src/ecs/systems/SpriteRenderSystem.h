#pragma once

#include "ecs/systems/ISystem.h"
#include "ecs/components/FPosition.h"
#include "ecs/components/FSprite.h"
#include "ecs/components/FLayer.h"
#include "core/ResourceManager.h"

#include <entt/entt.hpp>
#include <SDL3/SDL.h>
#include <algorithm>
#include <string>
#include <vector>
#include <tracy/Tracy.hpp>

namespace game::ecs {

struct SpriteRenderSystem : public ISystem {
    void update(entt::registry& /*reg*/, float /*dt*/) override {}

    void render(entt::registry& reg) override {
        ZoneScopedN("SpriteRenderSystem");

        auto* rm = reg.ctx().find<FResourceManager>();
        if (!rm) return;

        // For now we assume the renderer is globally available via ctx.
        // In Engine we store SDL_Renderer* under registry.ctx() as : SDL_Renderer* rendererCtx;
        auto* rendPtr = reg.ctx().find<SDL_Renderer*>();
        if (!rendPtr || *rendPtr == nullptr) {
            // if you want to handle this more elegantly later
            return;
        }

        SDL_Renderer* rend = *rendPtr;

        auto view = reg.view<const FPosition, const FSprite>();
        std::vector<entt::entity> entities(view.begin(), view.end());
        std::ranges::sort(entities, [&reg](entt::entity lhs, entt::entity rhs) {
            const int lhsLayer = reg.all_of<FLayer>(lhs) ? reg.get<FLayer>(lhs).depth : 0;
            const int rhsLayer = reg.all_of<FLayer>(rhs) ? reg.get<FLayer>(rhs).depth : 0;
            return lhsLayer < rhsLayer;
        });

        for (auto entity : entities) {
            const auto& pos = view.get<const FPosition>(entity);
            const auto& spr = view.get<const FSprite>(entity);

            SDL_Texture* tex = rm->tryLoadTexture(spr.texturePath);
            if (!tex) {
                continue;
            }

            SDL_FRect dst = { pos.x, pos.y, static_cast<float>(spr.width), static_cast<float>(spr.height) };
            SDL_RenderTexture(rend, tex, nullptr, &dst);  // SDL_RenderTexture for float rect (SDL3)
        }
    }

    std::string name() const override { return "SpriteRenderSystem"; }
};

} // namespace game::ecs
