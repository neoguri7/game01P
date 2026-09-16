#include "gameplay/data/FContentRegistry.h"

#include "core/Logger.h"
#include "core/data/FContentLoader.h"

#include <tracy/Tracy.hpp>

#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace game::gameplay {
namespace {

// 미결(design §4): 이동/스킬 AP와 사거리·피해 최종 수치는 밸런싱 단계에서 정한다 — 지금은 잠정값을
// data가 공급한다. 스키마는 확정된 항목만 담고, 미확정 항목(파벌, 장비, 룬, 이벤트 룸)은 필드로 만들지
// 않는다.
constexpr FContentSchema kUnitSchema{"data/units", "units", "id", 1, std::span<const FContentField>{kUnitFields}};
constexpr FContentSchema kSkillSchema{"data/skills", "skills", "id", 1, std::span<const FContentField>{kSkillFields}};
constexpr FContentSchema kEncounterSchema{
    "data/encounters", "encounters", "id", 1, std::span<const FContentField>{kEncounterFields}};

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

    // Cross-table references are checked here, in the one place that owns every table (R22): a typo in a
    // data file must fail the boot instead of becoming a silent no-op the first time that row is used.
    for (const FUnitContent& unit : registry.units.rows) {
        for (const std::string& skillId : unit.skillIds) {
            if (registry.skills.find(skillId) == nullptr) {
                LOG_ERROR("content: unit '{}' references unknown skill '{}'.", unit.id, skillId);
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
    }

    LOG_INFO("content ready: {} unit(s), {} skill(s), {} encounter(s).",
             registry.units.rows.size(),
             registry.skills.rows.size(),
             registry.encounters.rows.size());
    return registry;
}

} // namespace game::gameplay
