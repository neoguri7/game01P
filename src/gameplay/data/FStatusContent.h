#pragma once

#include "core/data/FContentRow.h"
#include "core/data/FContentSchema.h"

#include <cstddef>
#include <string>

namespace game::gameplay {

/// One status effect as authored in `assets/data/statuses.json`.
/// docs/design/skill-rune-progression.md §4: 화상(dot)/빙결·마비(control)/버프·디버프(stat), 중첩=갱신.
struct FStatusContent {
    std::string id;
    std::string displayName;
    std::string kind;         // "dot" | "control" | "stat"
    int magnitude{0};         // dot: 턴당 피해, paralyze: 행동불가 확률(%), stat: 증감치
    int durationTurns{1};     // 지속 턴 수
    int speedPct{100};        // 100 = 속도 변화 없음. 빙결처럼 속도를 낮추는 상태가 쓴다
};

/// Field order IS the projection order FContentLoader uses (R22).
/// `invariant:` this array, the kStatusField* indices below, decodeStatus and assets/data/statuses.json must
/// keep the same order and the same keys.
inline constexpr FContentField kStatusFields[] = {
    {"id", EContentFieldType::Text, true},               // boundary: asset key
    {"display_name", EContentFieldType::Text, true},     // boundary: asset key
    {"kind", EContentFieldType::Text, true},             // boundary: asset key
    {"magnitude", EContentFieldType::Number, false},     // boundary: asset key
    {"duration_turns", EContentFieldType::Number, false}, // boundary: asset key
    {"speed_pct", EContentFieldType::Number, false},     // boundary: asset key
};

inline constexpr std::size_t kStatusFieldId = 0;
inline constexpr std::size_t kStatusFieldDisplayName = 1;
inline constexpr std::size_t kStatusFieldKind = 2;
inline constexpr std::size_t kStatusFieldMagnitude = 3;
inline constexpr std::size_t kStatusFieldDurationTurns = 4;
inline constexpr std::size_t kStatusFieldSpeedPct = 5;

static_assert(std::size(kStatusFields) == 6, "kStatusFields and the kStatusField* indices drifted apart");

/// The status kinds (docs/design/skill-rune-progression.md §4). FContentRegistry rejects any other value.
inline constexpr std::string_view kStatusKindDot = "dot";
inline constexpr std::string_view kStatusKindControl = "control";
inline constexpr std::string_view kStatusKindStat = "stat";

/// Projects one validated row into a status. Cannot fail (R22): wrong types were rejected at load.
[[nodiscard]] FStatusContent decodeStatus(const FContentRow& row);

} // namespace game::gameplay
