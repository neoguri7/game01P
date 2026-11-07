#include "FEmberBattleState.h"
#include "FEmberHubState.h"

#include <SDL3/SDL.h>

#include "core/JsonLoader.h"
#include "core/Logger.h"
#include "core/InputState.h"
#include "ecs/components/FCamera.h"
#include "ecs/components/FFaction.h"
#include "ecs/components/FHealth.h"
#include "ecs/components/FTag.h"
#include "ecs/systems/TurnSystem.h"
#include "gameplay/battle/FBattleSetup.h"
#include "gameplay/combat/FCombatApply.h"
#include "gameplay/combat/FCombatTypes.h"
#include "gameplay/data/FContentRegistry.h"
#include "gameplay/factory/FEntityFactory.h"
#include "gameplay/grid/FGridSelection.h"
#include "gameplay/grid/FGridTypes.h"
#include "gameplay/dungeon/FDungeonRun.h"
#include "presentation/FBattleAnim.h"
#include "presentation/FDungeonArt.h"

std::span<const std::string_view> FEmberBattleState::RequiredSystems() const noexcept
{
    static constexpr std::string_view systems[]{
        "GridCameraSystem",
        "EmberGridRenderSystem",
        "TurnSystem",
        "TurnFlowSystem",
        "ActionResolverSystem",
        "BattleAnimationSystem",
        "CombatOutcomeSystem",
        "EmberCombatHudSystem"
    };
    return systems;
}

std::span<const std::string_view> FEmberBattleState::RequiredRenderSystems() const noexcept
{
    // render-only while a modal is pushed above the battle: the field and HUD keep drawing, their update stops.
    static constexpr std::string_view systems[]{
        "EmberGridRenderSystem",
        "EmberCombatHudSystem",
        "BattleAnimationSystem"
    };
    return systems;
}

void FEmberBattleState::OnEnter(entt::registry& Registry)
{
    battleReady = false;
    scene = std::make_unique<game::FScene>(Registry, "BattleGrid");
    scene->Load();

    if (!Registry.ctx().contains<game::FDungeonRun>()) {
        game::StartDungeonRun(Registry, static_cast<std::uint32_t>(SDL_GetTicks()));
    }
    const auto document = game::BuildDungeonBattlefield(Registry);
    const auto battlefield = game::FEntityFactory::CreateBattlefield(*scene, document);
    if (!battlefield) {
        LOG_ERROR("Battlefield creation failed: {}", battlefield.error().message);
        return;
    }
    Registry.ctx().erase<game::FGridTerrain>();
    Registry.ctx().emplace<game::FGridTerrain>(battlefield->terrain);

    if (auto* metrics = Registry.ctx().find<game::FGridMetrics>()) {
        *metrics = battlefield->metrics;
    } else {
        Registry.ctx().emplace<game::FGridMetrics>(battlefield->metrics);
    }

    const auto camera = scene->CreateEntity();
    if (!camera) {
        LOG_ERROR("Battle scene refused camera entity creation");
        return;
    }
    scene->Emplace<game::FTag>(*camera, game::FTag{"battle_camera"});
    const auto& metrics = battlefield->metrics;
    const glm::vec2 center = metrics.origin + glm::vec2{
        static_cast<float>(metrics.width) * metrics.tileSize * 0.5f,
        static_cast<float>(metrics.height) * metrics.tileSize * 0.5f
    };
    scene->Emplace<game::FCamera>(*camera, game::FCamera{center, 1.f});
    if (!Registry.ctx().contains<game::FGridSelection>()) {
        Registry.ctx().emplace<game::FGridSelection>();
    }

    // Combat setup: content supplies stats/AP/speed, the placement supplies the run-scope loadout
    // (skills + runes). Without content the battle would silently become a no-op, so it stops here.
    const auto* content = Registry.ctx().find<game::gameplay::FContentRegistry>();
    if (content == nullptr) {
        LOG_ERROR("Battle needs loaded gameplay content (FContentRegistry is missing).");
        return;
    }
    if (!game::SetupCombatUnits(Registry, *battlefield, *content)) {
        LOG_ERROR("Battle units could not be made combat-ready.");
        return;
    }
    game::ApplyRunParty(Registry);
    game::AssignDungeonSprites(Registry, battlefield->units);
    game::BeginBattleAnim(Registry);
    game::AttachUnitAnimations(Registry, battlefield->units);

    Registry.ctx().erase<game::FActionQueue>();
    Registry.ctx().emplace<game::FActionQueue>();
    Registry.ctx().erase<game::FBattleOutcome>();
    Registry.ctx().emplace<game::FBattleOutcome>();
    Registry.ctx().erase<game::FCombatLog>();
    Registry.ctx().emplace<game::FCombatLog>();
    Registry.ctx().erase<game::FCombatRuntime>();
    Registry.ctx().emplace<game::FCombatRuntime>();
    Registry.ctx().get<game::FCombatRuntime>().rng.state =
        Registry.ctx().get<game::FDungeonRun>().seed + static_cast<std::uint32_t>(
            Registry.ctx().get<game::FDungeonRun>().depth * 3 + Registry.ctx().get<game::FDungeonRun>().room);
    if (const char* autoPlay = SDL_getenv("GAME01P_AUTOPLAY"); autoPlay != nullptr && std::string_view(autoPlay) == "1") {
        if (auto* combat = Registry.ctx().find<game::FCombatRuntime>()) {
            combat->autoPlay = true;
            LOG_INFO("Battle auto-play enabled (GAME01P_AUTOPLAY=1).");
        }
    }
    // A run continued at a reward, shrine or camp step shows the room it just cleared: the enemies lie defeated
    // and no fight is run. Leaving the camp or choosing an event moves on to a normal battle.
    const bool continuedAfterRoom = Registry.ctx().get<game::FDungeonRun>().phase != game::EDungeonPhase::Battle;
    if (continuedAfterRoom) {
        for (const auto enemy : Registry.view<game::FEnemy, game::FHealth>()) {
            (void)game::ApplyDamageToUnit(Registry, enemy, Registry.get<game::FHealth>(enemy).current);
        }
        auto& outcome = Registry.ctx().get<game::FBattleOutcome>();
        outcome.resolved = true;
        outcome.victory = true;
        game::SettleUnitAnimations(Registry);
    }
    if (auto* runtime = Registry.ctx().find<game::FTurnRuntime>()) {
        runtime->stop();
        if (!continuedAfterRoom) {
            runtime->requests.push_back(game::FStartTurnSession{Registry.ctx().get<game::FCombatRuntime>().rng.state});
        }
    }

    LOG_INFO("Battle ready: {} unit(s).", battlefield->units.size());
    battleReady = true;
}

