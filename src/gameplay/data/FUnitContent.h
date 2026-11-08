#pragma once

#include "core/data/FContentRow.h"
#include "core/data/FContentSchema.h"

#include <cstddef>
#include <string>
#include <vector>

namespace game::gameplay {

/// One character or enemy template as authored in `assets/data/units.json`. Values only (R17/R23):
/// behaviour lives in systems, so retuning a unit is an asset edit, never a code edit.
/// docs/design/skill-rune-progression.md §1.1/§1.2: 스탯 6종(hp·공격력·마법공격력·속도·방어력·
/// 마법방어력)과 클래스(전사·궁수·법사)를 데이터로 저작한다.
struct FUnitContent {
    std::string id;
    std::string displayName;
    double maxHealth{0.0};
    double speed{0.0}; // initiative weight (design §4: 속도 기반 이니셔티브)
    int moveAp{0};     // design §4: 이동 AP (잠정 1)
    int skillAp{0};    // design §4: 스킬 AP (잠정 2)
    std::vector<std::string> skillIds; // design §5: 거점에서 유저가 고르는 해금 스킬 목록
    std::string spriteId;              // presentation id; FDungeonArt resolves it to an EDungeonSprite (P14-8)
    std::string className;             // "warrior" | "archer" | "mage" | 비어 있음(적). 게임 규칙은 아직 이 값에 의존하지 않는다

    // docs/design/skill-rune-progression.md §1.1: 피해 공식 `max(1, offense + power − defense)`의 입력.
    int attack{0};       // 공격력 (물리 공격 항)
    int magicAttack{0};  // 마법공격력 (마법 공격 항)
    int defense{0};      // 방어력 (물리 감소 항)
    int magicDefense{0}; // 마법방어력 (마법 감소 항)

    // docs/design/enemy-ai.md: the behavior-tree profile this unit runs (`assets/data/ai.json`). Empty =
    // the default profile, which plays any loadout from its skill roles, so a new monster needs no AI work.
    std::string aiProfile;
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
    {"class", EContentFieldType::Text, false},       // boundary: asset key
    {"attack", EContentFieldType::Number, false},    // boundary: asset key
    {"magic_attack", EContentFieldType::Number, false},  // boundary: asset key
    {"defense", EContentFieldType::Number, false},       // boundary: asset key
    {"magic_defense", EContentFieldType::Number, false}, // boundary: asset key
    {"ai", EContentFieldType::Id, false},                // boundary: asset key; names a profile in data/ai
};

inline constexpr std::size_t kUnitFieldId = 0;
inline constexpr std::size_t kUnitFieldDisplayName = 1;
inline constexpr std::size_t kUnitFieldMaxHealth = 2;
inline constexpr std::size_t kUnitFieldSpeed = 3;
inline constexpr std::size_t kUnitFieldMoveAp = 4;
inline constexpr std::size_t kUnitFieldSkillAp = 5;
inline constexpr std::size_t kUnitFieldSkills = 6;
inline constexpr std::size_t kUnitFieldSprite = 7;
inline constexpr std::size_t kUnitFieldClass = 8;
inline constexpr std::size_t kUnitFieldAttack = 9;
inline constexpr std::size_t kUnitFieldMagicAttack = 10;
inline constexpr std::size_t kUnitFieldDefense = 11;
inline constexpr std::size_t kUnitFieldMagicDefense = 12;
inline constexpr std::size_t kUnitFieldAi = 13;

static_assert(std::size(kUnitFields) == 14, "kUnitFields and the kUnitField* indices drifted apart");

/// Projects one validated row into a unit. Cannot fail: FContentLoader already rejected wrong types and
/// missing required keys, which is why this stays a straight field-by-field copy (R22).
[[nodiscard]] FUnitContent decodeUnit(const FContentRow& row);

} // namespace game::gameplay
