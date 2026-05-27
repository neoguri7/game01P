#pragma once

#include <string>
#include <vector>

#include <entt/entt.hpp>

namespace game {

enum class ETacticalD20ChecklistStatus {
    Pending,
    Hooked,
    Observed,
    Failed,
};

struct FTacticalD20ChecklistItem {
    std::string id;
    std::string category;
    std::string description;
    ETacticalD20ChecklistStatus status{ETacticalD20ChecklistStatus::Pending};
    std::string evidence;
};

struct FTacticalD20SystemOrderCheck {
    std::string description;
    bool passed{false};
    std::string evidence;
};

struct FTacticalD20ValidationChecklist {
    std::vector<FTacticalD20ChecklistItem> items;
    std::vector<FTacticalD20SystemOrderCheck> systemOrder;
};

const char* TacticalD20ChecklistStatusName(ETacticalD20ChecklistStatus status);
void EnsureTacticalD20ValidationChecklist(entt::registry& registry);
void MarkTacticalD20Checklist(entt::registry& registry,
                              const std::string& id,
                              ETacticalD20ChecklistStatus status,
                              const std::string& evidence);

} // namespace game
