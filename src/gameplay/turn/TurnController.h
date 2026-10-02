#pragma once
#include "gameplay/turn/TurnTypes.h"
#include <expected>
#include <random>

namespace game {

[[nodiscard]] std::expected<void, ETurnError> SetTurnUnitSpeed(
    entt::registry& registry, entt::entity unit, std::int32_t speed);

class TurnController {
public:
    [[nodiscard]] std::expected<void, ETurnError> start(entt::registry& registry, std::uint32_t seed);
    [[nodiscard]] std::expected<void, ETurnError> endTurn(entt::registry& registry, FTurnToken expectedTurn);
    void synchronize(entt::registry& registry);
    void stop();
    [[nodiscard]] FTurnSnapshot snapshot() const;
    [[nodiscard]] std::vector<FTurnEvent> takeEvents();

private:
    [[nodiscard]] std::expected<std::vector<FTurnEntry>, ETurnError> collect(entt::registry& registry) const;
    void beginRound(std::vector<FTurnEntry> entries);
    void advance(entt::registry& registry);
    void activate(std::size_t index);
    void finishSession(ETurnSessionEndReason reason);

    FTurnSnapshot state_;
    std::mt19937 random_;
    std::uint64_t turnSerial_ = 0;
    std::size_t cursor_ = 0;
    std::vector<FTurnEvent> events_;
};

} // namespace game
