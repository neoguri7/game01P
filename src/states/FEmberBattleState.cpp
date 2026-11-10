#include "FEmberBattleState.h"
#include "FEmberTitleState.h"

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
#include "gameplay/combat/FBattleFlow.h"
#include "gameplay/combat/FCombatApply.h"
#include "gameplay/combat/FCombatTypes.h"
#include "gameplay/data/FContentRegistry.h"
#include "gameplay/factory/FEntityFactory.h"
#include "gameplay/grid/FGridSelection.h"
#include "gameplay/grid/FGridTypes.h"
#include "gameplay/dungeon/FBattleObjective.h"
#include "gameplay/dungeon/FDungeonRun.h"
#include "gameplay/dungeon/FRunFlow.h"
#include "gameplay/dungeon/FRunSeed.h"
#include "presentation/FBattleAnim.h"
#include "presentation/FDungeonArt.h"
#include "presentation/FDungeonOverlay.h"
#include "presentation/FDungeonViews.h"
#include "states/FRunFlowStateMap.h"

namespace game {

bool BattleOwnsScreen(const entt::registry& registry)
{
    const auto* run = registry.ctx().find<FDungeonRun>();
    return run != nullptr && run->phase == EDungeonPhase::Battle;
}

} // namespace game

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
        "EmberCombatHudSystem",
        "BattleFlowSystem"
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
        // WP-28 A3: GAME01P_SEED overrides the ticks seed; a retry in the same process advances by one.
        game::StartDungeonRun(Registry, game::ResolveRunSeed(static_cast<std::uint32_t>(SDL_GetTicks())));
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
    Registry.ctx().erase<game::FBattleObjective>();
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
    // WP-39: crits and the seeded objective belong to a fought battle only (a room re-entered at its reward
    // step has neither). GAME01P_STAKES=0 switches them off for the auto-play test harnesses.
    if (!continuedAfterRoom && game::StakesEnabled()) game::ApplyBattleStakes(Registry);
    // WP-05a lifetime: every battle owns a fresh presentation watch (the flow system advances it) and a
    // fresh flow state; the previous battle's values must not leak into this one. `roomClearedOnEnter`
    // carries the saved-phase continue, `handoffDone` is the one handoff guard.
    Registry.ctx().erase<game::FPresentationWatch>();
    Registry.ctx().emplace<game::FPresentationWatch>();
    Registry.ctx().erase<game::FBattleFlowState>();
    auto& flowState = Registry.ctx().emplace<game::FBattleFlowState>();
    flowState.roomClearedOnEnter = continuedAfterRoom;
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
    // WP-05a lifetime: the gate (written by the animation system), its watch and the flow state die with
    // the battle — a stale gate would freeze the next encounter.
    Registry.ctx().erase<game::FPresentationGate>();
    Registry.ctx().erase<game::FPresentationWatch>();
    Registry.ctx().erase<game::FBattleFlowState>();
    Registry.ctx().erase<game::FActionQueue>();
    Registry.ctx().erase<game::FBattleOutcome>();
    Registry.ctx().erase<game::FCombatLog>();
    Registry.ctx().erase<game::FCombatRuntime>();
    Registry.ctx().erase<game::FBattleObjective>();
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
        // A failed battlefield setup is a wiring error, not a hub stop: the title is the only safe screen.
        return {EStateChangeKind::Root, std::string(kEmberTitleId)};
    }

    if (const auto* input = Registry.ctx().find<game::FInputState>();
        input && input->isActionPressed(game::EInputAction::Cancel) && game::BattleOwnsScreen(Registry)) {
        if (auto* ui = Registry.ctx().find<game::FDungeonUiState>()) ui->confirmAbandon = true;
    }

    auto* run = Registry.ctx().find<game::FDungeonRun>();
    if (run && run->leaveRequested) {
        // Back to the menu with the run kept on disk; Continue picks it up from the last autosave.
        const auto result = game::ApplyRunIntent(Registry, game::ERunIntent::LeaveRun);
        if (result) return game::ToStateTransition(*result, kEmberBattleId);
        // The save failed: the run stays and the battle keeps playing. The reason is shown in the run message.
        if (auto* current = Registry.ctx().find<game::FDungeonRun>()) {
            current->leaveRequested = false;
            current->message = "Could not save the expedition: " + result.error();
        }
        return {};
    }
    if (run && run->abandonRequested) {
        const auto result = game::ApplyRunIntent(Registry, game::ERunIntent::AbandonRun);
        if (result) return game::ToStateTransition(*result, kEmberBattleId);
        run->abandonRequested = false;
        return {};
    }
    if (run && run->advanceRequested) {
        run->advanceRequested = false;
        // A2: leaving the battle context clears the whole stack, so the old battle's OnExit ctx erase runs
        // before the new battle's OnEnter. At depth 1 (today) this is identical to ReplaceTop.
        return {EStateChangeKind::Root, std::string(kEmberBattleId)};
    }

    const auto* outcome = Registry.ctx().find<game::FBattleOutcome>();
    auto* flow = Registry.ctx().find<game::FBattleFlowState>();
    // WP-05a handoff: the battle resolves once, after the presentation settles (level-triggered — the gate
    // is read every frame, never stored). FBattleFlowState::handoffDone is the single once-guard for both
    // the live run transition and the saved-phase continue modal.
    if (outcome != nullptr && outcome->resolved && flow != nullptr && !flow->handoffDone &&
        game::IsPresentationSettled(Registry)) {
        if (run != nullptr && !flow->roomClearedOnEnter && run->phase == game::EDungeonPhase::Battle) {
            const auto result = game::ResolveRoomVictory(Registry, outcome->victory);
            if (!result) {
                // The run is untouched and handoffDone stays false, so the next frame retries.
                LOG_ERROR("Battle resolution failed: {}", result.error());
                return {};
            }
            flow->handoffDone = true;
            return game::ToStateTransition(*result, kEmberBattleId);
        }
        // A saved-phase continue: the room was already cleared. Push its modal once, never re-resolve it.
        if (run != nullptr) {
            const auto modalId = game::RunModalIdForPhase(run->phase);
            if (!modalId.empty()) {
                flow->handoffDone = true;
                return {EStateChangeKind::Push, modalId};
            }
        }
    }
    return {};
}

void FEmberBattleState::Render(entt::registry& Registry)
{
    // The leave/abandon confirm lives with the battle, not the overlay: it is a popup over the field.
    // The battle draws it only while it owns the screen: when a run-phase modal (camp) is above, that
    // modal draws the shared confirm, and a second copy would duplicate its buttons and leak flags.
    if (!game::BattleOwnsScreen(Registry)) return;
    auto* ui = Registry.ctx().find<game::FDungeonUiState>();
    if (ui == nullptr || !ui->confirmAbandon) return;
    game::DrawAbandonConfirm(Registry, true, false);
}
