#pragma once
#include <entt/entt.hpp>
#include <cstdint>
#include <optional>
#include <variant>
#include <vector>

namespace game {

enum class ETurnState { Idle, Running };
enum class ETurnEntryState { Pending, Active, Completed, Skipped };
enum class ETurnError {
    AlreadyRunning, NoParticipants, InvalidSpeed, NotRunning,
    InvalidEntity, MissingSpeed, StaleTurn
};
enum class ETurnEndReason { Completed, Removed, Stopped, InvalidSpeed };
enum class ETurnSessionEndReason { Stopped, NoParticipants, InvalidSpeed };

struct FTurnToken {
    std::uint64_t sessionId = 0;
    std::uint64_t roundNumber = 0;
    std::uint64_t turnSerial = 0;
    entt::entity actor = entt::null;
    bool operator==(const FTurnToken&) const = default;
};

struct FTurnEntry {
    entt::entity entity = entt::null;
    std::int32_t speed = 0;
    ETurnEntryState state = ETurnEntryState::Pending;
    bool operator==(const FTurnEntry&) const = default;
};

struct FTurnSnapshot {
    ETurnState state = ETurnState::Idle;
    std::uint64_t sessionId = 0;
    std::uint64_t roundNumber = 0;
    std::optional<FTurnToken> currentTurn;
    std::vector<FTurnEntry> entries;
    bool operator==(const FTurnSnapshot&) const = default;
};

struct FTurnSessionStarted { std::uint64_t sessionId; std::uint32_t seed; };
struct FRoundStarted { FTurnSnapshot snapshot; };
struct FTurnStarted { FTurnToken token; };
struct FTurnEnded { FTurnToken token; ETurnEndReason reason; };
struct FRoundEnded { std::uint64_t sessionId; std::uint64_t roundNumber; };
struct FTurnSessionEnded { std::uint64_t sessionId; ETurnSessionEndReason reason; };
using FTurnEvent = std::variant<FTurnSessionStarted, FRoundStarted, FTurnStarted,
                                FTurnEnded, FRoundEnded, FTurnSessionEnded>;

[[nodiscard]] inline const char* TurnErrorName(ETurnError error) {
    switch (error) {
    case ETurnError::AlreadyRunning: return "AlreadyRunning";
    case ETurnError::NoParticipants: return "NoParticipants";
    case ETurnError::InvalidSpeed: return "InvalidSpeed";
    case ETurnError::NotRunning: return "NotRunning";
    case ETurnError::InvalidEntity: return "InvalidEntity";
    case ETurnError::MissingSpeed: return "MissingSpeed";
    case ETurnError::StaleTurn: return "StaleTurn";
    }
    return "Unknown";
}

} // namespace game
