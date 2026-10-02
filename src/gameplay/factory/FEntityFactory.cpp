#include "gameplay/factory/FEntityFactory.h"

#include <optional>
#include <utility>

#include <nlohmann/json.hpp>

#include "ecs/components/FTag.h"

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
