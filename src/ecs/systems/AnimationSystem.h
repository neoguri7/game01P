#pragma once

#include "core/AnimationLibrary.h"
#include "core/Logger.h"
#include "ecs/AnimationFunctions.h"
#include "ecs/components/FAnimator.h"
#include "ecs/components/FSprite.h"
#include "ecs/systems/ISystem.h"

#include <entt/entt.hpp>
#include <tracy/Tracy.hpp>

#include <cmath>
#include <string>

namespace game::ecs {

struct AnimationSystem : public ISystem {
    void update(entt::registry& registry, float dt) override {
        ZoneScopedN("AnimationSystem");
        const FAnimationLibrary* library = FAnimationLibrary::Find(registry);
        if (library == nullptr) {
            return;
        }

        auto view = registry.view<FAnimator, FSprite>();
        for (const entt::entity entity : view) {
            FAnimator& animator = view.get<FAnimator>(entity);
            FSprite& sprite = view.get<FSprite>(entity);
            const FAnimClip* clip = library->get(animator.clip);
            if (clip == nullptr || clip->frames.empty()) {
                if (animator.playing) {
                    LOG_ERROR("Entity {} references an invalid animation clip handle.", entt::to_integral(entity));
                    animator.playing = false;
                    animator.finished = true;
                }
                continue;
            }

            AdvanceAnimator(animator, *clip, static_cast<double>(dt));

            sprite.texture = clip->texture;
            sprite.source = clip->frames[animator.frame].source;
        }
    }

    std::string name() const override { return "AnimationSystem"; }
};

} // namespace game::ecs
