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
#include "ecs/components/FSkillLoadout.h"
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

std::expected<std::string, FFactoryError> ReadOptionalString(const nlohmann::json& document, const char* key) {
    const auto value = document.find(key);
    if (value == document.end()) {
        return std::string{};
    }
    if (!value->is_string() || value->get_ref<const std::string&>().empty()) {
        return std::unexpected(FFactoryError{ std::string("optional field '") + key + "' must be a non-empty string" });
    }
    return value->get<std::string>();
}

/// Optional per-placement loadout: `"skills": [ { "skill": "...", "runes": ["..."] } ]`.
/// docs/design/skill-rune-progression.md §3: runes attach to a skill, multiple per skill.
std::expected<std::vector<FSkillSlot>, FFactoryError> ReadSkillSlots(const nlohmann::json& document) {
    std::vector<FSkillSlot> skills;
    const auto skillsIt = document.find("skills");
    if (skillsIt == document.end()) {
        return skills;
    }
    if (!skillsIt->is_array()) {
        return std::unexpected(FFactoryError{ "field 'skills' must be an array" });
    }
    for (const auto& entry : *skillsIt) {
        auto skillId = ReadString(entry, "skill");
        if (!skillId) return std::unexpected(skillId.error());
        FSkillSlot slot;
        slot.skillId = std::move(*skillId);
        const auto runesIt = entry.find("runes");
        if (runesIt != entry.end()) {
            if (!runesIt->is_array()) {
                return std::unexpected(FFactoryError{ "field 'runes' must be an array" });
            }
            for (const auto& rune : *runesIt) {
                if (!rune.is_string() || rune.get_ref<const std::string&>().empty()) {
                    return std::unexpected(FFactoryError{ "'runes' entries must be non-empty strings" });
                }
                slot.runeIds.push_back(rune.get<std::string>());
            }
        }
        skills.push_back(std::move(slot));
    }
    return skills;
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
        return std::unexpected(FFactoryError{ "battlefield dimensions must be " + std::to_string(kGridWidth) +
                                              " by " + std::to_string(kGridHeight) });
    }
    if (metrics.width <= 0 || metrics.height <= 0 || metrics.tileSize <= 0.f) {
        return std::unexpected(FFactoryError{ "battlefield dimensions and tile_size must be positive" });
    }

    const auto units = document.find("units");
    if (units == document.end() || !units->is_array()) {
        return std::unexpected(FFactoryError{ "required field 'units' must be an array" });
    }

    FBattlefieldData battlefield{.metrics = metrics};
    if (const auto terrain = document.find("terrain"); terrain != document.end()) {
        if (!terrain->is_array() || terrain->size() != kGridHeight) {
            return std::unexpected(FFactoryError{"terrain must contain " + std::to_string(kGridHeight) + " rows"});
        }
        for (int y = 0; y < kGridHeight; ++y) {
            const auto& row = (*terrain)[y];
            if (!row.is_string() || row.get_ref<const std::string&>().size() != kGridWidth) {
                return std::unexpected(FFactoryError{"terrain rows must contain " + std::to_string(kGridWidth) + " cells"});
            }
            for (int x = 0; x < kGridWidth; ++x) {
                const char tile = row.get_ref<const std::string&>()[x];
                if (tile != '.' && tile != '#' && tile != '^') {
                    return std::unexpected(FFactoryError{"terrain supports only '.', '#' and '^'"});
                }
                battlefield.terrain.cells[y * kGridWidth + x] =
                    tile == '#' ? ETile::Wall : tile == '^' ? ETile::Trap : ETile::Floor;
            }
        }
    }
    for (const auto [key, destination] : {
             std::pair{"move_range", &battlefield.terrain.moveRange},
             std::pair{"trap_damage", &battlefield.terrain.trapDamage}}) {
        if (document.contains(key)) {
            const auto value = ReadInteger(document, key);
            if (!value || *value < 0 || *value > 64 || (std::string_view(key) == "move_range" && *value == 0)) {
                return std::unexpected(FFactoryError{"invalid movement or trap balance value"});
            }
            *destination = *value;
        }
    }
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
        if (battlefield.terrain.at(*x, *y) == ETile::Wall) {
            return std::unexpected(FFactoryError{"unit placement intersects a wall"});
        }
        if (!occupiedCells.emplace(*x, *y).second) {
            return std::unexpected(FFactoryError{ "multiple units occupy the same battlefield cell" });
        }

        auto unitTemplate = ReadOptionalString(unit, "unit");
        if (!unitTemplate) return std::unexpected(unitTemplate.error());
        auto skills = ReadSkillSlots(unit);
        if (!skills) return std::unexpected(skills.error());

        FUnitPlacement placement;
        placement.id = std::move(definition->id);
        placement.displayName = std::move(definition->displayName);
        placement.faction = definition->faction;
        placement.cell = {*x, *y};
        placement.unitTemplateId = std::move(*unitTemplate);
        placement.skills = std::move(*skills);
        battlefield.units.push_back(std::move(placement));
    }

    for (auto& placement : battlefield.units) {
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
        placement.entity = *entity;
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

} // namespace game
