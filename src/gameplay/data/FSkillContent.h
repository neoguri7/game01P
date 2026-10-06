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
    std::vector<std::string> tags; // 미결(design §5): 태그 세트 규모 미결 — 후보 목록에서만 고른다

    // docs/design/skill-rune-progression.md §2: 베이스 스킬은 무속성이며 룬이 형태/속성/수치를 덧씌운다.
    int power{0};                  // 스킬 위력(피해/회복 기본값). 강화 룬이 power_delta로 올린다
    std::string shape{"single"};   // 단일/확산/관통/장판 — 형태 룬이 덮어쓴다
    int areaRadius{0};             // 범위 반경(칸). shape=="zone" 등에서 사용
    std::string target{"enemy"};   // "enemy" | "ally" | "self" | "cell"
    std::string appliesStatus;     // 상태이상 id. 비어 있으면 없음
};

/// Field order IS the projection order FContentLoader uses (R22).
/// `invariant:` this array, the kSkillField* indices below, decodeSkill and assets/data/skills.json must
/// keep the same order and the same keys.
inline constexpr FContentField kSkillFields[] = {
    {"id", EContentFieldType::Text, true},             // boundary: asset key
    {"display_name", EContentFieldType::Text, true},   // boundary: asset key
    {"ap_cost", EContentFieldType::Number, true},      // boundary: asset key
    {"range", EContentFieldType::Number, false},       // boundary: asset key
    {"tags", EContentFieldType::IdArray, false},       // boundary: asset key
    {"power", EContentFieldType::Number, false},       // boundary: asset key
    {"shape", EContentFieldType::Text, false},         // boundary: asset key
    {"area_radius", EContentFieldType::Number, false}, // boundary: asset key
    {"target", EContentFieldType::Text, false},        // boundary: asset key
    {"applies_status", EContentFieldType::Id, false},  // boundary: asset key
};

inline constexpr std::size_t kSkillFieldId = 0;
inline constexpr std::size_t kSkillFieldDisplayName = 1;
inline constexpr std::size_t kSkillFieldApCost = 2;
inline constexpr std::size_t kSkillFieldRange = 3;
inline constexpr std::size_t kSkillFieldTags = 4;
inline constexpr std::size_t kSkillFieldPower = 5;
inline constexpr std::size_t kSkillFieldShape = 6;
inline constexpr std::size_t kSkillFieldAreaRadius = 7;
inline constexpr std::size_t kSkillFieldTarget = 8;
inline constexpr std::size_t kSkillFieldAppliesStatus = 9;

static_assert(std::size(kSkillFields) == 10, "kSkillFields and the kSkillField* indices drifted apart");

/// Projects one validated row into a skill. Cannot fail (R22): wrong types were rejected at load.
[[nodiscard]] FSkillContent decodeSkill(const FContentRow& row);

} // namespace game::gameplay
