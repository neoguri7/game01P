#include "gameplay/data/FEquipmentContent.h"

namespace game::gameplay {

FEquipmentContent decodeEquipment(const FContentRow& row) {
    FEquipmentContent equipment;
    equipment.id = row.text(kEquipmentFieldId);
    equipment.displayName = row.text(kEquipmentFieldDisplayName);
    equipment.slot = row.text(kEquipmentFieldSlot);
    equipment.attack = row.integer(kEquipmentFieldAttack, 0);
    equipment.maxHealth = row.integer(kEquipmentFieldMaxHealth, 0);
    equipment.moveAp = row.integer(kEquipmentFieldMoveAp, 0);
    equipment.skillAp = row.integer(kEquipmentFieldSkillAp, 0);
    equipment.range = row.integer(kEquipmentFieldRange, 0);
    return equipment;
}

} // namespace game::gameplay
