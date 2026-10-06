#include "gameplay/combat/FSkillResolver.h"

#include <algorithm>
#include <cstdlib>

#include "gameplay/grid/FGridTypes.h"

namespace game {
namespace {

void addStatus(std::vector<std::string>& statuses, const std::string& statusId) {
    if (statusId.empty()) {
        return;
    }
    if (std::ranges::find(statuses, statusId) == statuses.end()) {
        statuses.push_back(statusId);
    }
}

} // namespace

FResolvedSkill ResolveSkill(
    const gameplay::FSkillContent& skill,
    const std::vector<const gameplay::FRuneContent*>& runes) {
    FResolvedSkill resolved;
    resolved.skillId = skill.id;
    resolved.apCost = skill.apCost;
    resolved.power = skill.power;
    resolved.range = skill.range;
    resolved.shape = skill.shape;
    resolved.areaRadius = skill.areaRadius;
    resolved.target = skill.target;
    addStatus(resolved.statuses, skill.appliesStatus);

    int powerDelta = 0;
    for (const gameplay::FRuneContent* rune : runes) {
        if (rune == nullptr) {
            continue;
        }
        if (!rune->shapeOverride.empty()) {
            resolved.shape = rune->shapeOverride;
        }
        addStatus(resolved.statuses, rune->statusOnHit);
        resolved.apCost += rune->apDelta;
        resolved.range += rune->rangeDelta;
        resolved.areaRadius += rune->areaRadiusDelta;
        powerDelta += rune->powerDelta;
        resolved.runeIds.push_back(rune->id);
    }

    // docs/design/skill-rune-equipment.md §3.3: 강화 룬의 총 위력 증가 상한 = 기본 대비 +100% (잠정).
    // 하한은 0(위력은 음수가 될 수 없다). 상세 수치는 밸런싱 단계에서 데이터로만 조정한다.
    const int maxIncrease = std::max(0, skill.power);
    powerDelta = std::clamp(powerDelta, -std::max(0, skill.power), maxIncrease);
    resolved.power = std::max(0, skill.power + powerDelta);

    resolved.apCost = std::max(1, resolved.apCost);
    resolved.range = std::max(1, resolved.range);
    resolved.areaRadius = std::max(0, resolved.areaRadius);
    return resolved;
}

std::optional<FResolvedSkill> ResolveSkillSlot(
    const FSkillSlot& slot,
    const gameplay::FContentRegistry& content) {
    const gameplay::FSkillContent* skill = content.skills.find(slot.skillId);
    if (skill == nullptr) {
        return std::nullopt;
    }

    std::vector<const gameplay::FRuneContent*> runes;
    runes.reserve(slot.runeIds.size());
    for (const std::string& runeId : slot.runeIds) {
        // Unknown rune ids are skipped rather than failing: loadouts are run-scope player state, and a
        // stale id must degrade the loadout, not abort the fight. The prep UI refuses unknown ids.
        if (const gameplay::FRuneContent* rune = content.runes.find(runeId)) {
            runes.push_back(rune);
        }
    }
    return ResolveSkill(*skill, runes);
}

std::vector<FGridPosition> ComputeSkillCells(const FResolvedSkill& skill, const FGridPosition& center) {
    std::vector<FGridPosition> cells{center};
    if (skill.shape == "spread") {
        for (const FGridPosition& neighbor : OrthogonalNeighbors(center)) {
            cells.push_back(neighbor);
        }
    } else if (skill.shape == "zone") {
        const int radius = std::max(1, skill.areaRadius);
        for (int dy = -radius; dy <= radius; ++dy) {
            for (int dx = -radius; dx <= radius; ++dx) {
                if (dx == 0 && dy == 0) continue;
                if (std::abs(dx) + std::abs(dy) > radius) continue;
                cells.push_back(FGridPosition{center.x + dx, center.y + dy});
            }
        }
    } else if (skill.shape == "pierce") {
        // No pierce rune exists in data yet; the target cell alone keeps the action well-defined until
        // the line is implemented. The resolver logs the notice for the battle feed.
    }
    return cells;
}

std::optional<std::string> ValidateSkillSlot(
    const FSkillSlot& slot, const gameplay::FContentRegistry& content) {
    int elementCount = 0;
    bool hasFormRune = false;
    std::vector<std::string> seen;
    seen.reserve(slot.runeIds.size());
    for (const std::string& runeId : slot.runeIds) {
        const gameplay::FRuneContent* rune = content.runes.find(runeId);
        if (rune == nullptr) {
            return "unknown rune '" + runeId + "'";
        }
        if (std::ranges::find(seen, runeId) != seen.end()) {
            return "rune '" + runeId + "' is attached twice";
        }
        seen.push_back(runeId);

        if (rune->axis == gameplay::kRuneAxisElement) {
            if (++elementCount > 2) {
                return "more than 2 element runes";
            }
        } else if (rune->axis == gameplay::kRuneAxisForm) {
            if (hasFormRune) {
                return "more than 1 form rune";
            }
            hasFormRune = true;
        }
        // enhance: uniqueness is covered by the duplicate check above (고유 강화만).
    }
    return std::nullopt;
}

} // namespace game
