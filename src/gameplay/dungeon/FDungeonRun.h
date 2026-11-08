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
enum class EMenuRequest { None, Start, Continue, Quit };

struct FMenuRequest {
    EMenuRequest value{EMenuRequest::None};
    bool fresh{false};
};

/// The start intent carried from the title/runEnd screen to the preparation step. `fresh` means "ignore
/// the checkpoint and start at depth 1"; the preparation state consumes it exactly once (WP-02b).
/// `startCleared` (WP-08/D-21): the cleared index the run should start at (< 0 = the checkpoint/fresh
/// rule). The consumer is WP-21's start-dungeon choice — nothing reads it yet.
struct FRunStartRequest {
    bool fresh{false};
    int startCleared{-1};
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

/// 물질 강화 항목 (정본 §5: 항목별 상한은 데이터). 가격은 항목별 고정이 아니라 런 누적 곡선
/// (`data/dungeon camp.upgrade_cost_steps` + FCampProgress.upgradesTotal — WP-10)이 결정한다.
struct FStatUpgrade {
    std::string id;
    std::string displayName;
    std::string stat;  // health | attack | magic_attack | defense | magic_defense | speed
    int amount{1};
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
    bool abandonRequested{false};  // ends the run and deletes its save
    bool leaveRequested{false};    // back to the menu, keeping the save to continue from
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

/// The in-progress expedition's save slot. It is rewritten at every room start and every reward/event/camp step.
/// Continuing restarts the room the run was in from its beginning: rooms are generated from the saved seed.
struct FDungeonSave {
    std::string path;
    bool exists{false};  // a valid save is waiting; the fields below describe it
    int depth{0};
    int room{0};
    EDungeonPhase phase{EDungeonPhase::Battle};
    std::string savedAt;
    std::string error;
};

/// `savePath` defaults to the run save next to the checkpoint file (or the preference directory).
bool InitializeDungeon(entt::registry& registry, std::string checkpointPath = {}, std::string savePath = {});
/// The dungeon definition document (`data/dungeon`). The run-flow helpers read the same content boundary
/// the dungeon module uses instead of duplicating the loader.
[[nodiscard]] const nlohmann::json& DungeonDefinition(entt::registry& registry);
/// Installs a new run in the registry: seed, depth from the checkpoint, the definition party, and a fresh
/// per-run event log. It does not save — FRunFlow::StartRun saves after the install.
void InstallDungeonRun(entt::registry& registry, std::uint32_t seed, bool fresh);
/// WP-08/D-21: installs a run starting at `clearedIndex` (clamped to [0, checkpoint.cleared] — the
/// checkpoint never goes down). Applies the D-7 start-support materials scaled by the clamped index.
void InstallDungeonRunAt(entt::registry& registry, std::uint32_t seed, int clearedIndex);
/// Writes the current run to its save slot. A no-op without a run or a configured slot.
bool SaveDungeonRun(entt::registry& registry);
/// Removes the save: the expedition was lost or ended on purpose.
void DeleteDungeonSave(entt::registry& registry);
/// Replaces the registry's run with the saved one. On failure the run is untouched and `FDungeonSave::error` says why.
/// WP-08 resume rule: a saved phase=event whose gap no longer carries an event slot (content changed under
/// an old save) advances exactly like a reward step with no event. Runs inside this call, after install.
[[nodiscard]] bool ContinueDungeonRun(entt::registry& registry);
/// WP-08/D-21: a new run starting at `clearedIndex` cleared dungeons (clamped to the checkpoint). The only
/// new-run entry point for a start-dungeon choice (WP-21); StartRun(seed, fresh) delegates here with 0
/// (fresh) or the checkpoint's cleared count. The body lives in FRunFlow.cpp — the run phase machine's
/// single writer (TransitionTableSingleWriter).
void StartRunAt(entt::registry& registry, std::uint32_t seed, int clearedIndex);
/// WP-08 resume normalization (see ContinueDungeonRun): no-op unless the installed run is saved at
/// phase=event and the sequence no longer has an event slot in that gap. The body lives in FRunFlow.cpp
/// for the same single-writer reason.
void NormalizeResumedRunPhase(entt::registry& registry);
/// Legacy new-run entry point (kept for its many call sites; the move to the flow entry points is a later
/// separate WP): delegates to FRunFlow::StartRun, which installs the run and saves it.
void StartDungeonRun(entt::registry& registry, std::uint32_t seed, bool fresh = false);
[[nodiscard]] nlohmann::json BuildDungeonBattlefield(entt::registry& registry);
void ApplyRunParty(entt::registry& registry);
void CaptureRunParty(entt::registry& registry);
/// Legacy wrappers (kept for their many call sites; the move is a later separate WP): each delegates to
/// FRunFlow and logs the error.
void CompleteDungeonBattle(entt::registry& registry, bool victory);
/// 방 클리어 성장 안내(Reward)를 닫고 이벤트/다음 방/캠프로 진행한다.
void ContinueDungeonReward(entt::registry& registry);
void ChooseDungeonEvent(entt::registry& registry, bool rest);
/// 캠프를 떠나 다음 던전으로 내려간다. 강화는 캠프에서 UpgradeDungeonStat으로 먼저 한다.
void LeaveDungeonCamp(entt::registry& registry);
/// 회복 예산표 (WP-10 §3 / D-18): 회복원 전부가 데이터다. 이 구조체는 로드 결과의 그릇일 뿐이고
/// **기본값을 코드에 두지 않는다**(데이터와 코드가 이중 정본이 되는 것을 W6 비평이 기각). `recovery`
/// 블록은 정수 0–1000 루프 밖에서 검증된다 — 실수 3종은 0 ≤ v ≤ 1, camp_heal_percent는 0–100.
struct FDungeonRecovery {
    double koRecoverFraction{};         // KO/전투 후 바닥: max(1, floor(maxHp × fraction))
    int campAutoHeal{};                 // 캠프 도착 자동 회복(비선택·무료 항)
    int campHealCost{};                 // 유료 치료 가격(물질)
    int campHealPercent{};              // 유료 치료량 = maxHp의 % (멤버별)
    double eventChestHpCostFraction{};  // 상자 선택의 파티 HP 비용 비율(유효 HP 기준)
    double freeHealCapFraction{};       // 무료 회복 예산 상한 = maxHp의 % (검증·테스트 기준값)
};

/// The loaded recovery block; all-zero when the registry never booted the dungeon (a wiring error that is
/// logged once). The data is the only source of these numbers — never reintroduce in-code defaults.
[[nodiscard]] const FDungeonRecovery& DungeonRecovery(const entt::registry& registry);

/// KO 바닥: max(1, floor(maxHealth × fraction)) — 기존 정수 나눗셈(max/3)과 같은 형태의 규칙.
[[nodiscard]] int KoRecoverFloor(int maxHealth, double fraction);

/// 런 단위 최대 HP (WP-10 ★4): content.units 베이스 maxHealth + bonusHealth + 노드 보너스.maxHealth —
/// FCombatSheet.cpp:41,:55-57과 같은 입력. FCombatSheet는 편집하지 않고 동등성 테스트
/// (RunMaxHpMatchesCombatSheet)로 묶는다. 재파생 수렴은 WP-18(W11) 몫.
/// A return <= 0 means the member's unit template could not be resolved (logged): callers must treat the
/// maximum as unknown — never clamp against it.
[[nodiscard]] int DungeonMemberMaxHealth(const entt::registry& registry, const FRunMember& member);
/// 유효 HP = health < 0(미포착=만피 센티넬) ? maxHp : min(health, maxHp). maxHp <= 0(알 수 없음)이면
/// 클램프하지 않고 보유 HP를 그대로 돌려준다.
[[nodiscard]] int DungeonEffectiveHealth(const entt::registry& registry, const FRunMember& member);
/// 상자 비용 순수 함수: min(floor(maxHealth × fraction), effectiveHealth − 1) — KO 불가·HP 1 바닥.
/// 손실 0이면 0으로 정직하게 계산한다(최소 1피해 강제 없음 — F3-4). 표시와 적용이 같은 함수를 쓴다.
/// Unknown maxHealth(알 수 없는 멤버)는 손실 0 — 0을 향해 클램프하지 않는다.
[[nodiscard]] int ChestHpLoss(int maxHealth, int effectiveHealth, double fraction);
[[nodiscard]] int ChestHpLossFor(const entt::registry& registry, const FRunMember& member);
/// 상자 선택의 비용 적용: 파티 전원에게 ChestHpLossFor만큼 HP를 잃게 하고 합계 손실을 돌려준다.
int ApplyChestHpCost(entt::registry& registry);

/// 정본 §6: 인접 노드 할당. 전투/패배 중에는 잠긴다(Reward/Event/Camp에서만).
[[nodiscard]] bool CanAllocateDungeonNode(
    const entt::registry& registry, std::size_t memberIndex, std::string_view nodeId);
[[nodiscard]] bool AllocateDungeonNode(
    entt::registry& registry, std::size_t memberIndex, std::string_view nodeId);
[[nodiscard]] std::optional<std::string> ValidateDungeonStatUpgrade(
    const entt::registry& registry, std::size_t memberIndex, std::size_t upgradeIndex);
[[nodiscard]] std::optional<std::string> UpgradeDungeonStat(
    entt::registry& registry, std::size_t memberIndex, std::size_t upgradeIndex);
/// WP-10: 캠프 유료 치료(recovery.camp_heal_cost → 멤버별 maxHp의 camp_heal_percent%). 센티넬(-1=만피)은
/// 스킵. 전원이 이미 만피면 거부 — 구매는 선택이라 강제되지 않는다(PaidHealIsOptional).
[[nodiscard]] std::optional<std::string> ValidateDungeonCampHeal(const entt::registry& registry);
[[nodiscard]] std::optional<std::string> PayDungeonCampHeal(entt::registry& registry);
/// WP-10: 초기화(camp.respec_cost) — 할당된 노드 전부를 passivePoints로 되돌린다. stat 강화는 환불하지
/// 않고 곡선 카운터(upgradesTotal)도 건드리지 않는다.
[[nodiscard]] std::optional<std::string> ValidateDungeonRespec(const entt::registry& registry);
[[nodiscard]] std::optional<std::string> RespecDungeonNodes(entt::registry& registry);
/// Loadout window (WP-27): rune edits are allowed in {Camp, Reward} and rejected elsewhere with
/// "Runes can be changed between rooms, not in battle." Validation/failure returns a reason and leaves all
/// runes unchanged (removal is rule-allowed in Reward but the UI only exposes it at camp).
[[nodiscard]] std::optional<std::string> ValidateDungeonRuneEquip(
    const entt::registry& registry, std::size_t memberIndex, std::size_t slotIndex, std::size_t runeIndex);
[[nodiscard]] std::optional<std::string> EquipDungeonRune(
    entt::registry& registry, std::size_t memberIndex, std::size_t slotIndex, std::size_t runeIndex);
[[nodiscard]] std::optional<std::string> RemoveDungeonRune(
    entt::registry& registry, std::size_t memberIndex, std::size_t slotIndex, std::size_t runeIndex);
[[nodiscard]] int DungeonBalance(entt::registry& registry, const char* key);

} // namespace game
