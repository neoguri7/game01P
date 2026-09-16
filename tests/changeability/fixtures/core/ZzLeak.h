#pragma once
#include "gameplay/Player.h"            // V7 [R7-Blocker]: core must not know gameplay
#include <string>

namespace game::core {
namespace {
    constexpr float kZzMaxSpeed = 3.5f;  // V12 [R12]: duplicate of ecs/components/FZzBad.h:5
}

struct ZzSettings {                       // V6 [R6-Blocker]: singleton
    static ZzSettings& instance() { static ZzSettings s; return s; }
    static ZzSettings* getInstance() { return &instance(); }
    float gravity{9.8f};                  // V4: gameplay/engine value literal, unmarked as fallback
};

// V6b [R6]: mutable namespace-scope global defined in a header (ODR + global state)
int staticInstanceCount = 0;

// R4 NEGATIVE CONTROL (must NOT be reported): the one permitted code fallback.
// The `// fallback:` marker must sit on the SAME line as the value so the bundle can filter it.
constexpr float kZzFallbackGravity = 120.0f;  // fallback: 엔진 중력 기본값 — assets/data/ 도입 시 이 값만 남긴다.

// R7 NEGATIVE CONTROL (must NOT be reported): comment-only keyword mentions.
// The word Player and the path gameplay/ appear here in prose only.
}
