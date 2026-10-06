#pragma once

#include <string>
#include <vector>

namespace game {

/// One skill as a unit carries it, with the runes attached to that skill.
/// docs/design/skill-rune-equipment.md §3: 룬은 스킬에 여러 개 장착된다(속성 ≤2, 형태 ≤1, 강화 고유만).
struct FSkillSlot {
    std::string skillId;
    std::vector<std::string> runeIds;
};

/// The unit's skill list. Loaded from unit content (`skills`) and extended with run-scope rune
/// attachments by the prep/run layer; combat only reads it.
struct FSkillLoadout {
    std::vector<FSkillSlot> slots;
};

} // namespace game
