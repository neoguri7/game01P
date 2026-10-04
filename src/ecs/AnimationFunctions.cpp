#include "ecs/AnimationFunctions.h"

#include "core/AnimationLibrary.h"
#include "core/Logger.h"
#include "ecs/components/FAnimator.h"
#include "ecs/components/FSprite.h"

namespace game::ecs {

bool PlayAnimation(entt::registry& registry, entt::entity entity, FAnimClipId clipId, bool restart) {
    if (!registry.valid(entity)) {
        LOG_ERROR("Cannot play animation on an invalid entity.");
        return false;
    }
    if (!registry.all_of<FSprite>(entity)) {
        LOG_ERROR("Cannot play animation: entity {} has no FSprite.", entt::to_integral(entity));
        return false;
    }

    const FAnimationLibrary* library = FAnimationLibrary::Find(registry);
    if (library == nullptr || library->get(clipId) == nullptr) {
        LOG_ERROR("Cannot play invalid animation clip handle {}.", clipId.value);
        return false;
    }

    FAnimator& animator = registry.get_or_emplace<FAnimator>(entity);
    if (!restart && animator.clip == clipId && animator.playing) {
        return true;
    }

    const FAnimClip* clip = library->get(clipId);
    animator.clip = clipId;
    animator.frameTime = 0.0;
    animator.frame = 0;
    animator.playing = true;
    animator.loop = clip->loop;
    animator.finished = false;
    return true;
}

} // namespace game::ecs
