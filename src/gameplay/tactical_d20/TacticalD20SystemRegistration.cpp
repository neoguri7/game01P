#include "gameplay/tactical_d20/TacticalD20SystemRegistration.h"

#include "core/SystemManager.h"
#include "gameplay/tactical_d20/FTacticalD20ValidationChecklist.h"
#include "gameplay/tactical_d20/systems/actions/TacticalD20ActionEconomySystem.h"
#include "gameplay/tactical_d20/systems/actions/TacticalD20AttackActionResolutionSystem.h"
#include "gameplay/tactical_d20/systems/actions/TacticalD20DashActionResolutionSystem.h"
#include "gameplay/tactical_d20/systems/actions/TacticalD20DodgeActionResolutionSystem.h"
#include "gameplay/tactical_d20/systems/actions/TacticalD20MoveActionResolutionSystem.h"
#include "gameplay/tactical_d20/systems/actions/TacticalD20WaitActionResolutionSystem.h"
#include "gameplay/tactical_d20/systems/ai/TacticalD20EnemyAiSystem.h"
#include "gameplay/tactical_d20/systems/flow/TacticalD20CombatLifecycleSystem.h"
#include "gameplay/tactical_d20/systems/flow/TacticalD20ConditionSystem.h"
#include "gameplay/tactical_d20/systems/flow/TacticalD20InitiativeSystem.h"
#include "gameplay/tactical_d20/systems/flow/TacticalD20SetupSystem.h"
#include "gameplay/tactical_d20/systems/input/TacticalD20CommandDragInputSystem.h"
#include "gameplay/tactical_d20/systems/input/TacticalD20CommandValidationSystem.h"
#include "gameplay/tactical_d20/systems/input/TacticalD20MovementPathValidationSystem.h"
#include "gameplay/tactical_d20/systems/presentation/TacticalD20TelemetrySystem.h"
#include "gameplay/tactical_d20/systems/presentation/TacticalD20UnitLabelSystem.h"
#include "gameplay/tactical_d20/systems/presentation/TacticalD20ValidationChecklistSystem.h"
#include "gameplay/tactical_d20/systems/presentation/TacticalD20VisualFeedbackSystem.h"

#include <algorithm>
#include <limits>
#include <vector>

namespace game {
namespace {

std::size_t FirstIndex(const std::vector<std::string>& names, const char* name) {
    const auto found = std::ranges::find(names, name);
    return found == names.end() ? std::numeric_limits<std::size_t>::max() : static_cast<std::size_t>(found - names.begin());
}

std::size_t LastIndex(const std::vector<std::string>& names, const char* name) {
    for (std::size_t i = names.size(); i > 0; --i) {
        if (names[i - 1] == name) return i - 1;
    }
    return std::numeric_limits<std::size_t>::max();
}

bool Before(std::size_t lhs, std::size_t rhs) {
    return lhs != std::numeric_limits<std::size_t>::max() && rhs != std::numeric_limits<std::size_t>::max() && lhs < rhs;
}

void AddOrderCheck(FTacticalD20ValidationChecklist& checklist, const std::string& description, bool passed, const std::string& evidence) {
    checklist.systemOrder.push_back({description, passed, evidence});
}

} // namespace

void RegisterTacticalD20TurnFlowSystems(SystemManager& systemManager) {
    // Tactical D20 flow order: input state is captured by Engine::processInput.
    // First drag pass emits drop requests; validators emit validation events;
    // second drag pass consumes those events for snapback/acceptance; action
    // economy stores accepted commands; lifecycle advances combat state.
    systemManager.addSystem<TacticalD20SetupSystem>();
    systemManager.addSystem<TacticalD20InitiativeSystem>();
    systemManager.addSystem<TacticalD20ConditionSystem>();
    systemManager.addSystem<TacticalD20CommandDragInputSystem>();
    systemManager.addSystem<TacticalD20MovementPathValidationSystem>();
    systemManager.addSystem<TacticalD20CommandValidationSystem>();
    systemManager.addSystem<TacticalD20CommandDragInputSystem>();
    systemManager.addSystem<TacticalD20ActionEconomySystem>();
    systemManager.addSystem<TacticalD20EnemyAiSystem>();
    systemManager.addSystem<TacticalD20MoveActionResolutionSystem>();
    systemManager.addSystem<TacticalD20DashActionResolutionSystem>();
    systemManager.addSystem<TacticalD20DodgeActionResolutionSystem>();
    systemManager.addSystem<TacticalD20AttackActionResolutionSystem>();
    systemManager.addSystem<TacticalD20WaitActionResolutionSystem>();
    systemManager.addSystem<TacticalD20CombatLifecycleSystem>();
    systemManager.addSystem<TacticalD20UnitLabelSystem>();
    systemManager.addSystem<TacticalD20ValidationChecklistSystem>();
    systemManager.addSystem<TacticalD20TelemetrySystem>();
}

void RegisterTacticalD20VisualSystems(SystemManager& systemManager) {
    systemManager.addSystem<TacticalD20VisualFeedbackSystem>();
}

void ValidateTacticalD20SystemOrder(SystemManager& systemManager, entt::registry& registry) {
    EnsureTacticalD20ValidationChecklist(registry);
    auto& checklist = registry.ctx().get<FTacticalD20ValidationChecklist>();
    checklist.systemOrder.clear();
    const auto names = systemManager.getSystemNames();
    const auto setup = FirstIndex(names, "TacticalD20SetupSystem");
    const auto condition = FirstIndex(names, "TacticalD20ConditionSystem");
    const auto firstDrag = FirstIndex(names, "TacticalD20CommandDragInputSystem");
    const auto lastDrag = LastIndex(names, "TacticalD20CommandDragInputSystem");
    const auto movement = FirstIndex(names, "TacticalD20MovementPathValidationSystem");
    const auto validation = FirstIndex(names, "TacticalD20CommandValidationSystem");
    const auto actionEconomy = FirstIndex(names, "TacticalD20ActionEconomySystem");
    const auto enemyAi = FirstIndex(names, "TacticalD20EnemyAiSystem");
    const auto actionResolution = FirstIndex(names, "TacticalD20MoveActionResolutionSystem");
    const auto lifecycle = FirstIndex(names, "TacticalD20CombatLifecycleSystem");
    const auto telemetry = FirstIndex(names, "TacticalD20TelemetrySystem");

    AddOrderCheck(checklist, "setup/config before tactical updates", Before(setup, firstDrag), "TacticalD20SetupSystem precedes drag/validation.");
    AddOrderCheck(checklist, "input state before drag update", true, "Engine::processInput runs before SystemManager::updateAll.");
    AddOrderCheck(checklist, "drag request before validation", Before(firstDrag, movement) && Before(movement, validation), "Drag -> movement path validation -> command validation.");
    AddOrderCheck(checklist, "validation before action queue/resolution", Before(validation, lastDrag) && Before(lastDrag, actionEconomy) && Before(actionEconomy, actionResolution), "Validation events are consumed before action resolution.");
    AddOrderCheck(checklist, "start-turn conditions before command/enemy decisions", Before(condition, firstDrag) && Before(condition, enemyAi), "Condition ticking runs before command selection and EnemyThinking.");
    AddOrderCheck(checklist, "enemy AI before action resolution", Before(enemyAi, actionResolution), "EnemyThinking queues commands before resolvers.");
    AddOrderCheck(checklist, "telemetry/log overlay after gameplay", Before(lifecycle, telemetry), "Telemetry captures post-gameplay state.");
}

} // namespace game
