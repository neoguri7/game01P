#pragma once

#include "core/data/FContentRow.h"
#include "core/data/FContentSchema.h"

#include <cstddef>
#include <string>

namespace game::gameplay {

/// One equipment as authored in `assets/data/equipment.json`. Stats only: no affixes, no rarity, no
/// class restriction (docs/design/skill-rune-progression.md §5). Numeric +N enhancement is applied later
/// by the equipment mechanism and is not part of the authored row.
struct FEquipmentContent {
    std::string id;
    std::string displayName;
    std::string slot; // "weapon" | "offhand" | "boots" (docs/design/skill-rune-progression.md §5.2)
    int attack{0};
    int maxHealth{0};
    int moveAp{0};
    int skillAp{0};
    int range{0};
};

/// Field order IS the projection order FContentLoader uses (R22).
/// `invariant:` this array, the kEquipmentField* indices below, decodeEquipment and
/// assets/data/equipment.json must keep the same order and the same keys.
inline constexpr FContentField kEquipmentFields[] = {
    {"id", EContentFieldType::Text, true},             // boundary: asset key
    {"display_name", EContentFieldType::Text, true},   // boundary: asset key
    {"slot", EContentFieldType::Text, true},           // boundary: asset key
    {"attack", EContentFieldType::Number, false},      // boundary: asset key
    {"max_health", EContentFieldType::Number, false},  // boundary: asset key
    {"move_ap", EContentFieldType::Number, false},     // boundary: asset key
    {"skill_ap", EContentFieldType::Number, false},    // boundary: asset key
    {"range", EContentFieldType::Number, false},       // boundary: asset key
};

inline constexpr std::size_t kEquipmentFieldId = 0;
inline constexpr std::size_t kEquipmentFieldDisplayName = 1;
inline constexpr std::size_t kEquipmentFieldSlot = 2;
inline constexpr std::size_t kEquipmentFieldAttack = 3;
inline constexpr std::size_t kEquipmentFieldMaxHealth = 4;
inline constexpr std::size_t kEquipmentFieldMoveAp = 5;
inline constexpr std::size_t kEquipmentFieldSkillAp = 6;
inline constexpr std::size_t kEquipmentFieldRange = 7;

static_assert(std::size(kEquipmentFields) == 8, "kEquipmentFields and the kEquipmentField* indices drifted apart");

/// Projects one validated row into an equipment. Cannot fail (R22): wrong types were rejected at load.
[[nodiscard]] FEquipmentContent decodeEquipment(const FContentRow& row);

} // namespace game::gameplay
