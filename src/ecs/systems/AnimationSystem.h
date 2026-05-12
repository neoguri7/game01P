#pragma once
#include "ecs/components/FAnimation.h"
#include "ecs/components/FSprite.h"
#include "ecs/systems/ISystem.h"
#include <entt/entt.hpp>
#include <string>
#include <tracy/Tracy.hpp>

namespace game::ecs {

/// Advances animation frames and updates the linked FSprite texture+source rect.
struct AnimationSystem : public ISystem {
    void update(entt::registry& reg, float dt) override {
        ZoneScopedN("AnimationSystem");

        auto view = reg.view<FAnimation>();
        for (auto entity : view) {
            auto& anim = view.get<FAnimation>(entity);
            if (!anim.playing || anim.frames.empty()) continue;

            anim.timer += dt;

            // Advance frame
            while (anim.timer >= anim.frames[anim.currentFrame].duration) {
                anim.timer -= anim.frames[anim.currentFrame].duration;
                anim.currentFrame++;

                if (anim.currentFrame >= static_cast<int>(anim.frames.size())) {
                    if (anim.loop) {
                        anim.currentFrame = 0;
                    } else {
                        anim.currentFrame = static_cast<int>(anim.frames.size()) - 1;
                        anim.playing = false;
                        break;
                    }
                }
            }

            // Update sprite if present
            if (reg.all_of<FSprite>(entity)) {
                auto& spr = reg.get<FSprite>(entity);
                spr.texturePath = anim.sheetPath;

                const auto& frame = anim.frames[anim.currentFrame];
                spr.width  = frame.srcW;
                spr.height = frame.srcH;
                // TODO: add source rect to FSprite when rendering supports it
            }
        }
    }

    std::string name() const override { return "AnimationSystem"; }
};

} // namespace game::ecs
