#include "gameplay/ai/FSkillRole.h"

namespace game::ai {

ESkillRole ClassifySkill(const FResolvedSkill& skill) {
    const bool ownSide = skill.target == "ally" || skill.target == "self";
    if (ownSide) {
        if (skill.power > 0) {
            return ESkillRole::Heal;
        }
        return skill.statuses.empty() ? ESkillRole::Utility : ESkillRole::Buff;
    }
    if (skill.power > 0) {
        return ESkillRole::Damage;
    }
    return skill.statuses.empty() ? ESkillRole::Utility : ESkillRole::Debuff;
}

std::string_view SkillRoleName(ESkillRole role) {
    switch (role) {
    case ESkillRole::Damage: return "damage";
    case ESkillRole::Heal: return "heal";
    case ESkillRole::Buff: return "buff";
    case ESkillRole::Debuff: return "debuff";
    case ESkillRole::Utility: return "utility";
    }
    return "unknown";
}

std::optional<FRoleMask> ParseRoleMask(std::string_view name) {
    if (name == "damage") return RoleBit(ESkillRole::Damage);
    if (name == "heal") return RoleBit(ESkillRole::Heal);
    if (name == "buff") return RoleBit(ESkillRole::Buff);
    if (name == "debuff") return RoleBit(ESkillRole::Debuff);
    if (name == "hostile") return kRolesHostile;
    if (name == "friendly") return kRolesFriendly;
    if (name == "any") return kRolesAny;
    return std::nullopt;
}

} // namespace game::ai
