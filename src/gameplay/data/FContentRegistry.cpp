#include "gameplay/data/FContentRegistry.h"

#include "core/Logger.h"
#include "core/data/FContentLoader.h"

#include <tracy/Tracy.hpp>

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace game::gameplay {
namespace {

// 미결(design §4): 이동/스킬 AP와 사거리 최종 수치는 밸런싱 단계에서 정한다 — 지금은 잠정값을 data가
// 공급한다. 스키마는 확정된 항목만 담고, 미확정 항목(파벌, 이벤트 룸)은 이 슬라이스에서 필드로 만들지 않는다.
constexpr FContentSchema kUnitSchema{"data/units", "units", "id", 1, std::span<const FContentField>{kUnitFields}};
constexpr FContentSchema kSkillSchema{"data/skills", "skills", "id", 1, std::span<const FContentField>{kSkillFields}};
constexpr FContentSchema kRuneSchema{"data/runes", "runes", "id", 1, std::span<const FContentField>{kRuneFields}};
constexpr FContentSchema kStatusSchema{"data/statuses", "statuses", "id", 1, std::span<const FContentField>{kStatusFields}};
// WP-25: the engine table is the one optional table of this slice (see the load comment in load()).
constexpr FContentSchema kEngineSchema{"data/engine_ignition", "engines", "id", 1, std::span<const FContentField>{kEngineFields}};

/// Loads one asset and projects every row. False means the asset was unusable and the reason is logged.
template <typename TRow, typename TDecode>
[[nodiscard]] bool loadTable(
    const FAssetManager& assets, const FContentSchema& schema, FContentTable<TRow>& table, TDecode decode) {
    const std::optional<std::vector<FContentRow>> rows = FContentLoader::load(assets, schema);
    if (!rows) {
        return false;
    }
    table.rows.reserve(rows->size());
    for (const FContentRow& row : *rows) {
        table.rows.push_back(decode(row));
    }
    return true;
}

/// Shape ids shared by skills and form runes (docs/design/skill-rune-progression.md §3.2): a form rune
/// replaces the skill's shape, so both sides must speak the same vocabulary.
[[nodiscard]] bool isKnownShape(std::string_view shape) {
    return shape == "single" || shape == "spread" || shape == "pierce" || shape == "zone";
}

/// Skill target side (docs/design/skill-rune-progression.md §2): who the skill may be aimed at.
[[nodiscard]] bool isKnownTarget(std::string_view target) {
    return target == "enemy" || target == "ally" || target == "self" || target == "cell";
}

/// Skill damage types (docs/design/skill-rune-progression.md §1.1). Empty is allowed for skills that
/// never deal damage; otherwise the value is a closed set the damage rule switches on.
[[nodiscard]] bool isKnownDamageType(std::string_view damageType) {
    return damageType.empty() || damageType == kDamageTypePhysical || damageType == kDamageTypeMagical;
}

[[nodiscard]] bool isKnownStatusKind(std::string_view kind) {
    return kind == kStatusKindDot || kind == kStatusKindControl || kind == kStatusKindStat ||
           kind == kStatusKindMoveLock;
}

/// Control-status action-loss timing (WP-13, 30-decisions D-12): a closed set the rules switch on.
[[nodiscard]] bool isKnownStatusMode(std::string_view mode) {
    return mode == kStatusModeTurnStart || mode == kStatusModeOnApply;
}

} // namespace

std::optional<FContentRegistry> FContentRegistry::load(const FAssetManager& assets) {
    ZoneScopedN("FContentRegistry::load");

    FContentRegistry registry;

    if (!loadTable(assets, kUnitSchema, registry.units, decodeUnit)) {
        return std::nullopt;
    }
    if (!loadTable(assets, kSkillSchema, registry.skills, decodeSkill)) {
        return std::nullopt;
    }
    if (!loadTable(assets, kRuneSchema, registry.runes, decodeRune)) {
        return std::nullopt;
    }
    if (!loadTable(assets, kStatusSchema, registry.statuses, decodeStatus)) {
        return std::nullopt;
    }
    // WP-25: engine ignition is a run feature with a data-side off switch (empty engines = no ignition) and
    // the document may be absent entirely — the fixtures and the content-only test boots carry no engine
    // table, and a boot must not break just because a mod/one-root setup lacks it. A PRESENT document is
    // loaded and validated exactly like the required tables (failure below), so authoring typos still fail
    // the boot. `resolvePath` is the silent existence probe: `digest` would log a false ERROR for every
    // legitimate engine-less boot (R19 logging is for real failures), so the off state is announced by
    // exactly one INFO line instead.
    if (assets.resolvePath(kEngineSchema.assetId).has_value()) {
        if (!loadTable(assets, kEngineSchema, registry.engines, decodeEngine)) {
            return std::nullopt;
        }
    } else {
        LOG_INFO("content: no engine_ignition document; engine ignition is off.");
    }

    // A row's own vocabulary is checked here, not by the generic loader: `shape`/`axis`/`kind` are
    // closed sets that gameplay code switches on, so an unknown value must fail the boot (R19) instead
    // of falling through a switch mid-fight.
    for (const FSkillContent& skill : registry.skills.rows) {
        if (!isKnownShape(skill.shape)) {
            LOG_ERROR("content: skill '{}' has unknown shape '{}'.", skill.id, skill.shape);
            return std::nullopt;
        }
        if (!isKnownTarget(skill.target)) {
            LOG_ERROR("content: skill '{}' has unknown target '{}'.", skill.id, skill.target);
            return std::nullopt;
        }
        if (!isKnownDamageType(skill.damageType)) {
            LOG_ERROR("content: skill '{}' has unknown damage_type '{}'.", skill.id, skill.damageType);
            return std::nullopt;
        }
        if (skill.runeSockets < 0) {
            LOG_ERROR("content: skill '{}' has negative rune_sockets {}.", skill.id, skill.runeSockets);
            return std::nullopt;
        }
    }
    for (const FRuneContent& rune : registry.runes.rows) {
        if (!rune.shapeOverride.empty() && !isKnownShape(rune.shapeOverride)) {
            LOG_ERROR("content: rune '{}' has unknown shape_override '{}'.", rune.id, rune.shapeOverride);
            return std::nullopt;
        }
    }
    for (const FStatusContent& status : registry.statuses.rows) {
        if (!isKnownStatusKind(status.kind)) {
            LOG_ERROR("content: status '{}' has unknown kind '{}'.", status.id, status.kind);
            return std::nullopt;
        }
        if (!isKnownStatusMode(status.mode)) {
            LOG_ERROR("content: status '{}' has unknown mode '{}'.", status.id, status.mode);
            return std::nullopt;
        }
        if (status.chanceCap < 0 || status.chanceCap > 100) {
            LOG_ERROR("content: status '{}' has chance_cap {} outside 0..100.", status.id, status.chanceCap);
            return std::nullopt;
        }
    }

    // Cross-table references are checked here, in the one place that owns all tables (R22): a typo in an
    // asset must fail the boot instead of becoming a silent no-op the first time that row is used.
    for (const FUnitContent& unit : registry.units.rows) {
        for (const std::string& skillId : unit.skillIds) {
            if (registry.skills.find(skillId) == nullptr) {
                LOG_ERROR("content: unit '{}' references unknown skill '{}'.", unit.id, skillId);
                return std::nullopt;
            }
        }
    }
    for (const FSkillContent& skill : registry.skills.rows) {
        if (!skill.appliesStatus.empty() && registry.statuses.find(skill.appliesStatus) == nullptr) {
            LOG_ERROR("content: skill '{}' references unknown status '{}'.", skill.id, skill.appliesStatus);
            return std::nullopt;
        }
    }
    for (const FRuneContent& rune : registry.runes.rows) {
        if (!rune.statusOnHit.empty() && registry.statuses.find(rune.statusOnHit) == nullptr) {
            LOG_ERROR("content: rune '{}' references unknown status '{}'.", rune.id, rune.statusOnHit);
            return std::nullopt;
        }
        if (!rune.statusId.empty() && registry.statuses.find(rune.statusId) == nullptr) {
            LOG_ERROR("content: rune '{}' references unknown status_id '{}'.", rune.id, rune.statusId);
            return std::nullopt;
        }
    }
    // WP-25 engines: the trigger kind is a closed set the system switches on, the rune/status references must
    // resolve, and the row must carry a journal line — an engine that cannot fire or cannot speak is an
    // authoring bug, not a quiet no-op. Duplicate engine ids are rejected by FContentLoader's row identity
    // check (a second row with the same id would be shadowed by FContentTable::find).
    for (const FEngineContent& engine : registry.engines.rows) {
        if (engine.requiresRunes.empty()) {
            LOG_ERROR("content: engine '{}' has no requires_runes.", engine.id);
            return std::nullopt;
        }
        for (const std::string& runeId : engine.requiresRunes) {
            if (registry.runes.find(runeId) == nullptr) {
                LOG_ERROR("content: engine '{}' requires unknown rune '{}'.", engine.id, runeId);
                return std::nullopt;
            }
        }
        if (engine.triggerKind != kEngineTriggerStatusOnTarget) {
            LOG_ERROR("content: engine '{}' has unknown trigger kind '{}'.", engine.id, engine.triggerKind);
            return std::nullopt;
        }
        if (registry.statuses.find(engine.triggerStatusId) == nullptr) {
            LOG_ERROR("content: engine '{}' references unknown trigger status '{}'.",
                      engine.id, engine.triggerStatusId);
            return std::nullopt;
        }
        if (engine.minMagnitude < 0) {
            LOG_ERROR("content: engine '{}' has negative min_magnitude {}.", engine.id, engine.minMagnitude);
            return std::nullopt;
        }
        if (engine.log.empty()) {
            LOG_ERROR("content: engine '{}' has an empty log.", engine.id);
            return std::nullopt;
        }
    }

    LOG_INFO("content ready: {} unit(s), {} skill(s), {} rune(s), {} status(es), {} engine(s).",
             registry.units.rows.size(), registry.skills.rows.size(), registry.runes.rows.size(),
             registry.statuses.rows.size(), registry.engines.rows.size());
    return registry;
}

} // namespace game::gameplay
