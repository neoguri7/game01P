#include "gameplay/tactical_d20/FTacticalD20ValidationChecklist.h"

#include <algorithm>

namespace game {
namespace {

struct FSeedItem {
    const char* id;
    const char* category;
    const char* description;
    ETacticalD20ChecklistStatus status;
    const char* evidence;
};

constexpr FSeedItem ChecklistSeeds[] = {
    {"edge.drop_outside", "Edge Cases", "Command dropped outside board rejects and snaps back.", ETacticalD20ChecklistStatus::Hooked, "CommandDropValidated invalid reason hook."},
    {"edge.move_occupied", "Edge Cases", "Move dropped on occupied tile rejects.", ETacticalD20ChecklistStatus::Hooked, "Movement validation reason hook."},
    {"edge.move_wall_path", "Edge Cases", "Move path crossing wall rejects.", ETacticalD20ChecklistStatus::Hooked, "Movement validation reason hook."},
    {"edge.move_budget", "Edge Cases", "Move target exceeding budget rejects.", ETacticalD20ChecklistStatus::Hooked, "Movement validation cost/budget hook."},
    {"edge.attack_range", "Edge Cases", "Attack target outside range rejects.", ETacticalD20ChecklistStatus::Hooked, "Command validation reason hook."},
    {"edge.ranged_los", "Edge Cases", "Ranged line through wall rejects.", ETacticalD20ChecklistStatus::Hooked, "Line-of-sight validation hook."},
    {"edge.cover", "Edge Cases", "Ranged target cover applies +2 AC.", ETacticalD20ChecklistStatus::Hooked, "AttackResolved coverApplied hook."},
    {"edge.natural20", "Edge Cases", "Natural 20 always hits and crits.", ETacticalD20ChecklistStatus::Hooked, "Attack roll event hook."},
    {"edge.natural1", "Edge Cases", "Natural 1 always misses.", ETacticalD20ChecklistStatus::Hooked, "Attack roll event hook."},
    {"edge.adv_dis_cancel", "Edge Cases", "Advantage and disadvantage cancel to normal.", ETacticalD20ChecklistStatus::Hooked, "Roll breakdown hook."},
    {"edge.condition_skip", "Edge Cases", "Burning defeat or Stunned skip consumes turn.", ETacticalD20ChecklistStatus::Hooked, "Condition/action event hooks."},
    {"edge.enemy_no_path", "Edge Cases", "Enemy with no path waits and logs.", ETacticalD20ChecklistStatus::Hooked, "Combat log/checklist hook."},
    {"edge.config_fallback", "Edge Cases", "Missing or invalid config falls back and logs.", ETacticalD20ChecklistStatus::Hooked, "Config warnings hook."},
    {"edge.ui_capture", "Edge Cases", "UI mouse capture prevents gameplay drag.", ETacticalD20ChecklistStatus::Hooked, "Input state hook."},
    {"edge.combat_ended", "Edge Cases", "Combat ended disables command tokens.", ETacticalD20ChecklistStatus::Hooked, "Terminal state visual hook."},
    {"accept.ruleset", "Acceptance", "Ruleset prototype criteria mapped to systems.", ETacticalD20ChecklistStatus::Hooked, "Rules, initiative, action, condition systems."},
    {"accept.board", "Acceptance", "Tactical board criteria mapped to board/path/attack systems.", ETacticalD20ChecklistStatus::Hooked, "Board factories and validators."},
    {"accept.drag_drop", "Acceptance", "Drag-and-drop criteria mapped to drag and validation systems.", ETacticalD20ChecklistStatus::Hooked, "Command drag/validation events."},
    {"accept.conditions", "Acceptance", "Condition criteria mapped and visible in labels/debug UI.", ETacticalD20ChecklistStatus::Hooked, "Condition system and unit label system."},
    {"accept.observability", "Acceptance", "Observability criteria mapped to logs, telemetry, and Tracy zones.", ETacticalD20ChecklistStatus::Hooked, "Telemetry/log systems."},
    {"accept.scope", "Acceptance", "Out-of-scope tabletop features remain omitted.", ETacticalD20ChecklistStatus::Hooked, "Regression checklist entry."},
};

} // namespace

const char* TacticalD20ChecklistStatusName(ETacticalD20ChecklistStatus status) {
    switch (status) {
        case ETacticalD20ChecklistStatus::Pending: return "pending";
        case ETacticalD20ChecklistStatus::Hooked: return "hooked";
        case ETacticalD20ChecklistStatus::Observed: return "observed";
        case ETacticalD20ChecklistStatus::Failed: return "failed";
    }
    return "pending";
}

void EnsureTacticalD20ValidationChecklist(entt::registry& registry) {
    if (!registry.ctx().contains<FTacticalD20ValidationChecklist>()) {
        auto& checklist = registry.ctx().emplace<FTacticalD20ValidationChecklist>();
        for (const auto& seed : ChecklistSeeds) {
            checklist.items.push_back({seed.id, seed.category, seed.description, seed.status, seed.evidence});
        }
    }
}

void MarkTacticalD20Checklist(entt::registry& registry,
                              const std::string& id,
                              ETacticalD20ChecklistStatus status,
                              const std::string& evidence) {
    EnsureTacticalD20ValidationChecklist(registry);
    auto& checklist = registry.ctx().get<FTacticalD20ValidationChecklist>();
    const auto found = std::ranges::find_if(checklist.items, [&id](const auto& item) {
        return item.id == id;
    });
    if (found == checklist.items.end()) return;
    if (found->status == ETacticalD20ChecklistStatus::Observed && status == ETacticalD20ChecklistStatus::Hooked) return;
    found->status = status;
    found->evidence = evidence;
}

} // namespace game
