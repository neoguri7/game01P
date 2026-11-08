#include "gameplay/dungeon/FDungeonRun.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <set>
#include <SDL3/SDL.h>

#include "core/AssetManager.h"
#include "core/Logger.h"
#include "ecs/components/FDead.h"
#include "ecs/components/FFaction.h"
#include "ecs/components/FHealth.h"
#include "ecs/components/FSpeed.h"
#include "ecs/components/FStats.h"
#include "gameplay/combat/FCombatSheet.h"
#include "gameplay/combat/FSkillResolver.h"
#include "gameplay/data/FContentRegistry.h"
#include "gameplay/dungeon/FDungeonSave.h"
#include "gameplay/dungeon/FRunEventLog.h"
#include "gameplay/dungeon/FRunFlow.h"
#include "gameplay/dungeon/FRunLog.h"
#include "gameplay/dungeon/FSaveSections.h"
#include "gameplay/dungeon/FRoomGenerator.h"
#include "gameplay/factory/FEntityDefinitions.h"
#include "gameplay/factory/FEntityFactory.h"
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
    for (const auto& member : DungeonDefinition(registry).at("party")) rules.partyIds.push_back(member.at("id").get<std::string>());
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

std::optional<std::string> ValidateCampSlot(
    const FDungeonRun* run, std::size_t memberIndex, std::size_t slotIndex) {
    if (!run || run->phase != EDungeonPhase::Camp) return "Runes can only be changed at camp.";
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

bool IsKnownStatUpgrade(std::string_view stat) {
    return stat == "health" || stat == "attack" || stat == "magic_attack" || stat == "defense"
        || stat == "magic_defense" || stat == "speed";
}

std::vector<std::pair<int, int>> PartyCells(const nlohmann::json& party) {
    std::vector<std::pair<int, int>> cells;
    for (const auto& member : party) cells.emplace_back(member.at("grid_x").get<int>(), member.at("grid_y").get<int>());
    return cells;
}

// Fills the battlefield fields that every dungeon room shares.
void FinishBattlefield(nlohmann::json& document, const nlohmann::json& data) {
    document["width"] = kGridWidth; document["height"] = kGridHeight; document["tile_size"] = 64;
    document["origin_x"] = 0; document["origin_y"] = 0;
    document["move_range"] = data["balance"]["move_range"];
    document["trap_damage"] = data["balance"]["trap_damage"];
}

nlohmann::json BuildRoomDocument(const nlohmann::json& data, const gameplay::FRoomGenConfig& config,
                                 std::uint32_t seed, int depth, int room) {
    const auto cells = PartyCells(data.at("party"));
    const auto generated = gameplay::GenerateRoom(config, {seed, depth, room, room == 2, cells});
    nlohmann::json document;
    document["name"] = generated.name;
    document["terrain"] = generated.terrain;
    document["units"] = data.at("party");
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

bool InitializeDungeon(entt::registry& registry, std::string checkpointPath, std::string savePath) {
    const auto asset = registry.ctx().get<FAssetManager>().digest("data/dungeon");
    if (!asset) return false;
    const auto& data = asset->json();
    try {
        if (!data.at("schema_version").is_number_integer() || data.at("schema_version") != 1 ||
            !data.at("party").is_array() || data.at("party").size() != 4 ||
            !data.at("generation").is_object()) {
            LOG_ERROR("Dungeon needs schema version 1, four party members and a room generation block.");
            return false;
        }
        for (const char* key : {"move_range", "trap_damage", "room_heal", "event_heal", "event_materials",
                               "camp_heal", "room_materials", "room_experience",
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
            upgrade.cost = row.at("cost").get<int>();
            upgrade.cap = row.at("cap").get<int>();
            if (upgrade.id.empty() || upgrade.displayName.empty() || !IsKnownStatUpgrade(upgrade.stat)
                || upgrade.amount < 1 || upgrade.amount > 100 || upgrade.cost < 0 || upgrade.cost > 100
                || upgrade.cap < upgrade.amount || upgrade.cap > 100 || !upgradeIds.insert(upgrade.id).second) {
                LOG_ERROR("Dungeon stat upgrade '{}' is invalid.", upgrade.id);
                return false;
            }
            growth.statUpgrades.push_back(std::move(upgrade));
        }
        // 재초기화(테스트/디버그)에도 안전하도록 기존 ctx 성장 콘텐츠를 덮어쓴다.
        registry.ctx().erase<FDungeonGrowth>();
        registry.ctx().emplace<FDungeonGrowth>(std::move(growth));
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
        // Probe a spread of generated rooms so bad content stops the boot instead of a later battle.
        for (std::uint32_t seed = 1; seed <= 6; ++seed) {
            for (int room = 0; room < 3; ++room) {
                entt::registry probe;
                FScene scene(probe, "validation");
                scene.Load();
                const auto document = BuildRoomDocument(data, *config, seed, 1, room);
                const auto battlefield = FEntityFactory::CreateBattlefield(scene, document);
                if (!battlefield) {
                    LOG_ERROR("Generated dungeon room invalid: {}", battlefield.error().message);
                    return false;
                }
                if (!SetupCombatUnits(probe, *battlefield, content)) return false;
            }
        }
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
    registry.ctx().erase<FDungeonRun>();
    auto& run = registry.ctx().emplace<FDungeonRun>();
    run.seed = seed == 0 ? 1 : seed;
    run.depth = fresh ? 1 : registry.ctx().get<FDungeonCheckpoint>().cleared + 1;
    run.cleared = run.depth - 1;
    for (const auto& member : DungeonDefinition(registry).at("party")) {
        FRunMember runMember;
        runMember.id = member.at("id").get<std::string>();
        runMember.displayName = member.at("display_name").get<std::string>();
        run.party.push_back(std::move(runMember));
    }
    // A new run starts with a clean preserved-section slate and its own event log.
    ClearPreserved(registry);
    registry.ctx().erase<FRunEventLog>();
    registry.ctx().emplace<FRunEventLog>();
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
    if (!save->path.empty()) std::filesystem::remove(save->path, code);
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
    auto document = BuildRoomDocument(data, *config, run.seed, run.depth, run.room);
    run.roomName = document.at("name").get<std::string>();
    // WP-19a: a saved-phase continue (Reward/Event/Camp/Defeat) also builds the field but must not record a
    // battle that will not happen; only a real Battle phase writes dungeon_enter/room_start.
    if (run.phase == EDungeonPhase::Battle) {
        LogDungeonEnter(registry);
        LogRoomStart(registry);
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
    for (const auto entity : registry.view<FPartyMember, FUnitData, FHealth, FSkillLoadout>()) {
        const auto& id = registry.get<FUnitData>(entity).definition.id;
        auto member = std::ranges::find(run.party, id, &FRunMember::id);
        if (member == run.party.end()) continue;
        const auto& health = registry.get<FHealth>(entity);
        member->health = std::max(health.current, health.max / 3);
        member->skills = registry.get<FSkillLoadout>(entity).slots;
    }
}

// Legacy wrappers (WP-02a): the call sites stay in tests/DungeonTests.cpp, FEmberBattleState and
// FDungeonOverlay until WP-08 moves them. The bodies live in FRunFlow; these keep the signatures and
// log the failure the old void functions could not report.
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
    if (run->phase != EDungeonPhase::Camp) return "Stats can be reinforced at camp only.";
    if (memberIndex >= run->party.size()) return "Unknown party member.";
    if (upgradeIndex >= growth->statUpgrades.size()) return "Unknown stat upgrade.";
    const FStatUpgrade& upgrade = growth->statUpgrades[upgradeIndex];
    if (run->materials < upgrade.cost) return "Not enough reinforcement materials.";
    if (DungeonMemberStatBonus(run->party[memberIndex], upgrade.stat) + upgrade.amount > upgrade.cap) {
        return "This stat is at its run cap.";
    }
    return std::nullopt;
}

std::optional<std::string> UpgradeDungeonStat(
    entt::registry& registry, std::size_t memberIndex, std::size_t upgradeIndex) {
    if (const auto problem = ValidateDungeonStatUpgrade(registry, memberIndex, upgradeIndex)) return problem;
    auto& run = registry.ctx().get<FDungeonRun>();
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
    run.materials -= upgrade.cost;
    // WP-19a: one line per purchase (the member index rides along; -1 = party-wide/unknown).
    AppendRunEvent(
        registry, ERunEventKind::CampSpent, upgrade.id, upgrade.cost, static_cast<int>(memberIndex));
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
    run.party[memberIndex].skills[slotIndex].runeIds.push_back(run.runes[runeIndex]);
    run.runes.erase(run.runes.begin() + static_cast<std::ptrdiff_t>(runeIndex));
    SaveDungeonRun(registry);
    return std::nullopt;
}

std::optional<std::string> RemoveDungeonRune(
    entt::registry& registry, std::size_t memberIndex, std::size_t slotIndex, std::size_t runeIndex) {
    auto* run = registry.ctx().find<FDungeonRun>();
    if (const auto error = ValidateCampSlot(run, memberIndex, slotIndex)) return error;
    auto& slot = run->party[memberIndex].skills[slotIndex];
    if (runeIndex >= slot.runeIds.size()) return "Unknown fitted rune.";
    const auto* content = registry.ctx().find<gameplay::FContentRegistry>();
    if (!content) return "Gameplay content is unavailable.";
    auto candidate = slot;
    candidate.runeIds.erase(candidate.runeIds.begin() + static_cast<std::ptrdiff_t>(runeIndex));
    if (const auto error = ValidateSkillSlot(candidate, *content)) return error;
    run->runes.push_back(slot.runeIds[runeIndex]);
    slot = std::move(candidate);
    SaveDungeonRun(registry);
    return std::nullopt;
}

} // namespace game
