#pragma once

#include "ecs/components/FCollider.h"
#include "ecs/components/FDebugPrimitive.h"
#include "ecs/components/FLayer.h"
#include "ecs/components/FPosition.h"
#include "ecs/systems/ISystem.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <entt/entt.hpp>
#include <string>
#include <tracy/Tracy.hpp>
#include <vector>

namespace game::ecs {

struct DebugPrimitiveRenderSystem : public ISystem {
    void update(entt::registry& /*reg*/, float /*dt*/) override {}

    void render(entt::registry& reg) override {
        ZoneScopedN("DebugPrimitiveRenderSystem");

        auto* rendPtr = reg.ctx().find<SDL_Renderer*>();
        if (!rendPtr || *rendPtr == nullptr) return;

        SDL_Renderer* renderer = *rendPtr;
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

        auto view = reg.view<const FPosition, const FDebugPrimitive>();
        std::vector<entt::entity> entities(view.begin(), view.end());
        std::ranges::sort(entities, [&reg](entt::entity lhs, entt::entity rhs) {
            const int lhsLayer = reg.all_of<FLayer>(lhs) ? reg.get<FLayer>(lhs).depth : 0;
            const int rhsLayer = reg.all_of<FLayer>(rhs) ? reg.get<FLayer>(rhs).depth : 0;
            return lhsLayer < rhsLayer;
        });

        for (auto entity : entities) {
            const auto& pos = view.get<const FPosition>(entity);
            const auto& primitive = view.get<const FDebugPrimitive>(entity);

            SDL_FRect rect{pos.x, pos.y, primitive.width, primitive.height};
            if (primitive.filled) {
                SDL_SetRenderDrawColor(renderer, primitive.fillR, primitive.fillG, primitive.fillB, primitive.fillA);
                SDL_RenderFillRect(renderer, &rect);
            }

            SDL_SetRenderDrawColor(renderer, primitive.outlineR, primitive.outlineG, primitive.outlineB, primitive.outlineA);
            SDL_RenderRect(renderer, &rect);

            if (primitive.drawCollider && reg.all_of<FCollider>(entity)) {
                DrawCollider(renderer, pos, reg.get<FCollider>(entity));
            }
        }
    }

    std::string name() const override { return "DebugPrimitiveRenderSystem"; }

private:
    static void DrawCollider(SDL_Renderer* renderer, const FPosition& pos, const FCollider& collider) {
        SDL_SetRenderDrawColor(renderer, 255, 220, 80, 255);

        if (collider.type == EColliderType::AABB) {
            const SDL_FRect bounds{
                pos.x + collider.offset.x - collider.halfWidth,
                pos.y + collider.offset.y - collider.halfHeight,
                collider.halfWidth * 2.f,
                collider.halfHeight * 2.f
            };
            SDL_RenderRect(renderer, &bounds);
            return;
        }

        DrawCircle(renderer, pos.x + collider.offset.x, pos.y + collider.offset.y, collider.halfWidth);
    }

    static void DrawCircle(SDL_Renderer* renderer, float centerX, float centerY, float radius) {
        constexpr int segments = 32;
        constexpr float tau = 6.28318530717958647692f;

        float previousX = centerX + radius;
        float previousY = centerY;

        for (int i = 1; i <= segments; ++i) {
            const float angle = tau * static_cast<float>(i) / static_cast<float>(segments);
            const float nextX = centerX + std::cos(angle) * radius;
            const float nextY = centerY + std::sin(angle) * radius;
            SDL_RenderLine(renderer, previousX, previousY, nextX, nextY);
            previousX = nextX;
            previousY = nextY;
        }
    }
};

} // namespace game::ecs
