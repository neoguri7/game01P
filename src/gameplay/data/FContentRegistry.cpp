#include "gameplay/data/FContentRegistry.h"

#include "core/Logger.h"
#include "core/data/FContentLoader.h"

#include <tracy/Tracy.hpp>

#include <cmath>
#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace game::gameplay {
namespace {

// 미결(design §4): 이동/스킬 AP와 사거리·피해 최종 수치는 밸런싱 단계에서 정한다 — 지금은 잠정값을
// data가 공급한다. 스키마는 확정된 항목만 담는다: 던전 룸(§1)과 몬스터 행동 규칙(§2)은 slice 3에서 필드가
// 되었고, 파벌·장비·룬(§4)·이벤트 룸 내용은 여전히 필드로 만들지 않는다(slice 4).
constexpr FContentSchema kUnitSchema{"data/units", "units", "id", 1, std::span<const FContentField>{kUnitFields}};
constexpr FContentSchema kSkillSchema{"data/skills", "skills", "id", 1, std::span<const FContentField>{kSkillFields}};
constexpr FContentSchema kEncounterSchema{
    "data/encounters", "encounters", "id", 1, std::span<const FContentField>{kEncounterFields}};
constexpr FContentSchema kBehaviorSchema{
    "data/behaviors", "behaviors", "id", 1, std::span<const FContentField>{kBehaviorFields}};
constexpr FContentSchema kDungeonSchema{
    "data/dungeons", "dungeons", "id", 1, std::span<const FContentField>{kDungeonFields}};

/// `cells` is a flat [x, y, x, y, ...] list authored next to the unit list it belongs to.
/// why the check exists here: a half-authored cell list would otherwise surface as a unit standing on
/// (0,0) and overlapping another one — a wrong battle instead of a load error (R19/R22).
bool cellsMatchUnitList(const FEncounterContent& encounter, const std::vector<double>& cells, const std::vector<std::string>& unitIds) {
    if (cells.size() == unitIds.size() * 2) {
        return true;
    }

    LOG_ERROR("content: encounter '{}' lists {} unit(s) but {} cell coordinate(s); expected {} (x, y pairs).",
              encounter.id,
              unitIds.size(),
              cells.size(),
              unitIds.size() * 2);
    return false;
}

bool containsUnit(const FContentRegistry& registry, std::string_view unitId) {
    return registry.units.find(unitId) != nullptr;
}

/// why re-parse the tokens instead of trusting decodeBehavior: the projection returns a safe enum for an
/// unknown token (R22: decode cannot fail), so the registry is the only place that can turn "unknown
/// vocabulary" into a boot failure (R19). One helper checks both tokens the same way (R12).
bool behaviorVocabularyKnown(std::string_view behaviorId, std::string_view when, std::string_view then) {
    if (!behaviorConditionFromText(when)) {
        LOG_ERROR("content: behavior '{}' has unknown 'when' token '{}'.", behaviorId, when);
        return false;
    }
    if (!behaviorActionFromText(then)) {
        LOG_ERROR("content: behavior '{}' has unknown 'then' token '{}'.", behaviorId, then);
        return false;
    }
    return true;
}

/// why the checks live together and at load time: the projected dungeon holds only enums, so the raw kind
/// tokens are gone after decoding; and a rooms/room_kinds length mismatch, an unknown kind, and an unknown
/// encounter are the three ways a room can be wrong, so one function keeps the room index in the message
/// (R19/R22/R31).
bool dungeonRowValid(const FContentRegistry& registry, const FDungeonContent& dungeon, const std::vector<std::string>& kindTokens) {
    // why the empty check exists: a dungeon with no rooms cannot produce a run — FRunFactory refuses it at entry
    // time, which is a runtime refusal for content that should never have loaded (R19/R22: design §1 확정은
    // 던전 = 룸 목록이 있다).
    if (dungeon.roomEncounterIds.empty()) {
        LOG_ERROR("content: dungeon '{}' has no rooms; a dungeon without rooms cannot be entered.", dungeon.id);
        return false;
    }

    if (dungeon.roomEncounterIds.size() != kindTokens.size()) {
        LOG_ERROR("content: dungeon '{}' lists {} room encounter(s) but {} room kind(s); expected one kind per room.",
                  dungeon.id,
                  dungeon.roomEncounterIds.size(),
                  kindTokens.size());
        return false;
    }

    for (std::size_t index = 0; index < dungeon.roomEncounterIds.size(); ++index) {
        const std::optional<ERoomKind> kind = roomKindFromText(kindTokens[index]);
        if (!kind) {
            LOG_ERROR("content: dungeon '{}' room {} has unknown kind '{}'.", dungeon.id, index, kindTokens[index]);
            return false;
        }
        // why the check exists: design §1 leaves event rooms unimplemented in this slice, so an authored
        // 'event' room must stop the boot instead of becoming a room the run silently skips (R19/R22).
        if (*kind != ERoomKind::Monster) {
            LOG_ERROR("content: dungeon '{}' room {} has kind 'event'; event rooms are not implemented yet (design §1).",
                      dungeon.id,
                      index);
            return false;
        }
        // why the check exists: a room names an encounter by id, so an id with no row would otherwise fail
        // mid-run instead of at boot (R19/R22).
        if (registry.encounters.find(dungeon.roomEncounterIds[index]) == nullptr) {
            LOG_ERROR("content: dungeon '{}' room {} references unknown encounter '{}'.",
                      dungeon.id,
                      index,
                      dungeon.roomEncounterIds[index]);
            return false;
        }
    }
    return true;
}

/// Flat [x, y, x, y, ...] list → every cell must be inside the authored grid.
/// why the check exists: an out-of-bounds cell used to fail only when a unit was spawned, which rolled the run
/// back to the hub mid-session instead of failing the boot (R19/R22).
bool cellsInsideGrid(const FEncounterContent& encounter, const std::vector<double>& cells) {
    for (std::size_t index = 0; index + 1 < cells.size(); index += 2) {
        const double x = cells[index];
        const double y = cells[index + 1];
        if (x < 0.0 || y < 0.0 || x >= static_cast<double>(encounter.gridWidth)
            || y >= static_cast<double>(encounter.gridHeight)) {
            LOG_ERROR("content: encounter '{}' places a unit outside the {}x{} grid at ({}, {}).",
                      encounter.id,
                      encounter.gridWidth,
                      encounter.gridHeight,
                      x,
                      y);
            return false;
        }
    }
    return true;
}

/// True when no party cell equals an enemy cell.
/// why the check exists: occupancy is tracked per side, so one shared cell puts two units on it and the battle
/// starts already broken — FGridOccupancy only guards movement afterwards (R19/R22).
bool cellsDisjointFromOpponent(const FEncounterContent& encounter) {
    for (std::size_t party = 0; party + 1 < encounter.partyCells.size(); party += 2) {
        for (std::size_t enemy = 0; enemy + 1 < encounter.enemyCells.size(); enemy += 2) {
            if (encounter.partyCells[party] == encounter.enemyCells[enemy]
                && encounter.partyCells[party + 1] == encounter.enemyCells[enemy + 1]) {
                LOG_ERROR("content: encounter '{}' puts a party and an enemy unit on the same cell ({}, {}).",
                          encounter.id,
                          encounter.partyCells[party],
                          encounter.partyCells[party + 1]);
                return false;
            }
        }
    }
    return true;
}

} // namespace

