#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace game::gameplay {

/// Player-visible narration of the battle — the "화면" of this slice. why in ctx() and not in components: the
/// log is battle output, not entity state, and the sprite/animation slice deletes this service together with
/// the text view (R9). why both a LOG_* line and this: LOG_* is for the developer log file (R31), this is the
/// game's own output.
struct FBattleLog {
    std::vector<std::string> lines;
    std::size_t capacity{200}; // fallback: 표시 상한 — 프레젠테이션 값이며 design §4의 전투 규칙 수치가 아니다

    void push(std::string line) {
        if (lines.size() >= capacity && capacity > 0) {
            lines.erase(lines.begin(), lines.begin() + static_cast<std::ptrdiff_t>(lines.size() - capacity + 1));
        }
        lines.push_back(std::move(line));
    }
};

} // namespace game::gameplay
