#pragma once

namespace game::ecs
{
    // V8 [R8]: ranged enum whose dispatch is spread over >=3 files.
    enum class EWeapon  // NOLINT(performance-enum-size): fixture keeps the default int base
    {
        Sword,
        Bow,
        Staff,
        Wand
    };

    struct FZzWeapon
    {
        EWeapon kind{EWeapon::Sword};
        int ammo{0};
    };
}
