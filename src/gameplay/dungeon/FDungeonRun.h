#pragma once

#include <cstdint>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <entt/entt.hpp>
#include <nlohmann/json.hpp>

#include "ecs/components/FSkillLoadout.h"
#include "gameplay/progression/FPassiveTree.h"

namespace game {

enum class EDungeonPhase { Battle, Reward, Event, Camp, Defeat };
enum class EMenuRequest { None, Start, Quit };

struct FMenuRequest {
    EMenuRequest value{EMenuRequest::None};
    bool fresh{false};
};

struct FRunMember {
    std::string id;
    std::string displayName;
    int health{-1};
    int bonusHealth{0};
    int bonusAttack{0};
    int bonusMagic{0};
    int bonusDefense{0};
    int bonusMagicDefense{0};
    int bonusSpeed{0};
    int experience{0};  // 정본 §6: 런 스코프 캐릭터 EXP (레벨 임계는 FDungeonGrowth가 정한다)
    int level{1};
    int passivePoints{0};
    std::vector<std::string> allocatedNodes;
    std::vector<FSkillSlot> skills;
};

/// 물질 강화 항목 (정본 §5: 항목별 비용·상한은 데이터). `stat`은 DungeonMemberStatBonus가 해석한다.
struct FStatUpgrade {
    std::string id;
    std::string displayName;
    std::string stat;  // health | attack | magic_attack | defense | magic_defense | speed
    int amount{1};
    int cost{1};
    int cap{1};
};

/// 런 성장 콘텐츠 (정본 §5/§6): 공용 노드 트리 + 물질 강화 항목 + 레벨 임계 곡선.
/// FDungeonRun(런 상태)과 달리 런 사이에도 불변이다. InitializeDungeon이 로드·검증한다.
struct FDungeonGrowth {
    FPassiveTree passiveTree;
    std::vector<FStatUpgrade> statUpgrades;
    int levelExperienceBase{10};
    int levelExperienceStep{5};

    [[nodiscard]] int ExperienceForNextLevel(int level) const;
};

/// 캐릭터가 산 강화 수치를 읽는다 — UI·검증·전투 합산이 같은 해석을 쓰도록 한 곳에 둔다.
[[nodiscard]] inline int DungeonMemberStatBonus(const FRunMember& member, std::string_view stat) {
    if (stat == "health") return member.bonusHealth;
    if (stat == "attack") return member.bonusAttack;
    if (stat == "magic_attack") return member.bonusMagic;
    if (stat == "defense") return member.bonusDefense;
    if (stat == "magic_defense") return member.bonusMagicDefense;
    if (stat == "speed") return member.bonusSpeed;
    return 0;
}

struct FDungeonRun {
    std::uint32_t seed{1};
    int depth{1};
    int room{0};
    int cleared{0};
    int materials{0};
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
/// 방 클리어 성장 안내(Reward)를 닫고 이벤트/다음 방/캠프로 진행한다.
void ContinueDungeonReward(entt::registry& registry);
void ChooseDungeonEvent(entt::registry& registry, bool rest);
/// 캠프를 떠나 다음 던전으로 내려간다. 강화는 캠프에서 UpgradeDungeonStat으로 먼저 한다.
void LeaveDungeonCamp(entt::registry& registry);
/// 정본 §6: 인접 노드 할당. 전투/패배 중에는 잠긴다(Reward/Event/Camp에서만).
[[nodiscard]] bool CanAllocateDungeonNode(
    const entt::registry& registry, std::size_t memberIndex, std::string_view nodeId);
[[nodiscard]] bool AllocateDungeonNode(
    entt::registry& registry, std::size_t memberIndex, std::string_view nodeId);
[[nodiscard]] std::optional<std::string> ValidateDungeonStatUpgrade(
    const entt::registry& registry, std::size_t memberIndex, std::size_t upgradeIndex);
[[nodiscard]] std::optional<std::string> UpgradeDungeonStat(
    entt::registry& registry, std::size_t memberIndex, std::size_t upgradeIndex);
/// Camp-only loadout edits. Validation/failure returns a reason and leaves all runes unchanged.
[[nodiscard]] std::optional<std::string> ValidateDungeonRuneEquip(
    const entt::registry& registry, std::size_t memberIndex, std::size_t slotIndex, std::size_t runeIndex);
[[nodiscard]] std::optional<std::string> EquipDungeonRune(
    entt::registry& registry, std::size_t memberIndex, std::size_t slotIndex, std::size_t runeIndex);
[[nodiscard]] std::optional<std::string> RemoveDungeonRune(
    entt::registry& registry, std::size_t memberIndex, std::size_t slotIndex, std::size_t runeIndex);
[[nodiscard]] int DungeonBalance(entt::registry& registry, const char* key);

} // namespace game
