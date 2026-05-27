#include "gameplay/tactical_d20/TacticalD20CommandValidationSystem.h"

#include "core/events/FEventBus.h"
#include "ecs/components/FActiveTacticalUnit.h"
#include "ecs/components/FCombatStateAwaitingCommand.h"
#include "ecs/components/FCombatStateDefeat.h"
#include "ecs/components/FCombatStateVictory.h"
#include "ecs/components/FPosition.h"
#include "ecs/components/FTacticalD20CommandDragState.h"
#include "ecs/components/FTacticalBoardTile.h"
#include "ecs/components/FTacticalUnit.h"
#include "ecs/components/FUnitStateDefeated.h"
#include "gameplay/tactical_d20/FTacticalD20Config.h"
#include "gameplay/tactical_d20/FTacticalD20Events.h"
#include "gameplay/tactical_d20/FTacticalD20StateLog.h"

#include <algorithm>
#include <cmath>
#include <fmt/format.h>
#include <string>
#include <string_view>
#include <tracy/Tracy.hpp>

namespace game {
namespace {

struct FCommandValidation {
    bool valid{false};
    std::string reason;
    int movementCostTiles{0};
    entt::entity targetEntity{entt::null};
};

entt::entity ActiveUnit(entt::registry& registry) {
    auto view = registry.view<FActiveTacticalUnit, FTacticalUnit>();
    for (auto entity : view) return entity;
    return entt::null;
}

bool IsAwaitingCommand(entt::registry& registry) {
    return !registry.view<FCombatStateAwaitingCommand>().empty();
}

bool IsCombatEnded(entt::registry& registry) {
    return !registry.view<FCombatStateVictory>().empty() || !registry.view<FCombatStateDefeat>().empty();
}
bool UnitHasAction(const FTacticalUnit& unit, std::string_view commandId) {
    return std::ranges::any_of(unit.actions, [commandId](const std::string& action) {
        return action == commandId;
    });
}

entt::entity UnitAtTile(entt::registry& registry, int tileX, int tileY) {
    auto view = registry.view<FTacticalUnit>(entt::exclude<FUnitStateDefeated>);
    for (auto entity : view) {
        const auto& unit = view.get<FTacticalUnit>(entity);
        if (unit.tileX == tileX && unit.tileY == tileY) return entity;
    }
    return entt::null;
}

entt::entity ResolveTargetUnit(entt::registry& registry, const FTacticalD20CommandDropRequestedEvent& request) {
    if (request.targetEntity != entt::null && registry.valid(request.targetEntity) && registry.all_of<FTacticalUnit>(request.targetEntity)) {
        return request.targetEntity;
    }
    if (!request.hasTargetTile) return entt::null;
    return UnitAtTile(registry, request.targetTileX, request.targetTileY);
}

const FTacticalD20MovementPathValidatedEvent* FindMoveResult(const FEventBus& bus, const FTacticalD20CommandDropRequestedEvent& request) {
    for (const auto& event : bus.frameEvents<FTacticalD20MovementPathValidatedEvent>()) {
        if (event.unit == request.unit && event.commandId == request.commandId && event.hasTargetTile == request.hasTargetTile
            && event.targetTileX == request.targetTileX && event.targetTileY == request.targetTileY) {
            return &event;
        }
    }
    return nullptr;
}
const FTacticalD20WeaponConfig* FindAttackWeapon(const FTacticalD20Config* config, const FTacticalUnit& unit) {
    if (!config) return nullptr;

    for (const auto& weaponId : unit.weaponProficiencies) {
        const auto found = std::ranges::find_if(config->weapons, [&weaponId](const FTacticalD20WeaponConfig& weapon) {
            return weapon.id == weaponId;
        });
        if (found != config->weapons.end()) return &(*found);
    }
    return nullptr;
}
bool IsWall(entt::registry& registry, int tileX, int tileY) {
    auto view = registry.view<FTacticalBoardTile>();
    for (auto entity : view) {
        const auto& tile = view.get<FTacticalBoardTile>(entity);
        if (tile.tileX == tileX && tile.tileY == tileY) return tile.isWall;
    }
    return false;
}
bool RangedLineOfSightClear(entt::registry& registry, int startX, int startY, int targetX, int targetY) {
    const int dx = std::abs(targetX - startX);
    const int dy = -std::abs(targetY - startY);
    const int stepX = startX < targetX ? 1 : -1;
    const int stepY = startY < targetY ? 1 : -1;
    int error = dx + dy;
    int x = startX;
    int y = startY;

    while (x != targetX || y != targetY) {
        const int twiceError = 2 * error;
        if (twiceError >= dy) {
            error += dy;
            x += stepX;
        }
        if (twiceError <= dx) {
            error += dx;
            y += stepY;
        }
        if ((x != targetX || y != targetY) && IsWall(registry, x, y)) return false;
    }
    return true;
}

FCommandValidation ValidateMove(const FEventBus& bus, const FTacticalD20CommandDropRequestedEvent& request) {
    FCommandValidation validation;
    const auto* move = FindMoveResult(bus, request);
    if (!move) {
        validation.reason = "movement path validation unavailable";
        return validation;
    }

    validation.valid = move->valid;
    validation.reason = move->invalidReason;
    validation.movementCostTiles = move->movementCostTiles;
    return validation;
}

FCommandValidation ValidateAttack(entt::registry& registry, const FTacticalD20CommandDropRequestedEvent& request) {
    FCommandValidation validation;
    const auto target = ResolveTargetUnit(registry, request);
    validation.targetEntity = target;
    if (target == entt::null) {
        validation.reason = "attack target is not a unit";
        return validation;
    }

    const auto& attacker = registry.get<FTacticalUnit>(request.unit);
    const auto& defender = registry.get<FTacticalUnit>(target);
    if (attacker.team == defender.team) {
        validation.reason = "attack target is not an enemy";
        return validation;
    }

    const auto* weapon = FindAttackWeapon(registry.ctx().find<FTacticalD20Config>(), attacker);
    const int rangeTiles = weapon ? std::max(weapon->rangeTiles, 1) : 1;
    const bool ranged = weapon && weapon->attackType == "ranged";
    const int distance = std::abs(defender.tileX - attacker.tileX) + std::abs(defender.tileY - attacker.tileY);

    if (!ranged && distance != 1) {
        validation.reason = "melee target is not adjacent";
        return validation;
    }
    if (distance > rangeTiles) {
        validation.reason = fmt::format("attack target outside range {}>{}", distance, rangeTiles);
        return validation;
    }
    if (ranged && !RangedLineOfSightClear(registry, attacker.tileX, attacker.tileY, defender.tileX, defender.tileY)) {
        validation.reason = "ranged line of sight is blocked";
        return validation;
    }

    validation.valid = true;
    return validation;
}
FCommandValidation ValidateActiveUnitTarget(entt::registry& registry, const FTacticalD20CommandDropRequestedEvent& request, bool allowTurnPanel) {
    FCommandValidation validation;
    const auto target = ResolveTargetUnit(registry, request);
    validation.targetEntity = target;
    if (allowTurnPanel && request.targetsTurnPanel) {
        validation.valid = true;
        return validation;
    }
    if (target != request.unit) {
        validation.reason = "command must target the active unit";
        return validation;
    }
    validation.valid = true;
    return validation;
}
FCommandValidation ValidateCommand(entt::registry& registry, const FEventBus& bus, const FTacticalD20CommandDropRequestedEvent& request) {
    FCommandValidation validation;
    if (request.unit == entt::null || request.unit != ActiveUnit(registry) || !registry.valid(request.unit) || !registry.all_of<FTacticalUnit>(request.unit)) {
        validation.reason = "active unit is unavailable";
        return validation;
    }
    if (!IsAwaitingCommand(registry) || IsCombatEnded(registry)) {
        validation.reason = "combat is not awaiting a player command";
        return validation;
    }

    const auto& unit = registry.get<FTacticalUnit>(request.unit);
    if (!UnitHasAction(unit, request.commandId)) {
        validation.reason = "active unit cannot use command";
        return validation;
    }

    if (request.commandId == "move") return ValidateMove(bus, request);
    if (request.commandId == "attack") return ValidateAttack(registry, request);
    if (request.commandId == "dash") return ValidateActiveUnitTarget(registry, request, false);
    if (request.commandId == "dodge") return ValidateActiveUnitTarget(registry, request, false);
    if (request.commandId == "wait") return ValidateActiveUnitTarget(registry, request, true);

    validation.reason = "unknown command";
    return validation;
}
std::string TargetLabel(entt::registry& registry, const FTacticalD20CommandDropValidatedEvent& event) {
    if (event.targetEntity != entt::null && registry.valid(event.targetEntity) && registry.all_of<FTacticalUnit>(event.targetEntity)) {
        return registry.get<FTacticalUnit>(event.targetEntity).id;
    }
    if (event.hasTargetTile) return fmt::format("tile({}, {})", event.targetTileX, event.targetTileY);
    return "none";
}

void AppendLog(entt::registry& registry, const FTacticalD20CommandDropValidatedEvent& event) {
    auto* log = registry.ctx().find<FTacticalD20StateLog>();
    if (!log) return;

    const auto target = TargetLabel(registry, event);
    const auto result = event.valid ? "valid" : fmt::format("invalid: {}", event.invalidReason);
    log->lines.push_back(fmt::format("[Event] CommandDropValidated command={} target={} result={}", event.commandId, target, result));
    log->lines.push_back(fmt::format("[Combat] {} command {}", event.commandId, result));

    if (const auto* config = registry.ctx().find<FTacticalD20Config>()) {
        while (static_cast<int>(log->lines.size()) > config->logging.stateLogMaxLines) log->lines.erase(log->lines.begin());
    }
}

void ResolveDragState(entt::registry& registry, const FTacticalD20CommandDropValidatedEvent& event) {
    if (event.token == entt::null || !registry.valid(event.token) || !registry.all_of<FTacticalD20CommandDragState>(event.token)) return;

    auto drag = registry.get<FTacticalD20CommandDragState>(event.token);
    drag.phase = event.valid ? ETacticalD20CommandDragPhase::CommandAccepted : ETacticalD20CommandDragPhase::DragRejected;
    drag.invalidReason = event.invalidReason;

    if (registry.all_of<FPosition>(event.token)) {
        auto& position = registry.get<FPosition>(event.token);
        position.x = drag.originX;
        position.y = drag.originY;
    }
    registry.remove<FTacticalD20CommandDragState>(event.token);
}

void PublishValidated(entt::registry& registry, const FTacticalD20CommandDropRequestedEvent& request, const FCommandValidation& validation) {
    FTacticalD20CommandDropValidatedEvent event;
    event.token = request.token;
    event.unit = request.unit;
    event.commandId = request.commandId;
    event.hasTargetTile = request.hasTargetTile;
    event.targetTileX = request.targetTileX;
    event.targetTileY = request.targetTileY;
    event.targetEntity = validation.targetEntity != entt::null ? validation.targetEntity : request.targetEntity;
    event.valid = validation.valid;
    event.invalidReason = validation.reason;
    event.movementCostTiles = validation.movementCostTiles;

    PUBLISH(FTacticalD20CommandDropValidatedEvent, registry, event);
    QUEUE_FRAME_EVENT(FTacticalD20CommandDropValidatedEvent, registry, event);
    AppendLog(registry, event);
    ResolveDragState(registry, event);

    if (!event.valid) return;

    const FTacticalD20CommandQueuedEvent command{
        event.unit,
        event.commandId,
        event.movementCostTiles,
        event.hasTargetTile,
        event.targetTileX,
        event.targetTileY,
        event.targetEntity};
    PUBLISH(FTacticalD20CommandQueuedEvent, registry, command);
    QUEUE_FRAME_EVENT(FTacticalD20CommandQueuedEvent, registry, command);
}

} // namespace

void TacticalD20CommandValidationSystem::update(entt::registry& registry, float /*dt*/) {
    ZoneScopedN("TacticalD20CommandValidationSystem");

    auto* bus = registry.ctx().find<FEventBus>();
    if (!bus) return;

    for (const auto& request : bus->frameEvents<FTacticalD20CommandDropRequestedEvent>()) {
        PublishValidated(registry, request, ValidateCommand(registry, *bus, request));
    }
}

} // namespace game
