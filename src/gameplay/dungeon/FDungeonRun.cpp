#include "gameplay/dungeon/FDungeonRun.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <set>
#include <tuple>
#include <SDL3/SDL.h>

#include "core/AssetManager.h"
#include "core/Logger.h"
#include "ecs/components/FBossGoals.h"
#include "ecs/components/FDead.h"
#include "ecs/components/FFaction.h"
#include "ecs/components/FHealth.h"
#include "ecs/components/FSpeed.h"
#include "ecs/components/FStats.h"
#include "gameplay/combat/FCombatSheet.h"
#include "gameplay/combat/FSkillResolver.h"
#include "gameplay/data/FContentRegistry.h"
#include "gameplay/dungeon/FBossContent.h"
#include "gameplay/dungeon/FDungeonCamp.h"
#include "gameplay/dungeon/FDungeonSave.h"
#include "gameplay/dungeon/FRunEventLog.h"
#include "gameplay/dungeon/FRunFlow.h"
#include "gameplay/dungeon/FRunLog.h"
#include "gameplay/dungeon/FRunSequence.h"
#include "gameplay/dungeon/FRunStart.h"
#include "gameplay/dungeon/FSaveSections.h"
#include "gameplay/dungeon/FRoomGenerator.h"
#include "gameplay/factory/FEntityDefinitions.h"
#include "gameplay/factory/FEntityFactory.h"
#include "gameplay/meta/FMetaProgress.h"
#include "gameplay/battle/FBattleSetup.h"

