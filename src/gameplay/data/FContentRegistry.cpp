#include "gameplay/data/FContentRegistry.h"

#include "core/Logger.h"
#include "core/data/FContentLoader.h"

#include <tracy/Tracy.hpp>

#include <span>
#include <string>
#include <vector>

namespace game::gameplay {
namespace {

// 미결(design §4): 이동/스킬 AP와 사거리 최종 수치는 밸런싱 단계에서 정한다 — 지금은 잠정값을 data가
// 공급한다. 스키마는 확정된 항목만 담고, 미확정 항목(파벌, 장비, 룬, 이벤트 룸)은 이 슬라이스에서
// 필드로 만들지 않는다.
constexpr FContentSchema kUnitSchema{"data/units", "units", "id", 1, std::span<const FContentField>{kUnitFields}};
constexpr FContentSchema kSkillSchema{"data/skills", "skills", "id", 1, std::span<const FContentField>{kSkillFields}};

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

    // Cross-table references are checked here, in the one place that owns both tables (R22): a typo in
    // units.json must fail the boot instead of becoming a silent no-op the first time that skill is used.
    for (const FUnitContent& unit : registry.units.rows) {
        for (const std::string& skillId : unit.skillIds) {
            if (registry.skills.find(skillId) == nullptr) {
                LOG_ERROR("content: unit '{}' references unknown skill '{}'.", unit.id, skillId);
                return std::nullopt;
            }
        }
    }

    LOG_INFO("content ready: {} unit(s), {} skill(s).", registry.units.rows.size(), registry.skills.rows.size());
    return registry;
}

} // namespace game::gameplay
