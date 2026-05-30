#pragma once

#include "ecs/components/FTacticalD20CommandDragState.h"

#include <entt/entt.hpp>
#include <string>

namespace game {

void PublishTacticalD20DragStateChanged(entt::registry& registry,
                                        entt::entity token,
                                        const std::string& commandId,
                                        ETacticalD20CommandDragPhase previous,
                                        ETacticalD20CommandDragPhase next,
                                        const std::string& reason = "");

} // namespace game
