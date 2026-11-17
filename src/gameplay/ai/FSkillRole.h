#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "gameplay/combat/FSkillResolver.h"

namespace game::ai {

/// What a skill is *for*, derived from its authored numbers so that a new skill needs no AI code:
///  - aimed at the caster's side: power > 0 heals, otherwise a status makes it a buff;
///  - aimed at the other side:    power > 0 damages, otherwise a status makes it a debuff.
/// Anything else (no power, no status) is a Utility the AI never picks on its own.
enum class ESkillRole : std::uint8_t { Damage, Heal, Buff, Debuff, Utility };

using FRoleMask = std::uint8_t;

[[nodiscard]] constexpr FRoleMask RoleBit(ESkillRole role) {
    return static_cast<FRoleMask>(1u << static_cast<unsigned>(role));
}

inline constexpr FRoleMask kRolesHostile = static_cast<FRoleMask>(RoleBit(ESkillRole::Damage) | RoleBit(ESkillRole::Debuff));
inline constexpr FRoleMask kRolesFriendly = static_cast<FRoleMask>(RoleBit(ESkillRole::Heal) | RoleBit(ESkillRole::Buff));
inline constexpr FRoleMask kRolesAny = static_cast<FRoleMask>(kRolesHostile | kRolesFriendly | RoleBit(ESkillRole::Utility));

[[nodiscard]] ESkillRole ClassifySkill(const FResolvedSkill& skill);
[[nodiscard]] std::string_view SkillRoleName(ESkillRole role);

/// Authored role names: damage | heal | buff | debuff | hostile (damage+debuff) | friendly (heal+buff) | any.
[[nodiscard]] std::optional<FRoleMask> ParseRoleMask(std::string_view name);

} // namespace game::ai
