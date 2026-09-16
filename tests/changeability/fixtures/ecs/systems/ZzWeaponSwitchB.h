#pragma once
#include "ecs/components/FZzWeapon.h"

namespace game::ecs
{
    // V8: switch site 2 of 3 for EWeapon.
    inline float zzDamageOf(EWeapon k)
    {
        switch (k)
        {
        case EWeapon::Sword:
            return 10.f;
        case EWeapon::Bow:
            return 7.f;
        case EWeapon::Staff:
            return 12.f;
        case EWeapon::Wand:
            return 9.f;
        }
        return 0.f;
    }
}
