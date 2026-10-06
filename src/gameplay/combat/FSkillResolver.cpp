#include "gameplay/combat/FSkillResolver.h"

#include <algorithm>
#include <cstdlib>

#include "gameplay/grid/FGridTypes.h"

namespace game {
namespace {

void addStatus(std::vector<FResolvedStatus>& statuses, const std::string& statusId) {
    if (statusId.empty()) {
        return;
    }
    if (std::ranges::find(statuses, statusId, &FResolvedStatus::statusId) == statuses.end()) {
        statuses.push_back(FResolvedStatus{statusId, 0, 0});
    }
}

[[nodiscard]] bool hasTag(const std::vector<std::string>& tags, const std::string& tag) {
    return std::ranges::find(tags, tag) != tags.end();
}

[[nodiscard]] std::string joinTags(const std::vector<std::string>& tags) {
    std::string joined;
    for (const std::string& tag : tags) {
        if (!joined.empty()) {
            joined += ", ";
        }
        joined += tag;
    }
    return joined;
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
    resolved.damageType = skill.damageType;
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
        resolved.lifeOnHit += rune->lifeOnHit;
        powerDelta += rune->powerDelta;
        resolved.runeIds.push_back(rune->id);
    }

    // Status deltas are summed in a second pass so the numbers do not depend on rune order either
    // (§3.2 fixes tag order-independence; keeping the numeric side order-free is one less rule to hold).
    for (const gameplay::FRuneContent* rune : runes) {
        if (rune == nullptr || (rune->statusDurationDelta == 0 && rune->statusMagnitudeDelta == 0)) {
            continue;
        }
        for (FResolvedStatus& status : resolved.statuses) {
            if (!rune->statusId.empty() && rune->statusId != status.statusId) {
                continue;
            }
            status.durationDelta += rune->statusDurationDelta;
            status.magnitudeDelta += rune->statusMagnitudeDelta;
        }
    }

    // docs/design/skill-rune-progression.md §3.3: 강화 룬의 총 위력 증가 상한 = 기본 대비 +100% (잠정).
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
        // §3.3 집중 룬이 반경을 0까지 낮출 수 있다 — 그때는 대상 셀 하나만 남는다.
        const int radius = std::max(0, skill.areaRadius);
        for (int dy = -radius; dy <= radius; ++dy) {
            for (int dx = -radius; dx <= radius; ++dx) {
                if (dx == 0 && dy == 0) continue;
                if (std::abs(dx) + std::abs(dy) > radius) continue;
                cells.push_back(FGridPosition{center.x + dx, center.y + dy});
            }
        }
    } else if (skill.shape == "pierce") {
        // 관통 직선은 전투 완성 슬라이스에서 구현한다(정본 §3.3 rune.pierce). 그때까지 대상 셀 하나로
        // 처리해 액션이 정의되게 유지한다.
    }
    return cells;
}

std::optional<std::string> CheckRuneTagRequirements(
    const gameplay::FSkillContent& skill,
    const std::vector<const gameplay::FRuneContent*>& runes) {
    // 고정점: adds_tags는 조건 없이 부여되므로 한 번의 합집합으로 충분하다.
    std::vector<std::string> effective = skill.tags;
    for (const gameplay::FRuneContent* rune : runes) {
        if (rune == nullptr) {
            continue;
        }
        for (const std::string& tag : rune->addsTags) {
            if (!hasTag(effective, tag)) {
                effective.push_back(tag);
            }
        }
    }

    for (const gameplay::FRuneContent* rune : runes) {
        if (rune == nullptr) {
            continue;
        }
        for (const std::string& tag : rune->requiresAllTags) {
            if (!hasTag(effective, tag)) {
                return "rune '" + rune->id + "' requires tag '" + tag + "'";
            }
        }
        if (!rune->requiresAnyTags.empty()) {
            bool satisfied = false;
            for (const std::string& tag : rune->requiresAnyTags) {
                if (hasTag(effective, tag)) {
                    satisfied = true;
                    break;
                }
            }
            if (!satisfied) {
                return "rune '" + rune->id + "' requires one of [" + joinTags(rune->requiresAnyTags) + "]";
            }
        }
        for (const std::string& tag : rune->excludesTags) {
            if (hasTag(effective, tag)) {
                return "rune '" + rune->id + "' cannot be used with tag '" + tag + "'";
            }
        }
    }
    return std::nullopt;
}

std::optional<std::string> ValidateSkillSlot(
    const FSkillSlot& slot, const gameplay::FContentRegistry& content) {
    const gameplay::FSkillContent* skill = content.skills.find(slot.skillId);
    if (skill == nullptr) {
        return "unknown skill '" + slot.skillId + "'";
    }

    // §3.1: 소켓 수가 유일한 수량 제한이다(축 폐기). 중복 금지는 아래 루프가 담당한다.
    if (slot.runeIds.size() > static_cast<std::size_t>(skill->runeSockets)) {
        return "more than " + std::to_string(skill->runeSockets) + " runes (socket limit)";
    }

    std::vector<std::string> seen;
    std::vector<const gameplay::FRuneContent*> runes;
    seen.reserve(slot.runeIds.size());
    runes.reserve(slot.runeIds.size());
    for (const std::string& runeId : slot.runeIds) {
        const gameplay::FRuneContent* rune = content.runes.find(runeId);
        if (rune == nullptr) {
            return "unknown rune '" + runeId + "'";
        }
        if (std::ranges::find(seen, runeId) != seen.end()) {
            return "rune '" + runeId + "' is attached twice";
        }
        seen.push_back(runeId);
        runes.push_back(rune);
    }

    if (const std::optional<std::string> tagProblem = CheckRuneTagRequirements(*skill, runes)) {
        return tagProblem;
    }
    return std::nullopt;
}

} // namespace game
