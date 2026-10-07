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
        for (const char* key : {"move_range", "trap_damage", "reward_health", "reward_attack",
                               "reward_magic", "room_heal", "event_heal", "event_materials",
                               "camp_heal", "camp_cost", "room_materials", "room_experience",
                               "level_experience", "enemy_health_per_depth", "enemy_attack_per_depth"}) {
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
        run.party.push_back(FRunMember{member.at("id").get<std::string>()});
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
    for (const auto entity : registry.view<FUnitData, FHealth, FStats>()) {
        auto& health = registry.get<FHealth>(entity);
        auto& stats = registry.get<FStats>(entity);
        if (registry.all_of<FPartyMember>(entity)) {
            const auto& id = registry.get<FUnitData>(entity).definition.id;
            const auto found = std::ranges::find(run.party, id, &FRunMember::id);
            if (found == run.party.end()) continue;
            health.max += found->bonusHealth;
            if (found->health >= 0) health.current = std::clamp(found->health, 1, health.max);
            else health.current = health.max;
            stats.attack += found->bonusAttack;
            stats.magicAttack += found->bonusMagic;
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
    run.experience += DungeonBalance(registry, "room_experience");
    run.level = 1 + run.experience / DungeonBalance(registry, "level_experience");
    run.phase = EDungeonPhase::Reward;
    run.message = "Room secured. Choose one run-growth node for the party.";
    LOG_INFO("Dungeon {} room {} cleared. Run EXP {}", run.depth, run.room + 1, run.experience);
}

void ChooseDungeonReward(entt::registry& registry, int choice) {
    auto& run = registry.ctx().get<FDungeonRun>();
    if (run.phase != EDungeonPhase::Reward || choice < 0 || choice > 2) return;
    for (auto& member : run.party) {
        if (choice == 0) {
            const int bonus = DungeonBalance(registry, "reward_health");
            member.bonusHealth += bonus;
            member.health += bonus;
        } else if (choice == 1) {
            member.bonusAttack += DungeonBalance(registry, "reward_attack");
        } else {
            member.bonusMagic += DungeonBalance(registry, "reward_magic");
        }
    }
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
        run.message = "Dungeon cleared. Checkpoint reached. Rest, fit runes, then descend.";
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

void LeaveDungeonCamp(entt::registry& registry, bool reinforce) {
    auto& run = registry.ctx().get<FDungeonRun>();
    if (run.phase != EDungeonPhase::Camp) return;
    if (reinforce) {
        const int cost = DungeonBalance(registry, "camp_cost");
        if (run.materials < cost) {
            run.message = "Not enough reinforcement materials.";
            return;
        }
        run.materials -= cost;
        for (auto& member : run.party) {
            member.bonusAttack += DungeonBalance(registry, "reward_attack");
            member.bonusMagic += DungeonBalance(registry, "reward_magic");
        }
    }
    run.depth = std::min(run.depth + 1, 1000);
    run.room = 0;
    Advance(registry);
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