namespace game {

const nlohmann::json& DungeonDefinition(entt::registry& registry) {
    return registry.ctx().get<FAssetManager>().digest("data/dungeon")->json();
}

namespace {

constexpr const char* kUnreadableSave = "Saved expedition is unreadable. Start a new expedition to replace it.";

std::optional<FDungeonSaveRules> SaveRules(entt::registry& registry) {
    const auto* content = registry.ctx().find<gameplay::FContentRegistry>();
    const auto* growth = registry.ctx().find<FDungeonGrowth>();
    if (content == nullptr || growth == nullptr) return std::nullopt;
    FDungeonSaveRules rules;
    rules.content = content;
    rules.passiveTree = &growth->passiveTree;
    // [WP-21b] The run party is chosen from the roster, so the save accepts WHICH ids may appear (dungeon
    // party ids ∪ every roster entry that is not a `locked: true` stub, no duplicates). The count stays
    // pinned to the four battlefield cells (kRunPartySize): Subset widens the id set, never the size. An
    // old save (the authored four) still loads.
    rules.partyMatch = EPartyMatch::Subset;
    rules.partySize = kRunPartySize;
    std::set<std::string> seen;
    const auto addPartyId = [&rules, &seen](const std::string& id) {
        if (seen.insert(id).second) rules.partyIds.push_back(id);
    };
    for (const auto& member : DungeonDefinition(registry).at("party")) {
        addPartyId(member.at("id").get<std::string>());
    }
    for (const FMetaRosterRow& row : MetaRosterCatalog(registry)) {
        if (!row.locked) addPartyId(row.id);
    }
    return rules;
}

/// A validated save document and the run it describes. `ParseSavedRun` only validates: it never applies a
/// section, so the menu refresh can inspect the slot without touching the run's module state.
struct FLoadedRun {
    FDungeonRun run;
    nlohmann::json document;
    std::string savedAt;
};

/// Reads and validates the save slot. `savedAt` receives the file's timestamp for the menu.
std::expected<FLoadedRun, std::string> ParseSavedRun(entt::registry& registry) {
    const auto rules = SaveRules(registry);
    if (!rules) return std::unexpected("expedition content is unavailable");
    auto document = ReadSaveFile(registry.ctx().get<FDungeonSave>().path);
    if (!document) return std::unexpected(document.error());
    FLoadedRun loaded;
    if (const auto stamp = document->find("saved_at"); stamp != document->end() && stamp->is_string()) {
        loaded.savedAt = stamp->get<std::string>().substr(0, 32);
    }
    auto run = DungeonRunFromJson(*document, *rules);
    if (!run) return std::unexpected(run.error());
    // WP-08: `room` is the battle ordinal of the dungeon at `depth - 1`; the save's static key range only
    // bounds it by the slot cap. The dynamic bound comes from the sequence. A v1 save maps identically —
    // its room is a battle ordinal too, and every dungeon keeps its battle count.
    if (const auto* sequence = FindRunSequence(registry)) {
        const auto position = sequence->PositionOf(run->depth - 1);
        if (run->room >= sequence->BattleCountAt(position)) {
            return std::unexpected("room does not match the dungeon sequence");
        }
    }
    loaded.run = std::move(*run);
    loaded.document = std::move(*document);
    return loaded;
}

/// Continue's load path: validate the document first, then unpack its sections. A section failure leaves
/// the document's run uninstalled (the caller keeps the current run).
std::expected<FDungeonRun, std::string> LoadAndUnpackSavedRun(entt::registry& registry, std::string& savedAt) {
    auto loaded = ParseSavedRun(registry);
    if (!loaded) return std::unexpected(loaded.error());
    savedAt = loaded->savedAt;
    if (const auto unpacked = UnpackSections(registry, loaded->document); !unpacked) {
        return std::unexpected(unpacked.error());
    }
    return std::move(loaded->run);
}

/// Looks at the save slot at boot so the menu knows whether it can offer Continue. Parse only — no unpack.
void RefreshDungeonSave(entt::registry& registry) {
    auto& save = registry.ctx().get<FDungeonSave>();
    save.exists = false;
    save.error.clear();
    std::error_code code;
    if (save.path.empty() || !std::filesystem::exists(save.path, code)) return;
    const auto loaded = ParseSavedRun(registry);
    if (!loaded) {
        save.error = kUnreadableSave;
        LOG_WARN("Ignoring saved expedition '{}': {}", save.path, loaded.error());
        return;
    }
    save.exists = true;
    save.depth = loaded->run.depth;
    save.room = loaded->run.room;
    save.phase = loaded->run.phase;
    save.savedAt = loaded->savedAt;
}

/// The loadout window (WP-27): runes may be changed between rooms — at camp and on the reward screen.
/// Battle/defeat/event steps reject every edit with the message below (one window, one sentence).
bool RunesChangeableIn(EDungeonPhase phase) {
    return phase == EDungeonPhase::Camp || phase == EDungeonPhase::Reward;
}

std::optional<std::string> ValidateCampSlot(
    const FDungeonRun* run, std::size_t memberIndex, std::size_t slotIndex) {
    if (!run || !RunesChangeableIn(run->phase)) return "Runes can be changed between rooms, not in battle.";
    if (memberIndex >= run->party.size()) return "Unknown party member.";
    if (slotIndex >= run->party[memberIndex].skills.size()) return "Unknown skill slot.";
    return std::nullopt;
}

std::optional<gameplay::FIntRange> ParseRange(const nlohmann::json& generation, const char* key, int lowest) {
    const auto found = generation.find(key);
    if (found == generation.end() || !found->is_array() || found->size() != 2 ||
        !(*found)[0].is_number_integer() || !(*found)[1].is_number_integer()) return std::nullopt;
    const gameplay::FIntRange range{(*found)[0].get<int>(), (*found)[1].get<int>()};
    if (range.min < lowest || range.max < range.min || range.max > 16) return std::nullopt;
    return range;
}

std::optional<gameplay::FRoomGenConfig> ParseRoomGeneration(
    const nlohmann::json& data, const gameplay::FContentRegistry& content) {
    const auto generation = data.find("generation");
    if (generation == data.end() || !generation->is_object()) return std::nullopt;
    gameplay::FRoomGenConfig config;
    try {
        for (const auto& name : generation->at("room_names")) config.names.push_back(name.get<std::string>());
        for (const auto& enemy : generation->at("enemies")) {
            config.enemies.push_back({enemy.at("unit").get<std::string>(),
                                      enemy.at("display_name").get<std::string>(),
                                      enemy.at("weight").get<int>()});
        }
        config.bossUnit = generation->at("boss_unit").get<std::string>();
        config.bossName = generation->at("boss_name").get<std::string>();
    } catch (const nlohmann::json::exception&) {
        return std::nullopt;
    }
    const auto enemyCount = ParseRange(*generation, "enemy_count", 1);
    const auto obstacleCount = ParseRange(*generation, "obstacle_count", 0);
    const auto trapCount = ParseRange(*generation, "trap_count", 0);
    if (config.names.empty() || config.enemies.empty() || !enemyCount || !obstacleCount || !trapCount ||
        enemyCount->max > 8 || !content.units.find(config.bossUnit)) return std::nullopt;
    for (const auto& enemy : config.enemies) {
        if (enemy.weight < 1 || !content.units.find(enemy.unit)) return std::nullopt;
    }
    config.enemyCount = *enemyCount;
    config.obstacleCount = *obstacleCount;
    config.trapCount = *trapCount;
    return config;
}

/// WP-15: resolves the (theme, slot) boss mapping into the room config. "" = a non-boss slot; a mapped boss
/// sets the body unit/name and the part rows and returns the bosses.json id; an unmapped boss leaves the
/// config untouched and returns `config.bossUnit` — the single-phase fallback id (a fallback boss still
/// produces its BossDefeated with `generation.boss_unit`, the WP-20 cross contract).
std::string ApplyBossSlot(gameplay::FRoomGenConfig& config, const FBossCatalog* catalog, std::string_view themeId,
                          const FRunRoomSlot& slot, bool bossBattle) {
    if (!bossBattle) return {};  // WP-15c: the caller asks FRunSequence::IsBossBattle, the one boss predicate
    if (catalog != nullptr) {
        if (const FBossDef* def = catalog->Resolve(themeId, slot.boss); def != nullptr) {
            config.bossUnit = def->unit;
            config.bossName = def->name;
            config.bossParts.clear();
            for (const FBossPartDef& part : def->parts) {
                config.bossParts.push_back({part.unit, part.name, part.count});
            }
            return def->id;
        }
    }
    return config.bossUnit;
}

bool IsKnownStatUpgrade(std::string_view stat) {
    return stat == "health" || stat == "attack" || stat == "magic_attack" || stat == "defense"
        || stat == "magic_defense" || stat == "speed";
}

/// The unknown-member report is once per id: views call the max-health helper every frame, and a broken
/// party id must be loud once — not a per-frame flood.
void ReportUnknownRunMember(std::string_view id) {
    static std::set<std::string> reported;
    if (reported.insert(std::string(id)).second) {
        LOG_ERROR("Run member '{}' has no unit template; its max HP is unknown.", id);
    }
}

/// The battlefield-placement report is once per id: a broken run member must be loud once, not flood every
/// room build (the same discipline as ReportUnknownRunMember).
void ReportUnplacedRunMember(std::string_view id) {
    static std::set<std::string> reported;
    if (reported.insert(std::string(id)).second) {
        LOG_ERROR("Run member '{}' has no unit template; it is not placed on the battlefield.", id);
    }
}

std::vector<std::pair<int, int>> PartyCells(const nlohmann::json& party) {
    std::vector<std::pair<int, int>> cells;
    for (const auto& member : party) cells.emplace_back(member.at("grid_x").get<int>(), member.at("grid_y").get<int>());
    return cells;
}

/// [WP-21b] The display name for an installed setup member: the authored dungeon party row's display_name
/// when the id is an authored row, else the roster catalog name. A member in neither keeps its id (the
/// setup validation refuses such a member before a run is installed).
std::string SetupMemberDisplayName(entt::registry& registry, std::string_view memberId) {
    for (const auto& row : DungeonDefinition(registry).at("party")) {
        if (row.at("id").get<std::string>() == memberId) return row.at("display_name").get<std::string>();
    }
    for (const FMetaRosterRow& row : MetaRosterCatalog(registry)) {
        if (row.id == memberId) return row.name;
    }
    return std::string(memberId);
}

/// [WP-21b] The `document["units"]` party rows for the installed run: each member's placement id, its
/// resolved unit template (ResolveRunMemberUnit — the single rule), its run display name and the i-th
/// dungeon.json party[] placement cell. A member whose unit does not resolve is skipped, and never more
/// members than authored cells are placed.
nlohmann::json BattlefieldPartyRows(entt::registry& registry, const FDungeonRun& run, const nlohmann::json& data) {
    const auto& cells = data.at("party");
    nlohmann::json party = nlohmann::json::array();
    for (std::size_t i = 0; i < run.party.size() && i < cells.size(); ++i) {
        const FRunMember& member = run.party[i];
        const gameplay::FUnitContent* unit = ResolveRunMemberUnit(registry, member.id);
        if (unit == nullptr) {
            ReportUnplacedRunMember(member.id);
            continue;
        }
        party.push_back({{"id", member.id},
                         {"display_name", member.displayName},
                         {"unit", unit->id},
                         {"faction", "party"},
                         {"grid_x", cells[i].at("grid_x")},
                         {"grid_y", cells[i].at("grid_y")}});
    }
    return party;
}

// Fills the battlefield fields that every dungeon room shares.
void FinishBattlefield(nlohmann::json& document, const nlohmann::json& data) {
    document["width"] = kGridWidth; document["height"] = kGridHeight; document["tile_size"] = 64;
    document["origin_x"] = 0; document["origin_y"] = 0;
    document["move_range"] = data["balance"]["move_range"];
    document["trap_damage"] = data["balance"]["trap_damage"];
}

// [WP-21b] `party` is only the party part of `document["units"]`: the boot probe passes the authored
// `dungeon.json party` (unchanged output), the live battlefield passes the installed run's rows
// (BattlefieldPartyRows). The generator cells ALWAYS come from the authored `data.at("party")` — terrain
// and enemy placement must not depend on who is in the party (seed determinism, battle-ordinal identity).
nlohmann::json BuildRoomDocument(const nlohmann::json& data, const nlohmann::json& party,
                                 const gameplay::FRoomGenConfig& config,
                                 std::uint32_t seed, int depth, int room, bool boss,
                                 gameplay::FGeneratedRoom* generatedOut = nullptr) {
    const auto cells = PartyCells(data.at("party"));
    const auto generated = gameplay::GenerateRoom(config, {seed, depth, room, boss, cells});
    if (generatedOut != nullptr) *generatedOut = generated;
    nlohmann::json document;
    document["name"] = generated.name;
    document["terrain"] = generated.terrain;
    document["units"] = party;
    int index = 1;
    for (const auto& enemy : generated.enemies) {
        document["units"].push_back({{"id", "enemy." + std::to_string(index++)},
                                     {"display_name", enemy.displayName}, {"unit", enemy.unit},
                                     {"faction", "enemy"}, {"grid_x", enemy.x}, {"grid_y", enemy.y}});
    }
    FinishBattlefield(document, data);
    return document;
}

constexpr const char* kSaveDirEnv = "GAME01P_SAVE_DIR"; // boundary: env key

/// The GAME01P_SAVE_DIR override, or nullopt when unset or empty. std::getenv, not SDL_getenv: SDL caches the
/// environment on first use, so a test that sets the variable later would not be seen.
std::optional<std::filesystem::path> SaveDirOverride() {
    const char* value = nullptr;
#ifdef _MSC_VER
#pragma warning(suppress: 4996) // std::getenv: read-only, no buffer to overflow
#endif
    value = std::getenv(kSaveDirEnv);
    if (value == nullptr || *value == '\0') return std::nullopt;
    return std::filesystem::path(value);
}

} // namespace

int DungeonBalance(entt::registry& registry, const char* key) {
    return DungeonDefinition(registry).at("balance").at(key).get<int>();
}

const FDungeonRecovery& DungeonRecovery(const entt::registry& registry) {
    if (const auto* recovery = registry.ctx().find<FDungeonRecovery>()) return *recovery;
    // A missing block is a wiring error, not a content default: never invent recovery numbers here — an
    // in-code copy of the data is the double-source the W6 review rejected. Zeroed = no healing.
    static const FDungeonRecovery kZero;
    static bool reported = false;
    if (!reported) {
        reported = true;
        LOG_ERROR("FDungeonRecovery is missing: InitializeDungeon did not load the recovery block.");
    }
    return kZero;
}

int KoRecoverFloor(int maxHealth, double fraction) {
    const int floored = static_cast<int>(std::floor(static_cast<double>(maxHealth) * fraction));
    return std::max(1, floored);
}

/// The unit template a run member maps to (WP-21 §1, the single rule): a `data/roster` row with this id
/// wins — `locked: true` resolves to nullptr (even when the row carries a `unit` key; locked rows are
/// excluded from resolution and cannot be fielded), a string `unit` key resolves through the content
/// table; otherwise (no row, no `unit` key, unreadable roster) the `data/dungeon` `party[].id → unit`
/// lookup decides. The run keeps placement ids ("party.warrior"); the unit table is keyed by template id
/// ("char.warrior"). Mirrors how FRewards resolves party skills — a const-registry-safe read for the run
/// HP derivation. nullptr means the member's unit cannot be resolved; callers keep their existing
/// warning/fallback.
const gameplay::FUnitContent* ResolveRunMemberUnit(const entt::registry& registry, std::string_view memberId) {
    const auto* assets = registry.ctx().find<FAssetManager>();
    const auto* content = registry.ctx().find<gameplay::FContentRegistry>();
    if (assets == nullptr || content == nullptr) return nullptr;
    // `resolvePath` is the silent existence probe (the FBossContent precedent): an absent roster document
    // is the dungeon fallback, not an error — content-only boots stay quiet.
    if (assets->resolvePath("data/roster")) {
        if (const auto roster = assets->digest("data/roster")) {
            try {
                for (const auto& row : roster->json().at("roster")) {
                    if (row.at("id").get<std::string>() != memberId) continue;
                    // A locked stub resolves to nullptr even when it carries a `unit` key: locked rows are
                    // excluded from resolution and cannot be fielded (WP §1).
                    if (row.value("locked", false)) return nullptr;
                    if (const auto unit = row.find("unit"); unit != row.end() && unit->is_string()) {
                        return content->units.find(unit->get<std::string>());
                    }
                    break;  // no `unit` key and not locked: the dungeon fallback decides
                }
            } catch (const nlohmann::json::exception& error) {
                LOG_WARN("Run member unit could not be resolved: {}", error.what());
            }
        }
    }
    const auto digest = assets->digest("data/dungeon");
    if (!digest) return nullptr;
    try {
        for (const auto& row : digest->json().at("party")) {
            if (row.at("id").get<std::string>() == memberId) {
                return content->units.find(row.at("unit").get<std::string>());
            }
        }
    } catch (const nlohmann::json::exception& error) {
        LOG_WARN("Run member unit could not be resolved: {}", error.what());
    }
    return nullptr;
}

int DungeonMemberMaxHealth(const entt::registry& registry, const FRunMember& member) {
    const auto* unit = ResolveRunMemberUnit(registry, member.id);
    if (unit == nullptr) {
        // Unknown member: return an unknown maximum instead of 0 + bonuses. A 0 would make the heal clamp
        // and the chest cost silently corrupt the run HP (callers treat <= 0 as unknown).
        ReportUnknownRunMember(member.id);
        return 0;
    }
    int maxHealth = static_cast<int>(unit->maxHealth);
    maxHealth += member.bonusHealth;
    if (const auto* growth = registry.ctx().find<FDungeonGrowth>()) {
        maxHealth += growth->passiveTree.Bonuses(member.allocatedNodes).maxHealth;
    }
    return maxHealth;
}

int DungeonEffectiveHealth(const entt::registry& registry, const FRunMember& member) {
    const int maxHealth = DungeonMemberMaxHealth(registry, member);
    if (maxHealth <= 0) return member.health < 0 ? 0 : member.health;  // 알 수 없는 최대: 클램프 금지
    if (member.health < 0) return maxHealth;                           // 미포착 = 만피 센티넬
    return std::min(member.health, maxHealth);
}

int ChestHpLoss(int maxHealth, int effectiveHealth, double fraction) {
    const int cost = static_cast<int>(std::floor(static_cast<double>(maxHealth) * fraction));
    return std::min(cost, std::max(0, effectiveHealth - 1));
}

int ChestHpLossFor(const entt::registry& registry, const FRunMember& member) {
    const int maxHealth = DungeonMemberMaxHealth(registry, member);
    if (maxHealth <= 0) return 0;  // 알 수 없는 멤버: 손실 0 (실패는 한 번 로그된다 — 0으로 클램프 금지)
    return ChestHpLoss(maxHealth, DungeonEffectiveHealth(registry, member),
                       DungeonRecovery(registry).eventChestHpCostFraction);
}

int ApplyChestHpCost(entt::registry& registry) {
    auto& run = registry.ctx().get<FDungeonRun>();
    int totalLoss = 0;
    for (auto& member : run.party) {
        const int loss = ChestHpLossFor(registry, member);
        if (loss <= 0) continue;
        member.health = DungeonEffectiveHealth(registry, member) - loss;
        totalLoss += loss;
    }
    return totalLoss;
}

bool InitializeDungeon(entt::registry& registry, std::string checkpointPath, std::string savePath) {
    const auto asset = registry.ctx().get<FAssetManager>().digest("data/dungeon");
    if (!asset) return false;
    const auto& data = asset->json();
    try {
        if (!data.at("schema_version").is_number_integer() || data.at("schema_version") != 2 ||
            !data.at("party").is_array() || data.at("party").size() != 4 ||
            !data.at("generation").is_object()) {
            LOG_ERROR("Dungeon needs schema version 2, four party members and a room generation block.");
            return false;
        }
        for (const char* key : {"move_range", "trap_damage", "room_heal", "event_heal", "event_materials",
                               "room_materials", "room_experience",
                               "level_experience", "level_experience_step",
                               "enemy_health_per_depth", "enemy_attack_per_depth"}) {
            const auto& value = data.at("balance").at(key);
            if (!value.is_number_integer() || value < 0 || value > 1000) {
                LOG_ERROR("Invalid dungeon balance field '{}'.", key);
                return false;
            }
        }
        if (data["balance"]["move_range"] < 1 || data["balance"]["move_range"] > 64 ||
            data["balance"]["trap_damage"] > 64 || data["balance"]["level_experience"] < 1) {
            LOG_ERROR("Dungeon movement, trap damage or level threshold is out of bounds.");
            return false;
        }
        // 런 성장 콘텐츠 (정본 §5/§6). passive_tree는 asset id, stat_upgrades는 항목 목록이다.
        FDungeonGrowth growth;
        growth.levelExperienceBase = data["balance"]["level_experience"].get<int>();
        growth.levelExperienceStep = data["balance"]["level_experience_step"].get<int>();
        const auto treeAsset = registry.ctx().get<FAssetManager>().digest(data.at("passive_tree").get<std::string>());
        if (!treeAsset) {
            LOG_ERROR("Dungeon passive_tree asset is missing.");
            return false;
        }
        const auto tree = FPassiveTree::Load(treeAsset->json());
        if (!tree) {
            LOG_ERROR("Dungeon passive tree is unusable (see the passive tree errors above).");
            return false;
        }
        growth.passiveTree = *tree;
        const auto upgradeRows = data.find("stat_upgrades");
        if (upgradeRows == data.end() || !upgradeRows->is_array() || upgradeRows->empty()) {
            LOG_ERROR("Dungeon stat_upgrades must be a nonempty array.");
            return false;
        }
        std::set<std::string> upgradeIds;
        for (const auto& row : *upgradeRows) {
            FStatUpgrade upgrade;
            upgrade.id = row.at("id").get<std::string>();
            upgrade.displayName = row.at("display_name").get<std::string>();
            upgrade.stat = row.at("stat").get<std::string>();
            upgrade.amount = row.at("amount").get<int>();
            upgrade.cap = row.at("cap").get<int>();
            if (upgrade.id.empty() || upgrade.displayName.empty() || !IsKnownStatUpgrade(upgrade.stat)
                || upgrade.amount < 1 || upgrade.amount > 100
                || upgrade.cap < upgrade.amount || upgrade.cap > 100 || !upgradeIds.insert(upgrade.id).second) {
                LOG_ERROR("Dungeon stat upgrade '{}' is invalid.", upgrade.id);
                return false;
            }
            growth.statUpgrades.push_back(std::move(upgrade));
        }
        // 재초기화(테스트/디버그)에도 안전하도록 기존 ctx 성장 콘텐츠를 덮어쓴다.
        registry.ctx().erase<FDungeonGrowth>();
        registry.ctx().emplace<FDungeonGrowth>(std::move(growth));
        // WP-10: 캠프 가격표와 회복 예산 블록. 정수 0–1000 루프 밖의 별도 검증이다(배열·실수 키).
        // 어느 하나라도 어긋나면 부팅 실패 — 수치를 추측한 캠프 경제로 시작하지 않는다.
        FCampTables camp;
        camp.actionsPerCamp = data.at("camp").at("actions_per_camp").get<int>();
        for (const auto& step : data.at("camp").at("upgrade_cost_steps")) {
            camp.upgradeCostSteps.push_back(step.get<int>());
        }
        for (const auto& step : data.at("camp").at("rune_offer_draw_cost_steps")) {
            camp.runeOfferDrawCostSteps.push_back(step.get<int>());
        }
        camp.runeOfferCampCap = data.at("camp").at("rune_offer_camp_cap").get<int>();
        camp.specialDiscount = data.at("camp").at("special_discount").get<int>();
        camp.respecCost = data.at("camp").at("respec_cost").get<int>();
        if (camp.actionsPerCamp < 0 || camp.runeOfferCampCap < 0 || camp.specialDiscount < 0 ||
            camp.respecCost < 0 || camp.upgradeCostSteps.empty() || camp.runeOfferDrawCostSteps.empty()) {
            LOG_ERROR("Dungeon camp block is invalid (negative counters, cap or an empty price curve).");
            return false;
        }
        for (const auto* steps : {&camp.upgradeCostSteps, &camp.runeOfferDrawCostSteps}) {
            int previous = 0;
            for (const int step : *steps) {
                if (step < 1 || step < previous) {
                    LOG_ERROR("Dungeon camp cost curves must be positive and non-decreasing.");
                    return false;
                }
                previous = step;
            }
        }
        FDungeonRecovery recovery;
        recovery.koRecoverFraction = data.at("recovery").at("ko_recover_fraction").get<double>();
        recovery.campAutoHeal = data.at("recovery").at("camp_auto_heal").get<int>();
        recovery.campHealCost = data.at("recovery").at("camp_heal_cost").get<int>();
        recovery.campHealPercent = data.at("recovery").at("camp_heal_percent").get<int>();
        recovery.eventChestHpCostFraction = data.at("recovery").at("event_chest_hp_cost_fraction").get<double>();
        recovery.freeHealCapFraction = data.at("recovery").at("free_heal_cap_fraction").get<double>();
        if (!(recovery.koRecoverFraction >= 0.0 && recovery.koRecoverFraction <= 1.0) ||
            !(recovery.eventChestHpCostFraction >= 0.0 && recovery.eventChestHpCostFraction <= 1.0) ||
            !(recovery.freeHealCapFraction >= 0.0 && recovery.freeHealCapFraction <= 1.0) ||
            recovery.campAutoHeal < 0 || recovery.campHealCost < 0 || recovery.campHealPercent < 0 ||
            recovery.campHealPercent > 100) {
            LOG_ERROR("Dungeon recovery block is invalid (fractions 0..1, heal percent 0..100).");
            return false;
        }
        registry.ctx().erase<FCampTables>();
        registry.ctx().emplace<FCampTables>(std::move(camp));
        registry.ctx().erase<FDungeonRecovery>();
        registry.ctx().emplace<FDungeonRecovery>(recovery);
        const auto& content = registry.ctx().get<gameplay::FContentRegistry>();
        const auto drops = data.find("rune_drops");
        if (drops == data.end() || !drops->is_array() || drops->empty()) {
            LOG_ERROR("Dungeon rune_drops must be a nonempty array of known rune IDs.");
            return false;
        }
        for (const auto& rune : *drops) {
            if (!rune.is_string() || !content.runes.find(rune.get<std::string>())) {
                LOG_ERROR("Dungeon rune_drops contains an invalid or unknown rune ID.");
                return false;
            }
        }
        std::set<std::string> partyIds;
        for (const auto& member : data.at("party")) {
            if (!content.units.find(member.at("unit").get<std::string>()) ||
                !partyIds.insert(member.at("id").get<std::string>()).second ||
                member.at("faction") != "party") {
                LOG_ERROR("Dungeon party needs unique IDs, known units and party faction.");
                return false;
            }
        }
        const auto config = ParseRoomGeneration(data, content);
        if (!config) {
            LOG_ERROR("Dungeon generation needs room names, known weighted enemies, a known boss and valid count ranges.");
            return false;
        }
        // WP-08: the room sequence (themes -> dungeons -> room slots) is the only source of room count,
        // boss placement and the cycle table. Parsed and validated here; installed before the probe reads it.
        std::string sequenceError;
        auto sequence = FRunSequence::Load(data, sequenceError);
        if (!sequence) {
            LOG_ERROR("Dungeon sequence invalid: {}", sequenceError);
            return false;
        }
        registry.ctx().erase<FRunSequence>();
        registry.ctx().emplace<FRunSequence>(std::move(*sequence));
        // WP-15: the optional bosses.json. Absent -> an empty catalog (every boss slot uses the single-phase
        // fallback); present but invalid stops the boot here, before the probe.
        if (!InstallBossCatalog(registry)) return false;
        const auto& rooms = registry.ctx().get<FRunSequence>();
        const auto* bossCatalog = FindBossCatalog(registry);
        // Probe one representative of every unique (family, boss, resolved boss id) tuple actually authored;
        // the room generation failure class is decided by (family, boss, pool, seed) — the ordinal only feeds
        // Mix. Cost is bounded by the tuple count, not the slot count (WP-08 §4).
        constexpr int kProbeSeeds = 3;
        FDungeonBootProbe probe;
        std::vector<std::tuple<std::string, std::string, std::string>> probedTuples;
        for (int index = 0; index < rooms.TotalDungeons(); ++index) {
            const auto position = rooms.PositionOf(index);
            const int battleCount = rooms.BattleCountAt(position);
            for (int ordinal = 0; ordinal < battleCount; ++ordinal) {
                const auto* slot = rooms.BattleSlotAt(position, ordinal);
                if (slot == nullptr) continue;
                // WP-08b: the family is per slot — copy the parsed config and set it (never mutate the
                // shared config; the probe walks several families). WP-15: the tuple key adds the resolved
                // boss id; ApplyBossSlot runs before BuildRoomDocument so a mapped boss (and its parts) is
                // probed through CreateBattlefield + SetupCombatUnits.
                auto tupleConfig = *config;
                tupleConfig.family = slot->family;
                const std::string bossId =
                    ApplyBossSlot(tupleConfig, bossCatalog, rooms.themes[position.theme].id, *slot,
                                  rooms.IsBossBattle(position, ordinal));
                const std::tuple<std::string, std::string, std::string> tuple{slot->family, slot->boss, bossId};
                if (std::ranges::find(probedTuples, tuple) != probedTuples.end()) continue;
                probedTuples.push_back(tuple);
                for (std::uint32_t seed = 1; seed <= kProbeSeeds; ++seed) {
                    entt::registry probeRegistry;
                    FScene scene(probeRegistry, "validation");
                    scene.Load();
                    const auto document =
                        BuildRoomDocument(data, data.at("party"), tupleConfig, seed, index + 1, ordinal, !slot->boss.empty());
                    const auto battlefield = FEntityFactory::CreateBattlefield(scene, document);
                    if (!battlefield) {
                        LOG_ERROR("Generated dungeon room invalid: {}", battlefield.error().message);
                        return false;
                    }
                    if (!SetupCombatUnits(probeRegistry, *battlefield, content)) return false;
                    ++probe.iterations;
                }
            }
        }
        probe.tuples = static_cast<int>(probedTuples.size());
        LOG_INFO("[room] probe tuples={} iterations={}", probe.tuples, probe.iterations);
        registry.ctx().erase<FDungeonBootProbe>();
        registry.ctx().emplace<FDungeonBootProbe>(probe);
    } catch (const nlohmann::json::exception& error) {
        LOG_ERROR("Dungeon content invalid: {}", error.what());
        return false;
    }
    // 재초기화(테스트/디버그)에도 안전하도록 덮어쓴다.
    registry.ctx().erase<FDungeonCheckpoint>();
    auto& checkpoint = registry.ctx().emplace<FDungeonCheckpoint>();
    // Callers that pick their own checkpoint file (tests) get a run save beside it; otherwise both live in the
    // preference directory, or in GAME01P_SAVE_DIR when that override is set.
    const bool defaultStorage = checkpointPath.empty();
    std::filesystem::path storageDirectory;
    bool prefPathFailed = false;
    if (defaultStorage) {
        if (const auto override = SaveDirOverride()) {
            // The override never falls back to the preference directory: that is the user's real save.
            std::error_code code;
            std::filesystem::create_directories(*override, code);
            if (code) {
                LOG_ERROR("GAME01P_SAVE_DIR '{}' could not be created: {}", override->string(), code.message());
            } else {
                storageDirectory = *override;
                LOG_INFO("save dir override: {}", storageDirectory.string());
            }
        } else if (const char* path = SDL_GetPrefPath("Game01P", "EmberDungeon")) {
            storageDirectory = path;
        } else {
            prefPathFailed = true;
        }
    }
    if (defaultStorage && !storageDirectory.empty()) {
        checkpointPath = (storageDirectory / "checkpoint.json").string();
    }
    if (savePath.empty()) {
        if (!defaultStorage) savePath = checkpointPath + ".run.json";
        else if (!storageDirectory.empty()) savePath = (storageDirectory / "run_save.json").string();
    }
    registry.ctx().erase<FDungeonSave>();
    registry.ctx().emplace<FDungeonSave>().path = std::move(savePath);
    RefreshDungeonSave(registry);
    if (!checkpointPath.empty()) {
        checkpoint.path = std::move(checkpointPath);
        std::ifstream file(checkpoint.path);
        if (file) {
            try {
                const auto saved = nlohmann::json::parse(file);
                if (saved.at("schema_version") != 1 || !saved.at("cleared").is_number_integer() ||
                    saved.at("cleared").get<int>() < 0 || saved.at("cleared").get<int>() > 999) {
                    checkpoint.error = "Invalid checkpoint. Starting at dungeon 1.";
                } else {
                    checkpoint.cleared = saved.at("cleared").get<int>();
                }
            } catch (const nlohmann::json::exception& error) {
                checkpoint.error = "Unreadable checkpoint. Starting at dungeon 1.";
                LOG_ERROR("Checkpoint parse failed: {}", error.what());
            }
        }
    } else {
        checkpoint.error = "Checkpoint storage unavailable.";
        if (prefPathFailed) LOG_ERROR("SDL_GetPrefPath: {}", SDL_GetError());
    }
    if (!checkpoint.error.empty()) { LOG_WARN("{}", checkpoint.error); }
    registry.ctx().erase<FMenuRequest>();
    registry.ctx().emplace<FMenuRequest>();
    return true;
}

void InstallDungeonRun(entt::registry& registry, std::uint32_t seed, bool fresh) {
    InstallDungeonRunAt(registry, seed, fresh ? 0 : registry.ctx().get<FDungeonCheckpoint>().cleared);
}

void InstallDungeonRunAt(entt::registry& registry, std::uint32_t seed, int clearedIndex) {
    // D-21: a run can start at any dungeon up to the checkpoint; the checkpoint itself never goes down.
    const auto& checkpoint = registry.ctx().get<FDungeonCheckpoint>();
    const int index = std::clamp(clearedIndex, 0, std::max(0, checkpoint.cleared));
    registry.ctx().erase<FDungeonRun>();
    auto& run = registry.ctx().emplace<FDungeonRun>();
    run.seed = seed == 0 ? 1 : seed;
    run.depth = index + 1;
    run.cleared = index;
    // WP-08/D-7: the start-support materials scale with the start index (min(index x per, cap)); a fresh
    // run (index 0) starts empty. The rune-offer half of the support lives in StartRunAt (it writes the
    // rewards module after its reset).
    if (const auto* sequence = FindRunSequence(registry)) {
        run.materials = sequence->StartMaterials(index);
    }
    // [WP-21b] The preparation screen's setup request is consumed exactly once — also when it is unusable
    // (validation lives in StartDungeonRunFromSetup, never here). Without a request the authored dungeon
    // party installs exactly as before.
    if (registry.ctx().contains<FRunSetupRequest>()) {
        FRunSetup setup = std::move(registry.ctx().get<FRunSetupRequest>().setup);
        registry.ctx().erase<FRunSetupRequest>();
        for (std::size_t i = 0; i < setup.party.size(); ++i) {
            FRunMember runMember;
            runMember.id = setup.party[i];
            runMember.displayName = SetupMemberDisplayName(registry, setup.party[i]);
            if (i < setup.skills.size()) {
                for (const std::string& skillId : setup.skills[i]) {
                    runMember.skills.push_back(FSkillSlot{skillId, {}});
                }
            }
            run.party.push_back(std::move(runMember));
        }
    } else {
        for (const auto& member : DungeonDefinition(registry).at("party")) {
            FRunMember runMember;
            runMember.id = member.at("id").get<std::string>();
            runMember.displayName = member.at("display_name").get<std::string>();
            run.party.push_back(std::move(runMember));
        }
    }
    // A new run starts with a clean preserved-section slate and its own event log.
    ClearPreserved(registry);
    registry.ctx().erase<FRunEventLog>();
    registry.ctx().emplace<FRunEventLog>();
    // WP-10: a new run starts with the camp counters at zero. Continue does NOT pass through here — it
    // unpacks `run.ext.camp` before installing the run, so the saved curve indices survive.
    registry.ctx().erase<FCampProgress>();
    registry.ctx().emplace<FCampProgress>();
    // WP-19a: the run log starts here (before the run's first save, so the save section carries its id).
    BeginRunLog(registry);
}

void StartDungeonRun(entt::registry& registry, std::uint32_t seed, bool fresh) {
    StartRun(registry, seed, fresh);
}

bool SaveDungeonRun(entt::registry& registry) {
    auto* save = registry.ctx().find<FDungeonSave>();
    const auto* run = registry.ctx().find<FDungeonRun>();
    if (save == nullptr || save->path.empty() || run == nullptr || run->phase == EDungeonPhase::Defeat) return false;
    auto document = DungeonRunToJson(*run);
    // Section pack failures are a whole-save failure: the previous file stays and the run stays.
    if (const auto packed = PackSections(registry, document); !packed) {
        save->error = "Could not save the expedition (section data failed).";
        LOG_ERROR("Expedition save aborted (sections): {}", packed.error());
        return false;
    }
    std::string reason;
    if (!WriteSaveFile(save->path, document, reason)) {
        save->error = "Could not save the expedition.";
        LOG_ERROR("{}", reason);
        return false;
    }
    save->exists = true;
    save->error.clear();
    save->depth = run->depth;
    save->room = run->room;
    save->phase = run->phase;
    save->savedAt = document.at("saved_at").get<std::string>();
    return true;
}

void DeleteDungeonSave(entt::registry& registry) {
    auto* save = registry.ctx().find<FDungeonSave>();
    if (save == nullptr) return;
    std::error_code code;
    if (!save->path.empty()) RemoveSaveFile(save->path, code);
    if (code) {
        save->error = "Could not delete the saved expedition.";
        LOG_ERROR("Could not delete '{}': {}", save->path, code.message());
        return;
    }
    save->exists = false;
    save->error.clear();
}

bool ContinueDungeonRun(entt::registry& registry) {
    auto* save = registry.ctx().find<FDungeonSave>();
    if (save == nullptr || save->path.empty()) return false;
    std::string savedAt;
    auto run = LoadAndUnpackSavedRun(registry, savedAt);
    if (!run) {
        // The slot is no use to anyone now; the menu stops offering it and says why. The current run is untouched.
        save->exists = false;
        save->error = kUnreadableSave;
        LOG_WARN("Continue failed for '{}': {}", save->path, run.error());
        return false;
    }
    registry.ctx().erase<FDungeonRun>();
    registry.ctx().emplace<FDungeonRun>(std::move(*run));
    registry.ctx().erase<FRunEventLog>();
    registry.ctx().emplace<FRunEventLog>();
    // WP-08: a phase=event save whose gap no longer has an event slot advances past it (never replays the
    // cleared battle) — the rule lives in FRunFlow.cpp, the run phase machine's single writer.
    NormalizeResumedRunPhase(registry);
    save->error.clear();
    return true;
}

nlohmann::json BuildDungeonBattlefield(entt::registry& registry) {
    auto& run = registry.ctx().get<FDungeonRun>();
    const auto& data = DungeonDefinition(registry);
    const auto& content = registry.ctx().get<gameplay::FContentRegistry>();
    // Initialization already proved the block valid; a failure here means content changed underneath us.
    const auto config = ParseRoomGeneration(data, content);
    if (!config) return nlohmann::json::object();
    // WP-08: the boss flag comes from the sequence slot of the current battle ordinal — never a room index.
    // WP-08b: the template family comes from the same slot (per-slot by copying the parsed config).
    const FRunRoomSlot* slot = nullptr;
    std::string themeId;
    bool bossBattle = false;  // WP-15c: FRunSequence::IsBossBattle is the single boss predicate
    if (const auto* sequence = FindRunSequence(registry)) {
        const auto position = sequence->PositionOf(run.depth - 1);
        slot = sequence->BattleSlotAt(position, run.room);
        bossBattle = sequence->IsBossBattle(position, run.room);
        // The (theme, slot) key of the WP-15 boss mapping; PositionOf clamps to a valid theme.
        if (position.theme < sequence->themes.size()) themeId = sequence->themes[position.theme].id;
    }
    auto slotConfig = *config;
    if (slot != nullptr) slotConfig.family = slot->family;
    // WP-15: resolve the (theme, slot) mapping into the document and mark the battle in ctx — for every
    // build, the saved-phase continue included. "" = a non-boss slot (no marker).
    const std::string bossId =
        slot != nullptr ? ApplyBossSlot(slotConfig, FindBossCatalog(registry), themeId, *slot, bossBattle)
                        : std::string{};
    if (!bossId.empty()) {
        registry.ctx().insert_or_assign<FBossBattle>(FBossBattle{bossId, slot->boss});
    } else {
        registry.ctx().erase<FBossBattle>();
    }
    gameplay::FGeneratedRoom generated;
    auto document = BuildRoomDocument(data, BattlefieldPartyRows(registry, run, data), slotConfig, run.seed,
                                      run.depth, run.room, slot != nullptr && !slot->boss.empty(), &generated);
    run.roomName = document.at("name").get<std::string>();
    // WP-19a: a saved-phase continue (Reward/Event/Camp/Defeat) also builds the field but must not record a
    // battle that will not happen; only a real Battle phase writes dungeon_enter/room_start. The emergent
    // room metric (WP-08b) logs on the same real-battle condition — never from the boot probe.
    if (run.phase == EDungeonPhase::Battle) {
        LogDungeonEnter(registry);
        LogRoomStart(registry);
        const auto metrics = gameplay::MeasureRoom(generated, PartyCells(data.at("party")));
        const auto strength = gameplay::FamilyStrengthOf(slotConfig.family);
        LOG_INFO("[room] family={} strength={} walls={} reachable={:.2f} min_path={} cover={} traps={}",
                 slotConfig.family, strength ? gameplay::FamilyStrengthName(*strength) : "unknown", metrics.walls,
                 metrics.reachableRatio, metrics.minPath, metrics.coverCells, metrics.traps);
        // WP-15: one line per real boss battle; parts = the mapped part rows' summed count (0 = fallback).
        if (bossBattle && slot != nullptr) {
            int partCount = 0;
            for (const gameplay::FBossPartSpawn& part : slotConfig.bossParts) partCount += part.count;
            LOG_INFO("[boss] slot={} boss={} parts={}", slot->boss, bossId, partCount);
        }
    }
    return document;
}

void ApplyRunParty(entt::registry& registry) {
    // WP-03: 런 보너스는 제자리 가산이 아니라 RefreshCombatSheet의 파생 계산이 소유한다(멱등 —
    // 호출마다 값이 부푸는 결함 제거). 순서가 계약이다: 갱신(갱신 후 max) → ApplyBattleStart(HP/로드아웃).
    if (const auto* content = registry.ctx().find<gameplay::FContentRegistry>()) {
        RefreshAllCombatSheets(registry, *content);
    }
    ApplyBattleStart(registry);
}

void CaptureRunParty(entt::registry& registry) {
    auto& run = registry.ctx().get<FDungeonRun>();
    // WP-10 / D-18: the KO floor is data (`recovery.ko_recover_fraction`), max(1, floor(maxHp × fraction)).
    const double fraction = DungeonRecovery(registry).koRecoverFraction;
    for (const auto entity : registry.view<FPartyMember, FUnitData, FHealth, FSkillLoadout>()) {
        const auto& id = registry.get<FUnitData>(entity).definition.id;
        auto member = std::ranges::find(run.party, id, &FRunMember::id);
        if (member == run.party.end()) continue;
        const auto& health = registry.get<FHealth>(entity);
        member->health = std::max(health.current, KoRecoverFloor(health.max, fraction));
        member->skills = registry.get<FSkillLoadout>(entity).slots;
    }
}

// Legacy wrappers (WP-02a): the call sites stay in tests/DungeonTests.cpp, FEmberBattleState and
// FDungeonOverlay; moving them is a later separate WP (WP-08 keeps signature, name and body). The bodies
// live in FRunFlow; these keep the signatures and log the failure the old void functions could not report.
void CompleteDungeonBattle(entt::registry& registry, bool victory) {
    if (const auto result = ResolveRoomVictory(registry, victory); !result) {
        LOG_ERROR("Room victory could not be resolved: {}", result.error());
    }
}

void ContinueDungeonReward(entt::registry& registry) {
    if (const auto result = ApplyRunIntent(registry, ERunIntent::ContinueReward); !result) {
        LOG_ERROR("Reward step could not be continued: {}", result.error());
    }
}

void ChooseDungeonEvent(entt::registry& registry, bool rest) {
    if (const auto result = ApplyRunIntent(registry, rest ? ERunIntent::ChooseEventRest : ERunIntent::ChooseEventLoot);
        !result) {
        LOG_ERROR("Event choice could not be applied: {}", result.error());
    }
}

void LeaveDungeonCamp(entt::registry& registry) {
    if (const auto result = ApplyRunIntent(registry, ERunIntent::LeaveCamp); !result) {
        LOG_ERROR("Camp could not be left: {}", result.error());
    }
}

int FDungeonGrowth::ExperienceForNextLevel(int level) const {
    return std::max(1, levelExperienceBase + levelExperienceStep * std::max(0, level - 1));
}

bool CanAllocateDungeonNode(const entt::registry& registry, std::size_t memberIndex, std::string_view nodeId) {
    const auto* run = registry.ctx().find<FDungeonRun>();
    const auto* growth = registry.ctx().find<FDungeonGrowth>();
    if (run == nullptr || growth == nullptr) return false;
    if (run->phase == EDungeonPhase::Battle || run->phase == EDungeonPhase::Defeat) return false;
    if (memberIndex >= run->party.size()) return false;
    const FRunMember& member = run->party[memberIndex];
    if (member.passivePoints <= 0) return false;
    return growth->passiveTree.CanAllocate(member.allocatedNodes, nodeId);
}

bool AllocateDungeonNode(entt::registry& registry, std::size_t memberIndex, std::string_view nodeId) {
    if (!CanAllocateDungeonNode(registry, memberIndex, nodeId)) return false;
    auto& member = registry.ctx().get<FDungeonRun>().party[memberIndex];
    member.allocatedNodes.emplace_back(nodeId);
    --member.passivePoints;
    SaveDungeonRun(registry);
    return true;
}

std::optional<std::string> ValidateDungeonStatUpgrade(
    const entt::registry& registry, std::size_t memberIndex, std::size_t upgradeIndex) {
    const auto* run = registry.ctx().find<FDungeonRun>();
    const auto* growth = registry.ctx().find<FDungeonGrowth>();
    if (run == nullptr || growth == nullptr) return "Run state is unavailable.";
    const auto* camp = registry.ctx().find<FCampTables>();
    const auto* progress = registry.ctx().find<FCampProgress>();
    if (camp == nullptr || progress == nullptr) return "Camp content is unavailable.";
    if (run->phase != EDungeonPhase::Camp) return "Stats can be reinforced at camp only.";
    if (memberIndex >= run->party.size()) return "Unknown party member.";
    if (upgradeIndex >= growth->statUpgrades.size()) return "Unknown stat upgrade.";
    const FStatUpgrade& upgrade = growth->statUpgrades[upgradeIndex];
    // The price is the run-cumulative curve, never a per-row constant (WP-10 §2).
    if (run->materials < UpgradeCostOf(*camp, *progress)) return "Not enough reinforcement materials.";
    if (DungeonMemberStatBonus(run->party[memberIndex], upgrade.stat) + upgrade.amount > upgrade.cap) {
        return "This stat is at its run cap.";
    }
    return std::nullopt;
}

std::optional<std::string> UpgradeDungeonStat(
    entt::registry& registry, std::size_t memberIndex, std::size_t upgradeIndex) {
    if (const auto problem = ValidateDungeonStatUpgrade(registry, memberIndex, upgradeIndex)) return problem;
    auto& run = registry.ctx().get<FDungeonRun>();
    auto& progress = DungeonCampProgress(registry);
    const int cost = UpgradeCostOf(DungeonCampTables(registry), progress);
    const FStatUpgrade upgrade = registry.ctx().get<FDungeonGrowth>().statUpgrades[upgradeIndex];
    FRunMember& member = run.party[memberIndex];
    if (upgrade.stat == "health") {
        member.bonusHealth += upgrade.amount;
        if (member.health >= 0) member.health += upgrade.amount;  // 최대 HP가 늘면 그만큼 채운다
    } else if (upgrade.stat == "attack") member.bonusAttack += upgrade.amount;
    else if (upgrade.stat == "magic_attack") member.bonusMagic += upgrade.amount;
    else if (upgrade.stat == "defense") member.bonusDefense += upgrade.amount;
    else if (upgrade.stat == "magic_defense") member.bonusMagicDefense += upgrade.amount;
    else if (upgrade.stat == "speed") member.bonusSpeed += upgrade.amount;
    run.materials -= cost;
    // The curve index is a stored counter, not a derivation from bonus*/amount: reward/event effects may
    // move those fields and a derived index would silently drift (WP-10 §1).
    progress.upgradesTotal += 1;
    // WP-19a: one line per purchase. The id is the contract name `upgrade.<stat>`; the member index rides
    // along (0..3, -1 = party-wide/unknown).
    AppendRunEvent(
        registry, ERunEventKind::CampSpent, "upgrade." + upgrade.stat, cost, static_cast<int>(memberIndex));
    SaveDungeonRun(registry);
    return std::nullopt;
}

std::optional<std::string> ValidateDungeonCampHeal(const entt::registry& registry) {
    const auto* run = registry.ctx().find<FDungeonRun>();
    if (run == nullptr) return "Run state is unavailable.";
    if (run->phase != EDungeonPhase::Camp) return "The party can be healed at camp only.";
    const auto& recovery = DungeonRecovery(registry);
    if (run->materials < recovery.campHealCost) return "Not enough reinforcement materials.";
    bool anyHealable = false;
    bool anyBelowMax = false;
    for (const auto& member : run->party) {
        if (member.health < 0) continue;  // -1 = unclaimed = full: the sentinel never needs healing
        const int maxHealth = DungeonMemberMaxHealth(registry, member);
        if (maxHealth <= 0) continue;  // unknown maximum: cannot prove it needs healing (logged by the helper)
        const int amount = maxHealth * recovery.campHealPercent / 100;
        if (amount <= 0) continue;  // percent 0: the purchase would buy nothing
        anyHealable = true;
        if (member.health < maxHealth) {
            anyBelowMax = true;
            break;
        }
    }
    if (!anyHealable) return "The camp heal would restore nothing.";
    if (!anyBelowMax) return "The party is already at full health.";
    return std::nullopt;
}

std::optional<std::string> PayDungeonCampHeal(entt::registry& registry) {
    if (const auto problem = ValidateDungeonCampHeal(registry)) return problem;
    auto& run = registry.ctx().get<FDungeonRun>();
    const auto& recovery = DungeonRecovery(registry);
    for (auto& member : run.party) {
        if (member.health < 0) continue;  // -1 = unclaimed = already full: the sentinel skips paid healing too
        const int maxHealth = DungeonMemberMaxHealth(registry, member);
        if (maxHealth <= 0) continue;  // unknown maximum: skip (logged once), never clamp to 0
        const int amount = maxHealth * recovery.campHealPercent / 100;  // the data rule, no code floor
        member.health = std::min(member.health + amount, maxHealth);
    }
    run.materials -= recovery.campHealCost;
    AppendRunEvent(registry, ERunEventKind::CampSpent, "heal", recovery.campHealCost, -1);
    SaveDungeonRun(registry);
    return std::nullopt;
}

std::optional<std::string> ValidateDungeonRespec(const entt::registry& registry) {
    const auto* run = registry.ctx().find<FDungeonRun>();
    const auto* camp = registry.ctx().find<FCampTables>();
    if (run == nullptr || camp == nullptr) return "Run state is unavailable.";
    if (run->phase != EDungeonPhase::Camp) return "Nodes can be reallocated at camp only.";
    if (run->materials < camp->respecCost) return "Not enough reinforcement materials.";
    bool anyAllocated = false;
    for (const auto& member : run->party) {
        if (!member.allocatedNodes.empty()) {
            anyAllocated = true;
            break;
        }
    }
    if (!anyAllocated) return "No allocated passive nodes to reallocate.";
    return std::nullopt;
}

std::optional<std::string> RespecDungeonNodes(entt::registry& registry) {
    if (const auto problem = ValidateDungeonRespec(registry)) return problem;
    auto& run = registry.ctx().get<FDungeonRun>();
    const int cost = DungeonCampTables(registry).respecCost;
    for (auto& member : run.party) {
        // Only nodes move: bought stat bonuses and the upgrade curve counter are never refunded (WP-10 §6).
        member.passivePoints += static_cast<int>(member.allocatedNodes.size());
        member.allocatedNodes.clear();
        // Clearing a node can lower the run max HP: clamp, or a full-HP member would sit above the new
        // derivation and break the §4 displayed-equals-applied invariant.
        const int maxHealth = DungeonMemberMaxHealth(registry, member);
        if (member.health >= 0 && maxHealth > 0) member.health = std::min(member.health, maxHealth);
    }
    run.materials -= cost;
    AppendRunEvent(registry, ERunEventKind::CampSpent, "respec", cost, -1);
    SaveDungeonRun(registry);
    return std::nullopt;
}

std::optional<std::string> ValidateDungeonRuneEquip(
    const entt::registry& registry, std::size_t memberIndex, std::size_t slotIndex, std::size_t runeIndex) {
    const auto* run = registry.ctx().find<FDungeonRun>();
    if (const auto error = ValidateCampSlot(run, memberIndex, slotIndex)) return error;
    if (runeIndex >= run->runes.size()) return "Unknown collected rune.";
    const auto* content = registry.ctx().find<gameplay::FContentRegistry>();
    if (!content) return "Gameplay content is unavailable.";
    auto candidate = run->party[memberIndex].skills[slotIndex];
    candidate.runeIds.push_back(run->runes[runeIndex]);
    return ValidateSkillSlot(candidate, *content);
}

std::optional<std::string> EquipDungeonRune(
    entt::registry& registry, std::size_t memberIndex, std::size_t slotIndex, std::size_t runeIndex) {
    if (const auto error = ValidateDungeonRuneEquip(registry, memberIndex, slotIndex, runeIndex)) return error;
    auto& run = registry.ctx().get<FDungeonRun>();
    const std::string runeId = run.runes[runeIndex];  // WP-27: logged below; capture before the erase
    run.party[memberIndex].skills[slotIndex].runeIds.push_back(runeId);
    run.runes.erase(run.runes.begin() + static_cast<std::ptrdiff_t>(runeIndex));
    // WP-27: one `loadout` line per successful edit (camp equips log "camp" through the same call).
    LogRunEvent(registry, nlohmann::json{{"t", "loadout"},
                                         {"depth", run.depth},
                                         {"room", run.room},
                                         {"phase", PhaseId(run.phase)},
                                         {"member", memberIndex},
                                         {"slot", slotIndex},
                                         {"rune", runeId},
                                         {"action", "equip"}});
    SaveDungeonRun(registry);
    return std::nullopt;
}

std::optional<std::string> RemoveDungeonRune(
    entt::registry& registry, std::size_t memberIndex, std::size_t slotIndex, std::size_t runeIndex) {
    auto* run = registry.ctx().find<FDungeonRun>();
    if (const auto error = ValidateCampSlot(run, memberIndex, slotIndex)) return error;
    // WP-27: removal is rule-allowed in Reward too, but the UI only exposes it at camp (the reward modal
    // fits the picked rune; unslotting is a camp service).
    auto& slot = run->party[memberIndex].skills[slotIndex];
    if (runeIndex >= slot.runeIds.size()) return "Unknown fitted rune.";
    const auto* content = registry.ctx().find<gameplay::FContentRegistry>();
    if (!content) return "Gameplay content is unavailable.";
    auto candidate = slot;
    candidate.runeIds.erase(candidate.runeIds.begin() + static_cast<std::ptrdiff_t>(runeIndex));
    if (const auto error = ValidateSkillSlot(candidate, *content)) return error;
    const std::string runeId = slot.runeIds[runeIndex];  // WP-27: logged below; capture before the move
    run->runes.push_back(runeId);
    slot = std::move(candidate);
    LogRunEvent(registry, nlohmann::json{{"t", "loadout"},
                                         {"depth", run->depth},
                                         {"room", run->room},
                                         {"phase", PhaseId(run->phase)},
                                         {"member", memberIndex},
                                         {"slot", slotIndex},
                                         {"rune", runeId},
                                         {"action", "remove"}});
    SaveDungeonRun(registry);
    return std::nullopt;
}

} // namespace game
