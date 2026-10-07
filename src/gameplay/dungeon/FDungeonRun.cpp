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
            !data.at("rooms").is_array() || data.at("rooms").size() != 3) {
            LOG_ERROR("Dungeon needs schema version 1, four party members and three combat rooms.");
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
        if (!content.units.find("enemy.guardian")) {
            LOG_ERROR("Dungeon guardian unit definition is missing.");
            return false;
        }
        for (const auto& room : data.at("rooms")) {
            if (!room.at("name").is_string() || room.at("name").get_ref<const std::string&>().empty() ||
                !room.at("enemies").is_array() || room.at("enemies").empty()) {
                LOG_ERROR("Dungeon room needs a name and a nonempty enemy placement array.");
                return false;
            }
            entt::registry probe;
            FScene scene(probe, "validation");
            scene.Load();
            auto document = room;
            document["units"] = data.at("party");
            for (const auto& enemy : room.at("enemies")) {
                if (!content.units.find(enemy.at("unit").get<std::string>()) || enemy.at("faction") != "enemy") {
                    LOG_ERROR("Dungeon room needs known enemy units and enemy factions.");
                    return false;
                }
                document["units"].push_back(enemy);
            }
            document["width"] = 8; document["height"] = 8; document["tile_size"] = 64;
            document["origin_x"] = 0; document["origin_y"] = 0;
            document["move_range"] = data["balance"]["move_range"];
            document["trap_damage"] = data["balance"]["trap_damage"];
            const auto battlefield = FEntityFactory::CreateBattlefield(scene, document);
            if (!battlefield) {
                LOG_ERROR("Dungeon room invalid: {}", battlefield.error().message);
                return false;
            }
            if (!SetupCombatUnits(probe, *battlefield, content)) return false;
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
    // Rotate authored rooms by the run seed; terrain and placements stay paired.
    const int index = (run.room + static_cast<int>(run.seed % 3u)) % 3;
    auto document = data.at("rooms").at(index);
    run.roomName = document.at("name").get<std::string>();
    document["units"] = data.at("party");
    for (const auto& enemy : document.at("enemies")) document["units"].push_back(enemy);
    if (run.room == 2) {
        auto& guardian = document["units"].back();
        guardian["unit"] = "enemy.guardian";
        guardian["display_name"] = "Stone guardian";
    }
    document["width"] = 8; document["height"] = 8; document["tile_size"] = 64;
    document["origin_x"] = 0; document["origin_y"] = 0;
    document["move_range"] = data["balance"]["move_range"];
    document["trap_damage"] = data["balance"]["trap_damage"];
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
