#pragma once

#include "core/data/FContentRow.h"
#include "core/data/FContentSchema.h"

#include <cstddef>
#include <string>
#include <vector>

namespace game::gameplay {

/// One character or enemy template as authored in `assets/data/units.json`. Values only (R17/R23):
/// behaviour lives in systems, so retuning a unit is an asset edit, never a code edit.
struct FUnitContent {
    std::string id;
    std::string displayName;
    double maxHealth{0.0};
    double speed{0.0}; // initiative weight (design §4: 속도 기반 이니셔티브)
    int moveAp{0};     // design §4: 이동 AP (잠정 1)
    int skillAp{0};    // design §4: 스킬 AP (잠정 2)
    std::vector<std::string> skillIds; // design §5: 거점에서 유저가 고르는 해금 스킬 목록
    std::string spriteId;              // presentation id; nothing resolves it yet
    // design §2: 몬스터 행동트리 규칙 id 목록 — 목록 순서가 우선순위다 (첫 일치 규칙 1개 실행).
    std::vector<std::string> behaviorIds; // optional field; only enemy units author it today
};

/// Field order IS the projection order FContentLoader uses (R22).
/// `invariant:` this array, the kUnitField* indices below, decodeUnit and assets/data/units.json must
/// keep the same order and the same keys.
inline constexpr FContentField kUnitFields[] = {
    {"id", EContentFieldType::Text, true},           // boundary: asset key
    {"display_name", EContentFieldType::Text, true}, // boundary: asset key
    {"max_health", EContentFieldType::Number, true}, // boundary: asset key
    {"speed", EContentFieldType::Number, true},      // boundary: asset key
    {"move_ap", EContentFieldType::Number, false},   // boundary: asset key
    {"skill_ap", EContentFieldType::Number, false},  // boundary: asset key
    {"skills", EContentFieldType::IdArray, false},   // boundary: asset key
    {"sprite", EContentFieldType::Text, false},      // boundary: asset key
    // why behavior is appended last instead of beside `skills`: every existing kUnitField* index is part of
    // the saved contract, so appending keeps all eight stable (R22).
    {"behavior", EContentFieldType::IdArray, false}, // boundary: asset key — behavior rule ids, in priority order
};

inline constexpr std::size_t kUnitFieldId = 0;
inline constexpr std::size_t kUnitFieldDisplayName = 1;
inline constexpr std::size_t kUnitFieldMaxHealth = 2;
inline constexpr std::size_t kUnitFieldSpeed = 3;
inline constexpr std::size_t kUnitFieldMoveAp = 4;
inline constexpr std::size_t kUnitFieldSkillAp = 5;
inline constexpr std::size_t kUnitFieldSkills = 6;
inline constexpr std::size_t kUnitFieldSprite = 7;
inline constexpr std::size_t kUnitFieldBehavior = 8;

static_assert(std::size(kUnitFields) == 9, "kUnitFields and the kUnitField* indices drifted apart");

/// Projects one validated row into a unit. Cannot fail: FContentLoader already rejected wrong types and
/// missing required keys, which is why this stays a straight field-by-field copy (R22).
[[nodiscard]] FUnitContent decodeUnit(const FContentRow& row);

} // namespace game::gameplay
