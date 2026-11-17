#pragma once
#include "gameplay/turn/TurnController.h"
#include <deque>
#include <string>

namespace game {

struct FStartTurnSession { std::uint32_t seed = 42; };
struct FEndTurn { FTurnToken token; };
struct FStopTurnSession {};
using FTurnRequest = std::variant<FStartTurnSession, FEndTurn, FStopTurnSession>;

struct FTurnRuntime {
    TurnController controller;
    std::deque<FTurnRequest> requests;
    std::optional<ETurnError> lastError;

    void stop() {
        controller.stop();
        requests.clear();
    }
};

void ShutdownTurnRuntime(entt::registry& registry);

namespace ecs {

struct TurnSystem {
    void onRegister(entt::registry& registry);
    void update(entt::registry& registry, float deltaTime);
    [[nodiscard]] std::string name() const { return "TurnSystem"; }
};

} // namespace ecs
} // namespace game
