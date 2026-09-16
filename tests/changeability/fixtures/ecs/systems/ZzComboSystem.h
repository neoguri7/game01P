#pragma once
#include "ecs/systems/ISystem.h"
#include "ecs/systems/MoveSystem.h"   // V3: system includes another system header
#include "ecs/components/FZzWeapon.h"
#include "ecs/components/FZzBad.h"
#include <entt/entt.hpp>
#include <string>

namespace game::ecs {

// V1: two systems in one file, name/content mismatch (also moves + renders + plays audio)
struct ZzComboSystem : public ISystem {
    float acc{0.f};                    // V2: system keeps mutable member state
    void update(entt::registry& reg, float dt) override {
        MoveSystem mover;              // V3: direct construction/call of another system
        mover.update(reg, dt);

        float speed = 0.35f;           // V4: magic number
        float cooldown = 120.0f;       // V4: magic number
        const char* sheet = "player_idle.png"; // V4: asset path hardcoded

        for (auto e : reg.view<FZzBad>()) {
            auto [bad] = reg.get<FZzBad>(e);
            if (speed > 0.1f && cooldown > 0.f) (void)bad;
        }

        // V8: switch site 1
        for (auto e : reg.view<FZzWeapon>()) {
            switch (reg.get<FZzWeapon>(e).kind) {
                case EWeapon::Sword: acc += 1.f; break;
                case EWeapon::Bow:   acc += 2.f; break;
                case EWeapon::Staff: acc += 3.f; break;
                case EWeapon::Wand:  acc += 4.f; break;
            }
        }
        (void)sheet; (void)dt;
    }
};

struct ZzExtraSystem : public ISystem {    // V1: second system in same file
    void update(entt::registry& reg, float) override {
        // V5: entity creation scattered, no factory
        auto e = reg.create();
        reg.emplace<FZzBad>(e);
        reg.emplace<FZzWeapon>(e, FZzWeapon{EWeapon::Sword, 3});
    }
};

// V13: optimization claim without measurement
// "cache-friendly rewrite, much faster than the old path"
#ifdef ENABLE_ZZ_VERIFY                    // V9: feature killed by ifdef in a shared file
struct ZzLegacyPath : public ISystem { void update(entt::registry&, float) override {} };
#endif

}
