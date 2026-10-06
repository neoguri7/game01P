#pragma once

#include <optional>
#include <string>
#include <vector>

#include "ecs/components/FSkillLoadout.h"
#include "gameplay/data/FContentRegistry.h"
#include "gameplay/data/FRuneContent.h"
#include "gameplay/data/FSkillContent.h"

namespace game {

/// A base skill after every attached rune has been applied (docs/design/skill-rune-equipment.md §3, §6).
/// This is the "베이스 + 룬" stage of the pipeline; equipment and later layers consume this, never the raw
/// authored row.
struct FResolvedSkill {
    std::string skillId;
    int apCost{1};
    int power{0};
    int range{1};
    std::string shape{"single"};
    int areaRadius{0};
    std::string target{"enemy"};
    std::vector<std::string> statuses; // base skill + element runes; applied on hit, in order
    std::vector<std::string> runeIds;  // provenance for tooltips/diagnostics
};

/// Applies runes in order: form runes replace the shape, element runes add a status, enhance runes move
/// the numbers. Clamps keep the result inside the designed floor (ap >= 1, range >= 1, radius >= 0) and
/// the design's provisional ceiling (총 위력 증가는 기본 대비 +100% 이내, §3.3).
[[nodiscard]] FResolvedSkill ResolveSkill(
    const gameplay::FSkillContent& skill,
    const std::vector<const gameplay::FRuneContent*>& runes);

/// Resolves one loadout slot against the loaded content tables. Nullopt when the slot names a skill the
/// tables do not contain (a wiring bug: unit content references are validated at boot).
[[nodiscard]] std::optional<FResolvedSkill> ResolveSkillSlot(
    const FSkillSlot& slot,
    const gameplay::FContentRegistry& content);

} // namespace game
