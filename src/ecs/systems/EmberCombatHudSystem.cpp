#include "ecs/systems/EmberCombatHudSystem.h"

#include <algorithm>
#include <imgui.h>
#include "core/EngineState.h"
#include "core/InputState.h"
#include "ecs/components/FActionPoints.h"
#include "ecs/components/FActiveStatuses.h"
#include "ecs/components/FCamera.h"
#include "ecs/components/FFaction.h"
#include "ecs/components/FGridPosition.h"
#include "ecs/components/FHealth.h"
#include "ecs/components/FSkillLoadout.h"
#include "ecs/components/FTag.h"
#include "ecs/systems/TurnSystem.h"
#include "gameplay/combat/FCombatQuery.h"
#include "gameplay/combat/FCombatRules.h"
#include "gameplay/combat/FCombatTypes.h"
#include "gameplay/combat/FSkillResolver.h"
#include "gameplay/dungeon/FDungeonRun.h"
#include "gameplay/grid/FCameraMath.h"
#include "gameplay/grid/FGridQuery.h"
#include "gameplay/grid/FGridSelection.h"
#include "gameplay/grid/FGridTerrain.h"
#include "presentation/FIsoView.h"

namespace game::ecs {

bool CanTargetWithSkill(entt::registry& registry, entt::entity actor, entt::entity target, const FResolvedSkill& skill) {
    if (!IsAlive(registry, target) || !registry.all_of<FHealth, FGridPosition>(target)) return false;
    const bool own = skill.target == "ally" || skill.target == "self";
    if (registry.all_of<FPartyMember>(target) != own || (skill.target == "self" && target != actor)) return false;
    const auto from = registry.get<FGridPosition>(actor);
    const auto to = registry.get<FGridPosition>(target);
    return IsInRange(from, to, skill.range) && HasGridLineOfSight(registry, from, to);
}

void EmberCombatHudSystem::update(entt::registry& registry, float) {
    auto* selection = registry.ctx().find<FGridSelection>();
    auto* runtime = registry.ctx().find<FTurnRuntime>();
    const auto* outcome = registry.ctx().find<FBattleOutcome>();
    auto* input = registry.ctx().find<FInputState>();
    if (!selection || !runtime || !outcome || !input) return;
    selection->reachable.clear();
    selection->affected.clear();
    selection->hovered.reset();
    selection->selected = entt::null;
    const auto snapshot = runtime->controller.snapshot();
    if (outcome->resolved || !snapshot.currentTurn) return;
    const auto token = *snapshot.currentTurn;
    const auto actor = token.actor;
    selection->selected = actor;
    if (boundTurn != token.turnSerial) { boundTurn = token.turnSerial; selectedSkill = 0; }
    if (!IsAlive(registry, actor) || !registry.all_of<FPartyMember, FActionPoints, FSkillLoadout>(actor)) return;
    auto& queue = registry.ctx().get<FActionQueue>();
    const auto& points = registry.get<FActionPoints>(actor);
    const auto& terrain = registry.ctx().get<FGridTerrain>();
    if (points.move > 0) selection->reachable = ComputeReachableCells(registry, actor, terrain.moveRange);
    const auto& loadout = registry.get<FSkillLoadout>(actor);
    constexpr EInputAction skills[]{EInputAction::Skill1, EInputAction::Skill2, EInputAction::Skill3, EInputAction::Skill4};
    for (int index = 0; index < 4; ++index) {
        if (input->isActionPressed(skills[index]) && index < static_cast<int>(loadout.slots.size())) selectedSkill = index;
    }
    if (input->isActionPressed(EInputAction::EndTurn)) {
        if (queue.pending.empty()) runtime->requests.push_back(FEndTurn{token});
        return;
    }
    if (!queue.pending.empty()) return;
    auto destination = registry.get<FGridPosition>(actor);
    bool keyMove = false;
    if (input->isActionPressed(EInputAction::MoveUp)) { --destination.y; keyMove = true; }
    else if (input->isActionPressed(EInputAction::MoveDown)) { ++destination.y; keyMove = true; }
    else if (input->isActionPressed(EInputAction::MoveLeft)) { --destination.x; keyMove = true; }
    else if (input->isActionPressed(EInputAction::MoveRight)) { ++destination.x; keyMove = true; }
    if (keyMove) { queue.pending.push_back(FMoveRequest{actor, destination, token}); return; }
    if (input->uiCapturesMouse) return;
    const auto& window = registry.ctx().get<FEngineWindowState>();
    const auto& grid = registry.ctx().get<FGridMetrics>();
    const auto view = MakeIsoView(grid, static_cast<float>(window.width), static_cast<float>(window.height));
    int x = 0, y = 0;
    if (!IsoScreenToCell(view, grid, input->mousePos, x, y)) return;
    selection->hovered = FGridPosition{x, y};
    const auto target = FindGridOccupant(registry, x, y);
    if (selectedSkill >= 0 && selectedSkill < static_cast<int>(loadout.slots.size())) {
        const auto& slot = loadout.slots[selectedSkill];
        const auto skill = ResolveSkillSlot(slot, registry.ctx().get<gameplay::FContentRegistry>());
        if (skill && skill->target == "cell") {
            const auto from = registry.get<FGridPosition>(actor);
            if (IsInRange(from, {x, y}, skill->range) && HasGridLineOfSight(registry, from, {x, y})) {
                selection->affected = ComputeSkillCells(*skill, {x, y}, from);
            }
            if (input->mouseLeftPressed) {
                queue.pending.push_back(FActionRequest{actor, target, slot.skillId, token, {x, y}});
            }
            return;
        }
    }
    if (target != entt::null && selectedSkill < static_cast<int>(loadout.slots.size())) {
        const auto& slot = loadout.slots[selectedSkill];
        const auto skill = ResolveSkillSlot(slot, registry.ctx().get<gameplay::FContentRegistry>());
        if (skill && CanTargetWithSkill(registry, actor, target, *skill)) {
            selection->affected = ComputeSkillCells(*skill, {x, y}, registry.get<FGridPosition>(actor));
        }
        if (input->mouseLeftPressed) queue.pending.push_back(FActionRequest{actor, target, slot.skillId, token, {x, y}});
    } else if (input->mouseLeftPressed) {
        queue.pending.push_back(FMoveRequest{actor, {x, y}, token});
    }
}

} // namespace game::ecs
