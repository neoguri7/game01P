#pragma once
#include "core/events/FCollisionEvent.h"
#include "core/events/FEventBus.h"
#include "utils/cute_c2.h"
#include "ecs/components/FCollider.h"
#include "ecs/components/FPosition.h"
#include "ecs/systems/ISystem.h"
#include <entt/entt.hpp>
#include <vector>
#include <string>
#include <tracy/Tracy.hpp>

namespace game {

struct CollisionSystem : public ecs::ISystem {
    void update(entt::registry& reg, float /*dt*/) override {
        ZoneScopedN("CollisionSystem");

        auto* events = reg.ctx().find<FEventBus>();
        if (!events) return;

        auto view = reg.view<FCollider, FPosition>();
        std::vector<entt::entity> ents(view.begin(), view.end());

        for (size_t i = 0; i < ents.size(); ++i) {
            for (size_t j = i + 1; j < ents.size(); ++j) {
                entt::entity aEnt = ents[i];
                entt::entity bEnt = ents[j];

                const auto& colA = view.get<FCollider>(aEnt);
                const auto& colB = view.get<FCollider>(bEnt);

                // Bitmask layer filter
                if ((colA.collisionLayer & colB.collisionLayer) == 0) continue;

                const auto& posA = view.get<FPosition>(aEnt);
                const auto& posB = view.get<FPosition>(bEnt);

                bool hit = false;
                c2Manifold m{};

                if (colA.type == EColliderType::AABB && colB.type == EColliderType::AABB) {
                    c2AABB abbA{{posA.x + colA.offset.x - colA.halfWidth, posA.y + colA.offset.y - colA.halfHeight},
                                {posA.x + colA.offset.x + colA.halfWidth, posA.y + colA.offset.y + colA.halfHeight}};
                    c2AABB abbB{{posB.x + colB.offset.x - colB.halfWidth, posB.y + colB.offset.y - colB.halfHeight},
                                {posB.x + colB.offset.x + colB.halfWidth, posB.y + colB.offset.y + colB.halfHeight}};
                    c2AABBtoAABBManifold(abbA, abbB, &m);
                    hit = m.count > 0;
                } else if (colA.type == EColliderType::Circle && colB.type == EColliderType::Circle) {
                    c2Circle cA{{posA.x + colA.offset.x, posA.y + colA.offset.y}, colA.halfWidth};
                    c2Circle cB{{posB.x + colB.offset.x, posB.y + colB.offset.y}, colB.halfWidth};
                    c2CircletoCircleManifold(cA, cB, &m);
                    hit = m.count > 0;
                } else {
                    // Mixed (AABB × Circle): approximate both as circles
                    c2Circle cA{{posA.x + colA.offset.x, posA.y + colA.offset.y}, colA.halfWidth};
                    c2Circle cB{{posB.x + colB.offset.x, posB.y + colB.offset.y}, colB.halfWidth};
                    if (colA.type == EColliderType::AABB) cA.r = std::max(colA.halfWidth, colA.halfHeight);
                    if (colB.type == EColliderType::AABB) cB.r = std::max(colB.halfWidth, colB.halfHeight);
                    c2CircletoCircleManifold(cA, cB, &m);
                    hit = m.count > 0;
                }

                if (hit && m.count > 0) {
                    events->queueFrame<FCollisionEvent>({aEnt, bEnt, m});
                }
            }
        }
    }

    std::string name() const override { return "CollisionSystem"; }
};

} // namespace game