std::optional<FContentRegistry> FContentRegistry::load(const FAssetManager& assets) {
    ZoneScopedN("FContentRegistry::load");

    FContentRegistry registry;

    const std::optional<std::vector<FContentRow>> unitRows = FContentLoader::load(assets, kUnitSchema);
    if (!unitRows) {
        return std::nullopt;
    }
    registry.units.rows.reserve(unitRows->size());
    for (const FContentRow& row : *unitRows) {
        registry.units.rows.push_back(decodeUnit(row));
    }

    const std::optional<std::vector<FContentRow>> skillRows = FContentLoader::load(assets, kSkillSchema);
    if (!skillRows) {
        return std::nullopt;
    }
    registry.skills.rows.reserve(skillRows->size());
    for (const FContentRow& row : *skillRows) {
        registry.skills.rows.push_back(decodeSkill(row));
    }

    const std::optional<std::vector<FContentRow>> encounterRows = FContentLoader::load(assets, kEncounterSchema);
    if (!encounterRows) {
        return std::nullopt;
    }
    registry.encounters.rows.reserve(encounterRows->size());
    for (const FContentRow& row : *encounterRows) {
        registry.encounters.rows.push_back(decodeEncounter(row));
    }

    const std::optional<std::vector<FContentRow>> behaviorRows = FContentLoader::load(assets, kBehaviorSchema);
    if (!behaviorRows) {
        return std::nullopt;
    }
    registry.behaviors.rows.reserve(behaviorRows->size());
    for (const FContentRow& row : *behaviorRows) {
        // why the registry re-parses while it still has the raw row: an unknown `when`/`then` token projects to
        // a safe enum (R22), so decode cannot report it (see behaviorVocabularyKnown).
        if (!behaviorVocabularyKnown(row.text(kBehaviorFieldId), row.text(kBehaviorFieldWhen), row.text(kBehaviorFieldThen))) {
            return std::nullopt;
        }
        registry.behaviors.rows.push_back(decodeBehavior(row));
    }

    const std::optional<std::vector<FContentRow>> dungeonRows = FContentLoader::load(assets, kDungeonSchema);
    if (!dungeonRows) {
        return std::nullopt;
    }
    registry.dungeons.rows.reserve(dungeonRows->size());
    for (const FContentRow& row : *dungeonRows) {
        const FDungeonContent dungeon = decodeDungeon(row);
        // why the registry re-parses the raw kind tokens: the projection keeps only enums, so a kind outside the
        // vocabulary would otherwise be lost (see dungeonRowValid).
        if (!dungeonRowValid(registry, dungeon, row.textArray(kDungeonFieldRoomKinds))) {
            return std::nullopt;
        }
        registry.dungeons.rows.push_back(dungeon);
    }

    // why numeric range checks live here instead of in decode: a decode function is a straight projection that
    // cannot fail (R22), while "values are data" (R4/R22) means malformed data must fail the boot. Without these,
    // a negative apCost *refunded* AP in FSkillResolveSystem and maxHealth <= 0 spawned a unit that countLiving
    // counted as living while no damage could ever down it (F4).
    for (const FSkillContent& skill : registry.skills.rows) {
        if (skill.apCost < 0 || skill.range < 0 || skill.power < 0.0) {
            LOG_ERROR("content: skill '{}' has a negative value (ap_cost {}, range {}, power {}); expected >= 0.",
                      skill.id,
                      skill.apCost,
                      skill.range,
                      skill.power);
            return std::nullopt;
        }
    }
    for (const FUnitContent& unit : registry.units.rows) {
        if (unit.maxHealth <= 0.0 || unit.speed <= 0.0 || unit.moveAp < 0 || unit.skillAp < 0) {
            LOG_ERROR("content: unit '{}' has a non-positive value (max_health {}, speed {}, move_ap {}, skill_ap {}); "
                      "expected > 0 for health/speed and >= 0 for AP.",
                      unit.id,
                      unit.maxHealth,
                      unit.speed,
                      unit.moveAp,
                      unit.skillAp);
            return std::nullopt;
        }
    }

    // why the threshold checks are condition-specific: decodeBehavior projects a value (R22), so a threshold
    // outside its condition's domain silently inverts the rule — `self_hp_at_or_below: 3` always fires,
    // `self_hp_at_or_below: -1` never fires, and a fractional `opponent_count_at_or_above` truncates to a
    // different count than the author read (F4/R19/R22).
    for (const FBehaviorContent& behavior : registry.behaviors.rows) {
        if (behavior.condition == EBehaviorCondition::SelfHpAtOrBelow
            && (behavior.threshold < 0.0 || behavior.threshold > 1.0)) {
            LOG_ERROR("content: behavior '{}' has ratio threshold {}; expected 0~1 for self_hp_at_or_below.",
                      behavior.id,
                      behavior.threshold);
            return std::nullopt;
        }
        if (behavior.condition == EBehaviorCondition::OpponentCountAtOrAbove
            && (behavior.threshold < 0.0 || std::fmod(behavior.threshold, 1.0) != 0.0)) {
            LOG_ERROR("content: behavior '{}' has count threshold {}; expected a whole number >= 0 for "
                      "opponent_count_at_or_above.",
                      behavior.id,
                      behavior.threshold);
            return std::nullopt;
        }
    }

    // Cross-table references are checked here, in the one place that owns every table (R22): a typo in a
    // data file must fail the boot instead of becoming a silent no-op the first time that row is used.
    for (const FUnitContent& unit : registry.units.rows) {
        for (const std::string& skillId : unit.skillIds) {
            if (registry.skills.find(skillId) == nullptr) {
                LOG_ERROR("content: unit '{}' references unknown skill '{}'.", unit.id, skillId);
                return std::nullopt;
            }
        }
        for (const std::string& ruleId : unit.behaviorIds) {
            // why the check exists: firstMatchingBehavior skips an unknown rule id with a warning, so a typo
            // would silently shrink a monster's tree; the registry is the one place that fails the boot instead
            // (R19/R22).
            if (registry.behaviors.find(ruleId) == nullptr) {
                LOG_ERROR("content: unit '{}' references unknown behavior '{}'.", unit.id, ruleId);
                return std::nullopt;
            }
        }
    }

    for (const FEncounterContent& encounter : registry.encounters.rows) {
        // why both sides are required: an encounter with no enemy side is a data bug that would otherwise
        // present as an instant victory, and an instant victory looks like working content.
        if (encounter.partyIds.empty() || encounter.enemyIds.empty()) {
            LOG_ERROR("content: encounter '{}' needs at least one unit on each side (party {}, enemies {}).",
                      encounter.id,
                      encounter.partyIds.size(),
                      encounter.enemyIds.size());
            return std::nullopt;
        }

        for (const std::string& unitId : encounter.partyIds) {
            if (!containsUnit(registry, unitId)) {
                LOG_ERROR("content: encounter '{}' references unknown party unit '{}'.", encounter.id, unitId);
                return std::nullopt;
            }
        }
        for (const std::string& unitId : encounter.enemyIds) {
            if (!containsUnit(registry, unitId)) {
                LOG_ERROR("content: encounter '{}' references unknown enemy unit '{}'.", encounter.id, unitId);
                return std::nullopt;
            }
        }

        if (!cellsMatchUnitList(encounter, encounter.partyCells, encounter.partyIds)
            || !cellsMatchUnitList(encounter, encounter.enemyCells, encounter.enemyIds)) {
            return std::nullopt;
        }

        if (encounter.gridWidth <= 0 || encounter.gridHeight <= 0) {
            LOG_ERROR("content: encounter '{}' has a non-positive grid ({}x{}).",
                      encounter.id,
                      encounter.gridWidth,
                      encounter.gridHeight);
            return std::nullopt;
        }

        // why both checks live after the grid check: a cell is only inside or outside a grid once the grid is
        // known to be sane, and a shared cell is only meaningful once both sides have valid cells (R19/R22).
        if (!cellsInsideGrid(encounter, encounter.partyCells) || !cellsInsideGrid(encounter, encounter.enemyCells)
            || !cellsDisjointFromOpponent(encounter)) {
            return std::nullopt;
        }
    }

    LOG_INFO("content ready: {} unit(s), {} skill(s), {} encounter(s), {} behavior(s), {} dungeon(s).",
             registry.units.rows.size(),
             registry.skills.rows.size(),
             registry.encounters.rows.size(),
             registry.behaviors.rows.size(),
             registry.dungeons.rows.size());
    return registry;
}

} // namespace game::gameplay
