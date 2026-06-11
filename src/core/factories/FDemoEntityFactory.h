#pragma once

#include <cstdint>
#include <entt/entt.hpp>
#include <string>

namespace game {

struct FDemoEntityDesc {
    float x{200.f};
    float y{200.f};
    float vx{50.f};
    float vy{-30.f};
    float width{48.f};
    float height{48.f};

    std::string tag{"demo_mover"};
    int layer{10};
    int gridX{2};
    int gridY{2};
    int speedFeet{30};

    int strength{10};
    int dexterity{12};
    int constitution{10};

    std::string commandId{"move"};
    std::string commandName{"Move"};

    std::uint8_t fillR{80};
    std::uint8_t fillG{160};
    std::uint8_t fillB{240};
    std::uint8_t fillA{190};

    bool defeated{false};
};

struct FDemoEntityFactory {
    static entt::entity create(entt::registry& registry);
    static entt::entity create(entt::registry& registry, const FDemoEntityDesc& desc);
};

} // namespace game
