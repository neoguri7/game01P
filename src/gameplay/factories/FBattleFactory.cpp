#include "gameplay/factories/FBattleFactory.h"

#include "core/Logger.h"
#include "gameplay/components/FApPool.h"
#include "gameplay/components/FBattleDefeat.h"
#include "gameplay/components/FBattleOngoing.h"
#include "gameplay/components/FBattleRound.h"
#include "gameplay/components/FBattleVictory.h"
#include "gameplay/components/FDisplayName.h"
#include "gameplay/components/FDowned.h"
#include "gameplay/components/FGridPosition.h"
#include "gameplay/components/FHealth.h"
#include "gameplay/components/FInitiative.h"
#include "gameplay/components/FSkillSet.h"
#include "gameplay/components/FTeamEnemy.h"
#include "gameplay/components/FTeamPlayer.h"
#include "gameplay/components/FTurnActive.h"
#include "gameplay/components/FUnitRef.h"
#include "gameplay/data/FContentRegistry.h"
#include "gameplay/data/FEncounterContent.h"
#include "gameplay/data/FUnitContent.h"

#include <tracy/Tracy.hpp>

#include <string>
#include <vector>

namespace game::gameplay {
namespace {

/// One spawn = the component set every unit has, regardless of side. why a helper instead of a builder class:
/// the composition is fixed for this slice, and R10 forbids an abstraction before a second shape exists.
entt::entity spawnUnit(entt::registry& registry,
                       const FUnitContent& unit,
                       int x,
                       int y,
                       bool playerSide) {
    const entt::entity entity = registry.create();
    registry.emplace<FUnitRef>(entity, FUnitRef{unit.id});
    registry.emplace<FDisplayName>(entity, FDisplayName{unit.displayName});
    registry.emplace<FGridPosition>(entity, FGridPosition{x, y});
    registry.emplace<FHealth>(entity, FHealth{unit.maxHealth, unit.maxHealth});
    registry.emplace<FInitiative>(entity, FInitiative{unit.speed});
    registry.emplace<FSkillSet>(entity, FSkillSet{unit.skillIds});
    // why AP starts at zero: FTurnStartSystem refills it from content when the unit's turn opens, so there is
    // exactly one place that decides "what a fresh turn looks like" (미결(design §4): AP 회복 수단 여부).
    registry.emplace<FApPool>(entity, FApPool{});
    if (playerSide) {
        registry.emplace<FTeamPlayer>(entity);
    } else {
        registry.emplace<FTeamEnemy>(entity);
    }
    return entity;
}

/// Spawns one side and reports the first cell that cannot be used. why not "skip the bad unit": a fight with a
/// silently missing unit is a wrong fight, and the content check in FContentRegistry cannot see cell collisions.
bool spawnSide(entt::registry& registry,
               const FContentRegistry& content,
               const std::vector<std::string>& unitIds,
               const std::vector<double>& cells,
               bool playerSide,
               const FBattleState& state) {
    std::vector<FGridPosition> taken;

    for (std::size_t index = 0; index < unitIds.size(); ++index) {
        const FUnitContent* unit = content.units.find(unitIds[index]);
        if (unit == nullptr) {
            LOG_ERROR("battle: unit row '{}' disappeared between content validation and spawn.", unitIds[index]);
            return false;
        }

        const FGridPosition cell{static_cast<int>(cells[index * 2]), static_cast<int>(cells[index * 2 + 1])};
        if (!state.inBounds(cell)) {
            LOG_ERROR("battle: unit '{}' spawns off the {}x{} grid at ({}, {}).",
                      unit->id,
                      state.gridWidth,
                      state.gridHeight,
                      cell.x,
                      cell.y);
            return false;
        }
        for (const FGridPosition& occupied : taken) {
            if (occupied.x == cell.x && occupied.y == cell.y) {
                LOG_ERROR("battle: two units of one side share cell ({}, {}).", cell.x, cell.y);
                return false;
            }
        }
        taken.push_back(cell);

        spawnUnit(registry, *unit, cell.x, cell.y, playerSide);
    }
    return true;
}

} // namespace

bool FBattleFactory::buildBattle(entt::registry& registry,
                                 const FContentRegistry& content,
                                 std::string_view encounterId,
                                 FBattleState& state) {
    ZoneScopedN("FBattleFactory::buildBattle");

    const FEncounterContent* encounter = content.encounters.find(encounterId);
    if (encounter == nullptr) {
        LOG_ERROR("battle: encounter '{}' is not in the content table.", encounterId);
        return false;
    }

    // Grid size comes from the encounter row (R4). why copied into the service: bounds checks happen every
    // move, and re-reading the row per frame would spread the encounter lookup over the whole battle.
    state.gridWidth = encounter->gridWidth;
    state.gridHeight = encounter->gridHeight;

    if (!spawnSide(registry, content, encounter->partyIds, encounter->partyCells, true, state)
        || !spawnSide(registry, content, encounter->enemyIds, encounter->enemyCells, false, state)) {
        // why a rollback: a partially spawned battle would still run, with one side quietly smaller — the
        // worst possible failure for a balance-critical slice (R19).
        for (const auto entity : registry.view<FUnitRef>()) {
            registry.destroy(entity);
        }
        state.gridWidth = 8;  // fallback: 8 (design §4 확정 8x8 — 배치 실패 시 서비스는 초기값으로 되돌린다)
        state.gridHeight = 8; // fallback: 8 (design §4 확정 8x8)
        return false;
    }

    const entt::entity battle = registry.create();
    registry.emplace<FBattleRound>(battle, FBattleRound{0});
    registry.emplace<FBattleOngoing>(battle);
    state.battle = battle;
    state.order.clear();
    state.cursor = 0;

    LOG_INFO("battle '{}' ready: {} party unit(s) vs {} enemy unit(s) on {}x{}.",
             encounter->id,
             encounter->partyIds.size(),
             encounter->enemyIds.size(),
             state.gridWidth,
             state.gridHeight);
    return true;
}

void FBattleFactory::openTurn(entt::registry& registry, entt::entity unit) {
    if (unit == entt::null || !registry.valid(unit) || registry.all_of<FTurnActive>(unit)) {
        return;
    }
    registry.emplace<FTurnActive>(unit);
}

void FBattleFactory::closeTurn(entt::registry& registry, entt::entity unit) {
    if (unit == entt::null || !registry.valid(unit)) {
        return;
    }
    registry.remove<FTurnActive>(unit);
}

void FBattleFactory::markDowned(entt::registry& registry, entt::entity unit) {
    if (unit == entt::null || !registry.valid(unit) || registry.all_of<FDowned>(unit)) {
        return;
    }
    registry.emplace<FDowned>(unit);
    // why the turn tag goes away here: a unit can be dropped by a counter-attack while it holds the turn, and a
    // downed unit without this line would still be the acting unit of the frame.
    registry.remove<FTurnActive>(unit);
}

void FBattleFactory::endBattleAsVictory(entt::registry& registry, entt::entity battle) {
    if (battle == entt::null || !registry.valid(battle)) {
        return;
    }
    registry.remove<FBattleOngoing>(battle);
    registry.emplace<FBattleVictory>(battle);
}

void FBattleFactory::endBattleAsDefeat(entt::registry& registry, entt::entity battle) {
    if (battle == entt::null || !registry.valid(battle)) {
        return;
    }
    registry.remove<FBattleOngoing>(battle);
    registry.emplace<FBattleDefeat>(battle);
}

} // namespace game::gameplay
