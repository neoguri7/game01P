#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <entt/entt.hpp>

#include "core/Logger.h"

namespace game {

/// One queued battle action. Skill-only for now; 이동은 다음 슬라이스에서 추가한다.
struct FActionRequest {
    entt::entity actor{entt::null};
    entt::entity target{entt::null};
    std::string skillId;
};

struct FActionQueue {
    std::vector<FActionRequest> pending;
};

/// Battle result, written once when one side is wiped. `resolved` guards repeated evaluation;
/// `exitRequested` is set by the HUD's "거점으로" button.
struct FBattleOutcome {
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

/// Per-battle bookkeeping owned by TurnFlowSystem: the last turn serial an enemy action was scheduled
/// for. (Player turns are scheduled by the HUD, so only enemies need this guard.)
/// `autoPlay` is a debug switch (env `GAME01P_AUTOPLAY=1`): both sides fight automatically so the
/// combat pipeline can be verified from the log without input.
struct FCombatRuntime {
    std::uint64_t scheduledTurnSerial{0};
    bool autoPlay{false};
};

} // namespace game
