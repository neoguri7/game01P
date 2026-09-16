#pragma once

#include "core/data/FContentRow.h"
#include "core/data/FContentSchema.h"

#include <cstddef>
#include <string>
#include <vector>

namespace game::gameplay {

/// One skill template as authored in `assets/data/skills.json`. This is the skill as authored, before
/// any run-scope variation: design §5 has runes change behaviour later, and that belongs to a later
/// slice once the 변형 슬롯 rules are decided.
struct FSkillContent {
    std::string id;
    std::string displayName;
    int apCost{0}; // design §4: 스킬 AP 소모 (고비용 상한은 미결)
    int range{0};  // 미결(design §4): 사거리 수치 — 칸 단위
    // 미결(design §4): 피해 공식 — 확정 전에는 이 power를 그대로 피해로 쓴다(경감·관통·치명타 없음).
    double power{0.0};
    std::vector<std::string> tags; // 미결(design §5): 태그 세트 규모 미결 — 후보 목록에서만 고른다
};

/// Field order IS the projection order FContentLoader uses (R22).
/// `invariant:` this array, the kSkillField* indices below, decodeSkill and assets/data/skills.json must
/// keep the same order and the same keys.
inline constexpr FContentField kSkillFields[] = {
    {"id", EContentFieldType::Text, true},           // boundary: asset key
    {"display_name", EContentFieldType::Text, true}, // boundary: asset key
    {"ap_cost", EContentFieldType::Number, true},    // boundary: asset key
    {"range", EContentFieldType::Number, false},     // boundary: asset key
    {"power", EContentFieldType::Number, false},     // boundary: asset key
    {"tags", EContentFieldType::IdArray, false},     // boundary: asset key
};

inline constexpr std::size_t kSkillFieldId = 0;
inline constexpr std::size_t kSkillFieldDisplayName = 1;
inline constexpr std::size_t kSkillFieldApCost = 2;
inline constexpr std::size_t kSkillFieldRange = 3;
inline constexpr std::size_t kSkillFieldPower = 4;
inline constexpr std::size_t kSkillFieldTags = 5;

static_assert(std::size(kSkillFields) == 6, "kSkillFields and the kSkillField* indices drifted apart");

/// Projects one validated row into a skill. Cannot fail (R22): wrong types were rejected at load.
[[nodiscard]] FSkillContent decodeSkill(const FContentRow& row);

} // namespace game::gameplay
