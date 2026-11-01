#pragma once

#include "core/AnimationLibrary.h"
#include "core/AssetHandles.h"
#include "ecs/components/FAnimator.h"

#include <entt/entt.hpp>

namespace game::ecs {

/// Moves a running animator `dt` seconds along `clip`: advances frames, loops, or finishes a one-shot clip on its
/// last frame. Shared by the sprite AnimationSystem and the battle animations, which have no FSprite.
void AdvanceAnimator(FAnimator& animator, const FAnimClip& clip, double dt);

/// Points a bare animator at `clipId` and starts it from the first frame.
void StartClip(FAnimator& animator, FAnimClipId clipId, const FAnimClip& clip);

/// Starts a clip on an entity with an FSprite. Passing restart=false leaves the same running clip intact.
[[nodiscard]] bool PlayAnimation(entt::registry& registry, entt::entity entity, FAnimClipId clip, bool restart = false);

} // namespace game::ecs
