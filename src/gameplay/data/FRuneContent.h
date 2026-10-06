#pragma once

#include "core/data/FContentRow.h"
#include "core/data/FContentSchema.h"

#include <cstddef>
#include <string>

namespace game::gameplay {

/// One rune as authored in `assets/data/runes.json`. Runes modify a base skill: an element rune applies
/// a status on hit, a form rune replaces the skill's shape, an enhance rune moves numbers.
/// docs/design/skill-rune-progression.md §3.
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

static_assert(std::size(kRuneFields) == 9, "kRuneFields and the kRuneField* indices drifted apart");

/// The three rune axes (docs/design/skill-rune-progression.md §3). Kept as strings because they cross the
/// JSON boundary; FContentRegistry rejects any other value at boot.
inline constexpr std::string_view kRuneAxisElement = "element";
inline constexpr std::string_view kRuneAxisForm = "form";
inline constexpr std::string_view kRuneAxisEnhance = "enhance";

/// Projects one validated row into a rune. Cannot fail (R22): wrong types were rejected at load.
[[nodiscard]] FRuneContent decodeRune(const FContentRow& row);

} // namespace game::gameplay
