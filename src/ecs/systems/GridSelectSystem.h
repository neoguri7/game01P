#pragma once

#include <entt/entt.hpp>
#include <string>

#include "core/EngineState.h"
#include "core/InputState.h"
#include "ecs/components/FCamera.h"
#include "ecs/components/FTag.h"
#include "ecs/components/FActionPoints.h"
#include "ecs/components/FSkillLoadout.h"
#include "ecs/systems/TurnSystem.h"
#include "gameplay/combat/FCombatQuery.h"
#include "gameplay/combat/FCombatRules.h"
#include "gameplay/combat/FCombatTypes.h"
#include "gameplay/combat/FSkillResolver.h"
#include "ecs/systems/ISystem.h"
#include "gameplay/grid/FCameraMath.h"
#include "gameplay/grid/FGridQuery.h"
#include "gameplay/grid/FGridSelection.h"
#include "gameplay/grid/FGridTypes.h"

namespace game::ecs {

struct GridSelectSystem : ISystem {
    void update(entt::registry& registry, float) override {
        auto* selection = registry.ctx().find<FGridSelection>();
        if (!selection) return;
        selection->hovered.reset();
        selection->preview.clear();

        auto* input = registry.ctx().find<FInputState>();
        const auto* metrics = registry.ctx().find<FGridMetrics>();
        if (!input || !metrics) {
            selection->reachable.clear();
            selection->selected = entt::null;
            return;
        }

        FCamera camera{};
        for (const auto entity : registry.view<const FCamera, const FTag>()) {
            if (registry.get<const FTag>(entity).value == "battle_camera") {
                camera = registry.get<const FCamera>(entity);
                break;
            }
        }

        FViewport viewport{};
        if (const auto* window = registry.ctx().find<FEngineWindowState>();
            window && window->width > 0 && window->height > 0) {
            viewport.width = static_cast<float>(window->width);
            viewport.height = static_cast<float>(window->height);
        }

        auto* combat = registry.ctx().find<FCombatRuntime>();
        auto* runtime = registry.ctx().find<FTurnRuntime>();
        auto* queue = registry.ctx().find<FActionQueue>();
        const auto* outcome = registry.ctx().find<FBattleOutcome>();
        const auto* content = registry.ctx().find<gameplay::FContentRegistry>();
        const auto snapshot = runtime ? runtime->controller.snapshot() : FTurnSnapshot{};
        const auto actor = snapshot.currentTurn ? snapshot.currentTurn->actor : entt::null;
        if (!input->uiCapturesMouse) {
            int x = 0;
            int y = 0;
            if (WorldToGridCell(*metrics, ScreenToWorld(input->mousePos, camera, viewport), x, y)) {
                selection->hovered = FGridPosition{x, y};
            }
        }
        const bool playerTurn = combat && !combat->autoPlay && IsAlive(registry, actor)
            && registry.all_of<FPartyMember>(actor) && outcome && !outcome->resolved;
        if (combat && combat->inputActor != actor) {
            combat->inputActor = actor;
            combat->selectedSkill = 0;
            combat->moveSelected = false;
            selection->selected = actor;
        }

        if (input->mouseLeftPressed && !input->uiCapturesMouse) {
            const auto world = ScreenToWorld(input->mousePos, camera, viewport);
            int gridX = 0;
            int gridY = 0;
            if (WorldToGridCell(*metrics, world, gridX, gridY)) {
                const auto cellOrigin = GridCellTopLeftToWorld(*metrics, gridX, gridY);
                const auto local = world - cellOrigin;
                const bool insideHitbox = local.x >= kUnitHitboxInset && local.y >= kUnitHitboxInset
                    && local.x < metrics->tileSize - kUnitHitboxInset
                    && local.y < metrics->tileSize - kUnitHitboxInset;
                const auto target = insideHitbox ? FindGridOccupant(registry, gridX, gridY) : entt::null;
                if (target != entt::null) selection->selected = target;
                if (playerTurn && queue && content) {
                    FActionRequest request{actor};
                    request.turn = *snapshot.currentTurn;
                    request.cell = {gridX, gridY};
                    if (combat->moveSelected) {
                        request.action = EBattleAction::Move;
                        queue->pending.push_back(request);
                    } else {
                        const auto* loadout = registry.try_get<FSkillLoadout>(actor);
                        if (loadout && combat->selectedSkill >= 0
                            && combat->selectedSkill < static_cast<int>(loadout->slots.size())) {
                            const auto& slot = loadout->slots[combat->selectedSkill];
                            const auto skill = ResolveSkillSlot(slot, *content);
                            if (skill && (skill->target == "cell" || IsAlive(registry, target))) {
                                request.target = target;
                                request.skillId = slot.skillId;
                                queue->pending.push_back(request);
                            }
                        }
                    }
                }
            }
        }

        if (selection->selected != entt::null && !registry.valid(selection->selected)) {
            selection->selected = entt::null;
        }
        const auto* points = playerTurn ? registry.try_get<FActionPoints>(actor) : nullptr;
        selection->reachable = points && combat->moveSelected
            ? ComputeReachableCells(registry, actor, points->move)
            : std::vector<FGridPosition>{};
        selection->targetable.clear();
        if (playerTurn && content && !combat->moveSelected) {
            const auto* loadout = registry.try_get<FSkillLoadout>(actor);
            const auto* actorCell = registry.try_get<FGridPosition>(actor);
            if (loadout && actorCell && combat->selectedSkill >= 0
                && combat->selectedSkill < static_cast<int>(loadout->slots.size())) {
                const auto skill = ResolveSkillSlot(loadout->slots[combat->selectedSkill], *content);
                if (skill && points && points->skill >= skill->apCost) {
                    if (selection->hovered && IsInRange(*actorCell, *selection->hovered, skill->range)) {
                        selection->preview = ComputeSkillCells(*skill, *selection->hovered, *actorCell);
                    }
                    for (int y = 0; y < metrics->height; ++y) {
                        for (int x = 0; x < metrics->width; ++x) {
                            if (IsInRange(*actorCell, FGridPosition{x, y}, skill->range)) {
                                selection->targetable.push_back({x, y});
                            }
                        }
                    }
                }
            }
        }
    }

    std::string name() const override { return "GridSelectSystem"; }
};

} // namespace game::ecs
