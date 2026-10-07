#pragma once

#include <optional>
#include <string>
#include <vector>

#include "ecs/components/FGridPosition.h"
#include "ecs/components/FSkillLoadout.h"
#include "gameplay/data/FContentRegistry.h"
#include "gameplay/data/FRuneContent.h"
#include "gameplay/data/FSkillContent.h"

namespace game {

/// One status the resolved skill applies, with the rune-driven deltas already summed
/// (docs/design/skill-rune-progression.md §3.3/§4). The combat layer adds these to the status
/// definition when the effect is applied.
struct FResolvedStatus {
    std::string statusId;
    int durationDelta{0};
    int magnitudeDelta{0};
};

/// A base skill after every attached rune has been applied (docs/design/skill-rune-progression.md §3, §8).
/// This is the "베이스 + 룬" stage of the pipeline; the run-growth stage consumes this, never the raw
/// authored row.
struct FResolvedSkill {
    std::string skillId;
    int apCost{1};
    int power{0};
    int range{1};
    std::string shape{"single"};
    int areaRadius{0};
    std::string target{"enemy"};
    std::string damageType{"physical"}; // §1.1: 물리/마법 — 피해 공식의 공격/방어 항을 고른다
    int lifeOnHit{0};                   // §3.3 흡혈: 피해를 준 공격자의 HP 회복(명중당)
    std::vector<FResolvedStatus> statuses; // base skill + element runes, with enhance-rune deltas
    std::vector<std::string> runeIds;      // provenance for tooltips/diagnostics
};

/// Applies runes in order: form runes replace the shape, element runes add a status, enhance runes move
/// the numbers and the status deltas. Clamps keep the result inside the designed floor (ap >= 1,
/// range >= 1, radius >= 0) and the design's provisional ceiling (총 위력 증가는 기본 대비 +100% 이내, §3.3).
/// Tag legality is not re-checked here: ValidateSkillSlot owns that gate, exactly like rune existence.
[[nodiscard]] FResolvedSkill ResolveSkill(
    const gameplay::FSkillContent& skill,
    const std::vector<const gameplay::FRuneContent*>& runes);

/// Resolves one loadout slot against the loaded content tables. Nullopt when the slot names a skill the
/// tables do not contain (a wiring bug: unit content references are validated at boot).
[[nodiscard]] std::optional<FResolvedSkill> ResolveSkillSlot(
    const FSkillSlot& slot,
    const gameplay::FContentRegistry& content);

/// Every cell a resolved skill affects, given the primary target cell: single = the target cell alone,
/// spread = the target plus its orthogonal neighbors, zone = all cells within areaRadius (Manhattan,
/// radius 0 = the center cell alone), pierce = the target cell plus the straight line from `origin`
/// through the target (dominant axis) for at most `skill.range` cells. Every loop is bounded by range/radius.
[[nodiscard]] std::vector<FGridPosition> ComputeSkillCells(
    const FResolvedSkill& skill, const FGridPosition& center);
[[nodiscard]] std::vector<FGridPosition> ComputeSkillCells(
    const FResolvedSkill& skill, const FGridPosition& center, const FGridPosition& origin);

/// 서포트젬식 태그 판정 (docs/design/skill-rune-progression.md §3.2). 유효 태그 집합
/// T = 베이스 스킬 태그 ∪ 장착된 모든 룬의 adds_tags. 모든 룬이 requires_all ⊆ T,
/// requires_any ∩ T ≠ ∅, excludes ∩ T = ∅를 만족해야 한다. 실패 사유를 돌려주고, 통과면 nullopt.
[[nodiscard]] std::optional<std::string> CheckRuneTagRequirements(
    const gameplay::FSkillContent& skill,
    const std::vector<const gameplay::FRuneContent*>& runes);

/// Checks the build rules (docs/design/skill-rune-progression.md §3): the rune socket count
/// (`skill.rune_sockets`), duplicate runes, every id must exist, and the §3.2 tag rules must pass.
/// Returns the failure reason; nullopt when the loadout is legal. Authored loadouts are validated at
/// battle setup so an illegal build fails before the fight instead of silently losing runes mid-action.
[[nodiscard]] std::optional<std::string> ValidateSkillSlot(
    const FSkillSlot& slot, const gameplay::FContentRegistry& content);

} // namespace game
