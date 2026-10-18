#include "gameplay/dungeon/FDungeonRun.h"

#include <algorithm>
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
#include "gameplay/combat/FSkillResolver.h"
#include "gameplay/data/FContentRegistry.h"
#include "gameplay/dungeon/FRoomGenerator.h"
#include "gameplay/factory/FEntityDefinitions.h"
#include "gameplay/factory/FEntityFactory.h"
#include "gameplay/battle/FBattleSetup.h"

namespace game {
namespace {

const nlohmann::json& Definition(entt::registry& registry) {
    return registry.ctx().get<FAssetManager>().digest("data/dungeon")->json();
}

bool SaveCheckpoint(entt::registry& registry) {
    auto& checkpoint = registry.ctx().get<FDungeonCheckpoint>();
    if (checkpoint.path.empty()) return false;
    std::ofstream file(checkpoint.path, std::ios::trunc);
    file << nlohmann::json{{"schema_version", 1}, {"cleared", checkpoint.cleared}}.dump(2);
    file.flush();
    if (!file) {
        checkpoint.error = "Could not save dungeon checkpoint.";
        LOG_ERROR("{}", checkpoint.error);
        return false;
    }
    checkpoint.error.clear();
    return true;
}

void RestoreParty(entt::registry& registry, int amount) {
    auto& run = registry.ctx().get<FDungeonRun>();
    for (auto& member : run.party) {
        member.health += amount;
    }
}

void Advance(entt::registry& registry) {
    auto& run = registry.ctx().get<FDungeonRun>();
    run.phase = EDungeonPhase::Battle;
    run.advanceRequested = true;
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

// Fills the 8x8 battlefield fields that every dungeon room shares.
void FinishBattlefield(nlohmann::json& document, const nlohmann::json& data) {
    document["width"] = 8; document["height"] = 8; document["tile_size"] = 64;
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

} // namespace

int DungeonBalance(entt::registry& registry, const char* key) {
    return Definition(registry).at("balance").at(key).get<int>();
}

bool InitializeDungeon(entt::registry& registry, std::string checkpointPath) {
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
    if (checkpointPath.empty()) {
        if (const char* path = SDL_GetPrefPath("Game01P", "EmberDungeon")) {
            checkpointPath = std::string(path) + "checkpoint.json";
        }
    }
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
        LOG_ERROR("SDL_GetPrefPath: {}", SDL_GetError());
    }
    if (!checkpoint.error.empty()) { LOG_WARN("{}", checkpoint.error); }
    registry.ctx().erase<FMenuRequest>();
    registry.ctx().emplace<FMenuRequest>();
    return true;
}

void StartDungeonRun(entt::registry& registry, std::uint32_t seed, bool fresh) {
    registry.ctx().erase<FDungeonRun>();
    auto& run = registry.ctx().emplace<FDungeonRun>();
    run.seed = seed == 0 ? 1 : seed;
    run.depth = fresh ? 1 : registry.ctx().get<FDungeonCheckpoint>().cleared + 1;
    run.cleared = run.depth - 1;
    for (const auto& member : Definition(registry).at("party")) {
        FRunMember runMember;
        runMember.id = member.at("id").get<std::string>();
        runMember.displayName = member.at("display_name").get<std::string>();
        run.party.push_back(std::move(runMember));
    }
}

nlohmann::json BuildDungeonBattlefield(entt::registry& registry) {
    auto& run = registry.ctx().get<FDungeonRun>();
    const auto& data = Definition(registry);
    const auto& content = registry.ctx().get<gameplay::FContentRegistry>();
    // Initialization already proved the block valid; a failure here means content changed underneath us.
    const auto config = ParseRoomGeneration(data, content);
    if (!config) return nlohmann::json::object();
    auto document = BuildRoomDocument(data, *config, run.seed, run.depth, run.room);
    run.roomName = document.at("name").get<std::string>();
    return document;
}

void ApplyRunParty(entt::registry& registry) {
    const auto& run = registry.ctx().get<FDungeonRun>();
    const auto* growth = registry.ctx().find<FDungeonGrowth>();
    if (growth == nullptr) LOG_ERROR("Run growth content is missing; node bonuses are not applied.");
    for (const auto entity : registry.view<FUnitData, FHealth, FStats>()) {
        auto& health = registry.get<FHealth>(entity);
        auto& stats = registry.get<FStats>(entity);
        if (registry.all_of<FPartyMember>(entity)) {
            const auto& id = registry.get<FUnitData>(entity).definition.id;
            const auto found = std::ranges::find(run.party, id, &FRunMember::id);
            if (found == run.party.end()) continue;
            const FPassiveStatBonuses nodes = growth != nullptr
                ? growth->passiveTree.Bonuses(found->allocatedNodes) : FPassiveStatBonuses{};
            health.max += found->bonusHealth + nodes.maxHealth;
            if (found->health >= 0) health.current = std::clamp(found->health, 1, health.max);
            else health.current = health.max;
            stats.attack += found->bonusAttack + nodes.attack;
            stats.magicAttack += found->bonusMagic + nodes.magicAttack;
            stats.defense += found->bonusDefense + nodes.defense;
            stats.magicDefense += found->bonusMagicDefense + nodes.magicDefense;
            if (auto* speed = registry.try_get<FSpeed>(entity)) speed->value += found->bonusSpeed + nodes.speed;
            if (!found->skills.empty()) registry.get<FSkillLoadout>(entity).slots = found->skills;
        } else {
            const int extra = std::min(run.depth - 1, 100);
            health.max += extra * DungeonBalance(registry, "enemy_health_per_depth") + run.room * 5;
            health.current = health.max;
            stats.attack += extra * DungeonBalance(registry, "enemy_attack_per_depth") + run.room;
            stats.magicAttack += extra * DungeonBalance(registry, "enemy_attack_per_depth") + run.room;
        }
    }
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

void CompleteDungeonBattle(entt::registry& registry, bool victory) {
    auto& run = registry.ctx().get<FDungeonRun>();
    if (run.phase != EDungeonPhase::Battle) return;
    CaptureRunParty(registry);
    if (!victory) {
        run.phase = EDungeonPhase::Defeat;
        run.message = "The expedition is lost. Run growth and runes reset on retry.";
        return;
    }
    RestoreParty(registry, DungeonBalance(registry, "room_heal"));
    run.materials += DungeonBalance(registry, "room_materials");
    const auto& drops = Definition(registry).at("rune_drops");
    run.runes.push_back(drops.at((run.seed + run.depth * 3u + run.room) % drops.size()).get<std::string>());
    // 정본 §6: 런 EXP는 캐릭터별로 쌓이고 레벨업마다 노드 포인트 1을 준다(런마다 0부터).
    const int experienceReward = DungeonBalance(registry, "room_experience");
    int levelsGained = 0;
    if (const auto* growth = registry.ctx().find<FDungeonGrowth>()) {
        for (auto& member : run.party) {
            member.experience += experienceReward;
            while (member.experience >= growth->ExperienceForNextLevel(member.level)) {
                member.experience -= growth->ExperienceForNextLevel(member.level);
                ++member.level;
                ++member.passivePoints;
                ++levelsGained;
            }
        }
    }
    run.phase = EDungeonPhase::Reward;
    run.message = "Room secured. Each member gains " + std::to_string(experienceReward) + " EXP.";
    if (levelsGained > 0) run.message += " Level up: allocate a passive node.";
    LOG_INFO("Dungeon {} room {} cleared. {} EXP per member.", run.depth, run.room + 1, experienceReward);
}

void ContinueDungeonReward(entt::registry& registry) {
    auto& run = registry.ctx().get<FDungeonRun>();
    if (run.phase != EDungeonPhase::Reward) return;
    if (run.room == 0) {
        run.phase = EDungeonPhase::Event;
        run.message = "A quiet shrine offers shelter, or a sealed cache of reinforcement materials.";
    } else if (run.room == 2) {
        run.phase = EDungeonPhase::Camp;
        run.cleared = run.depth;
        auto& checkpoint = registry.ctx().get<FDungeonCheckpoint>();
        checkpoint.cleared = std::max(checkpoint.cleared, std::min(run.cleared, 999));
        (void)SaveCheckpoint(registry);
        RestoreParty(registry, DungeonBalance(registry, "camp_heal"));
        run.message = "Dungeon cleared. Checkpoint reached. Rest, fit runes, reinforce stats, then descend.";
    } else {
        ++run.room;
        Advance(registry);
    }
}

void ChooseDungeonEvent(entt::registry& registry, bool rest) {
    auto& run = registry.ctx().get<FDungeonRun>();
    if (run.phase != EDungeonPhase::Event) return;
    if (rest) RestoreParty(registry, DungeonBalance(registry, "event_heal"));
    else run.materials += DungeonBalance(registry, "event_materials");
    ++run.room;
    Advance(registry);
}

void LeaveDungeonCamp(entt::registry& registry) {
    auto& run = registry.ctx().get<FDungeonRun>();
    if (run.phase != EDungeonPhase::Camp) return;
    run.depth = std::min(run.depth + 1, 1000);
    run.room = 0;
    Advance(registry);
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
    return std::nullopt;
}

} // namespace game
