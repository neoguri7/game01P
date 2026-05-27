#include "gameplay/tactical_d20/TacticalD20SetupSystem.h"

#include "core/events/FEventBus.h"
#include "core/factories/FTacticalD20BoardTileFactory.h"
#include "core/factories/FTacticalD20CommandTokenFactory.h"
#include "core/factories/FTacticalD20UnitFactory.h"
#include "ecs/components/FCombatStateInitiativeRolling.h"
#include "ecs/components/FCombatStateSetup.h"
#include "gameplay/tactical_d20/FTacticalD20BoardInteraction.h"
#include "gameplay/tactical_d20/FTacticalD20CombatLog.h"
#include "gameplay/tactical_d20/FTacticalD20Config.h"
#include "gameplay/tactical_d20/FTacticalD20EventLog.h"
#include "gameplay/tactical_d20/FTacticalD20Events.h"
#include "gameplay/tactical_d20/FTacticalD20StateLog.h"
#include <algorithm>
#include <array>
#include <fmt/format.h>
#include <tracy/Tracy.hpp>

namespace game {
namespace {

bool TileInList(const std::vector<FTacticalD20TileConfig>& tiles, int x, int y) {
    return std::ranges::any_of(tiles, [x, y](const FTacticalD20TileConfig& tile) {
        return tile.x == x && tile.y == y;
    });
}

void SpawnBoard(entt::registry& registry, const FTacticalD20Config& config) {
    for (int y = 0; y < config.gridHeight; ++y) {
        for (int x = 0; x < config.gridWidth; ++x) {
            FTacticalD20BoardTileFactory::create(registry, x, y, config.tileFeet, TileInList(config.walls, x, y), TileInList(config.coverTiles, x, y));
        }
    }
}

void EnsureBoardInteractionState(entt::registry& registry) {
    if (!registry.ctx().contains<FTacticalD20BoardInteraction>()) {
        registry.ctx().emplace<FTacticalD20BoardInteraction>();
    }
}

void EnsureStateLog(entt::registry& registry) {
    if (!registry.ctx().contains<FTacticalD20StateLog>()) {
        registry.ctx().emplace<FTacticalD20StateLog>();
    }
}

void EnsureObservableLogs(entt::registry& registry) {
    if (!registry.ctx().contains<FTacticalD20CombatLog>()) registry.ctx().emplace<FTacticalD20CombatLog>();
    if (!registry.ctx().contains<FTacticalD20EventLog>()) registry.ctx().emplace<FTacticalD20EventLog>();
    EnsureStateLog(registry);
}

void AppendStateLog(entt::registry& registry, const FTacticalD20Config& config, const std::string& line) {
    auto& log = registry.ctx().get<FTacticalD20StateLog>();
    log.lines.push_back(line);
    while (static_cast<int>(log.lines.size()) > config.logging.stateLogMaxLines) {
        log.lines.erase(log.lines.begin());
    }
}

void SpawnUnits(entt::registry& registry, const FTacticalD20Config& config) {
    for (const auto& unit : config.units) {
        FTacticalD20UnitFactory::create(registry, FTacticalD20UnitSpawn{
            .id = unit.id,
            .team = unit.team,
            .displayName = unit.displayName,
            .tileX = unit.startTileX,
            .tileY = unit.startTileY,
            .maxHp = unit.maxHp,
            .armorClass = unit.armorClass,
            .speedFeet = unit.speedFeet,
            .abilities = unit.abilities,
            .weaponProficiencies = unit.weaponProficiencies,
            .savingThrowProficiencies = unit.savingThrowProficiencies,
            .skillProficiencies = unit.skillProficiencies,
            .actions = unit.actions,
        });
    }
}

void SpawnCommandTokens(entt::registry& registry) {
    constexpr std::array<std::pair<const char*, const char*>, 5> tokens{{
        {"move", "Move"},
        {"attack", "Attack"},
        {"dash", "Dash"},
        {"dodge", "Dodge"},
        {"wait", "Wait"},
    }};

    for (std::size_t i = 0; i < tokens.size(); ++i) {
        FTacticalD20CommandTokenFactory::create(registry, tokens[i].first, tokens[i].second, static_cast<int>(i));
    }
}

void TransitionSetupToInitiative(entt::registry& registry, const FTacticalD20Config& config) {
    // Transition table:
    //   FCombatStateSetup + setup entities spawned -> FCombatStateInitiativeRolling
    auto view = registry.view<FCombatStateSetup>();
    for (auto entity : view) {
        registry.remove<FCombatStateSetup>(entity);
        registry.emplace<FCombatStateInitiativeRolling>(entity);
        AppendStateLog(registry, config, fmt::format("[CombatState] {} -> {}", "CombatSetup", "InitiativeRolling"));
        const FTacticalD20CombatStateChangedEvent event{"CombatSetup", "InitiativeRolling"};
        PUBLISH(FTacticalD20CombatStateChangedEvent, registry, event);
    }
}

} // namespace

void TacticalD20SetupSystem::update(entt::registry& registry, float /*dt*/) {
    ZoneScopedN("TacticalD20SetupSystem");
    auto* config = registry.ctx().find<FTacticalD20Config>();
    if (!config || registry.view<FCombatStateSetup>().empty()) return;

    PUBLISH(FTacticalD20CombatSetupRequestedEvent, registry, FTacticalD20CombatSetupRequestedEvent{});
    EnsureBoardInteractionState(registry);
    EnsureObservableLogs(registry);
    SpawnBoard(registry, *config);
    SpawnUnits(registry, *config);
    SpawnCommandTokens(registry);
    const FTacticalD20CombatSetupCompletedEvent setupCompleted{
        .gridWidth = config->gridWidth,
        .gridHeight = config->gridHeight,
        .unitCount = static_cast<int>(config->units.size()),
    };
    PUBLISH(FTacticalD20CombatSetupCompletedEvent, registry, setupCompleted);
    TransitionSetupToInitiative(registry, *config);
}

} // namespace game
