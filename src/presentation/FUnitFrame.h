#pragma once

#include <algorithm>
#include <cstddef>

#include <entt/entt.hpp>

#include "core/AnimationLibrary.h"
#include "presentation/FBattleAnim.h"
#include "presentation/FIsoView.h"

namespace game {

/// Unscaled pixels from a unit's feet to the top of its picture: the idle clip's anchor.y when the unit is
/// animated and that is more than kUnitHeadAboveFeet, otherwise kUnitHeadAboveFeet.
[[nodiscard]] inline float UnitHeadAboveFeet(const entt::registry& registry, entt::entity entity) {
    const auto* anim = registry.try_get<FUnitAnim>(entity);
    const auto* data = registry.ctx().find<FBattleAnimData>();
    const auto* library = FAnimationLibrary::Find(registry);
    if (anim == nullptr || data == nullptr || library == nullptr || anim->set >= data->sets.size())
        return kUnitHeadAboveFeet;
    const auto* clip = library->get(data->sets[anim->set].clips[static_cast<std::size_t>(EUnitAnim::Idle)]);
    if (clip == nullptr) return kUnitHeadAboveFeet;
    return std::max(kUnitHeadAboveFeet, clip->anchor.y);
}

}  // namespace game
