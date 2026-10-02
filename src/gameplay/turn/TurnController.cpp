#include "gameplay/turn/TurnController.h"
#include "ecs/components/FSpeed.h"
#include "ecs/components/FTurnParticipant.h"
#include <algorithm>
#include <utility>

namespace game {

std::expected<void, ETurnError> SetTurnUnitSpeed(
    entt::registry& registry, entt::entity unit, std::int32_t speed) {
    if (speed < 0) return std::unexpected(ETurnError::InvalidSpeed);
    if (!registry.valid(unit)) return std::unexpected(ETurnError::InvalidEntity);
    auto* component = registry.try_get<FSpeed>(unit);
    if (!component) return std::unexpected(ETurnError::MissingSpeed);
    component->value = speed;
    return {};
}

std::expected<std::vector<FTurnEntry>, ETurnError> TurnController::collect(entt::registry& registry) const {
    std::vector<FTurnEntry> entries;
    for (auto entity : registry.view<FTurnParticipant, FSpeed>()) {
        const auto speed = registry.get<FSpeed>(entity).value;
        if (speed < 0) return std::unexpected(ETurnError::InvalidSpeed);
        entries.push_back({entity, speed});
    }
    if (entries.empty()) return std::unexpected(ETurnError::NoParticipants);
    std::ranges::sort(entries, [](const auto& a, const auto& b) {
        return entt::to_integral(a.entity) < entt::to_integral(b.entity);
    });
    std::stable_sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
        return a.speed > b.speed;
    });
    return entries;
}

std::expected<void, ETurnError> TurnController::start(entt::registry& registry, std::uint32_t seed) {
    if (state_.state == ETurnState::Running) return std::unexpected(ETurnError::AlreadyRunning);
    auto entries = collect(registry);
    if (!entries) return std::unexpected(entries.error());

    random_.seed(seed);
    ++state_.sessionId;
    state_.state = ETurnState::Running;
    state_.roundNumber = 0;
    turnSerial_ = 0;
    events_.emplace_back(FTurnSessionStarted{state_.sessionId, seed});
    beginRound(std::move(*entries));
    return {};
}

void TurnController::beginRound(std::vector<FTurnEntry> entries) {
    for (std::size_t first = 0; first < entries.size();) {
        std::size_t last = first + 1;
        while (last < entries.size() && entries[last].speed == entries[first].speed) ++last;
        for (std::size_t count = last - first; count > 1; --count) {
            std::uniform_int_distribution<std::size_t> pick(0, count - 1);
            std::swap(entries[first + count - 1], entries[first + pick(random_)]);
        }
        first = last;
    }
    ++state_.roundNumber;
    state_.entries = std::move(entries);
    cursor_ = 0;
    state_.entries.front().state = ETurnEntryState::Active;
    state_.currentTurn = FTurnToken{state_.sessionId, state_.roundNumber, ++turnSerial_, state_.entries.front().entity};
    events_.emplace_back(FRoundStarted{snapshot()});
    events_.emplace_back(FTurnStarted{*state_.currentTurn});
}

void TurnController::activate(std::size_t index) {
    cursor_ = index;
    auto& entry = state_.entries[index];
    entry.state = ETurnEntryState::Active;
    state_.currentTurn = FTurnToken{state_.sessionId, state_.roundNumber, ++turnSerial_, entry.entity};
    events_.emplace_back(FTurnStarted{*state_.currentTurn});
}

void TurnController::advance(entt::registry& registry) {
    for (std::size_t index = cursor_ + 1; index < state_.entries.size(); ++index) {
        if (state_.entries[index].state == ETurnEntryState::Pending) {
            activate(index);
            return;
        }
    }
    events_.emplace_back(FRoundEnded{state_.sessionId, state_.roundNumber});
    auto entries = collect(registry);
    if (!entries) {
        finishSession(entries.error() == ETurnError::InvalidSpeed
            ? ETurnSessionEndReason::InvalidSpeed : ETurnSessionEndReason::NoParticipants);
        return;
    }
    beginRound(std::move(*entries));
}

void TurnController::synchronize(entt::registry& registry) {
    if (state_.state != ETurnState::Running) return;
    bool activeRemoved = false;
    for (auto& entry : state_.entries) {
        if (entry.state != ETurnEntryState::Pending && entry.state != ETurnEntryState::Active) continue;
        if (registry.valid(entry.entity) && registry.all_of<FTurnParticipant, FSpeed>(entry.entity)) continue;
        if (entry.state == ETurnEntryState::Active) activeRemoved = true;
        entry.state = ETurnEntryState::Skipped;
    }
    if (activeRemoved) {
        const auto token = *state_.currentTurn;
        state_.currentTurn.reset();
        events_.emplace_back(FTurnEnded{token, ETurnEndReason::Removed});
        advance(registry);
    }
}

std::expected<void, ETurnError> TurnController::endTurn(entt::registry& registry, FTurnToken expectedTurn) {
    if (state_.state != ETurnState::Running) return std::unexpected(ETurnError::NotRunning);
    if (state_.currentTurn != expectedTurn) return std::unexpected(ETurnError::StaleTurn);
    synchronize(registry);
    if (state_.currentTurn != expectedTurn) return std::unexpected(ETurnError::StaleTurn);

    state_.entries[cursor_].state = ETurnEntryState::Completed;
    state_.currentTurn.reset();
    events_.emplace_back(FTurnEnded{expectedTurn, ETurnEndReason::Completed});
    advance(registry);
    return {};
}

void TurnController::finishSession(ETurnSessionEndReason reason) {
    if (state_.currentTurn) {
        events_.emplace_back(FTurnEnded{*state_.currentTurn,
            reason == ETurnSessionEndReason::InvalidSpeed ? ETurnEndReason::InvalidSpeed : ETurnEndReason::Stopped});
    }
    state_.state = ETurnState::Idle;
    state_.roundNumber = 0;
    state_.currentTurn.reset();
    state_.entries.clear();
    cursor_ = 0;
    events_.emplace_back(FTurnSessionEnded{state_.sessionId, reason});
}

void TurnController::stop() {
    if (state_.state == ETurnState::Running) finishSession(ETurnSessionEndReason::Stopped);
}

FTurnSnapshot TurnController::snapshot() const { return state_; }

std::vector<FTurnEvent> TurnController::takeEvents() { return std::exchange(events_, {}); }

} // namespace game
