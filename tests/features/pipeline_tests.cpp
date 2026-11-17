// WP-03 pipeline tests: the base+derived combat sheet (FBaseCombatStats + RefreshCombatSheet), the
// ApplyRunParty wrapper (refresh → ApplyBattleStart), and the preview API (PreviewSkill) that the action
// resolver consumes as its single calculation path.
//
// The fixture mirrors FEmberBattleState::OnEnter's combat section without SDL: dungeon run → battlefield →
// SetupCombatUnits → battle-scoped ctx → ApplyRunParty. Content comes from the frozen fixtures
// (TestAssetRoots), so ship balance changes never move these expectations.

#define GAME01P_TEST_SUPPORT_CHECK
#include "../TestSupport.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <entt/entt.hpp>
#include <nlohmann/json.hpp>

#include "ecs/components/FActionPoints.h"
#include "ecs/components/FActiveStatuses.h"
#include "ecs/components/FBaseCombatStats.h"
#include "ecs/components/FDead.h"
#include "ecs/components/FFaction.h"
#include "ecs/components/FGridPosition.h"
#include "ecs/components/FHealth.h"
#include "ecs/components/FSkillLoadout.h"
#include "ecs/components/FSpeed.h"
#include "ecs/components/FStats.h"
#include "ecs/systems/ActionResolverSystem.h"
#include "ecs/systems/TurnSystem.h"
#include "gameplay/battle/FBattleSetup.h"
#include "gameplay/combat/FCombatApply.h"
#include "gameplay/combat/FCombatQuery.h"
#include "gameplay/combat/FCombatRules.h"
#include "gameplay/combat/FCombatSheet.h"
#include "gameplay/combat/FCombatTypes.h"
#include "gameplay/combat/FSkillResolver.h"
#include "gameplay/combat/FStatusRules.h"
#include "gameplay/data/FContentRegistry.h"
#include "gameplay/dungeon/FDungeonRun.h"
#include "gameplay/factory/FEntityFactory.h"
#include "gameplay/grid/FGridTerrain.h"
#include "gameplay/grid/FGridTypes.h"

