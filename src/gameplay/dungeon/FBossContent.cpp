#include "gameplay/dungeon/FBossContent.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/AssetManager.h"
#include "core/Logger.h"
#include "gameplay/dungeon/FRunSequence.h"

namespace game {
namespace {

/// Mirrors FRunSequence::Load: every refusal names the first broken rule and hands back nullopt.
std::optional<FBossCatalog> Fail(std::string& error, std::string message) {
    error = std::move(message);
    return std::nullopt;
}

bool IsKnownSlot(std::string_view slot) {
    return slot == kBossSlotMid || slot == kBossSlotFinal;
}

} // namespace

const FBossDef* FBossCatalog::Find(std::string_view id) const {
    const auto found = std::ranges::find_if(bosses, [id](const FBossDef& def) { return def.id == id; });
    return found == bosses.end() ? nullptr : &*found;
}

const FBossDef* FBossCatalog::Resolve(std::string_view theme, std::string_view slot) const {
    const auto entry = std::ranges::find_if(map, [theme, slot](const FBossMapEntry& row) {
        return row.theme == theme && row.slot == slot;
    });
    if (entry == map.end()) return nullptr;
    return Find(entry->boss);
}

std::optional<FBossCatalog> LoadBossCatalog(const nlohmann::json& document,
                                            const gameplay::FContentRegistry& content,
                                            std::span<const std::string> themeIds, std::string& error) {
    if (!document.is_object()) return Fail(error, "bosses.json must be an object");
    const auto version = document.find("schema_version");
    if (version == document.end() || !version->is_number_integer() ||
        version->get<int>() != kBossCatalogSchemaVersion) {
        return Fail(error, "schema_version must be " + std::to_string(kBossCatalogSchemaVersion));
    }
    const auto mapRows = document.find("map");
    if (mapRows == document.end() || !mapRows->is_array()) return Fail(error, "`map` must be an array");
    const auto bossRows = document.find("bosses");
    if (bossRows == document.end() || !bossRows->is_array()) return Fail(error, "`bosses` must be an array");

    FBossCatalog catalog;
    std::vector<std::string> bossIds;
    try {
        for (const auto& row : *bossRows) {
            if (!row.is_object()) return Fail(error, "every boss must be an object");
            const auto id = row.find("id");
            if (id == row.end() || !id->is_string() || id->get_ref<const std::string&>().empty()) {
                return Fail(error, "every boss needs a nonempty string `id`");
            }
            const std::string bossId = id->get<std::string>();
            if (std::ranges::find(bossIds, bossId) != bossIds.end()) {
                return Fail(error, "boss id '" + bossId + "' appears twice");
            }
            bossIds.push_back(bossId);

            const auto unit = row.find("unit");
            if (unit == row.end() || !unit->is_string()) {
                return Fail(error, "boss '" + bossId + "' needs a string `unit`");
            }
            const std::string unitId = unit->get<std::string>();
            if (!content.units.find(unitId)) {
                return Fail(error, "boss '" + bossId + "' names unknown unit '" + unitId + "'");
            }
            FBossDef def;
            def.id = bossId;
            def.unit = unitId;
            // `name` is display-only; absent, the id stands in (the log lines are never empty).
            def.name = bossId;
            if (const auto name = row.find("name"); name != row.end()) {
                if (!name->is_string()) return Fail(error, "boss '" + bossId + "' `name` must be a string");
                def.name = name->get<std::string>();
            }

            const auto phases = row.find("phases");
            if (phases == row.end() || !phases->is_array() || phases->empty()) {
                return Fail(error, "boss '" + bossId + "' needs a nonempty `phases` array");
            }
            int gatedPhases = 0;
            for (std::size_t index = 0; index < phases->size(); ++index) {
                const auto& rowPhase = (*phases)[index];
                const std::string where = "boss '" + bossId + "' phase " + std::to_string(index + 1);
                if (!rowPhase.is_object()) return Fail(error, where + " must be an object");
                const auto hpAbove = rowPhase.find("hp_above");
                if (hpAbove == rowPhase.end() || !hpAbove->is_number_integer()) {
                    return Fail(error, where + " needs an integer `hp_above`");
                }
                const auto hpValue = hpAbove->get<std::int64_t>();
                if (hpValue < 0 || hpValue > 100) {
                    return Fail(error, where + " `hp_above` must be 0..100");
                }
                FBossPhaseDef phase;
                phase.hpAbove = static_cast<int>(hpValue);
                if (!def.phases.empty() && phase.hpAbove >= def.phases.back().hpAbove) {
                    return Fail(error, where + " `hp_above` must be strictly descending");
                }
                if (index + 1 == phases->size() && phase.hpAbove != 0) {
                    return Fail(error, where + " is last; its `hp_above` must be 0");
                }
                if (const auto gate = rowPhase.find("gate"); gate != rowPhase.end()) {
                    if (!gate->is_string() ||
                        std::string_view(gate->get_ref<const std::string&>()) != kBossGateParts) {
                        return Fail(error, where + " `gate` must be \"" + std::string(kBossGateParts) + "\"");
                    }
                    phase.partsGate = true;
                    ++gatedPhases;
                }
                if (const auto immune = rowPhase.find("body_immunities"); immune != rowPhase.end()) {
                    if (!immune->is_array()) {
                        return Fail(error, where + " `body_immunities` must be an array");
                    }
                    for (const auto& statusRow : *immune) {
                        if (!statusRow.is_string()) {
                            return Fail(error, where + " `body_immunities` rows must be strings");
                        }
                        const std::string statusId = statusRow.get<std::string>();
                        if (!content.statuses.find(statusId)) {
                            return Fail(error, where + " names unknown status '" + statusId + "'");
                        }
                        phase.bodyImmunities.push_back(statusId);
                    }
                }
                if (const auto notes = rowPhase.find("notes"); notes != rowPhase.end() && notes->is_string()) {
                    phase.notes = notes->get<std::string>();
                }
                def.phases.push_back(std::move(phase));
            }
            if (gatedPhases > 1) {
                return Fail(error, "boss '" + bossId + "' has more than one gated phase");
            }

            int partSum = 0;
            if (const auto parts = row.find("parts"); parts != row.end()) {
                if (!parts->is_array()) return Fail(error, "boss '" + bossId + "' `parts` must be an array");
                for (const auto& rowPart : *parts) {
                    if (!rowPart.is_object()) {
                        return Fail(error, "every part of boss '" + bossId + "' must be an object");
                    }
                    const auto partUnit = rowPart.find("unit");
                    if (partUnit == rowPart.end() || !partUnit->is_string()) {
                        return Fail(error, "boss '" + bossId + "' part needs a string `unit`");
                    }
                    const std::string partUnitId = partUnit->get<std::string>();
                    if (!content.units.find(partUnitId)) {
                        return Fail(error, "boss '" + bossId + "' part unit '" + partUnitId + "' is unknown");
                    }
                    FBossPartDef part;
                    part.unit = partUnitId;
                    // The part display name is display-only too; absent, the unit id stands in.
                    part.name = partUnitId;
                    if (const auto partName = rowPart.find("name"); partName != rowPart.end()) {
                        if (!partName->is_string()) {
                            return Fail(error, "boss '" + bossId + "' part `name` must be a string");
                        }
                        part.name = partName->get<std::string>();
                    }
                    const auto count = rowPart.find("count");
                    if (count == rowPart.end() || !count->is_number_integer()) {
                        return Fail(error, "boss '" + bossId + "' part needs an integer `count`");
                    }
                    const auto countValue = count->get<std::int64_t>();
                    if (countValue < 1 || countValue > kMaxBossParts) {
                        return Fail(error, "boss '" + bossId + "' part `count` must be 1.." +
                                               std::to_string(kMaxBossParts));
                    }
                    part.count = static_cast<int>(countValue);
                    partSum += part.count;
                    def.parts.push_back(std::move(part));
                }
            }
            if (partSum > kMaxBossParts) {
                return Fail(error, "boss '" + bossId + "' spawns more than " + std::to_string(kMaxBossParts) +
                                       " parts");
            }
            if (const auto gated = std::ranges::find_if(def.phases, &FBossPhaseDef::partsGate);
                gated != def.phases.end()) {
                if (gated->hpAbove <= 0 || def.parts.empty()) {
                    return Fail(error, "boss '" + bossId +
                                           "' gated phase needs hp_above > 0 and at least one part");
                }
            }
            catalog.bosses.push_back(std::move(def));
        }

        for (const auto& row : *mapRows) {
            if (!row.is_object()) return Fail(error, "every map row must be an object");
            const auto theme = row.find("theme");
            if (theme == row.end() || !theme->is_string()) return Fail(error, "every map row needs a string `theme`");
            const std::string themeId = theme->get<std::string>();
            if (std::ranges::find(themeIds, themeId) == themeIds.end()) {
                return Fail(error, "map row names unknown theme '" + themeId + "'");
            }
            const auto slot = row.find("slot");
            if (slot == row.end() || !slot->is_string()) return Fail(error, "every map row needs a string `slot`");
            const std::string slotId = slot->get<std::string>();
            if (!IsKnownSlot(slotId)) {
                return Fail(error, "map row (theme '" + themeId + "') `slot` must be \"mid\" or \"final\"");
            }
            const auto boss = row.find("boss");
            if (boss == row.end() || !boss->is_string()) return Fail(error, "every map row needs a string `boss`");
            const std::string bossRef = boss->get<std::string>();
            if (std::ranges::find(bossIds, bossRef) == bossIds.end()) {
                return Fail(error, "map row names unknown boss '" + bossRef + "'");
            }
            const auto duplicate = std::ranges::find_if(catalog.map, [&themeId, &slotId](const FBossMapEntry& entry) {
                return entry.theme == themeId && entry.slot == slotId;
            });
            if (duplicate != catalog.map.end()) {
                return Fail(error, "map row (theme '" + themeId + "', slot '" + slotId + "') appears twice");
            }
            catalog.map.push_back({themeId, slotId, bossRef});
        }
    } catch (const nlohmann::json::exception& exception) {
        return Fail(error, std::string("bosses.json is unusable: ") + exception.what());
    }
    return catalog;
}

bool InstallBossCatalog(entt::registry& registry) {
    const auto* assets = registry.ctx().find<FAssetManager>();
    const auto* content = registry.ctx().find<gameplay::FContentRegistry>();
    const auto* sequence = FindRunSequence(registry);
    if (assets == nullptr || content == nullptr || sequence == nullptr) {
        LOG_ERROR("bosses.json cannot be installed: the asset manager, content or run sequence is missing.");
        return false;
    }
    // The file stays optional: absence is a design state (every boss slot uses the single-phase fallback),
    // not an error. `resolvePath` is the silent existence probe; `digest` only runs when the file is there.
    if (!assets->resolvePath("data/bosses")) {
        registry.ctx().erase<FBossCatalog>();
        registry.ctx().emplace<FBossCatalog>();
        LOG_INFO("[boss] bosses.json absent: every boss slot uses the single-phase fallback.");
        return true;
    }
    const auto document = assets->digest("data/bosses");
    if (document == nullptr) {
        LOG_ERROR("bosses.json could not be loaded.");  // the asset error above names the cause
        return false;
    }
    std::vector<std::string> themeIds;
    themeIds.reserve(sequence->themes.size());
    for (const FRunThemeDef& theme : sequence->themes) themeIds.push_back(theme.id);
    std::string error;
    auto catalog = LoadBossCatalog(document->json(), *content, themeIds, error);
    if (!catalog) {
        LOG_ERROR("bosses.json invalid: {}", error);
        return false;
    }
    // Re-initialization (tests, debug reloads) replaces any previous catalog.
    registry.ctx().erase<FBossCatalog>();
    const auto& installed = registry.ctx().emplace<FBossCatalog>(std::move(*catalog));
    LOG_INFO("[boss] bosses.json ready: {} boss(es), {} map row(s).", installed.bosses.size(), installed.map.size());
    return true;
}

const FBossCatalog* FindBossCatalog(const entt::registry& registry) {
    return registry.ctx().find<FBossCatalog>();
}

} // namespace game
