#include "FBattleState.h"

#include "FHubState.h"

#include <SDL3/SDL.h>

#include "core/JsonLoader.h"
#include "core/Logger.h"
#include "core/InputState.h"
#include "core/EngineState.h"
#include "core/AssetManager.h"
#include "core/ResourceManager.h"
#include "ecs/components/FCamera.h"
#include "ecs/components/FHealth.h"
#include "ecs/components/FSpeed.h"
#include "ecs/components/FSprite.h"
#include "ecs/components/FStats.h"
#include "ecs/components/FTag.h"
#include "ecs/systems/TurnSystem.h"
#include "gameplay/battle/FBattleSetup.h"
#include "gameplay/combat/FCombatTypes.h"
#include "gameplay/data/FContentRegistry.h"
#include "gameplay/factory/FEntityFactory.h"
#include "gameplay/grid/FGridSelection.h"
#include "gameplay/grid/FGridTypes.h"
#include "gameplay/grid/FGridTerrain.h"
#include "gameplay/grid/FBattleLayout.h"
#include "gameplay/run/FPrototypeRun.h"
#include <algorithm>

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
        "SpriteRenderSystem",
        "CombatHudSystem"
    };
    return systems;
}

void FBattleState::OnEnter(entt::registry& Registry)
{
    battleReady = false;
    scene = std::make_unique<game::FScene>(Registry, "BattleGrid");
    scene->Load();

    const auto* run = Registry.ctx().find<game::FPrototypeRun>();
    const auto* room = run ? run->CurrentRoom() : nullptr;
    const auto document = game::FJsonLoader::Load(room ? room->battlefield : "assets/data/battlefield.json");
    if (!document) {
        LOG_ERROR("Battlefield data load failed: {}", document.error().message);
        return;
    }

    auto battlefield = game::FEntityFactory::CreateBattlefield(*scene, *document);
    if (!battlefield) {
        LOG_ERROR("Battlefield creation failed: {}", battlefield.error().message);
        return;
    }
    Registry.ctx().erase<game::FGridTerrain>();
    battlefield->terrain.moveRange = 1;
    Registry.ctx().emplace<game::FGridTerrain>(battlefield->terrain);
    if (run && run->active) {
        std::erase_if(battlefield->units, [&](game::FUnitPlacement& placement) {
            if (placement.faction != game::EUnitFaction::Party) return false;
            const auto member = std::ranges::find_if(run->party, [&](const game::FPreparedMember& prepared) {
                return prepared.placementId == placement.id && prepared.enabled;
            });
            if (member == run->party.end()) {
                Registry.destroy(placement.entity);
                return true;
            }
            placement.skills = member->skills;
            return false;
        });
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
    const auto* window = Registry.ctx().find<game::FEngineWindowState>();
    const game::FViewport viewport{window ? static_cast<float>(window->width) : 1280.f,
                                  window ? static_cast<float>(window->height) : 720.f};
    scene->Emplace<game::FCamera>(*camera, game::FitBattleCamera(metrics, viewport));
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
    if (run && run->active) {
        for (const game::FUnitPlacement& placement : battlefield->units) {
            if (placement.faction != game::EUnitFaction::Party) continue;
            const auto member = std::ranges::find_if(run->party, [&](const game::FPreparedMember& prepared) {
                return prepared.placementId == placement.id && prepared.enabled;
            });
            if (member == run->party.end()) continue;
            const game::FPassiveStatBonuses bonuses = run->PassiveBonuses(member->placementId);
            auto& stats = Registry.get<game::FStats>(placement.entity);
            stats.attack += bonuses.attack;
            stats.magicAttack += bonuses.magicAttack;
            stats.defense += bonuses.defense;
            stats.magicDefense += bonuses.magicDefense;
            auto& health = Registry.get<game::FHealth>(placement.entity);
            health.max += bonuses.maxHealth;
            health.current = health.max;
            auto& speed = Registry.get<game::FSpeed>(placement.entity);
            speed.value += static_cast<std::int32_t>(bonuses.speed);
        }
    }
    const auto* assets = Registry.ctx().find<game::FAssetManager>();
    auto* resources = Registry.ctx().find<game::FResourceManager>();
    if (assets && resources) {
        for (const game::FUnitPlacement& placement : battlefield->units) {
            const auto* unit = content->units.find(placement.unitTemplateId);
            if (!unit || unit->spriteId.empty()) continue;
            const auto texture = resources->loadTexture(*assets, unit->spriteId);
            if (!texture) continue;
            const float spriteSize = battlefield->metrics.tileSize;
            scene->Emplace<game::FSprite>(placement.entity, game::FSprite{
                .texture = *texture,
                .width = spriteSize,
                .height = spriteSize
            });
        }
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
    Registry.ctx().erase<game::FGridTerrain>();
    scene.reset();
    battleReady = false;
}

FStateTransition FBattleState::Update(entt::registry& Registry, float DeltaTime)
{
    (void)DeltaTime;
    if (!battleReady) {
        if (auto* run = Registry.ctx().find<game::FPrototypeRun>()) run->End();
        return {EStateChangeKind::ReplaceTop, std::string(kForestHubId)};
    }

    const auto* input = Registry.ctx().find<game::FInputState>();
    const auto* outcome = Registry.ctx().find<game::FBattleOutcome>();
    if ((input && input->isActionPressed(game::EInputAction::Cancel)) || (outcome && outcome->abandonRequested)) {
        if (auto* run = Registry.ctx().find<game::FPrototypeRun>()) run->End();
        return {EStateChangeKind::ReplaceTop, std::string(kForestHubId)};
    }

    if (outcome && outcome->resolved) {
        if (outcome->exitRequested || (input && input->isActionPressed(game::EInputAction::Confirm))) {
            if (auto* run = Registry.ctx().find<game::FPrototypeRun>(); run && run->active) {
                (void)run->ResolveCurrentBattle(outcome->victory);
            }
            return {EStateChangeKind::ReplaceTop, std::string(kForestHubId)};
        }
    }
    return {};
}
