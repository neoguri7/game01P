#pragma once

#include <cstdint>
#include <entt/entt.hpp>
#include <string>

namespace game {

struct FDemoObstacleDesc {
    float x{360.f};
    float y{260.f};
    float width{96.f};
    float height{64.f};
    std::string tag{"demo_cover"};
    int layer{2};
    int gridX{4};
    int gridY{3};

    std::uint8_t fillR{90};
    std::uint8_t fillG{90};
    std::uint8_t fillB{110};
    std::uint8_t fillA{190};
};

struct FDemoObstacleFactory {
    static entt::entity create(entt::registry& registry);
    static entt::entity create(entt::registry& registry, const FDemoObstacleDesc& desc);
};

} // namespace game
