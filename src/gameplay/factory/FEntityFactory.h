#pragma once

#include <expected>
#include <string>

#include <entt/entt.hpp>
#include <nlohmann/json_fwd.hpp>

#include "gameplay/factory/FEntityDefinitions.h"
#include "gameplay/factory/FScene.h"

namespace game {

struct FFactoryError {
    std::string message;
};

class FEntityFactory {
public:
    [[nodiscard]] static std::expected<entt::entity, FFactoryError> CreateUnit(
        FScene& scene,
        const nlohmann::json& document
    );
    [[nodiscard]] static std::expected<FSkillDefinition, FFactoryError> CreateSkill(
        const nlohmann::json& document
    );
    [[nodiscard]] static std::expected<FRoomDefinition, FFactoryError> CreateRoom(
        const nlohmann::json& document
    );
    [[nodiscard]] static std::expected<FEquipmentDefinition, FFactoryError> CreateEquipment(
        const nlohmann::json& document
    );
};

} // namespace game
