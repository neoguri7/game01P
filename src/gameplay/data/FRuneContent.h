#pragma once

#include "core/data/FContentRow.h"
#include "core/data/FContentSchema.h"

#include <cstddef>
#include <string>
#include <vector>

namespace game::gameplay {

/// One rune as authored in `assets/data/runes.json`. Runes are the support-gem layer
/// (docs/design/skill-rune-progression.md §3): an element rune applies a status on hit, a form rune
/// replaces the skill's shape, an enhance rune moves numbers or status values.
///
/// Tag rules are strict (§3.2): a rune may only be attached when its `requires_*` tags are satisfied by
/// the skill's effective tag set (base tags ∪ tags added by attached runes) and its `excludes` tags are
/// absent from that set.
struct FRuneContent {
    std::string id;
    std::string displayName;
    std::string axis;          // "element" | "form" | "enhance"
    std::string statusOnHit;   // element: status id applied on hit (empty = none)
    std::string shapeOverride; // form: shape id that replaces the skill's shape (empty = keep)
    int powerDelta{0};         // enhance: addition to skill power
    int apDelta{0};            // enhance: addition to skill AP cost
    int rangeDelta{0};         // enhance: addition to skill range
    int areaRadiusDelta{0};    // enhance: addition to skill area radius

    // 서포트젬식 태그 규칙 (§3.2). 판정은 고정점: 장착 룬의 adds_tags까지 포함한 유효 태그 집합으로 검사한다.
    std::vector<std::string> requiresAllTags; // 전부 있어야 장착 가능
    std::vector<std::string> requiresAnyTags; // 하나 이상 있어야 장착 가능(비면 조건 없음)
    std::vector<std::string> excludesTags;    // 하나라도 있으면 장착 불가
    std::vector<std::string> addsTags;        // 이 룬이 스킬에 부여하는 태그

    // 상태 보정 (§3.3). statusId가 비면 스킬이 부여하는 모든 상태에 적용된다.
    std::string statusId;        // 보정 대상 상태 id (비면 전체)
    int statusDurationDelta{0};  // 지속 +N
    int statusMagnitudeDelta{0}; // 강도 +N (마비는 확률 %p)
};

/// Field order IS the projection order FContentLoader uses (R22).
/// `invariant:` this array, the kRuneField* indices below, decodeRune and assets/data/runes.json must keep
/// the same order and the same keys.
inline constexpr FContentField kRuneFields[] = {
    {"id", EContentFieldType::Text, true},               // boundary: asset key
    {"display_name", EContentFieldType::Text, true},     // boundary: asset key
    {"axis", EContentFieldType::Text, true},             // boundary: asset key
    {"status_on_hit", EContentFieldType::Id, false},     // boundary: asset key
    {"shape_override", EContentFieldType::Text, false},  // boundary: asset key
    {"power_delta", EContentFieldType::Number, false},   // boundary: asset key
    {"ap_delta", EContentFieldType::Number, false},      // boundary: asset key
    {"range_delta", EContentFieldType::Number, false},   // boundary: asset key
    {"area_radius_delta", EContentFieldType::Number, false}, // boundary: asset key
    {"requires_all_tags", EContentFieldType::IdArray, false}, // boundary: asset key
    {"requires_any_tags", EContentFieldType::IdArray, false}, // boundary: asset key
    {"excludes_tags", EContentFieldType::IdArray, false},     // boundary: asset key
    {"adds_tags", EContentFieldType::IdArray, false},         // boundary: asset key
    {"status_id", EContentFieldType::Id, false},              // boundary: asset key
    {"status_duration_delta", EContentFieldType::Number, false},  // boundary: asset key
    {"status_magnitude_delta", EContentFieldType::Number, false}, // boundary: asset key
};

inline constexpr std::size_t kRuneFieldId = 0;
inline constexpr std::size_t kRuneFieldDisplayName = 1;
inline constexpr std::size_t kRuneFieldAxis = 2;
inline constexpr std::size_t kRuneFieldStatusOnHit = 3;
inline constexpr std::size_t kRuneFieldShapeOverride = 4;
inline constexpr std::size_t kRuneFieldPowerDelta = 5;
inline constexpr std::size_t kRuneFieldApDelta = 6;
inline constexpr std::size_t kRuneFieldRangeDelta = 7;
inline constexpr std::size_t kRuneFieldAreaRadiusDelta = 8;
inline constexpr std::size_t kRuneFieldRequiresAllTags = 9;
inline constexpr std::size_t kRuneFieldRequiresAnyTags = 10;
inline constexpr std::size_t kRuneFieldExcludesTags = 11;
inline constexpr std::size_t kRuneFieldAddsTags = 12;
inline constexpr std::size_t kRuneFieldStatusId = 13;
inline constexpr std::size_t kRuneFieldStatusDurationDelta = 14;
inline constexpr std::size_t kRuneFieldStatusMagnitudeDelta = 15;

static_assert(std::size(kRuneFields) == 16, "kRuneFields and the kRuneField* indices drifted apart");

/// The three rune axes (docs/design/skill-rune-progression.md §3). Kept as strings because they cross the
/// JSON boundary; FContentRegistry rejects any other value at boot.
inline constexpr std::string_view kRuneAxisElement = "element";
inline constexpr std::string_view kRuneAxisForm = "form";
inline constexpr std::string_view kRuneAxisEnhance = "enhance";

/// Projects one validated row into a rune. Cannot fail (R22): wrong types were rejected at load.
[[nodiscard]] FRuneContent decodeRune(const FContentRow& row);

} // namespace game::gameplay