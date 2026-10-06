#include "FBattleState.h"

#include <SDL3/SDL.h>

#include "core/JsonLoader.h"
#include "core/Logger.h"
#include "core/InputState.h"
#include "ecs/components/FCamera.h"
#include "ecs/components/FTag.h"
#include "ecs/systems/TurnSystem.h"
#include "gameplay/battle/FBattleSetup.h"
#include "gameplay/combat/FCombatTypes.h"
#include "gameplay/data/FContentRegistry.h"
#include "gameplay/factory/FEntityFactory.h"
#include "gameplay/grid/FGridSelection.h"
#include "gameplay/grid/FGridTypes.h"

std::span<const std::string_view> FBattleState::RequiredSystems() const noexcept
{
    static constexpr std::string_view systems[]{
        "GridCameraSystem",
        "GridSelectSystem",
        "GridRenderSystem",
        "TurnSystem",
        "TurnFlowSystem",
        "ActionResolverSystem",
        "CombatOutcomeSystem",
        "CombatHudSystem"
    };
    return systems;
}

void FBattleState::OnEnter(entt::registry& Registry)
{
    battleReady = false;
    scene = std::make_unique<game::FScene>(Registry, "BattleGrid");
    scene->Load();

    const auto document = game::FJsonLoader::Load("assets/data/battlefield.json");
    if (!document) {
        LOG_ERROR("Battlefield data load failed: {}", document.error().message);
        return;
    }

    const auto battlefield = game::FEntityFactory::CreateBattlefield(*scene, *document);
    if (!battlefield) {
        LOG_ERROR("Battlefield creation failed: {}", battlefield.error().message);
        return;
    }

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

    Registry.ctx().erase<game::FActionQueue>();
    Registry.ctx().emplace<game::FActionQueue>();
    Registry.ctx().erase<game::FBattleOutcome>();
    Registry.ctx().emplace<game::FBattleOutcome>();
    Registry.ctx().erase<game::FCombatLog>();
    Registry.ctx().emplace<game::FCombatLog>();
    Registry.ctx().erase<game::FCombatRuntime>();
    Registry.ctx().emplace<game::FCombatRuntime>();
    if (const char* autoPlay = SDL_getenv("GAME01P_AUTOPLAY"); autoPlay != nullptr && std::string_view(autoPlay) == "1") {
        if (auto* combat = Registry.ctx().find<game::FCombatRuntime>()) {
            combat->autoPlay = true;
            LOG_INFO("Battle auto-play enabled (GAME01P_AUTOPLAY=1).");
        }
    }
    if (auto* runtime = Registry.ctx().find<game::FTurnRuntime>()) {
        runtime->stop();
        runtime->requests.push_back(game::FStartTurnSession{1234u}); // 잠정 시드 — 런 시드 슬라이스에서 대체
    }

    LOG_INFO("Battle ready: {} unit(s).", battlefield->units.size());
    battleReady = true;
}

void FBattleState::OnExit(entt::registry& Registry)
{
    if (auto* runtime = Registry.ctx().find<game::FTurnRuntime>()) {
        runtime->stop();
    }
    Registry.ctx().erase<game::FActionQueue>();
    Registry.ctx().erase<game::FBattleOutcome>();
    Registry.ctx().erase<game::FCombatLog>();
    Registry.ctx().erase<game::FCombatRuntime>();
    Registry.ctx().erase<game::FGridSelection>();
    Registry.ctx().erase<game::FGridMetrics>();
    scene.reset();
    battleReady = false;
}

EStateTransition FBattleState::Update(entt::registry& Registry, float DeltaTime)
{
    (void)DeltaTime;
    if (!battleReady) {
        return EStateTransition::ToHub;
    }

    const auto* input = Registry.ctx().find<game::FInputState>();
    if (input && input->isActionPressed(game::EInputAction::Cancel)) {
        return EStateTransition::ToHub;
    }

    const auto* outcome = Registry.ctx().find<game::FBattleOutcome>();
    if (outcome && outcome->resolved) {
        if (outcome->exitRequested) {
            return EStateTransition::ToHub;
        }
        if (input && input->isActionPressed(game::EInputAction::Confirm)) {
            return EStateTransition::ToHub;
        }
    }
    return EStateTransition::None;
}
