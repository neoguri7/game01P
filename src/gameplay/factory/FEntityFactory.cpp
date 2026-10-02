#include "gameplay/factory/FEntityFactory.h"

#include <cmath>
#include <limits>
#include <optional>
#include <set>
#include <utility>

#include <nlohmann/json.hpp>

#include "ecs/components/FFaction.h"
#include "ecs/components/FLayer.h"
#include "ecs/components/FPosition.h"
#include "ecs/components/FTag.h"
#include "gameplay/grid/FGridTypes.h"

namespace game {
namespace {

std::expected<std::string, FFactoryError> ReadString(
    const nlohmann::json& document,
    const char* key
) {
    if (!document.is_object()) {
        return std::unexpected(FFactoryError{ "definition must be a JSON object" });
    }

    const auto value = document.find(key);
    if (value == document.end() || !value->is_string()) {
        return std::unexpected(FFactoryError{ std::string("required string field '") + key + "' is missing" });
    }

    auto result = value->get<std::string>();
    if (result.empty()) {
        return std::unexpected(FFactoryError{ std::string("required string field '") + key + "' is empty" });
    }
    return result;
}

std::expected<int, FFactoryError> ReadInteger(const nlohmann::json& document, const char* key) {
    if (!document.is_object()) {
        return std::unexpected(FFactoryError{ "definition must be a JSON object" });
    }
    const auto value = document.find(key);
    if (value == document.end() || !value->is_number()) {
        return std::unexpected(FFactoryError{ std::string("required integer field '") + key + "' is missing" });
    }
    const double number = value->get<double>();
    if (!std::isfinite(number) || std::floor(number) != number ||
        number < static_cast<double>(std::numeric_limits<int>::min()) ||
        number > static_cast<double>(std::numeric_limits<int>::max())) {
        return std::unexpected(FFactoryError{ std::string("field '") + key + "' must be a valid integer" });
    }
    return static_cast<int>(number);
}

std::expected<float, FFactoryError> ReadFloat(const nlohmann::json& document, const char* key) {
    if (!document.is_object()) {
        return std::unexpected(FFactoryError{ "definition must be a JSON object" });
    }
    const auto value = document.find(key);
    if (value == document.end() || !value->is_number()) {
        return std::unexpected(FFactoryError{ std::string("required number field '") + key + "' is missing" });
    }
    const double number = value->get<double>();
    if (!std::isfinite(number) ||
        number < static_cast<double>(std::numeric_limits<float>::lowest()) ||
        number > static_cast<double>(std::numeric_limits<float>::max())) {
        return std::unexpected(FFactoryError{ std::string("field '") + key + "' must be a valid number" });
    }
    return static_cast<float>(number);
}

std::expected<FUnitDefinition, FFactoryError> ReadUnitDefinition(const nlohmann::json& document) {
    auto id = ReadString(document, "id");
    if (!id) return std::unexpected(id.error());
    auto displayName = ReadString(document, "display_name");
    if (!displayName) return std::unexpected(displayName.error());
    auto faction = ReadString(document, "faction");
    if (!faction) return std::unexpected(faction.error());

    EUnitFaction typedFaction;
    if (*faction == "party") {
        typedFaction = EUnitFaction::Party;
    } else if (*faction == "enemy") {
        typedFaction = EUnitFaction::Enemy;
    } else {
        return std::unexpected(FFactoryError{ "field 'faction' must be 'party' or 'enemy'" });
    }

    return FUnitDefinition{ std::move(*id), std::move(*displayName), typedFaction };
}

} // namespace

std::expected<entt::entity, FFactoryError> FEntityFactory::CreateUnit(
    FScene& scene,
    const nlohmann::json& document
) {
    auto definition = ReadUnitDefinition(document);
    if (!definition) return std::unexpected(definition.error());
    if (!scene.IsLoaded()) {
        return std::unexpected(FFactoryError{ "unit cannot be created in an unloaded scene" });
    }

    const auto entity = scene.CreateEntity();
    if (!entity) return std::unexpected(FFactoryError{ "scene refused entity creation" });

    scene.Emplace<FTag>(*entity, FTag{ definition->displayName });
    scene.Emplace<FUnitData>(*entity, FUnitData{ *definition });
    return *entity;
}

std::expected<FBattlefieldData, FFactoryError> FEntityFactory::CreateBattlefield(
    FScene& scene,
    const nlohmann::json& document
) {
    if (!scene.IsLoaded()) {
        return std::unexpected(FFactoryError{ "battlefield cannot be created in an unloaded scene" });
    }

    auto width = ReadInteger(document, "width");
    if (!width) return std::unexpected(width.error());
    auto height = ReadInteger(document, "height");
    if (!height) return std::unexpected(height.error());
    auto tileSize = ReadFloat(document, "tile_size");
    if (!tileSize) return std::unexpected(tileSize.error());
    auto originX = ReadFloat(document, "origin_x");
    if (!originX) return std::unexpected(originX.error());
    auto originY = ReadFloat(document, "origin_y");
    if (!originY) return std::unexpected(originY.error());

    FGridMetrics metrics{
        .width = *width,
        .height = *height,
        .tileSize = *tileSize,
        .origin = {*originX, *originY}
    };
    if (metrics.width != kGridWidth || metrics.height != kGridHeight) {
        return std::unexpected(FFactoryError{ "battlefield dimensions must be 8 by 8" });
    }
    if (metrics.width <= 0 || metrics.height <= 0 || metrics.tileSize <= 0.f) {
        return std::unexpected(FFactoryError{ "battlefield dimensions and tile_size must be positive" });
    }

    const auto units = document.find("units");
    if (units == document.end() || !units->is_array()) {
        return std::unexpected(FFactoryError{ "required field 'units' must be an array" });
    }

    FBattlefieldData battlefield{.metrics = metrics};
    std::set<std::pair<int, int>> occupiedCells;
    for (const auto& unit : *units) {
        auto definition = ReadUnitDefinition(unit);
        if (!definition) return std::unexpected(definition.error());
        auto x = ReadInteger(unit, "grid_x");
        if (!x) return std::unexpected(x.error());
        auto y = ReadInteger(unit, "grid_y");
        if (!y) return std::unexpected(y.error());

        if (!IsInsideGrid(metrics, *x, *y)) {
            return std::unexpected(FFactoryError{ "unit placement is outside the battlefield" });
        }
        if (!occupiedCells.emplace(*x, *y).second) {
            return std::unexpected(FFactoryError{ "multiple units occupy the same battlefield cell" });
        }

        battlefield.units.push_back({
            std::move(definition->id),
            std::move(definition->displayName),
            definition->faction,
            {*x, *y}
        });
    }

    for (const auto& placement : battlefield.units) {
        const auto entity = scene.CreateEntity();
        if (!entity) return std::unexpected(FFactoryError{ "scene refused battlefield unit creation" });

        scene.Emplace<FTag>(*entity, FTag{placement.displayName});
        scene.Emplace<FUnitData>(*entity, FUnitData{FUnitDefinition{
            placement.id,
            placement.displayName,
            placement.faction
        }});
        scene.Emplace<FGridPosition>(*entity, placement.cell);
        const auto worldPosition = GridCellTopLeftToWorld(metrics, placement.cell.x, placement.cell.y);
        scene.Emplace<FPosition>(*entity, FPosition{worldPosition.x, worldPosition.y});
        scene.Emplace<FLayer>(*entity, FLayer{1});
        if (placement.faction == EUnitFaction::Party) {
            scene.Emplace<FPartyMember>(*entity);
        } else {
            scene.Emplace<FEnemy>(*entity);
        }
    }

    return battlefield;
}

std::expected<FSkillDefinition, FFactoryError> FEntityFactory::CreateSkill(const nlohmann::json& document) {
    auto id = ReadString(document, "id");
    if (!id) return std::unexpected(id.error());
    auto displayName = ReadString(document, "display_name");
    if (!displayName) return std::unexpected(displayName.error());
    return FSkillDefinition{ std::move(*id), std::move(*displayName) };
}

std::expected<FRoomDefinition, FFactoryError> FEntityFactory::CreateRoom(const nlohmann::json& document) {
    auto id = ReadString(document, "id");
    if (!id) return std::unexpected(id.error());
    auto displayName = ReadString(document, "display_name");
    if (!displayName) return std::unexpected(displayName.error());
    auto roomType = ReadString(document, "room_type");
    if (!roomType) return std::unexpected(roomType.error());
    return FRoomDefinition{ std::move(*id), std::move(*displayName), std::move(*roomType) };
}

std::expected<FEquipmentDefinition, FFactoryError> FEntityFactory::CreateEquipment(const nlohmann::json& document) {
    auto id = ReadString(document, "id");
    if (!id) return std::unexpected(id.error());
    auto displayName = ReadString(document, "display_name");
    if (!displayName) return std::unexpected(displayName.error());
    auto slot = ReadString(document, "slot");
    if (!slot) return std::unexpected(slot.error());
    return FEquipmentDefinition{ std::move(*id), std::move(*displayName), std::move(*slot) };
}

} // namespace game
