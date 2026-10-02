#pragma once
#include "gameplay/turn/TurnTypes.h"
#include <deque>
#include <string>

namespace game {

struct FResetTurnDemo {};
struct FAddTurnDemoUnit { std::int32_t speed = 20; };
struct FSetTurnDemoSpeed { entt::entity entity; std::int32_t speed; };
struct FSetTurnDemoParticipation { entt::entity entity; bool participating; };
struct FDeleteTurnDemoUnit { entt::entity entity; };
using FTurnDemoRequest = std::variant<FResetTurnDemo, FAddTurnDemoUnit, FSetTurnDemoSpeed,
                                      FSetTurnDemoParticipation, FDeleteTurnDemoUnit>;
struct FTurnDemoUnit { entt::entity entity; std::string label; };

struct FTurnDemo {
    std::vector<FTurnDemoUnit> units;
    std::deque<FTurnDemoRequest> requests;
    std::optional<ETurnError> lastError;
    std::uint32_t seed = 42;
    std::int32_t addedSpeed = 20;
    std::uint64_t nextLabel = 1;
};

struct TurnDemoSystem {
    void onRegister(entt::registry& registry);
    void update(entt::registry& registry, float deltaTime);
    [[nodiscard]] std::string name() const { return "TurnDemoSystem"; }
};

[[nodiscard]] std::string TurnDemoLabel(const entt::registry& registry, entt::entity entity);
void RenderTurnDemo(entt::registry& registry);
void ShutdownTurnDemo(entt::registry& registry);

} // namespace game
