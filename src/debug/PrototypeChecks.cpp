#include "debug/PrototypeChecks.h"
#include <glm/geometric.hpp>

#include "core/InputState.h"
#include "core/JsonLoader.h"
#include "core/events/FCollisionEvent.h"
#include "core/events/FEventBus.h"
#include "ecs/components/FCollider.h"
#include "ecs/components/FSpeed.h"
#include "ecs/systems/ActionResolverSystem.h"
#include "ecs/systems/CollisionSystem.h"
#include "ecs/systems/CombatOutcomeSystem.h"
#include "ecs/systems/GridSelectSystem.h"
#include "ecs/systems/TurnFlowSystem.h"
#include "gameplay/battle/FBattleSetup.h"
#include "gameplay/combat/FAutoAction.h"
#include "gameplay/factory/FEntityFactory.h"
#include "gameplay/grid/FBattleLayout.h"
#include "states/FBattleState.h"
#include "states/FHubState.h"

namespace game {

bool RunPrototypeChecks(const gameplay::FContentRegistry& content, const FPrototypeRun& definition) {
    bool passed = true;
    int checks = 0;
    const auto check = [&](bool condition, const char* name) {
        ++checks;
        if (!condition) {
            LOG_ERROR("[verify] FAILED: {}", name);
            passed = false;
        }
    };

    FPrototypeRun run = definition;
    check(run.Start(content), "prepared party starts a run");
    check(!run.Start(content), "cannot restart an active run");
    for (const auto& member : run.party) {
        for (const auto& slot : member.skills) check(slot.runeIds.empty(), "starting rune sockets are empty");
    }
    FPrototypeRun progressionRun = definition;
    auto& preparedMember = progressionRun.party.front();
    const std::string placementId = preparedMember.placementId;
    const std::string skillId = preparedMember.skills.front().skillId;
    const std::string runeId = progressionRun.runeInventory.front();
    check(progressionRun.EquipRune(placementId, skillId, runeId, content), "compatible starter rune sockets into skill");
    check(!progressionRun.EquipRune(placementId, skillId, runeId, content), "consumed starter rune cannot be equipped twice");
    check(progressionRun.UnequipRune(placementId, skillId, runeId), "unsocketing returns rune to run inventory");
    check(progressionRun.Start(content), "prepared party starts a progression-check run");
    const auto allocatedMember = std::ranges::find(progressionRun.party, placementId, &FPreparedMember::placementId);
    const auto firstNode = std::ranges::find(progressionRun.passiveNodes, "node.vitality", &FPassiveNode::id);
    check(firstNode != progressionRun.passiveNodes.end() && !progressionRun.CanAllocatePassiveNode(placementId, firstNode->id),
          "cannot allocate passive node before earning a point");
    check(allocatedMember != progressionRun.party.end() && progressionRun.ResolveCurrentBattle(true), "battle victory awards run experience");
    check(allocatedMember->experience == 10 && allocatedMember->level == 2 && allocatedMember->passivePoints == 1,
          "battle experience crosses the first level threshold and grants a point");
    check(firstNode != progressionRun.passiveNodes.end() && progressionRun.AllocatePassiveNode(placementId, firstNode->id),
          "earned point allocates a node adjacent to the tree root");
    check(allocatedMember->passivePoints == 0 && progressionRun.PassiveBonuses(placementId).maxHealth > 0,
          "allocated passive node spends one point and applies stat bonus");
    const std::string disconnectedNode = "node.endurance";
    auto disconnected = std::ranges::find(progressionRun.passiveNodes, disconnectedNode, &FPassiveNode::id);
    if (disconnected != progressionRun.passiveNodes.end() && disconnected->parentId != progressionRun.passiveRootNodeId) {
        check(!progressionRun.CanAllocatePassiveNode(placementId, disconnectedNode), "cannot allocate passive beyond an unallocated connection");
    }
    progressionRun.End();
    check(allocatedMember->level == 1 && allocatedMember->experience == 0 && allocatedMember->passivePoints == 0
          && allocatedMember->allocatedNodes.empty(), "ending run resets run progression");

    run.Advance();
    check(run.CurrentRoom() && run.CurrentRoom()->kind == EPrototypeRoom::Event, "victory reaches event waypoint");
    run.Advance();
    run.Advance();
    run.Advance();
    check(run.CurrentRoom() && run.CurrentRoom()->kind == EPrototypeRoom::Camp && run.completedDungeons == 1,
          "three battles reach camp and advance checkpoint");
    run.Advance();
    check(run.roomIndex == 0 && run.dungeonIndex == 1 && run.active, "camp continues into another dungeon");
    run.End();
    check(!run.active && run.roomIndex == 0 && run.completedDungeons == 1, "run end retains only session checkpoint/preparation");
    check(run.Start(content) && run.dungeonIndex == 1, "next run starts from cleared checkpoint");
    run.End();
    for (auto& member : run.party) member.enabled = false;
    check(!run.Start(content), "empty party cannot start");
    run = definition;
    run.party.front().skills.clear();
    check(!run.Start(content), "empty skill loadout cannot start");
    run = definition;
    run.party.front().skills.push_back(run.party.front().skills.front());
    check(!run.Start(content), "duplicate prepared skills cannot start");

    entt::registry collisions;
    auto& eventBus = collisions.ctx().emplace<FEventBus>();
    const auto colliderA = collisions.create();
    const auto colliderB = collisions.create();
    collisions.emplace<FPosition>(colliderA, FPosition{0.f, 0.f});
    collisions.emplace<FPosition>(colliderB, FPosition{8.f, 0.f});
    collisions.emplace<FCollider>(colliderA, FCollider{.halfWidth = 8.f, .halfHeight = 8.f});
    collisions.emplace<FCollider>(colliderB, FCollider{.halfWidth = 8.f, .halfHeight = 8.f});
    CollisionSystem collisionSystem;
    collisionSystem.update(collisions, 0.f);
    const auto& overlapEvents = eventBus.frameEvents<FCollisionEvent>();
    check(overlapEvents.size() == 1 && overlapEvents.front().manifold.count > 0,
          "overlapping same-layer AABBs queue one collision event with contact manifold");
    eventBus.beginFrame();
    collisions.get<FPosition>(colliderB).x = 32.f;
    collisionSystem.update(collisions, 0.f);
    check(eventBus.frameEvents<FCollisionEvent>().empty(), "separated AABBs do not queue collision event");
    eventBus.beginFrame();
    collisions.get<FPosition>(colliderB).x = 8.f;
    collisions.get<FCollider>(colliderB).collisionLayer = 2;
    collisionSystem.update(collisions, 0.f);
    check(eventBus.frameEvents<FCollisionEvent>().empty(), "nonmatching collision layers suppress events");

    const auto document = FJsonLoader::Load(definition.rooms.front().battlefield);
    if (!document) return false;
    entt::registry registry;
    FScene scene(registry, "PrototypeChecks");
    scene.Load();
    const auto battlefield = FEntityFactory::CreateBattlefield(scene, *document);
    if (!battlefield || !SetupCombatUnits(registry, *battlefield, content)) return false;
    registry.ctx().emplace<gameplay::FContentRegistry>(content);
    registry.ctx().emplace<FGridMetrics>(battlefield->metrics);
    auto& queue = registry.ctx().emplace<FActionQueue>();
    auto& combat = registry.ctx().emplace<FCombatRuntime>();
    registry.ctx().emplace<FBattleOutcome>();
    registry.ctx().emplace<FGridSelection>();
    auto& input = registry.ctx().emplace<FInputState>();
    auto& runtime = registry.ctx().emplace<FTurnRuntime>();
    const auto actor = battlefield->units.front().entity;
    const auto target = FindNearestAlive(registry, actor, false);
    if (target == entt::null) return false;
    registry.get<FSpeed>(actor).value = 100;
    if (!runtime.controller.start(registry, 1234u)) return false;
    const auto token = *runtime.controller.snapshot().currentTurn;
    check(token.actor == actor, "test actor owns the current turn");
    auto& points = registry.get<FActionPoints>(actor);
    auto& health = registry.get<FHealth>(target);
    health.current = health.max = 1000;
    const auto skill = registry.get<FSkillLoadout>(actor).slots.front().skillId;
    const auto resolved = ResolveSkillSlot(registry.get<FSkillLoadout>(actor).slots.front(), content);
    if (!resolved) return false;
    ecs::ActionResolverSystem resolver;
    const auto submit = [&](FActionRequest request) {
        queue.pending.push_back(std::move(request));
        resolver.update(registry, 0.f);
    };
    FActionRequest attack{actor, target, skill};
    attack.turn = token;
    auto stale = attack;
    ++stale.turn.turnSerial;
    submit(stale);
    check(health.current == 1000 && points.skill == points.skillMax, "stale turn token rejected without AP/damage");
    auto wrongActor = attack;
    wrongActor.actor = target;
    submit(wrongActor);
    check(health.current == 1000 && points.skill == points.skillMax, "out-of-turn actor rejected");
    auto wrongSide = attack;
    wrongSide.target = actor;
    submit(wrongSide);
    check(health.current == 1000 && points.skill == points.skillMax, "wrong target faction rejected");
    submit(attack);
    check(health.current < 1000 && points.skill == points.skillMax - resolved->apCost,
          "selected target takes damage and only skill AP is spent");
    points.skill = 0;
    const int beforeRejectedAttack = health.current;
    submit(attack);
    check(health.current == beforeRejectedAttack, "exhausted skill AP rejects attack");

    FActionRequest move{actor};
    move.turn = token;
    move.action = EBattleAction::Move;
    move.cell = registry.get<FGridPosition>(target);
    submit(move);
    check(points.move == points.moveMax, "occupied destination rejected");
    move.cell = {-1, -1};
    submit(move);
    check(points.move == points.moveMax, "off-grid destination rejected");
    const auto reachable = ComputeReachableCells(registry, actor, points.move);
    if (reachable.empty()) return false;
    move.cell = reachable.front();
    submit(move);
    const auto moved = registry.get<FGridPosition>(actor);
    const auto world = GridCellTopLeftToWorld(battlefield->metrics, moved.x, moved.y);
    const auto position = registry.get<FPosition>(actor);
    check(moved.x == move.cell.x && moved.y == move.cell.y && position.x == world.x && position.y == world.y
          && points.move == points.moveMax - 1 && points.skill == 0, "movement synchronizes coordinates and separate AP");
    const auto exhaustedCell = moved;
    move.cell = {moved.x, moved.y - 1};
    submit(move);
    check(registry.get<FGridPosition>(actor).x == exhaustedCell.x
          && registry.get<FGridPosition>(actor).y == exhaustedCell.y, "exhausted move AP rejects movement");

    auto& loadout = registry.get<FSkillLoadout>(actor);
    loadout.slots.push_back({"skill.fog", {}});
    points.skill = points.skillMax;
    FActionRequest fog{actor, entt::null, "skill.fog"};
    fog.turn = token;
    fog.cell = {moved.x + 1, moved.y};
    submit(fog);
    check(points.skill < points.skillMax, "cell-target skill accepts an empty grid cell");

    ecs::GridSelectSystem picker;
    picker.update(registry, 0.f);
    combat.selectedSkill = 0;
    const auto targetCell = registry.get<FGridPosition>(target);
    input.mousePos = WorldToScreen(GridCellCenterToWorld(battlefield->metrics, targetCell.x, targetCell.y), FCamera{}, FViewport{});
    input.mouseLeftPressed = true;
    picker.update(registry, 0.f);
    check(queue.pending.size() == 1 && queue.pending.front().target == target && queue.pending.front().turn == token,
          "unit hitbox click queues selected target with current token");
    queue.pending.clear();
    input.mousePos = WorldToScreen(GridCellTopLeftToWorld(battlefield->metrics, targetCell.x, targetCell.y)
        + glm::vec2{kUnitHitboxInset - 1.f}, FCamera{}, FViewport{});
    picker.update(registry, 0.f);
    check(queue.pending.empty(), "outside target hitbox does not queue attack");
    input.mousePos = WorldToScreen(GridCellCenterToWorld(battlefield->metrics, targetCell.x, targetCell.y), FCamera{}, FViewport{});
    input.uiCapturesMouse = true;
    picker.update(registry, 0.f);
    check(queue.pending.empty(), "UI capture prevents battlefield click-through");
    input.uiCapturesMouse = false;
    input.mouseLeftPressed = false;
    input.mouseLeftHeld = true;
    picker.update(registry, 0.f);
    check(queue.pending.empty(), "holding mouse does not repeat turn-based attacks");
    input.uiCapturesMouse = true;
    SDL_Event release{};
    release.type = SDL_EVENT_MOUSE_BUTTON_UP;
    release.button.button = SDL_BUTTON_LEFT;
    (void)input.processEvent(release);
    check(!input.mouseLeftHeld, "captured mouse release clears held state");

    for (const FViewport viewport : {FViewport{1280.f, 720.f}, FViewport{960.f, 640.f}, FViewport{1920.f, 1080.f}}) {
        const auto camera = FitBattleCamera(battlefield->metrics, viewport);
        const auto layout = BattleLayout(viewport);
        const auto top = WorldToScreen(battlefield->metrics.origin, camera, viewport);
        const auto bottom = WorldToScreen(battlefield->metrics.origin + glm::vec2{
            battlefield->metrics.width * battlefield->metrics.tileSize, battlefield->metrics.height * battlefield->metrics.tileSize}, camera, viewport);
        check(top.x >= layout.partyWidth && top.y >= layout.headerHeight
            && bottom.x <= viewport.width - layout.journalWidth && bottom.y <= viewport.height - layout.dockHeight,
            "camera fits the complete clearing between responsive HUD panels");
        const auto worldPoint = GridCellCenterToWorld(battlefield->metrics, targetCell.x, targetCell.y);
        const auto roundTrip = ScreenToWorld(WorldToScreen(worldPoint, camera, viewport), camera, viewport);
        check(glm::length(roundTrip - worldPoint) < 0.01f, "camera projection round-trip preserves target center");
    }
    const auto cameraEntity = registry.create();
    registry.emplace<FTag>(cameraEntity, FTag{"battle_camera"});
    const FCamera zoomedCamera{{370.f, 290.f}, 1.5f};
    registry.emplace<FCamera>(cameraEntity, zoomedCamera);
    input.uiCapturesMouse = false;
    input.mouseLeftPressed = true;
    input.mousePos = WorldToScreen(GridCellCenterToWorld(battlefield->metrics, targetCell.x, targetCell.y), zoomedCamera, FViewport{});
    points.skill = points.skillMax;
    combat.selectedSkill = static_cast<int>(loadout.slots.size()) - 1;
    picker.update(registry, 0.f);
    check(queue.pending.size() == 1 && queue.pending.front().skillId == "skill.fog"
        && queue.pending.front().target == target, "shared HUD selection is used by zoomed target hitbox picking");
    check(registry.ctx().get<FGridSelection>().hovered.has_value()
        && !registry.ctx().get<FGridSelection>().preview.empty(), "hover supplies a bounded resolved-skill area preview");
    queue.pending.clear();
    input.uiCapturesMouse = true;
    picker.update(registry, 0.f);
    check(queue.pending.empty() && !registry.ctx().get<FGridSelection>().hovered
        && registry.ctx().get<FGridSelection>().preview.empty(), "HUD capture clears world hover and prevents click-through");
    registry.destroy(cameraEntity);

    entt::registry automatic;
    FScene autoScene(automatic, "AutoCombatChecks");
    autoScene.Load();
    const auto autoBattle = FEntityFactory::CreateBattlefield(autoScene, *document);
    if (!autoBattle || !SetupCombatUnits(automatic, *autoBattle, content)) return false;
    automatic.ctx().emplace<gameplay::FContentRegistry>(content);
    automatic.ctx().emplace<FGridMetrics>(autoBattle->metrics);
    automatic.ctx().emplace<FActionQueue>();
    auto& autoRuntime = automatic.ctx().emplace<FCombatRuntime>();
    autoRuntime.autoPlay = true;
    auto& outcome = automatic.ctx().emplace<FBattleOutcome>();
    auto& turns = automatic.ctx().emplace<FTurnRuntime>();
    turns.requests.push_back(FStartTurnSession{1234u});
    ecs::TurnSystem turnSystem;
    ecs::TurnFlowSystem turnFlow;
    ecs::CombatOutcomeSystem outcomes;
    for (int frame = 0; frame < 10000 && !outcome.resolved; ++frame) {
        turnSystem.update(automatic, 1.f / 60.f);
        turnFlow.update(automatic, 1.f / 60.f);
        resolver.update(automatic, 1.f / 60.f);
        outcomes.update(automatic, 1.f / 60.f);
    }
    check(outcome.resolved, "actual turn/action/outcome pipeline terminates automated battle");
    check(!turns.lastError, "automatic AP actions preserve turn-token ordering");
    entt::registry routed;
    routed.ctx().emplace<gameplay::FContentRegistry>(content);
    auto& routedRun = routed.ctx().emplace<FPrototypeRun>(definition);
    routed.ctx().emplace<FTurnRuntime>();
    auto& routedInput = routed.ctx().emplace<FInputState>();
    check(routedRun.Start(content), "state-lifecycle run starts from prepared party");
    for (int battleIndex = 0; battleIndex < 3; ++battleIndex) {
        FBattleState battleState;
        battleState.OnEnter(routed);
        auto* battleOutcome = routed.ctx().find<FBattleOutcome>();
        auto* battleRuntime = routed.ctx().find<FCombatRuntime>();
        if (!battleOutcome || !battleRuntime) return false;
        for (const auto entity : routed.view<FPartyMember, FSkillLoadout>()) {
            for (const auto& slot : routed.get<FSkillLoadout>(entity).slots) {
                check(slot.runeIds.empty(), "prepared battlefield has no inherited authored runes");
            }
        }
        battleRuntime->autoPlay = true;
        for (int frame = 0; frame < 10000 && !battleOutcome->resolved; ++frame) {
            turnSystem.update(routed, 1.f / 60.f);
            turnFlow.update(routed, 1.f / 60.f);
            resolver.update(routed, 1.f / 60.f);
            outcomes.update(routed, 1.f / 60.f);
        }
        check(battleOutcome->resolved && battleOutcome->victory, "prepared party clears room through actual battle state");
        battleOutcome->exitRequested = true;
        check(battleState.Update(routed, 0.f) == EStateTransition::ToHub, "battle result advances app flow");
        battleState.OnExit(routed);
        check(routed.view<FGridPosition>().begin() == routed.view<FGridPosition>().end()
              && !routed.ctx().contains<FBattleOutcome>(), "battle scene unload removes entities and context");
        if (!routedRun.active) break;
        FHubState hub;
        hub.OnEnter(routed);
        if (routedRun.CurrentRoom()->kind == EPrototypeRoom::Event) {
            SDL_Event confirm{};
            confirm.type = SDL_EVENT_KEY_DOWN;
            confirm.key.scancode = SDL_SCANCODE_RETURN;
            (void)routedInput.processEvent(confirm);
            check(hub.Update(routed, 0.f) == EStateTransition::ToBattle, "event continuation reaches next battle");
            routedInput.beginFrame();
        } else if (routedRun.CurrentRoom()->kind == EPrototypeRoom::Battle) {
            check(hub.Update(routed, 0.f) == EStateTransition::ToBattle, "room flow enters next battle");
        }
        hub.OnExit(routed);
    }
    check(routedRun.CurrentRoom() && routedRun.CurrentRoom()->kind == EPrototypeRoom::Camp
          && routedRun.completedDungeons == 1, "actual three-room lifecycle reaches checkpoint camp");
    LOG_INFO("[verify] {} prototype checks: {}", checks, passed ? "PASS" : "FAIL");
    return passed;
}

} // namespace game