namespace {

// ---------------------------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------------------------

entt::entity FindUnit(entt::registry& registry, std::string_view id)
{
    for (const auto entity : registry.view<game::FUnitData>()) {
        if (registry.get<game::FUnitData>(entity).definition.id == id) return entity;
    }
    return entt::null;
}

std::vector<entt::entity> Enemies(entt::registry& registry)
{
    std::vector<entt::entity> enemies;
    for (const auto entity : registry.view<game::FEnemy, game::FUnitData>()) enemies.push_back(entity);
    return enemies;
}

game::FRunMember& Member(game::FDungeonRun& run, std::string_view id)
{
    const auto found = std::ranges::find(run.party, id, &game::FRunMember::id);
    CHECK(found != run.party.end());
    return *found;
}

void Place(entt::registry& registry, entt::entity unit, int x, int y)
{
    registry.get<game::FGridPosition>(unit) = {x, y};
}

/// 생성기 배치는 시드 기반이라 가변이다 — 모든 유닛을 모서리 줄로 옮겨 시나리오 배치를 결정적으로 만든다.
void ParkAll(entt::registry& registry)
{
    registry.ctx().get<game::FGridTerrain>().cells.fill(game::ETile::Floor);
    int slot = 0;
    for (const auto entity : registry.view<game::FPartyMember, game::FGridPosition>()) {
        Place(registry, entity, slot++, 11);
    }
    slot = 0;
    for (const auto entity : registry.view<game::FEnemy, game::FGridPosition>()) {
        Place(registry, entity, slot++, 10);
    }
}

/// 액터가 현재 턴이 될 때까지 턴을 넘긴다(리졸버의 토큰 검증을 통과하는 실제 토큰).
game::FTurnToken StartTurnFor(entt::registry& registry, entt::entity actor)
{
    auto& runtime = registry.ctx().get<game::FTurnRuntime>();
    if (runtime.controller.snapshot().state != game::ETurnState::Running) {
        CHECK(runtime.controller.start(registry, 1234u).has_value());
    }
    for (int step = 0; step < 16; ++step) {
        const auto snapshot = runtime.controller.snapshot();
        CHECK(snapshot.currentTurn.has_value());
        if (snapshot.currentTurn->actor == actor) return *snapshot.currentTurn;
        CHECK(runtime.controller.endTurn(registry, *snapshot.currentTurn).has_value());
    }
    throw std::runtime_error("the requested actor never became the active unit");
}

/// 프리뷰 → 실제 해석(ActionResolverSystem) → 동등 비교: 피해/치유/lethal/HP/부여(id·지속·강도)와
/// 예고되지 않은 유닛의 무변화. `expectedTargets` = 이 형태가 실제로 맞춰야 하는 유닛 수(확산/관통이
/// 한 유닛만 맞고 통과하는 것을 막는다), `targetsOwnSide` = 힐 계열, `expectGrants` = 상태 부여가
/// 실제로 걸리는지, `expectLethal` = 마무리 일격. 대상 상태(HP·상태이상)는 호출자가 시나리오 시작
/// 전에 정리해 둔다.
void CheckPreviewEqualsResolution(
    entt::registry& registry, entt::entity actor, const game::FSkillSlot& slot,
    entt::entity target, game::FGridPosition aim, std::size_t expectedTargets,
    bool targetsOwnSide = false, bool expectGrants = false, bool expectLethal = false)
{
    const auto& content = registry.ctx().get<game::gameplay::FContentRegistry>();
    const auto resolved = game::ResolveSkillSlot(slot, content);
    CHECK(resolved.has_value());

    // WP-22: the BuildLoadout fallback caps a default loadout at the pool's first four entries; a scenario
    // may name a pool skill beyond the cap (e.g. the mage's firebolt), so equip the slot explicitly before
    // the resolver looks it up.
    {
        auto& slots = registry.get<game::FSkillLoadout>(actor).slots;
        if (std::ranges::find(slots, slot.skillId, &game::FSkillSlot::skillId) == slots.end()) {
            slots.push_back(slot);
        }
    }

    const auto actorCell = registry.get<game::FGridPosition>(actor);
    const int skillApBefore = registry.get<game::FActionPoints>(actor).skill;
    std::vector<std::pair<entt::entity, int>> healthSnapshot;
    for (const auto entity : registry.view<game::FHealth>()) {
        healthSnapshot.emplace_back(entity, registry.get<game::FHealth>(entity).current);
    }

    const auto preview = game::PreviewSkill(registry, actor, *resolved, aim, actorCell);
    CHECK(preview.has_value());
    CHECK(preview->apCost == resolved->apCost);
    CHECK(preview->cells == game::ComputeSkillCells(*resolved, aim, actorCell));
    CHECK(preview->targets.size() == expectedTargets);

    // 프리뷰는 부작용이 없다: HP·AP 불변(롤도 적용도 하지 않는다).
    for (const auto& [entity, current] : healthSnapshot) {
        CHECK(registry.get<game::FHealth>(entity).current == current);
    }
    CHECK(registry.get<game::FActionPoints>(actor).skill == skillApBefore);

    // 실제 해석: 리졸버 큐 경로 그대로.
    registry.get<game::FActionPoints>(actor).skill = registry.get<game::FActionPoints>(actor).skillMax;
    game::FActionRequest request;
    request.actor = actor;
    request.target = target;
    request.skillId = slot.skillId;
    request.cell = aim;
    request.token = StartTurnFor(registry, actor);
    registry.ctx().get<game::FActionQueue>().pending.push_back(request);
    game::ecs::ActionResolverSystem resolver;
    resolver.update(registry, 0.f);

    const auto healthBeforeOf = [&](entt::entity unit) {
        const auto found = std::ranges::find(healthSnapshot, unit, &std::pair<entt::entity, int>::first);
        CHECK(found != healthSnapshot.end());
        return found->second;
    };
    for (const game::FPreviewTarget& expected : preview->targets) {
        const int before = healthBeforeOf(expected.unit);
        const auto& health = registry.get<game::FHealth>(expected.unit);
        CHECK(health.current == expected.healthAfter);
        CHECK(registry.all_of<game::FDead>(expected.unit) == expected.lethal);
        if (expectLethal) CHECK(expected.lethal);
        if (expectGrants) CHECK(!expected.statuses.empty());
        // 프리뷰 필드가 서로 모순되지 않는다(healthAfter만 맞는 우연 방지).
        if (targetsOwnSide) {
            CHECK(expected.heal == expected.healthAfter - before);
        } else if (!expected.lethal) {
            CHECK(expected.damage == before - expected.healthAfter);
        }

        const auto* active = registry.try_get<game::FActiveStatuses>(expected.unit);
        CHECK(active != nullptr);
        CHECK(active->effects.size() == expected.statuses.size());
        for (const game::FResolvedStatus& grant : expected.statuses) {
            const auto* definition = content.statuses.find(grant.statusId);
            CHECK(definition != nullptr);
            const auto effect =
                std::ranges::find(active->effects, grant.statusId, &game::FStatusEffect::statusId);
            CHECK(effect != active->effects.end());
            CHECK(effect->turnsRemaining == game::EffectiveStatusDuration(*definition, grant.durationDelta));
            CHECK(effect->magnitude == game::EffectiveStatusMagnitude(*definition, grant.magnitudeDelta));
        }
    }
    // 프리뷰가 예고하지 않은 유닛은 리졸버도 건드리지 않는다(형태·LoS·진영 어긋남 회귀 고정).
    for (const auto& [entity, current] : healthSnapshot) {
        if (std::ranges::find(preview->targets, entity, &game::FPreviewTarget::unit) != preview->targets.end()) {
            continue;
        }
        CHECK(registry.get<game::FHealth>(entity).current == current);
    }
}

/// 던전(Ember) 전투 셋업 재현: FEmberBattleState::OnEnter의 전투 구간과 같은 순서.
struct FFixture {
    entt::registry registry;
    std::unique_ptr<game::FScene> scene;
    std::filesystem::path checkpoint;
    std::filesystem::path runSave;