void FEmberBattleState::OnExit(entt::registry& Registry)
{
    if (auto* runtime = Registry.ctx().find<game::FTurnRuntime>()) {
        runtime->stop();
    }
    game::EndBattleAnim(Registry);
    Registry.ctx().erase<game::FActionQueue>();
    Registry.ctx().erase<game::FBattleOutcome>();
    Registry.ctx().erase<game::FCombatLog>();
    Registry.ctx().erase<game::FCombatRuntime>();
    Registry.ctx().erase<game::FGridSelection>();
    Registry.ctx().erase<game::FGridMetrics>();
    Registry.ctx().erase<game::FGridTerrain>();
    scene.reset();
    battleReady = false;
}

FStateTransition FEmberBattleState::Update(entt::registry& Registry, float DeltaTime)
{
    (void)DeltaTime;
    if (!battleReady) {
        return {EStateChangeKind::ReplaceTop, std::string(kEmberHubId)};
    }

    auto* run = Registry.ctx().find<game::FDungeonRun>();
    if (run && run->leaveRequested) {
        // Back to the menu with the run kept on disk; Continue picks it up from the last autosave.
        game::SaveDungeonRun(Registry);
        Registry.ctx().erase<game::FDungeonRun>();
        return {EStateChangeKind::ReplaceTop, std::string(kEmberHubId)};
    }
    if (run && run->abandonRequested) {
        game::DeleteDungeonSave(Registry);
        Registry.ctx().erase<game::FDungeonRun>();
        return {EStateChangeKind::ReplaceTop, std::string(kEmberHubId)};
    }
    if (run && run->advanceRequested) {
        run->advanceRequested = false;
        // A2: leaving the battle context clears the whole stack, so the old battle's OnExit ctx erase runs
        // before the new battle's OnEnter. At depth 1 (today) this is identical to ReplaceTop.
        return {EStateChangeKind::Root, std::string(kEmberBattleId)};
    }

    const auto* outcome = Registry.ctx().find<game::FBattleOutcome>();
    if (outcome && outcome->resolved) {
        game::CompleteDungeonBattle(Registry, outcome->victory);
    }
    return {};
}
