#pragma once
#include "ecs/components/FZzWeapon.h"
namespace game {
inline const char* zzIconOf(game::ecs::EWeapon k) {   // V8: switch site 3
    using game::ecs::EWeapon;
    switch (k) {
        case EWeapon::Sword: return "sword.png";
        case EWeapon::Bow:   return "bow.png";
        case EWeapon::Staff: return "staff.png";
        case EWeapon::Wand:  return "wand.png";
    }
    return "none.png";
}
}
