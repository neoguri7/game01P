#pragma once

#include "gameplay/components/FGridPosition.h"

#include <entt/entt.hpp>

#include <cstddef>
#include <vector>

namespace game::gameplay {

/// Battle-scope service in registry.ctx() (R6: no singleton). It holds only what is *not* entity state: the
/// battle entity, the predicted turn order with the cursor into it, and the grid size read from the encounter.
/// why an order vector instead of "current turn" pointers: the order is rebuilt per round from a total order
/// (speed, then spawn order), so determinism never depends on registry storage layout (R16), and the view can
/// print the upcoming turns — the 표시 that design §4 확정 requires.
struct FBattleState {
    entt::entity battle{entt::null};
    std::vector<entt::entity> order;
    std::size_t cursor{0};
    int gridWidth{8};  // fallback: 8 (design §4 확정 8x8 — the encounter row overrides it)
    int gridHeight{8}; // fallback: 8 (design §4 확정 8x8 — the encounter row overrides it)

    [[nodiscard]] bool inBounds(const FGridPosition& position) const {
        return position.x >= 0 && position.y >= 0 && position.x < gridWidth && position.y < gridHeight;
    }
};

} // namespace game::gameplay