    explicit FFixture(std::uint32_t seed = 1234)
    {
        std::filesystem::create_directories("out");
        checkpoint = std::filesystem::path("out") / ("pipeline-checkpoint-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".json");
        game::FAssetManager::Initialize(registry, TestAssetRoots());
        CHECK(game::gameplay::FGameplayServices::Initialize(registry));
        CHECK(game::InitializeDungeon(registry, checkpoint.string()));
        runSave = registry.ctx().get<game::FDungeonSave>().path;
        game::StartDungeonRun(registry, seed, true);

        const auto document = game::BuildDungeonBattlefield(registry);
        scene = std::make_unique<game::FScene>(registry, "PipelineTest");
        scene->Load();
        const auto battlefield = game::FEntityFactory::CreateBattlefield(*scene, document);
        CHECK(battlefield.has_value());
        registry.ctx().erase<game::FGridTerrain>();
        registry.ctx().emplace<game::FGridTerrain>(battlefield->terrain);
        if (auto* metrics = registry.ctx().find<game::FGridMetrics>()) {
            *metrics = battlefield->metrics;
        } else {
            registry.ctx().emplace<game::FGridMetrics>(battlefield->metrics);
        }
        const auto& content = registry.ctx().get<game::gameplay::FContentRegistry>();
        CHECK(game::SetupCombatUnits(registry, *battlefield, content));
        registry.ctx().emplace<game::FActionQueue>();
        registry.ctx().emplace<game::FCombatLog>();
        registry.ctx().emplace<game::FTurnRuntime>();
        registry.ctx().emplace<game::FCombatRuntime>();
        game::ApplyRunParty(registry);
    }

    ~FFixture()
    {
        scene.reset();
        std::error_code error;
        std::filesystem::remove(checkpoint, error);
        std::filesystem::remove(runSave, error);
        std::filesystem::remove(runSave.string() + ".tmp", error);
    }
};

// ---------------------------------------------------------------------------------------------
// Refresh (base+derived)
// ---------------------------------------------------------------------------------------------

void RefreshMatchesLegacyTotals()
{
    FFixture fixture;
    auto& registry = fixture.registry;
    auto& run = registry.ctx().get<game::FDungeonRun>();
    const auto& growth = registry.ctx().get<game::FDungeonGrowth>();
    const auto& content = registry.ctx().get<game::gameplay::FContentRegistry>();
    const auto warrior = FindUnit(registry, "party.warrior");
    CHECK(warrior != entt::null);
    const game::FBaseCombatStats base = registry.get<game::FBaseCombatStats>(warrior);

    // 물질 강화 + 노드 할당(구 ApplyRunParty가 제자리 가산하던 입력 그대로).
    auto& member = Member(run, "party.warrior");
    member.bonusHealth = 7;
    member.bonusAttack = 2;
    member.bonusMagic = 3;
    member.bonusDefense = 1;
    member.bonusMagicDefense = 4;
    member.bonusSpeed = 2;
    member.allocatedNodes = {"node.might", "node.vitality"};  // attack +1, max_health +5
    const game::FPassiveStatBonuses nodes = growth.passiveTree.Bonuses(member.allocatedNodes);

    game::RefreshAllCombatSheets(registry, content);

    const auto& stats = registry.get<game::FStats>(warrior);
    const auto& health = registry.get<game::FHealth>(warrior);
    const auto& speed = registry.get<game::FSpeed>(warrior);
    CHECK(stats.attack == base.stats.attack + member.bonusAttack + nodes.attack);
    CHECK(stats.magicAttack == base.stats.magicAttack + member.bonusMagic + nodes.magicAttack);
    CHECK(stats.defense == base.stats.defense + member.bonusDefense + nodes.defense);
    CHECK(stats.magicDefense == base.stats.magicDefense + member.bonusMagicDefense + nodes.magicDefense);
    CHECK(health.max == base.maxHealth + member.bonusHealth + nodes.maxHealth);
    CHECK(speed.value == base.speed + member.bonusSpeed + nodes.speed);
    // 베이스는 불변이다(갱신이 원본을 오염시키지 않는다).
    CHECK(registry.get<game::FBaseCombatStats>(warrior).stats.attack == base.stats.attack);
    CHECK(registry.get<game::FBaseCombatStats>(warrior).maxHealth == base.maxHealth);
}

void RefreshIsIdempotent()
{
    FFixture fixture;
    auto& registry = fixture.registry;
    auto& run = registry.ctx().get<game::FDungeonRun>();
    const auto& content = registry.ctx().get<game::gameplay::FContentRegistry>();
    const auto warrior = FindUnit(registry, "party.warrior");
    CHECK(warrior != entt::null);

    auto& member = Member(run, "party.warrior");
    member.bonusAttack = 3;
    member.bonusHealth = 9;
    member.allocatedNodes = {"node.might"};
    game::RefreshCombatSheet(registry, warrior, content);

    const int attack = registry.get<game::FStats>(warrior).attack;
    const int maxHealth = registry.get<game::FHealth>(warrior).max;
    for (int repeat = 0; repeat < 10; ++repeat) {
        game::RefreshCombatSheet(registry, warrior, content);
        CHECK(registry.get<game::FStats>(warrior).attack == attack);
        CHECK(registry.get<game::FHealth>(warrior).max == maxHealth);
    }
}

void ReadersSeeRefreshedValues()
{
    FFixture fixture;
    auto& registry = fixture.registry;
    auto& run = registry.ctx().get<game::FDungeonRun>();
    const auto& content = registry.ctx().get<game::gameplay::FContentRegistry>();
    const auto warrior = FindUnit(registry, "party.warrior");
    CHECK(warrior != entt::null);
    const game::FBaseCombatStats base = registry.get<game::FBaseCombatStats>(warrior);

    // (a) 이니셔티브: TurnController.collect가 갱신된 FSpeed를 읽는다(베이스 4 → 24, 전장 최고).
    Member(run, "party.warrior").bonusSpeed = 20;
    game::RefreshCombatSheet(registry, warrior, content);
    auto& runtime = registry.ctx().get<game::FTurnRuntime>();
    CHECK(runtime.controller.start(registry, 7u).has_value());
    const auto snapshot = runtime.controller.snapshot();
    CHECK(snapshot.currentTurn.has_value());
    CHECK(snapshot.currentTurn->actor == warrior);

    // (b) HP 바: 렌더가 읽는 FHealth.max가 보너스를 반영한다.
    Member(run, "party.warrior").bonusHealth = 11;
    game::RefreshCombatSheet(registry, warrior, content);
    auto& health = registry.get<game::FHealth>(warrior);
    CHECK(health.max == base.maxHealth + 11);

    // (c) 회복 상한(CaptureRunParty): KO 바닥 = max(1, floor(max × ko_recover_fraction)) — 갱신된 max 기준.
    const double fraction = game::DungeonRecovery(registry).koRecoverFraction;
    const int legacyFloor = game::KoRecoverFloor(base.maxHealth, fraction);
    const int refreshedFloor = game::KoRecoverFloor(health.max, fraction);
    CHECK(refreshedFloor != legacyFloor);  // 두 값이 같으면 이 테스트는 아무것도 증명하지 못한다
    health.current = 10;
    game::CaptureRunParty(registry);
    CHECK(Member(run, "party.warrior").health == refreshedFloor);
}

void EnemyDepthScalingMatches()
{
    FFixture fixture;
    auto& registry = fixture.registry;
    auto& run = registry.ctx().get<game::FDungeonRun>();
    const auto& content = registry.ctx().get<game::gameplay::FContentRegistry>();
    run.depth = 3;
    run.room = 2;
    const int healthPerDepth = game::DungeonBalance(registry, "enemy_health_per_depth");
    const int attackPerDepth = game::DungeonBalance(registry, "enemy_attack_per_depth");
    const int extra = 2;  // min(depth-1, 100)

    const auto enemies = Enemies(registry);
    CHECK(enemies.size() >= 3);
    for (const auto enemy : enemies) {
        const game::FBaseCombatStats base = registry.get<game::FBaseCombatStats>(enemy);
        game::RefreshCombatSheet(registry, enemy, content);
        const auto& stats = registry.get<game::FStats>(enemy);
        const auto& health = registry.get<game::FHealth>(enemy);
        CHECK(health.max == base.maxHealth + extra * healthPerDepth + run.room * 5);
        CHECK(stats.attack == base.stats.attack + extra * attackPerDepth + run.room);
        CHECK(stats.magicAttack == base.stats.magicAttack + extra * attackPerDepth + run.room);
    }

    // 파티는 적 스케일의 영향을 받지 않는다.
    const auto warrior = FindUnit(registry, "party.warrior");
    game::RefreshCombatSheet(registry, warrior, content);
    CHECK(registry.get<game::FStats>(warrior).attack ==
          registry.get<game::FBaseCombatStats>(warrior).stats.attack);
}

// ---------------------------------------------------------------------------------------------
// Wrapper (ApplyRunParty = refresh → ApplyBattleStart)
// ---------------------------------------------------------------------------------------------

void ApplyBattleStartKeepsLoadout()
{
    FFixture fixture;
    auto& registry = fixture.registry;
    auto& run = registry.ctx().get<game::FDungeonRun>();
    const auto warrior = FindUnit(registry, "party.warrior");
    CHECK(warrior != entt::null);

    Member(run, "party.warrior").skills = {game::FSkillSlot{"skill.snipe", {"rune.power"}}};
    game::ApplyRunParty(registry);

    const auto& loadout = registry.get<game::FSkillLoadout>(warrior);
    CHECK(loadout.slots.size() == 1);
    CHECK(loadout.slots[0].skillId == "skill.snipe");
    CHECK(loadout.slots[0].runeIds == std::vector<std::string>{"rune.power"});
}

void WrapperOrderKeepsHp()
{
    FFixture fixture;
    auto& registry = fixture.registry;
    auto& run = registry.ctx().get<game::FDungeonRun>();
    const auto warrior = FindUnit(registry, "party.warrior");
    CHECK(warrior != entt::null);
    const game::FBaseCombatStats base = registry.get<game::FBaseCombatStats>(warrior);

    auto& member = Member(run, "party.warrior");
    member.bonusHealth = 20;                  // max 140 → 160
    member.health = base.maxHealth + 5;       // 145: 베이스 max보다 크고 갱신 max보다 작다
    game::ApplyRunParty(registry);

    const auto& health = registry.get<game::FHealth>(warrior);
    CHECK(health.max == base.maxHealth + 20);
    // 역순(ApplyBattleStart 먼저)이면 베이스 max 140으로 clamp되어 140이 된다 — 순서 회귀 고정.
    CHECK(health.current == base.maxHealth + 5);

    // 런 잔여가 -1(미설정)이면 갱신 후 max로 시작한다(구 동작).
    member.health = -1;
    game::ApplyRunParty(registry);
    CHECK(health.current == health.max);
}

void EnemiesStartAtScaledMax()
{
    FFixture fixture;
    auto& registry = fixture.registry;
    auto& run = registry.ctx().get<game::FDungeonRun>();
    run.depth = 4;
    run.room = 1;
    const int extra = 3;  // min(depth-1, 100)
    const int healthPerDepth = game::DungeonBalance(registry, "enemy_health_per_depth");

    for (const auto enemy : Enemies(registry)) {
        registry.get<game::FHealth>(enemy).current = 1;  // 전투 중 피해를 가정
    }
    game::ApplyRunParty(registry);

    for (const auto enemy : Enemies(registry)) {
        const auto& health = registry.get<game::FHealth>(enemy);
        const game::FBaseCombatStats base = registry.get<game::FBaseCombatStats>(enemy);
        CHECK(health.max == base.maxHealth + extra * healthPerDepth + run.room * 5);
        CHECK(health.current == health.max);
    }
}

void MaxChangeClampRule()
{
    FFixture fixture;
    auto& registry = fixture.registry;
    auto& run = registry.ctx().get<game::FDungeonRun>();
    const auto& content = registry.ctx().get<game::gameplay::FContentRegistry>();
    const auto warrior = FindUnit(registry, "party.warrior");
    CHECK(warrior != entt::null);
    const game::FBaseCombatStats base = registry.get<game::FBaseCombatStats>(warrior);
    auto& member = Member(run, "party.warrior");
    auto& health = registry.get<game::FHealth>(warrior);

    // 증가: max가 늘어도 current는 유지된다(비율 보존 없음).
    member.bonusHealth = 20;
    health.current = 100;
    game::RefreshCombatSheet(registry, warrior, content);
    CHECK(health.max == base.maxHealth + 20);
    CHECK(health.current == 100);

    // 감소: max가 줄면 current는 새 max로 clamp된다.
    health.current = base.maxHealth + 10;  // 150 < 170(갱신 max)
    member.bonusHealth = 0;
    game::RefreshCombatSheet(registry, warrior, content);
    CHECK(health.max == base.maxHealth);
    CHECK(health.current == base.maxHealth);
}

void AttackModifierNotDoubleCounted()
{
    FFixture fixture;
    auto& registry = fixture.registry;
    ParkAll(registry);
    const auto& content = registry.ctx().get<game::gameplay::FContentRegistry>();
    const auto warrior = FindUnit(registry, "party.warrior");
    const auto enemies = Enemies(registry);
    CHECK(warrior != entt::null && !enemies.empty());
    Place(registry, warrior, 1, 1);
    Place(registry, enemies[0], 2, 1);

    const auto* buff = content.statuses.find("status.buff");
    CHECK(buff != nullptr);
    game::ApplyStatus(registry.get<game::FActiveStatuses>(warrior), *buff);

    // 시트를 갱신해도 상태 보정은 접히지 않는다(④ 빈 확장점 — 사용 시점 합성이 유지된다).
    game::RefreshCombatSheet(registry, warrior, content);
    CHECK(registry.get<game::FStats>(warrior).attack ==
          registry.get<game::FBaseCombatStats>(warrior).stats.attack);

    const auto resolved = game::ResolveSkillSlot(game::FSkillSlot{"skill.strike", {}}, content);
    CHECK(resolved.has_value());
    const auto& active = registry.get<game::FActiveStatuses>(warrior);
    const int modifier = game::StatusAttackModifier(active, game::CollectStatuses(active, content));
    CHECK(modifier == buff->magnitude);  // +2
    // 데미지는 attackModifier 1회분만 받는다: 시트가 접은 뒤 한 번 더 더하면 여기서 2배가 된다.
    const int expected = game::ComputeDamage(
        registry.get<game::FStats>(warrior).attack + modifier, resolved->power,
        registry.get<game::FStats>(enemies[0]).defense);
    const int before = registry.get<game::FHealth>(enemies[0]).current;

    registry.get<game::FActionPoints>(warrior).skill = registry.get<game::FActionPoints>(warrior).skillMax;
    game::FActionRequest request;
    request.actor = warrior;
    request.target = enemies[0];
    request.skillId = "skill.strike";
    request.token = StartTurnFor(registry, warrior);
    registry.ctx().get<game::FActionQueue>().pending.push_back(request);
    game::ecs::ActionResolverSystem resolver;
    resolver.update(registry, 0.f);

    CHECK(before - registry.get<game::FHealth>(enemies[0]).current == expected);
}

// ---------------------------------------------------------------------------------------------
// Preview = resolution
// ---------------------------------------------------------------------------------------------

void PreviewEqualsResolution()
{
    FFixture fixture;
    auto& registry = fixture.registry;
    ParkAll(registry);

    const auto warrior = FindUnit(registry, "party.warrior");
    const auto mage = FindUnit(registry, "party.mage");
    const auto healer = FindUnit(registry, "party.healer");
    const auto enemies = Enemies(registry);
    CHECK(warrior != entt::null && mage != entt::null && healer != entt::null);
    CHECK(enemies.size() >= 3);

    // 시나리오를 독립적으로: 대상의 HP를 채우고 상태이상을 비운다.
    const auto full = [&](std::initializer_list<entt::entity> units) {
        for (const auto unit : units) {
            auto& health = registry.get<game::FHealth>(unit);
            health.current = health.max;
            registry.get<game::FActiveStatuses>(unit).effects.clear();
        }
    };

    // 단일(물리): 전사 → 인접한 적.
    Place(registry, warrior, 1, 1);
    Place(registry, enemies[0], 2, 1);
    full({enemies[0]});
    CheckPreviewEqualsResolution(registry, warrior, {"skill.strike", {}}, enemies[0], {2, 1}, 1);

    // 확산: 대상 + 직교 이웃 — 인접한 두 적이 함께 맞는다.
    Place(registry, enemies[0], 2, 1);
    Place(registry, enemies[1], 3, 1);
    full({enemies[0], enemies[1]});
    CheckPreviewEqualsResolution(registry, warrior, {"skill.cleave", {}}, enemies[0], {2, 1}, 2);

    // 관통: 시전자→대상 직선의 셀들 — 일직선의 두 적이 함께 맞는다.
    Place(registry, enemies[0], 2, 1);
    Place(registry, enemies[1], 3, 1);
    full({enemies[0], enemies[1]});
    CheckPreviewEqualsResolution(registry, warrior, {"skill.lunge", {}}, enemies[1], {3, 1}, 2);

    // 장판 + 상태 부여: 셀 지정(안개, power 0) — 범위 안 적 전부에 디버프.
    Place(registry, mage, 2, 5);
    Place(registry, enemies[0], 2, 2);
    Place(registry, enemies[1], 1, 2);
    full({enemies[0], enemies[1]});
    CheckPreviewEqualsResolution(registry, mage, {"skill.fog", {}}, entt::null, {2, 2}, 2, false, true);

    // 단일(마법) + 상태 부여: 피해와 화상이 함께 예고된다. (안개 대상이 쓰던 (2,2)를 비운다.)
    Place(registry, mage, 2, 5);
    Place(registry, enemies[0], 0, 10);
    Place(registry, enemies[1], 1, 10);
    Place(registry, enemies[2], 2, 2);
    full({enemies[2]});
    CheckPreviewEqualsResolution(registry, mage, {"skill.firebolt", {}}, enemies[2], {2, 2}, 1, false, true);

    // 힐: 과잉 치유는 max-current로 잘린다.
    Place(registry, healer, 5, 5);
    Place(registry, warrior, 5, 6);
    full({warrior});
    registry.get<game::FHealth>(warrior).current = registry.get<game::FHealth>(warrior).max - 5;
    CheckPreviewEqualsResolution(registry, healer, {"skill.heal", {}}, warrior, {5, 6}, 1, true);

    // 마무리 일격: lethal 예고 == 실제 사망(FDead).
    Place(registry, warrior, 1, 1);
    Place(registry, enemies[0], 2, 1);
    full({enemies[0]});
    registry.get<game::FHealth>(enemies[0]).current = 1;
    CheckPreviewEqualsResolution(registry, warrior, {"skill.strike", {}}, enemies[0], {2, 1}, 1, false, false, true);
    CHECK(registry.all_of<game::FDead>(enemies[0]));
}

void PreviewLogsReason()
{
    FFixture fixture;
    auto& registry = fixture.registry;
    ParkAll(registry);
    const auto& content = registry.ctx().get<game::gameplay::FContentRegistry>();
    const auto warrior = FindUnit(registry, "party.warrior");
    const auto enemies = Enemies(registry);
    CHECK(warrior != entt::null && !enemies.empty());
    Place(registry, warrior, 1, 1);
    Place(registry, enemies[0], 2, 1);

    const auto resolved = game::ResolveSkillSlot(game::FSkillSlot{"skill.strike", {}}, content);
    CHECK(resolved.has_value());
    const int offense = registry.get<game::FStats>(warrior).attack;  // 상태 없음 → 보정 0
    const int defense = registry.get<game::FStats>(enemies[0]).defense;
    const int damage = game::ComputeDamage(offense, resolved->power, defense);

    registry.get<game::FActionPoints>(warrior).skill = registry.get<game::FActionPoints>(warrior).skillMax;
    game::FActionRequest request;
    request.actor = warrior;
    request.target = enemies[0];
    request.skillId = "skill.strike";
    request.token = StartTurnFor(registry, warrior);
    registry.ctx().get<game::FActionQueue>().pending.push_back(request);
    game::ecs::ActionResolverSystem resolver;
    resolver.update(registry, 0.f);

    const std::string reason = "[atk " + std::to_string(offense) + " + pwr " + std::to_string(resolved->power)
        + " - def " + std::to_string(defense) + " = " + std::to_string(damage) + "]";
    const auto& log = registry.ctx().get<game::FCombatLog>();
    const bool found = std::ranges::any_of(log.lines, [&](const std::string& line) {
        return line.find(reason) != std::string::npos;
    });
    CHECK(found);
    CHECK(damage == offense + resolved->power - defense);  // 이 시나리오는 하한이 걸리지 않는다

    // 하한(최소 1)이 걸리면 원식과 결과가 다르다 — 로그는 그 경우만 (min)을 붙인다.
    registry.get<game::FStats>(enemies[0]).defense = 30;
    auto& enemyHealth = registry.get<game::FHealth>(enemies[0]);
    enemyHealth.current = enemyHealth.max;
    registry.get<game::FActionPoints>(warrior).skill = registry.get<game::FActionPoints>(warrior).skillMax;
    game::FActionRequest floored;
    floored.actor = warrior;
    floored.target = enemies[0];
    floored.skillId = "skill.strike";
    floored.token = StartTurnFor(registry, warrior);
    registry.ctx().get<game::FActionQueue>().pending.push_back(floored);
    resolver.update(registry, 0.f);

    CHECK(game::ComputeDamage(offense, resolved->power, 30) == game::kMinimumDamage);  // 하한이 실제로 걸린다
    const std::string flooredReason = "[atk " + std::to_string(offense) + " + pwr "
        + std::to_string(resolved->power) + " - def 30 = 1 (min)]";
    const bool flooredFound = std::ranges::any_of(log.lines, [&](const std::string& line) {
        return line.find(flooredReason) != std::string::npos;
    });
    CHECK(flooredFound);
}

} // namespace

int main()
{
    return game_test::RunTests("pipeline", {
        {"refresh matches legacy totals", RefreshMatchesLegacyTotals},
        {"refresh is idempotent", RefreshIsIdempotent},
        {"readers see refreshed values", ReadersSeeRefreshedValues},
        {"enemy depth scaling matches", EnemyDepthScalingMatches},
        {"apply battle start keeps loadout", ApplyBattleStartKeepsLoadout},
        {"wrapper order keeps hp", WrapperOrderKeepsHp},
        {"enemies start at scaled max", EnemiesStartAtScaledMax},
        {"max change clamp rule", MaxChangeClampRule},
        {"attack modifier not double counted", AttackModifierNotDoubleCounted},
        {"preview equals resolution", PreviewEqualsResolution},
        {"preview logs reason", PreviewLogsReason},
    });
}
