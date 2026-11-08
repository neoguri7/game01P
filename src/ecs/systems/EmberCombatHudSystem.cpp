#include "ecs/systems/EmberCombatHudSystem.h"

#include <algorithm>
#include <imgui.h>
#include <utility>
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
#include "gameplay/combat/FBattleCommand.h"
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
#include "presentation/FUiFrame.h"

namespace game::ecs {

bool CanTargetWithSkill(entt::registry& registry, entt::entity actor, entt::entity target, const FResolvedSkill& skill) {
    if (!IsAlive(registry, target) || !registry.all_of<FHealth, FGridPosition>(target)) return false;
    const bool own = skill.target == "ally" || skill.target == "self";
    if (registry.all_of<FPartyMember>(target) != own || (skill.target == "self" && target != actor)) return false;
    const auto from = registry.get<FGridPosition>(actor);
    const auto to = registry.get<FGridPosition>(target);
    return IsInRange(from, to, skill.range) && HasGridLineOfSight(registry, from, to);
}

bool SubmitHudAction(entt::registry& registry, FBattleAction action) {
    return SubmitBattleCommand(registry, FBattleCommand{std::move(action), ECommandSource::Player, 0});
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
    if (outcome->resolved || !snapshot.currentTurn) {
        // WP-12: a resolved (or idle) battle has nothing to advertise — clear the frame's display data.
        StoreCombatInfoSnapshot(registry, FCombatInfoSnapshot{});
        return;
    }
    const auto token = *snapshot.currentTurn;
    const auto actor = token.actor;
    selection->selected = actor;
    if (boundTurn != token.turnSerial) {
        boundTurn = token.turnSerial;
        selectedSkill = 0;
    }
    if (auto* combat = registry.ctx().find<FCombatRuntime>()) combat->selectedSkill = selectedSkill;

    const auto& terrain = registry.ctx().get<FGridTerrain>();
    const bool playerActor =
        IsAlive(registry, actor) && registry.all_of<FPartyMember, FActionPoints, FSkillLoadout>(actor);
    if (playerActor) {
        const auto& points = registry.get<FActionPoints>(actor);
        if (points.move > 0) selection->reachable = ComputeReachableCells(registry, actor, terrain.moveRange);
    }

    // WP-12: the hover is read by the snapshot every battle frame (intent reach cells, skill preview,
    // move path) — including the enemy turn, which the pre-WP-12 HUD returned from before picking.
    if (!input->uiCapturesMouse) {
        const auto& grid = registry.ctx().get<FGridMetrics>();
        const auto view = ui::FindBattleIsoView(registry, grid);
        int x = 0;
        int y = 0;
        if (view && IsoScreenToCell(*view, grid, input->mousePos, x, y)) {
            selection->hovered = FGridPosition{x, y};
        }
    }

    if (playerActor) {
        const auto& loadout = registry.get<FSkillLoadout>(actor);
        constexpr EInputAction skills[]{EInputAction::Skill1, EInputAction::Skill2, EInputAction::Skill3,
                                        EInputAction::Skill4};
        for (int index = 0; index < 4; ++index) {
            if (input->isActionPressed(skills[index]) && index < static_cast<int>(loadout.slots.size())) {
                selectedSkill = index;
            }
        }
        if (auto* combat = registry.ctx().find<FCombatRuntime>()) combat->selectedSkill = selectedSkill;
    }

    // The one snapshot of this frame: built after the input pass above (hover + selected skill are final
    // for this frame), before any early return below. The view and the grid renderer only read it.
    StoreCombatInfoSnapshot(registry, BuildCombatInfoSnapshot(registry));

    if (!playerActor) return;
    const auto& loadout = registry.get<FSkillLoadout>(actor);
    auto& queue = registry.ctx().get<FActionQueue>();
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
    if (keyMove) {
        // 관례 a: the pressed key submits immediately; the API records a refusal itself, so either way the
        // input is consumed and nothing else happens this frame.
        [[maybe_unused]] const bool submitted = SubmitHudAction(registry, FMoveRequest{actor, destination, token});
        return;
    }
    if (!selection->hovered) return;
    const FGridPosition hovered = *selection->hovered;
    const entt::entity target = FindGridOccupant(registry, hovered.x, hovered.y);
    if (selectedSkill >= 0 && selectedSkill < static_cast<int>(loadout.slots.size())) {
        const auto& slot = loadout.slots[selectedSkill];
        const auto skill = ResolveSkillSlot(slot, registry.ctx().get<gameplay::FContentRegistry>());
        if (skill && skill->target == "cell") {
            const auto from = registry.get<FGridPosition>(actor);
            if (IsInRange(from, hovered, skill->range) && HasGridLineOfSight(registry, from, hovered)) {
                selection->affected = ComputeSkillCells(*skill, hovered, from);
            }
            if (input->mouseLeftPressed) {
                [[maybe_unused]] const bool submitted =
                    SubmitHudAction(registry, FActionRequest{actor, target, slot.skillId, token, hovered});
            }
            return;
        }
    }
    if (target != entt::null && selectedSkill < static_cast<int>(loadout.slots.size())) {
        const auto& slot = loadout.slots[selectedSkill];
        const auto skill = ResolveSkillSlot(slot, registry.ctx().get<gameplay::FContentRegistry>());
        if (skill && CanTargetWithSkill(registry, actor, target, *skill)) {
            selection->affected = ComputeSkillCells(*skill, hovered, registry.get<FGridPosition>(actor));
        }
        if (input->mouseLeftPressed) {
            [[maybe_unused]] const bool submitted =
                SubmitHudAction(registry, FActionRequest{actor, target, slot.skillId, token, hovered});
        }
    } else if (input->mouseLeftPressed) {
        [[maybe_unused]] const bool submitted = SubmitHudAction(registry, FMoveRequest{actor, hovered, token});
    }
}

} // namespace game::ecs
