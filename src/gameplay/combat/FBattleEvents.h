#pragma once

#include <string>
#include <vector>

#include <entt/entt.hpp>

#include "ecs/components/FGridPosition.h"

namespace game {

/// What a resolved action did, in the order a player should see it. The combat rules apply their outcome at once;
/// this record lets the presentation play the matching animations and effects afterwards, so the rules never wait
/// on a picture. Only battles that draw their actions register FBattleEvents (headless runs leave it out).
struct FBattleEvent {
    enum class EKind { Move, Skill };

    struct FTarget {
        entt::entity unit{entt::null};
        int healthBefore{0};
        int healthAfter{0};
        bool hostile{false};  // hurt or afflicted, as opposed to healed or helped
        bool died{false};
    };

    EKind kind{EKind::Skill};
    entt::entity actor{entt::null};
    std::string skillId;                  // Skill
    FGridPosition aim{};                  // Skill: the cell that was targeted
    std::vector<FGridPosition> path;      // Move: the cells walked, start first
    std::vector<FGridPosition> cells;     // Skill: every cell the skill covers
    std::vector<FTarget> targets;         // Skill: the units it reached
};

struct FBattleEvents {
    std::vector<FBattleEvent> pending;
};

} // namespace game
