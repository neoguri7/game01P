#pragma once

#include "core/data/FContentRow.h"
#include "core/data/FContentSchema.h"
#include "gameplay/data/FEffectContent.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace game::gameplay {

/// WP-40: how rare a rune is. The tier decides which odds table slot can draw it, never what it does.
enum class ERuneRarity : std::uint8_t { Common = 0, Uncommon = 1, Rare = 2 };

[[nodiscard]] std::optional<ERuneRarity> ParseRuneRarity(std::string_view name); // "common"|"uncommon"|"rare"
[[nodiscard]] std::string_view RuneRarityName(ERuneRarity rarity);              // "common"|"uncommon"|"rare"

/// One rune as authored in `assets/data/runes.json`. Runes are the support-gem layer
/// (docs/design/skill-rune-progression.md §3): they attach to a skill's rune sockets and modify it —
/// added elemental damage, shape changes, area/duration/power changes, leech.
///
/// 3차 개편: 축(속성/형태/강화)은 폐기. 제한은 소켓 수 + 태그 규칙 + 중복 금지가 담당한다.
/// Tag rules are strict (§3.2): a rune may only be attached when its `requires_*` tags are satisfied by
/// the skill's effective tag set (base tags ∪ tags added by attached runes) and its `excludes` tags are
/// absent from that set.
struct FRuneContent {
    std::string id;
    std::string displayName;
    std::string statusOnHit;   // element rune: status id applied on hit (empty = none)
    std::string shapeOverride; // form rune: shape id that replaces the skill's shape (empty = keep)
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

    // §3.3 흡혈: 피해를 준 공격자의 HP를 명중당 N 회복한다.
    int lifeOnHit{0};

    // WP-04: the rune's own typed hooks (optional field in JSON, authored by WP-22). They fire after the
    // skill's own hooks, in equip order; `source` is stamped "rune:<id>" at decode.
    std::vector<FHook> hooks;

    // WP-17: relative chance weight when this rune enters a reward offer pool (optional field, default 1).
    // It only biases the draw; the offer size and legality gate stay on rewards.json.
    int dropWeight{1};

    // WP-40: rarity tier (optional field, default common). It only picks the odds-table slot the rune is
    // drawn from; drop_weight then biases the draw inside the tier.
    std::string rarityName{"common"};        // as authored; validated at boot
    ERuneRarity rarity{ERuneRarity::Common}; // parsed; an unknown name reads as Common until boot rejects it
};

/// Field order IS the projection order FContentLoader uses (R22).
/// `invariant:` this array, the kRuneField* indices below, decodeRune and assets/data/runes.json must keep
/// the same order and the same keys.
inline constexpr FContentField kRuneFields[] = {
    {"id", EContentFieldType::Text, true},               // boundary: asset key
    {"display_name", EContentFieldType::Text, true},     // boundary: asset key
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
    {"life_on_hit", EContentFieldType::Number, false},            // boundary: asset key
    {"hooks", EContentFieldType::ObjectArray, false, &kHookSchema}, // boundary: asset key (WP-04)
    {"drop_weight", EContentFieldType::Number, false},            // boundary: asset key (WP-17)
    {"rarity", EContentFieldType::Text, false},                   // boundary: asset key (WP-40, last field)
};

inline constexpr std::size_t kRuneFieldId = 0;
inline constexpr std::size_t kRuneFieldDisplayName = 1;
inline constexpr std::size_t kRuneFieldStatusOnHit = 2;
inline constexpr std::size_t kRuneFieldShapeOverride = 3;
inline constexpr std::size_t kRuneFieldPowerDelta = 4;
inline constexpr std::size_t kRuneFieldApDelta = 5;
inline constexpr std::size_t kRuneFieldRangeDelta = 6;
inline constexpr std::size_t kRuneFieldAreaRadiusDelta = 7;
inline constexpr std::size_t kRuneFieldRequiresAllTags = 8;
inline constexpr std::size_t kRuneFieldRequiresAnyTags = 9;
inline constexpr std::size_t kRuneFieldExcludesTags = 10;
inline constexpr std::size_t kRuneFieldAddsTags = 11;
inline constexpr std::size_t kRuneFieldStatusId = 12;
inline constexpr std::size_t kRuneFieldStatusDurationDelta = 13;
inline constexpr std::size_t kRuneFieldStatusMagnitudeDelta = 14;
inline constexpr std::size_t kRuneFieldLifeOnHit = 15;
inline constexpr std::size_t kRuneFieldHooks = 16;
inline constexpr std::size_t kRuneFieldDropWeight = 17;
inline constexpr std::size_t kRuneFieldRarity = 18;

static_assert(std::size(kRuneFields) == 19, "kRuneFields and the kRuneField* indices drifted apart");

/// Projects one validated row into a rune. Cannot fail (R22): wrong types were rejected at load.
[[nodiscard]] FRuneContent decodeRune(const FContentRow& row);

} // namespace game::gameplay
