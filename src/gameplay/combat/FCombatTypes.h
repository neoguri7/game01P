#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>
#include <variant>

#include <entt/entt.hpp>

#include "core/Logger.h"
#include "gameplay/turn/TurnTypes.h"
#include "ecs/components/FGridPosition.h"

namespace game {

struct FActionRequest {
    entt::entity actor{entt::null};
    entt::entity target{entt::null};
    std::string skillId;
    FTurnToken token;
    FGridPosition cell{};
};

struct FMoveRequest {
    entt::entity actor{entt::null};
    FGridPosition destination;
    FTurnToken token;
};

using FBattleAction = std::variant<FActionRequest, FMoveRequest>;

struct FActionQueue {
    std::vector<FBattleAction> pending;
};

/// Battle result, written once when one side is wiped. `resolved` guards repeated evaluation;
/// `exitRequested` is set by the HUD's "거점으로" button.
struct FBattleOutcome {
    bool abandonRequested{false};
    bool resolved{false};
    bool victory{false};
    bool exitRequested{false};
};

/// Player-facing battle feed (Korean lines). Systems append; the HUD renders and trims.
struct FCombatLog {
    std::vector<std::string> lines;

    void push(std::string line) {
        // The battle feed is also mirrored into the dev log so a fight can be verified from `game01p.log`
        // even when nobody is watching the HUD (automated runs, crash reports).
        LOG_INFO("[battle] {}", line);
        lines.push_back(std::move(line));
        constexpr std::size_t kMaxLines = 100;
        if (lines.size() > kMaxLines) {
            lines.erase(lines.begin(), lines.begin() + static_cast<std::ptrdiff_t>(lines.size() - kMaxLines));
        }
    }
};

/// Battle RNG (xorshift32). Deterministic on purpose: an auto-battle verification run
/// (GAME01P_START=battle + GAME01P_AUTOPLAY=1) must reproduce the same fight from the same seed.
/// 런 시드 연동은 미결(정본 §11) — 지금은 고정 시드.
struct FBattleRng {
    std::uint32_t state{0x9e3779b9u};

    [[nodiscard]] std::uint32_t next() {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }

    /// 0..99
    [[nodiscard]] int rollPercent() { return static_cast<int>(next() % 100u); }

    [[nodiscard]] bool rollChance(int percent) {
        if (percent <= 0) {
            return false;
        }
        if (percent >= 100) {
            return true;
        }
        return rollPercent() < percent;
    }
};

/// Per-battle bookkeeping owned by the battle state (FBattleState creates and clears it with the rest of
/// the battle-scoped context): the last turn serial an enemy action was scheduled for (player turns are
/// scheduled by the HUD, so only auto actions need this guard). `autoPlay` is a debug switch (env
/// `GAME01P_AUTOPLAY=1`): both sides fight automatically so the combat pipeline can be verified from the
/// log without input. `rng` is battle-scoped so status rolls do not leak between fights.
inline constexpr int kMaxAutoActionsPerTurn = 8;
inline constexpr std::uint64_t kMaxBattleRounds = 60;

struct FCombatRuntime {
    std::uint64_t scheduledTurnSerial{0};
    /// The current turn's actor, mirrored by TurnFlowSystem on every update (null between turns/battles).
    /// Gameplay-side readers (FIntent) use it to tell a unit's own turn — live AP pools — from any other
    /// frame, where the pools still hold that unit's leftover from its last turn.
    entt::entity currentActor{entt::null};
    int autoActionsThisTurn{0};
    bool autoPlay{false};
    FBattleRng rng;
    float thinkDelay{0.f};
    int selectedSkill{0};
    bool moveSelected{false};
    entt::entity inputActor{entt::null};
};

} // namespace game
