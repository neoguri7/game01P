#pragma once

#include <cstdint>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>
#include <entt/entt.hpp>
#include <nlohmann/json.hpp>

#include "ecs/components/FSkillLoadout.h"

namespace game {

enum class EDungeonPhase { Battle, Reward, Event, Camp, Defeat };
enum class EMenuRequest { None, Start, Quit };

struct FMenuRequest {
    EMenuRequest value{EMenuRequest::None};
    bool fresh{false};
};

struct FRunMember {
    std::string id;
    int health{-1};
    int bonusHealth{0};
    int bonusAttack{0};
    int bonusMagic{0};
    std::vector<FSkillSlot> skills;
};

struct FDungeonRun {
    std::uint32_t seed{1};
    int depth{1};
    int room{0};
    int cleared{0};
    int materials{0};
    int experience{0};
    int level{1};
    EDungeonPhase phase{EDungeonPhase::Battle};
    bool advanceRequested{false};
    bool abandonRequested{false};
    std::string roomName;
    std::string message;
    std::vector<FRunMember> party;
    std::vector<std::string> runes;
};

struct FDungeonCheckpoint {
    int cleared{0};
    std::string path;
    std::string error;
};

bool InitializeDungeon(entt::registry& registry, std::string checkpointPath = {});
void StartDungeonRun(entt::registry& registry, std::uint32_t seed, bool fresh = false);
[[nodiscard]] nlohmann::json BuildDungeonBattlefield(entt::registry& registry);
void ApplyRunParty(entt::registry& registry);
void CaptureRunParty(entt::registry& registry);
void CompleteDungeonBattle(entt::registry& registry, bool victory);
void ChooseDungeonReward(entt::registry& registry, int choice);
void ChooseDungeonEvent(entt::registry& registry, bool rest);
void LeaveDungeonCamp(entt::registry& registry, bool reinforce);
/// Camp-only loadout edits. Validation/failure returns a reason and leaves all runes unchanged.
[[nodiscard]] std::optional<std::string> ValidateDungeonRuneEquip(
    const entt::registry& registry, std::size_t memberIndex, std::size_t slotIndex, std::size_t runeIndex);
[[nodiscard]] std::optional<std::string> EquipDungeonRune(
    entt::registry& registry, std::size_t memberIndex, std::size_t slotIndex, std::size_t runeIndex);
[[nodiscard]] std::optional<std::string> RemoveDungeonRune(
    entt::registry& registry, std::size_t memberIndex, std::size_t slotIndex, std::size_t runeIndex);
[[nodiscard]] int DungeonBalance(entt::registry& registry, const char* key);

} // namespace game
