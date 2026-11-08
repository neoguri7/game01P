#pragma once

#include "core/data/FContentRow.h"
#include "core/data/FContentSchema.h"

#include <cstddef>
#include <string>
#include <string_view>

namespace game::gameplay {

/// The status kinds (docs/design/skill-rune-progression.md §4). FContentRegistry rejects any other value.
inline constexpr std::string_view kStatusKindDot = "dot";
inline constexpr std::string_view kStatusKindControl = "control";
inline constexpr std::string_view kStatusKindStat = "stat";
/// WP-16: move denial (immobilize), not action denial. A `move_lock` status denies only the owner's next
/// turn's movement — the owner may still use skills (W5 review #8: reusing `control` would read as freeze).
inline constexpr std::string_view kStatusKindMoveLock = "move_lock";

/// How a `control` status decides its action loss (WP-13; 30-decisions D-12). `turn_start` rolls at the
/// owner's own turn start (freeze: magnitude 0 = always); `on_apply` rolls once when applied and stores
/// the outcome as FStatusEffect::skipPending. FContentRegistry rejects any other value at boot.
inline constexpr std::string_view kStatusModeTurnStart = "turn_start";
inline constexpr std::string_view kStatusModeOnApply = "on_apply";

/// `speed_pct` value meaning "no speed change". RefreshCombatSheet multiplies a unit's speed by pct/100,
/// so every status that does not move speed carries 100. An omitted field decodes to this (see
/// decodeStatus — the loader materializes missing optional numbers as 0).
inline constexpr int kStatusSpeedPctNeutral = 100;

/// `chance_cap` default for rows that omit it (30-decisions D-12: the 90% ceiling on a control chance).
inline constexpr int kDefaultChanceCap = 90;

/// One status effect as authored in `assets/data/statuses.json`.
/// docs/design/skill-rune-progression.md §4: 화상(dot)/빙결·마비(control)/버프·디버프(stat), 중첩=갱신.
struct FStatusContent {
    std::string id;
    std::string displayName;
    std::string kind;         // "dot" | "control" | "stat"
    int magnitude{0};         // dot: 턴당 피해, control: 행동불가 확률(%), stat: 증감치
    int durationTurns{1};     // 지속 턴 수
    int speedPct{kStatusSpeedPctNeutral}; // 100 = 속도 변화 없음. 빙결처럼 속도를 낮추는 상태가 쓴다
    // Control only: when the action loss is decided — kStatusModeTurnStart (default) | kStatusModeOnApply.
    std::string mode{std::string(kStatusModeTurnStart)};
    int chanceCap{kDefaultChanceCap}; // control 확률 상한(0..100). FContentRegistry가 범위를 검증한다
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
    {"mode", EContentFieldType::Text, false},            // boundary: asset key (WP-13, last fields)
    {"chance_cap", EContentFieldType::Number, false},    // boundary: asset key (WP-13, last fields)
};

inline constexpr std::size_t kStatusFieldId = 0;
inline constexpr std::size_t kStatusFieldDisplayName = 1;
inline constexpr std::size_t kStatusFieldKind = 2;
inline constexpr std::size_t kStatusFieldMagnitude = 3;
inline constexpr std::size_t kStatusFieldDurationTurns = 4;
inline constexpr std::size_t kStatusFieldSpeedPct = 5;
inline constexpr std::size_t kStatusFieldMode = 6;
inline constexpr std::size_t kStatusFieldChanceCap = 7;

static_assert(std::size(kStatusFields) == 8, "kStatusFields and the kStatusField* indices drifted apart");

/// Projects one validated row into a status. Cannot fail (R22): wrong types were rejected at load.
[[nodiscard]] FStatusContent decodeStatus(const FContentRow& row);

} // namespace game::gameplay
