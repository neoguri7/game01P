#include "gameplay/tactical_d20/TacticalD20ValidationChecklistSystem.h"

#include "core/InputState.h"
#include "core/events/FEventBus.h"
#include "ecs/components/FConditionDodge.h"
#include "ecs/components/FCombatStateDefeat.h"
#include "ecs/components/FCombatStateVictory.h"
#include "gameplay/tactical_d20/FTacticalD20CombatLog.h"
#include "gameplay/tactical_d20/FTacticalD20Config.h"
#include "gameplay/tactical_d20/FTacticalD20Events.h"
#include "gameplay/tactical_d20/FTacticalD20ValidationChecklist.h"

#include <string>
#include <tracy/Tracy.hpp>
#include <vector>

namespace game {
namespace {

struct FDodgeAttackObservationState {
    std::vector<std::pair<entt::entity, int>> targetAttackCounts;
};

bool Contains(const std::string& value, const char* needle) {
    return value.find(needle) != std::string::npos;
}

FDodgeAttackObservationState& DodgeObservationState(entt::registry& registry) {
    if (!registry.ctx().contains<FDodgeAttackObservationState>()) registry.ctx().emplace<FDodgeAttackObservationState>();
    return registry.ctx().get<FDodgeAttackObservationState>();
}

void ClearDodgeObservation(entt::registry& registry, entt::entity target) {
    auto& state = DodgeObservationState(registry);
    std::erase_if(state.targetAttackCounts, [target](const auto& entry) {
        return entry.first == target;
    });
}

int& DodgeAttackCount(entt::registry& registry, entt::entity target) {
    auto& state = DodgeObservationState(registry);
    for (auto& entry : state.targetAttackCounts) {
        if (entry.first == target) return entry.second;
    }
    state.targetAttackCounts.push_back({target, 0});
    return state.targetAttackCounts.back().second;
}

void MarkInvalidDrop(entt::registry& registry, const FTacticalD20CommandDropValidatedEvent& event) {
    if (event.valid) return;
    const auto& reason = event.invalidReason;
    if (Contains(reason, "outside board")) MarkTacticalD20Checklist(registry, "edge.drop_outside", ETacticalD20ChecklistStatus::Observed, reason);
    if (Contains(reason, "occupied")) MarkTacticalD20Checklist(registry, "edge.move_occupied", ETacticalD20ChecklistStatus::Observed, reason);
    if (Contains(reason, "wall") || Contains(reason, "blocked")) MarkTacticalD20Checklist(registry, "edge.move_wall_path", ETacticalD20ChecklistStatus::Observed, reason);
    if (Contains(reason, "requires")) MarkTacticalD20Checklist(registry, "edge.move_budget", ETacticalD20ChecklistStatus::Observed, reason);
    if (Contains(reason, "outside range") || Contains(reason, "exceeds range")) MarkTacticalD20Checklist(registry, "edge.attack_range", ETacticalD20ChecklistStatus::Observed, reason);
    if (Contains(reason, "line of sight")) MarkTacticalD20Checklist(registry, "edge.ranged_los", ETacticalD20ChecklistStatus::Observed, reason);
    if (Contains(reason, "melee") && Contains(reason, "not adjacent")) MarkTacticalD20Checklist(registry, "edge.melee_adjacency", ETacticalD20ChecklistStatus::Observed, reason);
}

void MarkAttack(entt::registry& registry, const FEventBus& bus) {
    for (const auto& event : bus.frameEvents<FTacticalD20AttackResolvedEvent>()) {
        if (event.coverApplied) MarkTacticalD20Checklist(registry, "edge.cover", ETacticalD20ChecklistStatus::Observed, "AttackResolved coverApplied=true");
        if (event.disadvantageApplied && registry.valid(event.target) && registry.all_of<FConditionDodge>(event.target)) {
            int& count = DodgeAttackCount(registry, event.target);
            ++count;
            if (count >= 2) {
                MarkTacticalD20Checklist(registry, "edge.dodge_multi_attack", ETacticalD20ChecklistStatus::Observed, "Multiple disadvantaged attacks resolved while Dodge remained active.");
            }
        }
    }
    for (const auto& event : bus.frameEvents<FTacticalD20AttackRollResolvedEvent>()) {
        if (event.naturalRoll == 20 && event.hit && event.criticalHit) {
            MarkTacticalD20Checklist(registry, "edge.natural20", ETacticalD20ChecklistStatus::Observed, event.breakdown);
        }
        if (event.naturalRoll == 1 && !event.hit) {
            MarkTacticalD20Checklist(registry, "edge.natural1", ETacticalD20ChecklistStatus::Observed, event.breakdown);
        }
        if (Contains(event.breakdown, "canceled to normal")) {
            MarkTacticalD20Checklist(registry, "edge.adv_dis_cancel", ETacticalD20ChecklistStatus::Observed, event.breakdown);
        }
    }
}

void MarkConditions(entt::registry& registry, const FEventBus& bus) {
    for (const auto& event : bus.frameEvents<FTacticalD20DamageAppliedEvent>()) {
        if (event.damageType == "fire" && event.defeated) {
            MarkTacticalD20Checklist(registry, "edge.burning_defeat_skip", ETacticalD20ChecklistStatus::Observed, "Burning damage defeated active unit.");
        }
    }
    for (const auto& event : bus.frameEvents<FTacticalD20ConditionTickedEvent>()) {
        if (event.conditionId == "stunned") {
            MarkTacticalD20Checklist(registry, "edge.stunned_skip", ETacticalD20ChecklistStatus::Observed, "Stunned condition ticked and turn skipped.");
        }
    }
    for (const auto& event : bus.frameEvents<FTacticalD20ConditionExpiredEvent>()) {
        if (event.conditionId == "dodge") ClearDodgeObservation(registry, event.unit);
    }
}

void MarkCoreAcceptance(entt::registry& registry, const FEventBus& bus) {
    if (!bus.frameEvents<FTacticalD20CombatSetupCompletedEvent>().empty()) {
        MarkTacticalD20Checklist(registry, "accept.board", ETacticalD20ChecklistStatus::Hooked, "Combat setup completed with board and units.");
    }
    if (!bus.frameEvents<FTacticalD20CommandDropValidatedEvent>().empty()) {
        MarkTacticalD20Checklist(registry, "accept.drag_drop", ETacticalD20ChecklistStatus::Hooked, "Command drop validation event observed.");
    }
    if (!bus.frameEvents<FTacticalD20CombatStateChangedEvent>().empty()) {
        MarkTacticalD20Checklist(registry, "accept.observability", ETacticalD20ChecklistStatus::Hooked, "State transition event/log observed.");
    }
}

void MarkPassiveHooks(entt::registry& registry) {
    if (const auto* config = registry.ctx().find<FTacticalD20Config>(); config && !config->warnings.empty()) {
        MarkTacticalD20Checklist(registry, "edge.config_fallback", ETacticalD20ChecklistStatus::Observed, config->warnings.front());
    }
    if (const auto* input = registry.ctx().find<FInputState>(); input && input->uiCapturesMouse) {
        MarkTacticalD20Checklist(registry, "edge.ui_capture", ETacticalD20ChecklistStatus::Observed, "ImGui requested mouse capture this frame.");
    }
    if (!registry.view<FCombatStateVictory>().empty() || !registry.view<FCombatStateDefeat>().empty()) {
        MarkTacticalD20Checklist(registry, "edge.combat_ended", ETacticalD20ChecklistStatus::Observed, "Terminal combat state active.");
    }
    if (const auto* log = registry.ctx().find<FTacticalD20CombatLog>()) {
        for (const auto& line : log->lines) {
            if (Contains(line, "no valid path")) {
                MarkTacticalD20Checklist(registry, "edge.enemy_no_path", ETacticalD20ChecklistStatus::Observed, line);
                break;
            }
        }
    }
}

} // namespace

void TacticalD20ValidationChecklistSystem::update(entt::registry& registry, float /*dt*/) {
    ZoneScopedN("TacticalD20ValidationChecklistSystem");
    EnsureTacticalD20ValidationChecklist(registry);
    if (const auto* bus = registry.ctx().find<FEventBus>()) {
        for (const auto& event : bus->frameEvents<FTacticalD20CommandDropValidatedEvent>()) MarkInvalidDrop(registry, event);
        MarkAttack(registry, *bus);
        MarkConditions(registry, *bus);
        MarkCoreAcceptance(registry, *bus);
    }
    MarkPassiveHooks(registry);
}

} // namespace game
