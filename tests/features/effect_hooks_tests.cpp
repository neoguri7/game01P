// WP-04 typed effect hook tests. Covers the whole pipeline end to end:
//   * content: DecodeHooks provenance (source/id), schema field append, per-kind parameter validation;
//   * registry: ctx-owned FEffectRegistry, boot-late ValidateEffectContent, scope separation;
//   * dispatch: slot-bound OnHit/OnKill (leech stays on its owning skill), OnDamageTaken survivor rule,
//     dead-owner rule, once-per-turn reset per owner, code listeners, recursion cap, audit log;
//   * turn events: FTurnEnded consumption before the turn start, skip-turn firing, outcome-resolved drop;
//   * migration: leech executes through the synthesized on_hit -> heal_self hook while
//     FResolvedSkill.lifeOnHit stays for the AI (FAiQueries.cpp is unedited).
//
// Unit-level tests run on a loaded content registry (game_test::LoadTestRegistry, so the composition root
// registered the core effects); combat tests mirror FEmberBattleState::OnEnter's section like
// tests/features/pipeline_tests.cpp does. No fixture JSON is added: hooks are injected into the in-memory
// FContentRegistry or handed to FireHooks as hand-built FActiveHooks.

#define GAME01P_TEST_SUPPORT_CHECK
#include "../TestSupport.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <entt/entt.hpp>
#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/spdlog.h>

#include "core/Logger.h"
#include "core/data/FContentValue.h"
#include "core/events/FEventBus.h"
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
#include "ecs/systems/CombatOutcomeSystem.h"
#include "ecs/systems/TurnFlowSystem.h"
#include "ecs/systems/TurnSystem.h"
#include "gameplay/battle/FBattleSetup.h"
#include "gameplay/combat/FCombatApply.h"
#include "gameplay/combat/FCombatQuery.h"
#include "gameplay/combat/FCombatRules.h"
#include "gameplay/combat/FCombatTypes.h"
#include "gameplay/combat/FEffectHooks.h"
#include "gameplay/combat/FSkillResolver.h"
#include "gameplay/combat/FStatusRules.h"
#include "gameplay/data/FContentRegistry.h"
#include "gameplay/data/FEffectContent.h"
#include "gameplay/dungeon/FDungeonRun.h"
#include "gameplay/factory/FEntityFactory.h"
#include "gameplay/grid/FGridTerrain.h"
#include "gameplay/grid/FGridTypes.h"

namespace {

using game::gameplay::EHookEvent;
using game::gameplay::FEffectScope;
using game::gameplay::FEffectSpec;
using game::gameplay::FHook;

// ---------------------------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------------------------

entt::entity FindUnit(entt::registry& registry, std::string_view id) {
    for (const auto entity : registry.view<game::FUnitData>()) {
        if (registry.get<game::FUnitData>(entity).definition.id == id) {
            return entity;
        }
    }
    return entt::null;
}

std::vector<entt::entity> Enemies(entt::registry& registry) {
    std::vector<entt::entity> enemies;
    for (const auto entity : registry.view<game::FEnemy, game::FUnitData>()) {
        enemies.push_back(entity);
    }
    return enemies;
}

std::vector<entt::entity> PartyMembers(entt::registry& registry) {
    std::vector<entt::entity> members;
    for (const auto entity : registry.view<game::FPartyMember, game::FUnitData>()) {
        members.push_back(entity);
    }
    std::ranges::sort(members, [](entt::entity left, entt::entity right) {
        return entt::to_integral(left) < entt::to_integral(right);
    });
    return members;
}

game::FRunMember& Member(game::FDungeonRun& run, std::string_view id) {
    const auto found = std::ranges::find(run.party, id, &game::FRunMember::id);
    CHECK(found != run.party.end());
    return *found;
}

std::size_t MemberIndex(const game::FDungeonRun& run, std::string_view id) {
    for (std::size_t index = 0; index < run.party.size(); ++index) {
        if (run.party[index].id == id) {
            return index;
        }
    }
    throw std::runtime_error("run member not found");
}

game::gameplay::FSkillContent& FindSkill(entt::registry& registry, std::string_view id) {
    auto& content = registry.ctx().get<game::gameplay::FContentRegistry>();
    for (auto& skill : content.skills.rows) {
        if (skill.id == id) {
            return skill;
        }
    }
    throw std::runtime_error("skill row not found");
}

game::gameplay::FRuneContent& FindRune(entt::registry& registry, std::string_view id) {
    auto& content = registry.ctx().get<game::gameplay::FContentRegistry>();
    for (auto& rune : content.runes.rows) {
        if (rune.id == id) {
            return rune;
        }
    }
    throw std::runtime_error("rune row not found");
}

void Place(entt::registry& registry, entt::entity unit, int x, int y) {
    registry.get<game::FGridPosition>(unit) = {x, y};
}

/// 생성기 배치는 시드 기반이라 가변이다 — 모든 유닛을 모서리 줄로 옮겨 시나리오 배치를 결정적으로 만든다.
void ParkAll(entt::registry& registry) {
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
game::FTurnToken StartTurnFor(entt::registry& registry, entt::entity actor) {
    auto& runtime = registry.ctx().get<game::FTurnRuntime>();
    if (runtime.controller.snapshot().state != game::ETurnState::Running) {
        CHECK(runtime.controller.start(registry, 1234u).has_value());
    }
    for (int step = 0; step < 16; ++step) {
        const auto snapshot = runtime.controller.snapshot();
        CHECK(snapshot.currentTurn.has_value());
        if (snapshot.currentTurn->actor == actor) {
            return *snapshot.currentTurn;
        }
        CHECK(runtime.controller.endTurn(registry, *snapshot.currentTurn).has_value());
    }
    throw std::runtime_error("the requested actor never became the active unit");
}

/// 액터의 스킬 사용 1회를 실제 리졸버 큐 경로로 실행한다(프리뷰 → 적용 파이프라인 그대로).
void AttackOnce(entt::registry& registry, entt::entity actor, entt::entity target, const std::string& skillId) {
    registry.get<game::FActionPoints>(actor).skill = registry.get<game::FActionPoints>(actor).skillMax;
    game::FActionRequest request;
    request.actor = actor;
    request.target = target;
    request.skillId = skillId;
    request.token = StartTurnFor(registry, actor);
    registry.ctx().get<game::FActionQueue>().pending.push_back(request);
    game::ecs::ActionResolverSystem resolver;
    resolver.update(registry, 0.f);
}

/// 첫 턴 주인을 고정한다(턴 컨트롤러가 속도 순으로 세우므로).
void MakeFirst(entt::registry& registry, entt::entity actor, std::int32_t speed = 500) {
    registry.get<game::FSpeed>(actor).value = speed;
}

const game::FCombatLog& Log(entt::registry& registry) {
    return registry.ctx().get<game::FCombatLog>();
}

bool LogHasLine(const game::FCombatLog& log, std::string_view needle) {
    return std::ranges::any_of(log.lines, [&](const std::string& line) {
        return line.find(needle) != std::string::npos;
    });
}

std::size_t CountLogLines(const game::FCombatLog& log, std::string_view needle) {
    std::size_t count = 0;
    for (const std::string& line : log.lines) {
        if (line.find(needle) != std::string::npos) {
            ++count;
        }
    }
    return count;
}

/// `expected` must appear as a contiguous subsequence of `lines` (by exact match).
bool ContainsSubsequence(const std::vector<std::string>& lines, const std::vector<std::string>& expected) {
    if (expected.empty()) {
        return true;
    }
    std::size_t next = 0;
    for (const std::string& line : lines) {
        if (line == expected[next] && ++next == expected.size()) {
            return true;
        }
    }
    return false;
}

/// Captures spdlog output while alive (used only where the "nested" guard line is the subject).
struct FLogCapture {
    std::ostringstream stream;
    std::shared_ptr<spdlog::logger> logger;

    FLogCapture() {
        logger = std::make_shared<spdlog::logger>(
            "wp04-test-capture", std::make_shared<spdlog::sinks::ostream_sink_mt>(stream));
        logger->set_pattern("%v");
        logger->set_level(spdlog::level::trace);
        SetCurrentGameLogger(logger);
    }

    ~FLogCapture() { SetCurrentGameLogger(nullptr); }

    [[nodiscard]] std::string text() const { return stream.str(); }
};

// ---------------------------------------------------------------------------------------------
// Hook builders (the shape DecodeHooks produces; provenance is stamped explicitly)
// ---------------------------------------------------------------------------------------------

FEffectSpec LogSpec(std::string text) {
    FEffectSpec spec;
    spec.kind = std::string(game::gameplay::kEffectKindLog);
    spec.text = std::move(text);
    return spec;
}

FEffectSpec DamageSelfSpec(int amount) {
    FEffectSpec spec;
    spec.kind = std::string(game::gameplay::kEffectKindDamageSelf);
    spec.amount = amount;
    return spec;
}

FEffectSpec HealSelfSpec(int amount) {
    FEffectSpec spec;
    spec.kind = std::string(game::gameplay::kEffectKindHealSelf);
    spec.amount = amount;
    return spec;
}

FEffectSpec HealTargetSpec(int amount) {
    FEffectSpec spec;
    spec.kind = std::string(game::gameplay::kEffectKindHealTarget);
    spec.amount = amount;
    return spec;
}

FEffectSpec StatusSelfSpec(std::string statusId, int durationDelta = 0, int magnitudeDelta = 0) {
    FEffectSpec spec;
    spec.kind = std::string(game::gameplay::kEffectKindApplyStatusSelf);
    spec.statusId = std::move(statusId);
    spec.duration = durationDelta;
    spec.magnitude = magnitudeDelta;
    return spec;
}

FEffectSpec StatusTargetSpec(std::string statusId, int durationDelta = 0, int magnitudeDelta = 0) {
    FEffectSpec spec;
    spec.kind = std::string(game::gameplay::kEffectKindApplyStatusTarget);
    spec.statusId = std::move(statusId);
    spec.duration = durationDelta;
    spec.magnitude = magnitudeDelta;
    return spec;
}

FHook MakeHook(EHookEvent event, FEffectSpec effect, bool oncePerTurn = false) {
    FHook hook;
    hook.event = event;
    hook.eventName = game::gameplay::HookEventName(event);
    hook.effect = std::move(effect);
    hook.oncePerTurn = oncePerTurn;
    return hook;
}

FHook Sourced(FHook hook, std::string source, std::string id = {}) {
    hook.source = std::move(source);
    hook.id = id.empty() ? hook.source + "#test" : std::move(id);
    return hook;
}

/// Hand-built active hooks (bypasses BuildActiveHooks — lazy build only runs when the component is absent).
void GiveHooks(
    entt::registry& registry, entt::entity owner, std::vector<FHook> hooks, std::string slotSkillId = {}) {
    game::FActiveHooks active;
    active.hooks.reserve(hooks.size());
    for (FHook& hook : hooks) {
        active.hooks.push_back(game::FActiveHook{std::move(hook), slotSkillId});
    }
    registry.emplace_or_replace<game::FActiveHooks>(owner, std::move(active));
}

/// One hand-built `hooks[]` row, exactly as FContentLoader projects it (WP-26 nested shape).
game::FContentRow HookRow(std::string event, std::string kind, double amount = 0.0, std::string text = {},
                          std::string id = {}) {
    game::FContentRow effect;
    effect.values.resize(std::size(game::gameplay::kEffectFields));
    effect.values[game::gameplay::kEffectFieldKind] =
        game::FContentValue{game::EContentFieldType::Text, std::move(kind)};
    effect.values[game::gameplay::kEffectFieldAmount] =
        game::FContentValue{game::EContentFieldType::Number, amount};
    effect.values[game::gameplay::kEffectFieldText] =
        game::FContentValue{game::EContentFieldType::Text, std::move(text)};

    game::FContentRow row;
    row.values.resize(std::size(game::gameplay::kHookFields));
    row.values[game::gameplay::kHookFieldEvent] =
        game::FContentValue{game::EContentFieldType::Id, std::move(event)};
    row.values[game::gameplay::kHookFieldEffect] = game::FContentValue{
        game::EContentFieldType::Object, std::vector<game::FContentRow>{std::move(effect)}};
    row.values[game::gameplay::kHookFieldOncePerTurn] =
        game::FContentValue{game::EContentFieldType::Boolean, false};
    row.values[game::gameplay::kHookFieldId] = game::FContentValue{game::EContentFieldType::Id, std::move(id)};
    return row;
}

/// Records listener calls for order/owner assertions.
struct FListenerRecorder {
    std::vector<std::string> events;
    std::vector<entt::entity> owners;
};

game::FEffectListener RecordEvent(FListenerRecorder& recorder, std::string label) {
    return [&recorder, label = std::move(label)](entt::registry&, const game::FHookContext& context) {
        recorder.events.push_back(label);
        recorder.owners.push_back(context.owner);
    };
}

// ---------------------------------------------------------------------------------------------
// Fixtures
// ---------------------------------------------------------------------------------------------

/// Single write root of this suite: the save dir override (main() commits one when the runner exports
/// none). Checkpoints and their side-by-side run saves live under it — never a path the game would call a
/// real save.
std::filesystem::path SaveDirRoot() {
    if (const char* overrideDir = std::getenv("GAME01P_SAVE_DIR");
        overrideDir != nullptr && *overrideDir != '\0') {
        return std::filesystem::path(overrideDir);
    }
    return std::filesystem::current_path() / "out" / "test-saves" / "effect_hooks_tests";
}

std::filesystem::path MakeCheckpointPath(std::string_view label) {
    const std::filesystem::path root = SaveDirRoot();
    std::filesystem::create_directories(root);
    static int counter = 0;
    return root /
        ("hooks-" + std::string{label} + "-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
         std::to_string(counter++) + ".json");
}

void RemoveCheckpointFiles(const std::filesystem::path& checkpoint) {
    std::error_code error;
    std::filesystem::remove(checkpoint, error);
    std::filesystem::remove(checkpoint.string() + ".run.json", error);
}

/// Content + composition root + a battle log; no battlefield. For registry/pure-dispatch tests.
struct FUnitTest {
    std::filesystem::path checkpoint;
    game_test::FTestRegistry loaded;

    explicit FUnitTest(std::string_view label)
        : checkpoint(MakeCheckpointPath(label)),
          loaded(game_test::LoadTestRegistry(TestAssetRoots(), checkpoint.string())) {
        CHECK(loaded.content);
        CHECK(loaded.dungeon);
        registry().ctx().emplace<game::FCombatLog>();
    }

    ~FUnitTest() { RemoveCheckpointFiles(checkpoint); }

    FUnitTest(const FUnitTest&) = delete;
    FUnitTest& operator=(const FUnitTest&) = delete;

    entt::registry& registry() { return loaded.registry; }
};

/// 던전(Ember) 전투 셋업 재현: FEmberBattleState::OnEnter의 전투 구간과 같은 순서 (pipeline_tests와 동일).
/// `prepare` runs after SetupCombatUnits and before ApplyRunParty, so it can seed the run-scope loadout —
/// the exact place the lazy hook build must observe.
struct FCombatTest {
    entt::registry registry;
    std::unique_ptr<game::FScene> scene;
    std::filesystem::path checkpoint;
    std::filesystem::path runSave;

    explicit FCombatTest(std::function<void(game::FDungeonRun&)> prepare = {}) {
        checkpoint = MakeCheckpointPath("battle");
        game::FAssetManager::Initialize(registry, TestAssetRoots());
        CHECK(game::gameplay::FGameplayServices::Initialize(registry));
        CHECK(game::InitializeDungeon(registry, checkpoint.string()));
        runSave = registry.ctx().get<game::FDungeonSave>().path;
        game::StartDungeonRun(registry, 1234u, true);

        const auto document = game::BuildDungeonBattlefield(registry);
        scene = std::make_unique<game::FScene>(registry, "EffectHookTest");
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
        if (prepare) {
            prepare(registry.ctx().get<game::FDungeonRun>());
        }
        registry.ctx().emplace<game::FActionQueue>();
        registry.ctx().emplace<game::FCombatLog>();
        registry.ctx().emplace<game::FTurnRuntime>();
        registry.ctx().emplace<game::FCombatRuntime>();
        registry.ctx().emplace<game::FEventBus>();
        registry.ctx().emplace<game::FBattleOutcome>();
        game::ApplyRunParty(registry);
    }

    ~FCombatTest() {
        scene.reset();
        RemoveCheckpointFiles(checkpoint);
        std::error_code error;
        std::filesystem::remove(runSave, error);
        std::filesystem::remove(runSave.string() + ".tmp", error);
    }

    FCombatTest(const FCombatTest&) = delete;
    FCombatTest& operator=(const FCombatTest&) = delete;
};

// ---------------------------------------------------------------------------------------------
// Content decoding
// ---------------------------------------------------------------------------------------------

/// HookSourceIsOrigin: DecodeHooks stamps "skill:<id>"/"rune:<id>" and "<source>#<index>", an authored id
/// wins, and the same provenance survives into the resolved skill merge (the synthesized leech included).
void HookSourceIsOrigin() {
    const std::vector<game::FContentRow> rows{
        HookRow("on_hit", "log", 0.0, "first"),
        HookRow("on_kill", "heal_self", 4.0, {}, "authored.id"),
    };
    const std::vector<FHook> decoded = game::gameplay::DecodeHooks(rows, "skill:skill.probe");
    CHECK(decoded.size() == 2);
    CHECK(decoded[0].event == EHookEvent::OnHit);
    CHECK(decoded[0].eventName == "on_hit");
    CHECK(decoded[0].source == "skill:skill.probe");
    CHECK(decoded[0].id == "skill:skill.probe#0");
    CHECK(decoded[0].effect.kind == game::gameplay::kEffectKindLog);
    CHECK(decoded[0].effect.text == "first");
    CHECK(decoded[1].source == "skill:skill.probe");
    CHECK(decoded[1].id == "authored.id"); // the authored id wins over the index default
    CHECK(decoded[1].effect.amount == 4);

    // The decoder keeps an unknown event name (validation rejects it later) — it must not silently
    // become OnHit just because the typed default is OnHit.
    const std::vector<game::FContentRow> bad{HookRow("on_sneak", "log", 0.0, "x")};
    const std::vector<FHook> unknown = game::gameplay::DecodeHooks(bad, "rune:rune.probe");
    CHECK(unknown.size() == 1);
    CHECK(unknown[0].eventName == "on_sneak");
    CHECK(!game::gameplay::ParseHookEvent(unknown[0].eventName).has_value());

    // Resolver merge: a rune's hooks carry the rune source; the leech synthesis carries "rune:<id>".
    FUnitTest fixture{"source"};
    auto& registry = fixture.registry();
    const auto& content = registry.ctx().get<game::gameplay::FContentRegistry>();
    const auto resolved = game::ResolveSkillSlot(game::FSkillSlot{"skill.strike", {"rune.leech"}}, content);
    CHECK(resolved.has_value());
    CHECK(resolved->hooks.size() == 1);
    CHECK(resolved->hooks[0].source == "rune:rune.leech");
    CHECK(resolved->hooks[0].eventName == "on_hit");
    CHECK(resolved->hooks[0].effect.kind == game::gameplay::kEffectKindHealSelf);
}

// ---------------------------------------------------------------------------------------------
// Dispatch core rules
// ---------------------------------------------------------------------------------------------

/// HookOrderIsSlotThenRune: within one hit, hooks fire in slot order, and inside one slot the skill's own
/// hooks first, then the rune hooks in equip order.
void HookOrderIsSlotThenRune() {
    FCombatTest fixture{[](game::FDungeonRun& run) {
        Member(run, "party.warrior").skills = {
            game::FSkillSlot{"skill.strike", {"rune.power"}},
            game::FSkillSlot{"skill.strike", {}}, // second slot: same skill, no rune
        };
    }};
    ParkAll(fixture.registry);
    auto& registry = fixture.registry;
    const auto warrior = FindUnit(registry, "party.warrior");
    const auto enemies = Enemies(registry);
    CHECK(warrior != entt::null && !enemies.empty());
    Place(registry, warrior, 1, 1);
    Place(registry, enemies[0], 2, 1);

    FindSkill(registry, "skill.strike").hooks = {
        Sourced(MakeHook(EHookEvent::OnHit, LogSpec("skill hook")), "skill:skill.strike", "skill:skill.strike#0")};
    FindRune(registry, "rune.power").hooks = {
        Sourced(MakeHook(EHookEvent::OnHit, LogSpec("rune hook")), "rune:rune.power", "rune:rune.power#0")};

    AttackOnce(registry, warrior, enemies[0], "skill.strike");

    // Expected fire order: slot0 skill hook -> slot0 rune hook -> slot1 skill hook (slot order, then the
    // slot's skill before its runes).
    CHECK(ContainsSubsequence(Log(registry).lines, {"skill hook", "rune hook", "skill hook"}));
    CHECK(CountLogLines(Log(registry), "-> log") == 3); // exactly the three data hooks fired
}

/// DeadOwnerDoesNotFire: a dead unit's hooks never fire — data hooks and listeners both stay silent.
void DeadOwnerDoesNotFire() {
    FUnitTest fixture{"deadowner"};
    auto& registry = fixture.registry();
    const entt::entity dead = registry.create();
    registry.emplace<game::FHealth>(dead, game::FHealth{10, 10});
    const entt::entity alive = registry.create();
    registry.emplace<game::FHealth>(alive, game::FHealth{10, 10});

    const FHook hook = Sourced(MakeHook(EHookEvent::OnTurnStart, LogSpec("alive only")), "rune:rune.probe");
    GiveHooks(registry, dead, {hook});
    GiveHooks(registry, alive, {hook});

    FListenerRecorder recorder;
    game::RegisterHookListener(registry, EHookEvent::OnTurnStart, RecordEvent(recorder, "start"));

    // Control: the same hook fires for a living owner.
    CHECK(game::FireHooks(registry, EHookEvent::OnTurnStart, game::FHookContext{.owner = alive}) == 2);
    CHECK(LogHasLine(Log(registry), "alive only"));

    CHECK(game::ApplyDamageToUnit(registry, dead, 999));
    CHECK(registry.all_of<game::FDead>(dead));
    const std::size_t linesBefore = Log(registry).lines.size();
    CHECK(game::FireHooks(registry, EHookEvent::OnTurnStart, game::FHookContext{.owner = dead}) == 0);
    CHECK(Log(registry).lines.size() == linesBefore); // no audit line for the dead owner
}

/// HealDeadTargetIsNoOp: heal_target on the dead `other` is a no-op; the hook itself still fires.
void HealDeadTargetIsNoOp() {
    FUnitTest fixture{"healdead"};
    auto& registry = fixture.registry();
    const entt::entity attacker = registry.create();
    const entt::entity victim = registry.create();
    registry.emplace<game::FHealth>(attacker, game::FHealth{10, 10});
    registry.emplace<game::FHealth>(victim, game::FHealth{10, 10});
    CHECK(game::ApplyDamageToUnit(registry, victim, 999));

    GiveHooks(registry, attacker,
              {Sourced(MakeHook(EHookEvent::OnKill, HealTargetSpec(10)), "rune:rune.probe")}, "skill.strike");
    const int fired = game::FireHooks(
        registry, EHookEvent::OnKill,
        game::FHookContext{.owner = attacker, .other = victim, .skillId = "skill.strike"});
    CHECK(fired == 1); // the hook fired; the heal was the no-op
    CHECK(registry.get<game::FHealth>(victim).current == 0);
    CHECK(!LogHasLine(Log(registry), "heals"));
}

/// SelfKillStopsRemainingHooks: damage_self that kills the owner stops that owner's remaining hooks, and
/// the effect speaks to the player like the status tick does: "<name> takes N damage" and "<name> falls!".
void SelfKillStopsRemainingHooks() {
    FUnitTest fixture{"selfkill"};
    auto& registry = fixture.registry();
    const entt::entity unit = registry.create();
    registry.emplace<game::FHealth>(unit, game::FHealth{3, 10});

    GiveHooks(registry, unit,
              {Sourced(MakeHook(EHookEvent::OnTurnStart, DamageSelfSpec(99)), "rune:rune.probe"),
               Sourced(MakeHook(EHookEvent::OnTurnStart, LogSpec("never runs")), "rune:rune.probe2")});
    const int fired = game::FireHooks(registry, EHookEvent::OnTurnStart, game::FHookContext{.owner = unit});
    CHECK(fired == 1);
    CHECK(registry.all_of<game::FDead>(unit));
    CHECK(!LogHasLine(Log(registry), "never runs"));
    CHECK(CountLogLines(Log(registry), "on_turn_start -> damage_self") == 1);
    CHECK(LogHasLine(Log(registry), "takes 99 damage")); // the drop is visible in the combat log
    CHECK(LogHasLine(Log(registry), "falls!"));          // and so is the KO
}

/// CodeListenerFiresWithoutDataHook: listeners run even when the owner has not a single data hook, and the
/// return value counts them.
void CodeListenerFiresWithoutDataHook() {
    FUnitTest fixture{"listeneronly"};
    auto& registry = fixture.registry();
    const entt::entity unit = registry.create();
    registry.emplace<game::FHealth>(unit, game::FHealth{10, 10});

    FListenerRecorder recorder;
    game::RegisterHookListener(registry, EHookEvent::OnTurnStart, RecordEvent(recorder, "L"));
    const int fired = game::FireHooks(registry, EHookEvent::OnTurnStart, game::FHookContext{.owner = unit});
    CHECK(fired == 1);
    CHECK(recorder.events == std::vector<std::string>{"L"});
    // The lazy build ran (and found no loadout): a unit without hooks still gets the empty component.
    CHECK(registry.all_of<game::FActiveHooks>(unit));
    CHECK(registry.get<game::FActiveHooks>(unit).hooks.empty());
}

/// CodeListenerOrder: listeners fire after the data hooks, in registration order; other events stay silent.
void CodeListenerOrder() {
    FUnitTest fixture{"listenerorder"};
    auto& registry = fixture.registry();
    const entt::entity unit = registry.create();
    registry.emplace<game::FHealth>(unit, game::FHealth{10, 10});
    GiveHooks(registry, unit, {Sourced(MakeHook(EHookEvent::OnTurnStart, LogSpec("data")), "rune:rune.probe")});

    FListenerRecorder recorder;
    game::RegisterHookListener(registry, EHookEvent::OnTurnStart, RecordEvent(recorder, "first"));
    game::RegisterHookListener(registry, EHookEvent::OnTurnStart, RecordEvent(recorder, "second"));
    game::RegisterHookListener(registry, EHookEvent::OnKill, RecordEvent(recorder, "other event"));

    const int fired = game::FireHooks(registry, EHookEvent::OnTurnStart, game::FHookContext{.owner = unit});
    CHECK(fired == 3); // one data hook + two listeners
    CHECK(recorder.events == (std::vector<std::string>{"first", "second"}));
    // The data hook's audit line precedes the listener callbacks (listeners do not log here, but the
    // one data line proves the data pass happened first and was not skipped).
    CHECK(LogHasLine(Log(registry), "[hook] rune:rune.probe on_turn_start -> log"));
}

/// OncePerTurnResetPerOwner: the once-per-turn mark is per owner, resets on that owner's OnTurnStart, and a
/// second dispatch in the same frame cannot reset or refire it.
void OncePerTurnResetPerOwner() {
    FUnitTest fixture{"once"};
    auto& registry = fixture.registry();
    const entt::entity one = registry.create();
    const entt::entity two = registry.create();
    registry.emplace<game::FHealth>(one, game::FHealth{10, 10});
    registry.emplace<game::FHealth>(two, game::FHealth{10, 10});
    GiveHooks(registry, one,
              {Sourced(MakeHook(EHookEvent::OnTurnStart, LogSpec("once-one"), true), "rune:rune.probe", "once.one")});
    GiveHooks(registry, two,
              {Sourced(MakeHook(EHookEvent::OnTurnStart, LogSpec("once-two"), true), "rune:rune.probe", "once.two")});

    CHECK(game::FireHooks(registry, EHookEvent::OnTurnStart, game::FHookContext{.owner = one}) == 1);
    CHECK(game::FireHooks(registry, EHookEvent::OnTurnStart, game::FHookContext{.owner = one}) == 0); // marked
    CHECK(game::FireHooks(registry, EHookEvent::OnTurnStart, game::FHookContext{.owner = two}) == 1);
    CHECK(CountLogLines(Log(registry), "once-one") == 1);
    CHECK(CountLogLines(Log(registry), "once-two") == 1);

    // The owner's own turn start resets its marks (and fires the start hooks again); the other unit keeps
    // its mark — the reset is per owner.
    const game::FTurnToken firstTurn{1, 1, 1, one};
    game::FireTurnStartedHooks(registry, firstTurn);
    CHECK(CountLogLines(Log(registry), "once-one") == 2);
    CHECK(game::FireHooks(registry, EHookEvent::OnTurnStart, game::FHookContext{.owner = two}) == 0);
    // Same token again: deduped, so no reset and no refire.
    game::FireTurnStartedHooks(registry, firstTurn);
    CHECK(CountLogLines(Log(registry), "once-one") == 2);
    // A new turn resets again.
    game::FireTurnStartedHooks(registry, game::FTurnToken{1, 1, 2, one});
    CHECK(CountLogLines(Log(registry), "once-one") == 3);
}

/// HookDamageDoesNotRetrigger: damage created by a hook effect goes through ApplyDamageToUnit and never
/// re-enters dispatch (no recursive OnDamageTaken).
void HookDamageDoesNotRetrigger() {
    FUnitTest fixture{"retrigger"};
    auto& registry = fixture.registry();
    const entt::entity attacker = registry.create();
    const entt::entity victim = registry.create();
    registry.emplace<game::FHealth>(attacker, game::FHealth{10, 10});
    registry.emplace<game::FHealth>(victim, game::FHealth{20, 20});
    GiveHooks(registry, victim, {Sourced(MakeHook(EHookEvent::OnDamageTaken, DamageSelfSpec(1)), "rune:rune.probe")});

    FListenerRecorder recorder;
    game::RegisterHookListener(registry, EHookEvent::OnDamageTaken, RecordEvent(recorder, "taken"));
    const int fired = game::FireHooks(
        registry, EHookEvent::OnDamageTaken, game::FHookContext{.owner = victim, .other = attacker});
    CHECK(fired == 2); // the data hook + the listener, once each
    CHECK(registry.get<game::FHealth>(victim).current == 19); // the hook's self-damage landed
    CHECK(recorder.events == std::vector<std::string>{"taken"}); // and did not refire the dispatch
}

/// NestedDepthCap: one nested level is allowed — a code listener that calls FireHooks again gets that
/// dispatch, flagged `nested` in the log. A second level is refused with a warning and returns 0. The
/// nested call lazily builds FActiveHooks on a second unit, exercising the storage-move safety path.
void NestedDepthCap() {
    FUnitTest fixture{"nested"};
    auto& registry = fixture.registry();
    const entt::entity unit = registry.create();
    registry.emplace<game::FHealth>(unit, game::FHealth{10, 10});
    GiveHooks(registry, unit, {Sourced(MakeHook(EHookEvent::OnTurnStart, LogSpec("outer-data")), "rune:probe0")});

    // The nested target has no FActiveHooks yet: the nested dispatch builds them lazily, which may move
    // the component pool's storage while the outer dispatch is still running.
    const entt::entity other = registry.create();
    registry.emplace<game::FHealth>(other, game::FHealth{10, 10});
    registry.emplace<game::FSkillLoadout>(other, game::FSkillLoadout{{game::FSkillSlot{"skill.strike", {}}}});
    FindSkill(registry, "skill.strike").hooks = {Sourced(MakeHook(EHookEvent::OnTurnStart, LogSpec("nested-data")),
                                                         "skill:skill.strike", "skill:skill.strike#0")};

    int listenerCalls = 0;
    int nestedReturn = -1;
    int innerReturn = -1;
    game::RegisterHookListener(registry, EHookEvent::OnTurnStart,
                               [&](entt::registry& r, const game::FHookContext&) {
                                   ++listenerCalls;
                                   if (listenerCalls == 1) {
                                       nestedReturn = game::FireHooks(
                                           r, EHookEvent::OnTurnStart, game::FHookContext{.owner = other});
                                   } else {
                                       innerReturn =
                                           game::FireHooks(r, EHookEvent::OnTurnStart, game::FHookContext{.owner = other});
                                   }
                               });

    FLogCapture capture;
    const int outer = game::FireHooks(registry, EHookEvent::OnTurnStart, game::FHookContext{.owner = unit});
    CHECK(outer == 2);         // the outer data hook + the listener
    CHECK(listenerCalls == 2); // the nested dispatch re-entered the listener once
    CHECK(nestedReturn == 2);  // one nested level ran: the lazily built data hook + the listener
    CHECK(innerReturn == 0);   // a second nested level is refused
    CHECK(LogHasLine(Log(registry), "outer-data"));
    CHECK(LogHasLine(Log(registry), "nested-data"));
    CHECK(LogHasLine(Log(registry), "[hook] nested on_turn_start")); // the nested dispatch is flagged
    CHECK(LogHasLine(Log(registry), "[hook] nested skill:skill.strike on_turn_start -> log"));
    CHECK(capture.text().find("refused") != std::string::npos); // depth 2 stopped with a warning
}

// ---------------------------------------------------------------------------------------------
// Action resolution: OnHit / OnKill / OnDamageTaken
// ---------------------------------------------------------------------------------------------

/// OnKillFiresForKiller: a lethal skill hit fires OnKill for the killer, with the audit line and owner.
void OnKillFiresForKiller() {
    FCombatTest fixture;
    ParkAll(fixture.registry);
    auto& registry = fixture.registry;
    const auto warrior = FindUnit(registry, "party.warrior");
    const auto enemies = Enemies(registry);
    CHECK(warrior != entt::null);
    CHECK(!enemies.empty());
    Place(registry, warrior, 1, 1);
    Place(registry, enemies[0], 2, 1);
    registry.get<game::FHealth>(enemies[0]).current = 1;
    FindSkill(registry, "skill.strike").hooks = {
        Sourced(MakeHook(EHookEvent::OnKill, LogSpec("kill")), "skill:skill.strike", "skill:skill.strike#0")};

    FListenerRecorder kills;
    game::RegisterHookListener(registry, EHookEvent::OnKill, RecordEvent(kills, "kill"));
    AttackOnce(registry, warrior, enemies[0], "skill.strike");

    CHECK(registry.all_of<game::FDead>(enemies[0]));
    CHECK(kills.events == std::vector<std::string>{"kill"});
    CHECK(kills.owners == std::vector<entt::entity>{warrior});
    CHECK(LogHasLine(Log(registry), "[hook] skill:skill.strike on_kill -> log"));
}

/// OnDamageTakenOrder: apply damage -> OnHit(attacker) -> then either OnKill (died) or OnDamageTaken
/// (survived). Hooks may kill the target, so the branch reads IsAlive after the OnHit dispatch.
void OnDamageTakenOrder() {
    FCombatTest fixture;
    ParkAll(fixture.registry);
    auto& registry = fixture.registry;
    const auto warrior = FindUnit(registry, "party.warrior");
    const auto enemies = Enemies(registry);
    CHECK(enemies.size() >= 2);
    Place(registry, warrior, 1, 1);
    Place(registry, enemies[0], 2, 1);
    Place(registry, enemies[1], 1, 2);

    FListenerRecorder order;
    game::RegisterHookListener(registry, EHookEvent::OnHit, RecordEvent(order, "on_hit"));
    game::RegisterHookListener(registry, EHookEvent::OnDamageTaken, RecordEvent(order, "on_damage_taken"));
    game::RegisterHookListener(registry, EHookEvent::OnKill, RecordEvent(order, "on_kill"));

    // Survivor: OnHit then OnDamageTaken, owners = attacker then victim.
    AttackOnce(registry, warrior, enemies[0], "skill.strike");
    CHECK(order.events == (std::vector<std::string>{"on_hit", "on_damage_taken"}));
    CHECK(order.owners == (std::vector<entt::entity>{warrior, enemies[0]}));

    // Lethal: OnHit then OnKill; owners = attacker in both.
    order.events.clear();
    order.owners.clear();
    registry.get<game::FHealth>(enemies[1]).current = 1;
    AttackOnce(registry, warrior, enemies[1], "skill.strike");
    CHECK(registry.all_of<game::FDead>(enemies[1]));
    CHECK(order.events == (std::vector<std::string>{"on_hit", "on_kill"}));
    CHECK(order.owners == (std::vector<entt::entity>{warrior, warrior}));
}

/// OnDamageTakenRequiresSurvivor: the victim's OnDamageTaken fires only when the victim survived the hit.
void OnDamageTakenRequiresSurvivor() {
    FCombatTest fixture;
    ParkAll(fixture.registry);
    auto& registry = fixture.registry;
    const auto warrior = FindUnit(registry, "party.warrior");
    const auto enemies = Enemies(registry);
    CHECK(enemies.size() >= 2);
    Place(registry, warrior, 1, 1);
    Place(registry, enemies[0], 2, 1);
    Place(registry, enemies[1], 1, 2);

    FListenerRecorder taken;
    game::RegisterHookListener(registry, EHookEvent::OnDamageTaken, RecordEvent(taken, "taken"));

    AttackOnce(registry, warrior, enemies[0], "skill.strike");
    CHECK(taken.owners == std::vector<entt::entity>{enemies[0]});

    taken.events.clear();
    taken.owners.clear();
    registry.get<game::FHealth>(enemies[1]).current = 1;
    AttackOnce(registry, warrior, enemies[1], "skill.strike");
    CHECK(registry.all_of<game::FDead>(enemies[1]));
    CHECK(taken.events.empty()); // a lethal hit fires OnKill, never OnDamageTaken
}

/// OnKillRequiresLiveKiller: a hook that kills the attacker during OnHit suppresses that attacker's OnKill.
void OnKillRequiresLiveKiller() {
    FCombatTest fixture;
    ParkAll(fixture.registry);
    auto& registry = fixture.registry;
    const auto warrior = FindUnit(registry, "party.warrior");
    const auto enemies = Enemies(registry);
    CHECK(warrior != entt::null && !enemies.empty());
    Place(registry, warrior, 1, 1);
    Place(registry, enemies[0], 2, 1);
    registry.get<game::FHealth>(enemies[0]).current = 1;
    GiveHooks(registry, warrior,
              {Sourced(MakeHook(EHookEvent::OnHit, DamageSelfSpec(999)), "skill:skill.probe")}, "skill.strike");

    FListenerRecorder kills;
    game::RegisterHookListener(registry, EHookEvent::OnKill, RecordEvent(kills, "kill"));
    AttackOnce(registry, warrior, enemies[0], "skill.strike");

    CHECK(registry.all_of<game::FDead>(enemies[0]));
    CHECK(registry.all_of<game::FDead>(warrior)); // the OnHit hook killed the attacker
    CHECK(kills.events.empty());                  // dead owner: the OnKill dispatch is refused
}

/// StatusKillDoesNotFireOnKill: a status tick kill is not a skill kill — OnKill stays silent.
void StatusKillDoesNotFireOnKill() {
    FCombatTest fixture;
    ParkAll(fixture.registry);
    auto& registry = fixture.registry;
    const auto& content = registry.ctx().get<game::gameplay::FContentRegistry>();
    const auto enemies = Enemies(registry);
    CHECK(!enemies.empty());
    const auto enemy = enemies[0];
    Place(registry, enemy, 2, 1);
    MakeFirst(registry, enemy); // the DoT ticks on this unit's own turn start

    const auto* burn = content.statuses.find("status.burn");
    CHECK(burn != nullptr);
    game::ApplyStatus(registry.get<game::FActiveStatuses>(enemy), *burn); // magnitude 3, lethal on 1 HP
    registry.get<game::FHealth>(enemy).current = 1;

    FListenerRecorder kills;
    game::RegisterHookListener(registry, EHookEvent::OnKill, RecordEvent(kills, "kill"));
    auto& runtime = registry.ctx().get<game::FTurnRuntime>();
    CHECK(runtime.controller.start(registry, 99u).has_value());
    const auto snapshot = runtime.controller.snapshot();
    CHECK(snapshot.currentTurn.has_value());
    CHECK(snapshot.currentTurn->actor == enemy);

    game::ecs::TurnFlowSystem flow;
    flow.update(registry, 0.f);
    CHECK(registry.all_of<game::FDead>(enemy));
    CHECK(kills.events.empty());
}

// ---------------------------------------------------------------------------------------------
// Turn events
// ---------------------------------------------------------------------------------------------

/// HudEndTurnFiresOnTurnEnd: a HUD-style FEndTurn request reaches OnTurnEnd through FTurnEnded, and a
/// second update in the same frame does not double-fire it.
void HudEndTurnFiresOnTurnEnd() {
    FCombatTest fixture;
    ParkAll(fixture.registry);
    auto& registry = fixture.registry;
    auto& runtime = registry.ctx().get<game::FTurnRuntime>();
    CHECK(runtime.controller.start(registry, 7u).has_value());
    const game::FTurnToken token = *runtime.controller.snapshot().currentTurn;

    FListenerRecorder ended;
    game::RegisterHookListener(registry, EHookEvent::OnTurnEnd, RecordEvent(ended, "end"));

    runtime.requests.push_back(game::FEndTurn{token}); // the way the HUD ends a turn
    game::ecs::TurnSystem turns;
    turns.update(registry, 0.f);
    game::ecs::TurnFlowSystem flow;
    flow.update(registry, 0.f);

    CHECK(ended.owners == std::vector<entt::entity>{token.actor});
    // Same frame again: FTurnEnded is still in the frame bus; the serial dedupe keeps it at one fire.
    flow.update(registry, 0.f);
    CHECK(ended.owners == std::vector<entt::entity>{token.actor});
}

/// FinalTurnEndNotFiredAfterOutcome: the outcome->resolved early return drops the final turn's OnTurnEnd —
/// the documented, intended loss (victory is OnRoomClear's job).
void FinalTurnEndNotFiredAfterOutcome() {
    FCombatTest fixture;
    ParkAll(fixture.registry);
    auto& registry = fixture.registry;
    const auto warrior = FindUnit(registry, "party.warrior");
    CHECK(warrior != entt::null);
    MakeFirst(registry, warrior);
    auto& runtime = registry.ctx().get<game::FTurnRuntime>();
    CHECK(runtime.controller.start(registry, 7u).has_value());
    const game::FTurnToken token = *runtime.controller.snapshot().currentTurn;
    CHECK(token.actor == warrior);

    FListenerRecorder ended;
    game::RegisterHookListener(registry, EHookEvent::OnTurnEnd, RecordEvent(ended, "end"));

    for (const auto enemy : Enemies(registry)) {
        CHECK(game::ApplyDamageToUnit(registry, enemy, registry.get<game::FHealth>(enemy).current));
    }
    runtime.requests.push_back(game::FEndTurn{token});
    game::ecs::TurnSystem turns;
    turns.update(registry, 0.f); // endTurn -> Completed FTurnEnded is in the frame bus now
    const auto* bus = registry.ctx().find<game::FEventBus>();
    CHECK(bus != nullptr);
    CHECK(!bus->frameEvents<game::FTurnEnded>().empty());

    game::ecs::CombatOutcomeSystem outcome;
    outcome.update(registry, 0.f);
    CHECK(registry.ctx().get<game::FBattleOutcome>().resolved);

    game::ecs::TurnFlowSystem flow;
    flow.update(registry, 0.f); // resolved guard: consumed nothing
    CHECK(ended.events.empty()); // the final OnTurnEnd is the intended loss
}

/// TurnStartHookOnSkipTurn: OnTurnStart fires after the tick and before the skipTurn FEndTurn is queued,
/// so a unit whose turn is skipped still runs its start hooks.
void TurnStartHookOnSkipTurn() {
    FCombatTest fixture;
    ParkAll(fixture.registry);
    auto& registry = fixture.registry;
    const auto& content = registry.ctx().get<game::gameplay::FContentRegistry>();
    const auto warrior = FindUnit(registry, "party.warrior");
    CHECK(warrior != entt::null);
    MakeFirst(registry, warrior);
    const auto* freeze = content.statuses.find("status.freeze");
    CHECK(freeze != nullptr);
    game::ApplyStatus(registry.get<game::FActiveStatuses>(warrior), *freeze); // magnitude 0 -> always skip

    FListenerRecorder started;
    game::RegisterHookListener(registry, EHookEvent::OnTurnStart, RecordEvent(started, "start"));
    auto& runtime = registry.ctx().get<game::FTurnRuntime>();
    CHECK(runtime.controller.start(registry, 7u).has_value());
    CHECK(runtime.controller.snapshot().currentTurn->actor == warrior);

    game::ecs::TurnFlowSystem flow;
    flow.update(registry, 0.f);
    CHECK(started.owners == std::vector<entt::entity>{warrior});
    CHECK(LogHasLine(Log(registry), "cannot act - turn skipped"));
    CHECK(!runtime.requests.empty()); // the skip queued the end-turn after the start hooks fired
}

// ---------------------------------------------------------------------------------------------
// Lazy build / battle immutability
// ---------------------------------------------------------------------------------------------

/// HooksBuiltAfterLoadoutApplied: the first fire builds from the applied run loadout, not from the
/// unit-content loadout that existed when SetupCombatUnits ran.
void HooksBuiltAfterLoadoutApplied() {
    FCombatTest fixture{[](game::FDungeonRun& run) {
        Member(run, "party.warrior").skills = {game::FSkillSlot{"skill.snipe", {}}};
    }};
    auto& registry = fixture.registry;
    const auto warrior = FindUnit(registry, "party.warrior");
    CHECK(warrior != entt::null);
    CHECK(registry.get<game::FSkillLoadout>(warrior).slots[0].skillId == "skill.snipe");
    FindSkill(registry, "skill.snipe").hooks = {
        Sourced(MakeHook(EHookEvent::OnTurnStart, LogSpec("lazy")), "skill:skill.snipe", "skill:skill.snipe#0")};

    CHECK(!registry.all_of<game::FActiveHooks>(warrior)); // nothing built it yet
    (void)game::FireHooks(registry, EHookEvent::OnTurnStart, game::FHookContext{.owner = warrior});

    const auto* active = registry.try_get<game::FActiveHooks>(warrior);
    CHECK(active != nullptr);
    CHECK(active->hooks.size() == 1);
    CHECK(active->hooks[0].slotSkillId == "skill.snipe"); // the run loadout, not strike/cleave/lunge
    CHECK(active->hooks[0].hook.source == "skill:skill.snipe");
    CHECK(LogHasLine(Log(registry), "lazy"));
}

/// HooksBuiltOncePerBattle: after the first build the FActiveHooks cache is reused — a loadout change is
/// invisible to dispatch (battle loadouts are immutable, and the cache must not follow one anyway).
void HooksBuiltOncePerBattle() {
    FCombatTest fixture;
    auto& registry = fixture.registry;
    const auto warrior = FindUnit(registry, "party.warrior");
    CHECK(warrior != entt::null);
    FindSkill(registry, "skill.strike").hooks = {
        Sourced(MakeHook(EHookEvent::OnTurnStart, LogSpec("first")), "skill:skill.strike", "skill:skill.strike#0")};
    FindSkill(registry, "skill.snipe").hooks = {
        Sourced(MakeHook(EHookEvent::OnTurnStart, LogSpec("second")), "skill:skill.snipe", "skill:skill.snipe#0")};

    CHECK(game::FireHooks(registry, EHookEvent::OnTurnStart, game::FHookContext{.owner = warrior}) == 1);
    // In-battle loadout mutation is illegal in production (camp-only); the cache must still ignore it.
    registry.get<game::FSkillLoadout>(warrior).slots = {game::FSkillSlot{"skill.snipe", {}}};
    CHECK(game::FireHooks(registry, EHookEvent::OnTurnStart, game::FHookContext{.owner = warrior}) == 1);

    const auto& active = registry.get<game::FActiveHooks>(warrior);
    CHECK(active.hooks.size() == 1);
    CHECK(active.hooks[0].hook.source == "skill:skill.strike"); // built once, reused
    CHECK(CountLogLines(Log(registry), "first") == 2);
    CHECK(!LogHasLine(Log(registry), "second"));
}

/// LoadoutImmutableDuringBattle: run-scope loadout edits are camp-only (ValidateCampSlot), so the battle
/// loadout cannot change mid-battle.
void LoadoutImmutableDuringBattle() {
    FCombatTest fixture{[](game::FDungeonRun& run) {
        Member(run, "party.warrior").skills = {game::FSkillSlot{"skill.strike", {}}};
        run.runes = {"rune.power"};
    }};
    auto& registry = fixture.registry;
    auto& run = registry.ctx().get<game::FDungeonRun>();
    CHECK(run.phase == game::EDungeonPhase::Battle);
    const std::size_t index = MemberIndex(run, "party.warrior");

    const std::optional<std::string> error = game::EquipDungeonRune(registry, index, 0, 0);
    CHECK(error.has_value()); // "Runes can only be changed at camp."
    CHECK(Member(run, "party.warrior").skills[0].runeIds.empty());
    CHECK(run.runes == std::vector<std::string>{"rune.power"}); // the rune was not consumed
}

// ---------------------------------------------------------------------------------------------
// Room clear
// ---------------------------------------------------------------------------------------------

/// OnRoomClearFiresForEachPartyMember: one dispatch per living party member, in entity-id order.
void OnRoomClearFiresForEachPartyMember() {
    FCombatTest fixture;
    ParkAll(fixture.registry);
    auto& registry = fixture.registry;
    for (const auto enemy : Enemies(registry)) {
        CHECK(game::ApplyDamageToUnit(registry, enemy, registry.get<game::FHealth>(enemy).current));
    }

    FListenerRecorder cleared;
    game::RegisterHookListener(registry, EHookEvent::OnRoomClear, RecordEvent(cleared, "clear"));
    game::ecs::CombatOutcomeSystem outcome;
    outcome.update(registry, 0.f);

    CHECK(registry.ctx().get<game::FBattleOutcome>().victory);
    const auto expected = PartyMembers(registry);
    CHECK(expected.size() == 4);
    CHECK(cleared.owners == expected);
}

/// OnRoomClearHealsBeforeCapture: the hook's heal lands before the next frame's CaptureRunParty records it.
void OnRoomClearHealsBeforeCapture() {
    FCombatTest fixture;
    ParkAll(fixture.registry);
    auto& registry = fixture.registry;
    const auto warrior = FindUnit(registry, "party.warrior");
    CHECK(warrior != entt::null);
    GiveHooks(registry, warrior, {Sourced(MakeHook(EHookEvent::OnRoomClear, HealSelfSpec(30)), "skill:probe")});
    auto& health = registry.get<game::FHealth>(warrior);
    health.current = health.max - 40;

    for (const auto enemy : Enemies(registry)) {
        CHECK(game::ApplyDamageToUnit(registry, enemy, registry.get<game::FHealth>(enemy).current));
    }
    game::ecs::CombatOutcomeSystem outcome;
    outcome.update(registry, 0.f);
    CHECK(health.current == health.max - 10);

    game::CaptureRunParty(registry); // the next frame's CompleteDungeonBattle step
    CHECK(Member(registry.ctx().get<game::FDungeonRun>(), "party.warrior").health == health.current);
}

/// OnRoomClearSkipsDeadMembers: a fallen party member gets no OnRoomClear.
void OnRoomClearSkipsDeadMembers() {
    FCombatTest fixture;
    ParkAll(fixture.registry);
    auto& registry = fixture.registry;
    const auto healer = FindUnit(registry, "party.healer");
    CHECK(healer != entt::null);
    CHECK(game::ApplyDamageToUnit(registry, healer, registry.get<game::FHealth>(healer).current));

    for (const auto enemy : Enemies(registry)) {
        CHECK(game::ApplyDamageToUnit(registry, enemy, registry.get<game::FHealth>(enemy).current));
    }
    FListenerRecorder cleared;
    game::RegisterHookListener(registry, EHookEvent::OnRoomClear, RecordEvent(cleared, "clear"));
    game::ecs::CombatOutcomeSystem outcome;
    outcome.update(registry, 0.f);

    CHECK(registry.ctx().get<game::FBattleOutcome>().victory);
    CHECK(cleared.owners.size() == 3);
    CHECK(std::ranges::find(cleared.owners, healer) == cleared.owners.end());
}

/// ContinueDoesNotRefireRoomClear: the continuedAfterRoom path sets `resolved` directly, so the outcome
/// system early-returns and OnRoomClear never fires for an already-cleared room.
void ContinueDoesNotRefireRoomClear() {
    FCombatTest fixture;
    ParkAll(fixture.registry);
    auto& registry = fixture.registry;
    auto& outcome = registry.ctx().get<game::FBattleOutcome>();
    outcome.resolved = true;
    outcome.victory = true; // FEmberBattleState's continuedAfterRoom branch

    FListenerRecorder cleared;
    game::RegisterHookListener(registry, EHookEvent::OnRoomClear, RecordEvent(cleared, "clear"));
    game::ecs::CombatOutcomeSystem system;
    system.update(registry, 0.f);
    CHECK(cleared.events.empty());
}

// ---------------------------------------------------------------------------------------------
// Leech migration
// ---------------------------------------------------------------------------------------------

/// LeechMovedToHook: the rune's heal executes through the synthesized `on_hit -> heal_self` hook — same
/// amount, same per-hit trigger, audit line instead of the old direct heal.
void LeechMovedToHook() {
    FCombatTest fixture{[](game::FDungeonRun& run) {
        Member(run, "party.warrior").skills = {game::FSkillSlot{"skill.strike", {"rune.leech"}}};
    }};
    ParkAll(fixture.registry);
    auto& registry = fixture.registry;
    const auto warrior = FindUnit(registry, "party.warrior");
    const auto enemies = Enemies(registry);
    CHECK(warrior != entt::null && !enemies.empty());
    Place(registry, warrior, 1, 1);
    Place(registry, enemies[0], 2, 1);
    auto& health = registry.get<game::FHealth>(warrior);
    health.current = health.max - 10;

    const auto& content = registry.ctx().get<game::gameplay::FContentRegistry>();
    const auto resolved = game::ResolveSkillSlot(game::FSkillSlot{"skill.strike", {"rune.leech"}}, content);
    CHECK(resolved.has_value());
    CHECK(resolved->lifeOnHit == 2); // the AI-visible field keeps the rune value
    CHECK(resolved->hooks.size() == 1);
    CHECK(resolved->hooks[0].effect.kind == game::gameplay::kEffectKindHealSelf);
    CHECK(resolved->hooks[0].effect.amount == 2);
    CHECK(resolved->hooks[0].source == "rune:rune.leech");

    AttackOnce(registry, warrior, enemies[0], "skill.strike");
    CHECK(health.current == health.max - 8); // leeched +2 through the hook
    CHECK(LogHasLine(Log(registry), "[hook] rune:rune.leech on_hit -> heal_self"));
    CHECK(LogHasLine(Log(registry), "heals +2 HP"));
    CHECK(!LogHasLine(Log(registry), "leech +")); // no direct heal path exists any more
}

/// LeechAiValueUnchanged: FResolvedSkill.lifeOnHit stays for the AI (FAiQueries.cpp reads it, unedited);
/// the hook pipeline does not move the field's value or the skill's numbers.
void LeechAiValueUnchanged() {
    FUnitTest fixture{"leechai"};
    auto& registry = fixture.registry();
    const auto& content = registry.ctx().get<game::gameplay::FContentRegistry>();
    const auto* leech = content.runes.find("rune.leech");
    CHECK(leech != nullptr);

    const auto resolved = game::ResolveSkillSlot(game::FSkillSlot{"skill.strike", {"rune.leech"}}, content);
    CHECK(resolved.has_value());
    CHECK(resolved->lifeOnHit == leech->lifeOnHit); // unchanged: still the rune's value
    CHECK(resolved->lifeOnHit > 0);
    const auto* strike = content.skills.find("skill.strike");
    CHECK(strike != nullptr);
    CHECK(resolved->power == strike->power); // numbers the AI values are untouched
    CHECK(resolved->statuses.empty());

    // The single execution path is the hook: without the rune there is no synthesis at all.
    const auto plain = game::ResolveSkillSlot(game::FSkillSlot{"skill.strike", {}}, content);
    CHECK(plain.has_value());
    CHECK(plain->lifeOnHit == 0);
    CHECK(plain->hooks.empty());
}

/// LeechOnlyOnOwningSkill: a leech rune on skill A must not heal when the unit attacks with skill B — the
/// active hook is bound to the slot that carries the acting skill.
void LeechOnlyOnOwningSkill() {
    FCombatTest fixture{[](game::FDungeonRun& run) {
        Member(run, "party.warrior").skills = {
            game::FSkillSlot{"skill.strike", {"rune.leech"}},
            game::FSkillSlot{"skill.arrow", {}},
        };
    }};
    ParkAll(fixture.registry);
    auto& registry = fixture.registry;
    const auto warrior = FindUnit(registry, "party.warrior");
    const auto enemies = Enemies(registry);
    CHECK(enemies.size() >= 2);
    Place(registry, warrior, 1, 1);
    Place(registry, enemies[0], 2, 1);
    Place(registry, enemies[1], 5, 1);
    auto& health = registry.get<game::FHealth>(warrior);
    health.current = health.max - 10;

    AttackOnce(registry, warrior, enemies[1], "skill.arrow"); // only skill.strike carries the leech
    CHECK(health.current == health.max - 10);
    CHECK(!LogHasLine(Log(registry), "heals"));

    AttackOnce(registry, warrior, enemies[0], "skill.strike");
    CHECK(health.current == health.max - 8); // the owning skill healed
}

// ---------------------------------------------------------------------------------------------
// Core effects / status refresh
// ---------------------------------------------------------------------------------------------

/// StatusHookRefreshesSheet: an apply_status_* hook calls RefreshCombatSheet on the affected unit. The
/// sheet recomputes from FBaseCombatStats, so a corrupted derived stat is reset — the observable side of
/// the WP-03 contract (statuses themselves compose at use time, not into the sheet).
void StatusHookRefreshesSheet() {
    FUnitTest fixture{"refresh"};
    auto& registry = fixture.registry();
    const entt::entity unit = registry.create();
    const game::FBaseCombatStats base{game::FStats{5, 2, 3, 1}, 20, 4};
    registry.emplace<game::FBaseCombatStats>(unit, base);
    registry.emplace<game::FStats>(unit, base.stats);
    registry.emplace<game::FHealth>(unit, game::FHealth{20, 20});
    registry.emplace<game::FActiveStatuses>(unit);
    registry.get<game::FStats>(unit).attack = base.stats.attack + 777; // corrupted derived value

    const auto& content = registry.ctx().get<game::gameplay::FContentRegistry>();
    const auto* buff = content.statuses.find("status.buff");
    CHECK(buff != nullptr);
    GiveHooks(registry, unit,
              {Sourced(MakeHook(EHookEvent::OnTurnStart, StatusSelfSpec("status.buff", 1, 0)), "rune:probe")});
    CHECK(game::FireHooks(registry, EHookEvent::OnTurnStart, game::FHookContext{.owner = unit}) == 1);

    CHECK(registry.get<game::FStats>(unit).attack == base.stats.attack); // the refresh ran
    const auto& active = registry.get<game::FActiveStatuses>(unit);
    CHECK(active.effects.size() == 1);
    CHECK(active.effects[0].statusId == "status.buff");
    CHECK(active.effects[0].turnsRemaining == buff->durationTurns + 1); // the spec's delta was folded in
}

// ---------------------------------------------------------------------------------------------
// Validation and boot order
// ---------------------------------------------------------------------------------------------

/// Injects `hook` into skill.strike, runs ValidateEffectContent, then removes it again.
bool ValidatesHook(entt::registry& registry, const FHook& hook) {
    auto& skill = FindSkill(registry, "skill.strike");
    skill.hooks = {hook};
    const bool valid = game::ValidateEffectContent(registry);
    skill.hooks.clear();
    return valid;
}

/// PerKindParamValidation: every core kind's validator rejects a spec with its required parameter missing
/// and accepts the authored form.
void PerKindParamValidation() {
    FUnitTest fixture{"params"};
    auto& registry = fixture.registry();

    CHECK(ValidatesHook(registry, Sourced(MakeHook(EHookEvent::OnHit, DamageSelfSpec(3)), "skill:probe")));
    CHECK(ValidatesHook(registry, Sourced(MakeHook(EHookEvent::OnHit, HealSelfSpec(3)), "skill:probe")));
    CHECK(ValidatesHook(registry, Sourced(MakeHook(EHookEvent::OnHit, HealTargetSpec(3)), "skill:probe")));
    CHECK(ValidatesHook(
        registry, Sourced(MakeHook(EHookEvent::OnHit, StatusSelfSpec("status.buff")), "skill:probe")));
    CHECK(ValidatesHook(
        registry, Sourced(MakeHook(EHookEvent::OnHit, StatusTargetSpec("status.debuff")), "skill:probe")));
    CHECK(ValidatesHook(registry, Sourced(MakeHook(EHookEvent::OnHit, LogSpec("hello")), "skill:probe")));

    CHECK(!ValidatesHook(registry, Sourced(MakeHook(EHookEvent::OnHit, DamageSelfSpec(0)), "skill:probe")));
    CHECK(!ValidatesHook(registry, Sourced(MakeHook(EHookEvent::OnHit, HealSelfSpec(0)), "skill:probe")));
    CHECK(!ValidatesHook(registry, Sourced(MakeHook(EHookEvent::OnHit, HealTargetSpec(0)), "skill:probe")));
    CHECK(!ValidatesHook(registry, Sourced(MakeHook(EHookEvent::OnHit, StatusSelfSpec("")), "skill:probe")));
    CHECK(!ValidatesHook(registry, Sourced(MakeHook(EHookEvent::OnHit, StatusTargetSpec("")), "skill:probe")));
    CHECK(!ValidatesHook(registry, Sourced(MakeHook(EHookEvent::OnHit, LogSpec("")), "skill:probe")));

    // The registry-free shape half: the Run-only vocabulary (pool/member) is a shape error in a
    // Battle-scope spec, through both the direct call and the content path (boot would fail).
    FEffectSpec runOnly = LogSpec("x");
    runOnly.pool = "materials";
    CHECK(game::gameplay::ValidateEffectSpec(runOnly, FEffectScope::Battle).has_value());
    CHECK(!game::gameplay::ValidateEffectSpec(runOnly, FEffectScope::Run).has_value()); // legal in Run scope
    runOnly.pool.clear();
    runOnly.member = "party.warrior";
    CHECK(game::gameplay::ValidateEffectSpec(runOnly, FEffectScope::Battle).has_value());
    FHook runShape = Sourced(MakeHook(EHookEvent::OnHit, LogSpec("x")), "skill:probe");
    runShape.effect.pool = "runes";
    CHECK(!ValidatesHook(registry, runShape));
}

/// UnknownKindFailsBoot: a hook naming an unregistered kind fails validation, and the failure line names
/// the source, id and kind (the same bool GameplayServices' last statement consumes).
void UnknownKindFailsBoot() {
    FUnitTest fixture{"unknownkind"};
    auto& registry = fixture.registry();
    FHook hook = Sourced(MakeHook(EHookEvent::OnHit, LogSpec("x")), "skill:skill.strike", "skill:skill.strike#0");
    hook.effect.kind = "does_not_exist";

    FindSkill(registry, "skill.strike").hooks = {hook};
    FLogCapture capture;
    CHECK(!game::ValidateEffectContent(registry));
    const std::string text = capture.text();
    CHECK(text.find("does_not_exist") != std::string::npos);
    CHECK(text.find("skill:skill.strike#0") != std::string::npos);
    FindSkill(registry, "skill.strike").hooks.clear();
}

/// ScopeMismatchFailsBoot: content hooks are battle-scoped, so a hook naming a Run-scope kind fails
/// validation even though the kind itself is registered.
void ScopeMismatchFailsBoot() {
    FUnitTest fixture{"scope"};
    auto& registry = fixture.registry();
    game::RegisterEffect(registry, "test.run_effect", FEffectScope::Run,
                         [](entt::registry&, const game::FEffectContext&, const FEffectSpec&) {},
                         [](const FEffectSpec&) { return true; });
    FHook hook = Sourced(MakeHook(EHookEvent::OnHit, LogSpec("x")), "skill:skill.strike");
    hook.effect.kind = "test.run_effect";
    CHECK(!ValidatesHook(registry, hook)); // registered, but Run-scoped -> boot would stop

    FHook battleKind = Sourced(MakeHook(EHookEvent::OnHit, LogSpec("x")), "skill:skill.strike");
    battleKind.effect.kind = "log";
    CHECK(ValidatesHook(registry, battleKind)); // control: a Battle-scoped kind passes
}

/// EffectValidationRunsLast: validation consults the registry as it is when validation runs — a kind
/// registered after the content load is visible to a re-run, which is how the last-statement boot order
/// lets every Register*Effects anchor contribute before the check.
void EffectValidationRunsLast() {
    FUnitTest fixture{"last"};
    auto& registry = fixture.registry();
    FHook hook = Sourced(MakeHook(EHookEvent::OnTurnStart, LogSpec("late")), "skill:skill.strike");
    hook.effect.kind = "test.late_kind";
    CHECK(!ValidatesHook(registry, hook)); // not registered yet

    game::RegisterEffect(registry, "test.late_kind", FEffectScope::Battle,
                         [](entt::registry&, const game::FEffectContext&, const FEffectSpec&) {},
                         [](const FEffectSpec&) { return true; });
    CHECK(ValidatesHook(registry, hook)); // now the same hook validates — validation ran after registration
}

/// EffectRegistrationBeforeValidation: the composition root registered every core kind before its last
/// statement ran, so a core-kind hook re-validates green against the loaded content.
void EffectRegistrationBeforeValidation() {
    FUnitTest fixture{"registration"};
    auto& registry = fixture.registry();
    const auto* effects = registry.ctx().find<game::FEffectRegistry>();
    CHECK(effects != nullptr);
    for (const std::string_view kind : {"damage_self", "heal_self", "heal_target", "apply_status_self",
                                        "apply_status_target", "log"}) {
        CHECK(effects->find(kind) != nullptr);
    }
    CHECK(game::ValidateEffectContent(registry)); // the bool the boot's last statement consumes
    CHECK(ValidatesHook(registry, Sourced(MakeHook(EHookEvent::OnHit, HealSelfSpec(1)), "skill:probe")));
}

/// RegistryIsCtxOwned: the effect registry lives in the registry context — a fresh registry without the
/// composition root has none, and two loaded registries hold independent instances (no globals).
void RegistryIsCtxOwned() {
    entt::registry bare;
    game::FAssetManager::Initialize(bare, TestAssetRoots());
    CHECK(bare.ctx().find<game::FEffectRegistry>() == nullptr);
    CHECK(bare.ctx().find<game::FEffectDispatchState>() == nullptr);

    FUnitTest first{"ctxone"};
    CHECK(first.registry().ctx().find<game::FEffectRegistry>() != nullptr);
    game::RegisterEffect(first.registry(), "test.ctx_only", FEffectScope::Battle,
                         [](entt::registry&, const game::FEffectContext&, const FEffectSpec&) {},
                         [](const FEffectSpec&) { return true; });

    FUnitTest second{"ctxtwo"};
    CHECK(second.registry().ctx().find<game::FEffectRegistry>() != nullptr);
    CHECK(second.registry().ctx().get<game::FEffectRegistry>().find("test.ctx_only") == nullptr);
}

/// ShutdownErasesEffectRegistry: FGameplayServices::Shutdown erases the effect registry and the listener
/// state, so listeners cannot lapse into the next battle.
void ShutdownErasesEffectRegistry() {
    FUnitTest fixture{"shutdown"};
    auto& registry = fixture.registry();
    game::RegisterHookListener(registry, EHookEvent::OnTurnStart,
                               [](entt::registry&, const game::FHookContext&) {});
    CHECK(registry.ctx().contains<game::FEffectRegistry>());
    CHECK(registry.ctx().contains<game::FEffectDispatchState>());

    game::gameplay::FGameplayServices::Shutdown(registry);
    CHECK(!registry.ctx().contains<game::FEffectRegistry>());
    CHECK(!registry.ctx().contains<game::FEffectDispatchState>());
}

/// NoPassiveTreeEdit: FPassiveTree is not part of this WP (no node hooks) — the tree still loads and
/// computes its authored numbers, and there is no hook surface on it to author into.
void NoPassiveTreeEdit() {
    FUnitTest fixture{"passive"};
    auto& registry = fixture.registry();
    const auto& growth = registry.ctx().get<game::FDungeonGrowth>();
    CHECK(!growth.passiveTree.empty());
    CHECK(growth.passiveTree.Find("node.might") != nullptr); // the authored nodes are intact
    const game::FPassiveStatBonuses bonuses = growth.passiveTree.Bonuses({"node.might", "node.vitality"});
    CHECK(bonuses.attack > 0);
    CHECK(bonuses.maxHealth > 0);
    // FPassiveTree has no `hooks` member at all (compile-time): the closest runtime assertion is that the
    // tree remains a pure stat-bonus structure and the effect validation never reports it.
    CHECK(game::ValidateEffectContent(registry));
}

// ---------------------------------------------------------------------------------------------
// Scope separation / run-state safety
// ---------------------------------------------------------------------------------------------

/// BattleScopeCannotMutateRun: every core Battle effect fires on a battle unit and the run state
/// (materials, runes, EXP, member bonuses/health/skills) stays byte-identical.
void BattleScopeCannotMutateRun() {
    FCombatTest fixture;
    ParkAll(fixture.registry);
    auto& registry = fixture.registry;
    auto& run = registry.ctx().get<game::FDungeonRun>();
    const auto warrior = FindUnit(registry, "party.warrior");
    const auto enemies = Enemies(registry);
    CHECK(warrior != entt::null && !enemies.empty());
    run.materials = 7;
    run.runes = {"rune.power"};
    Member(run, "party.warrior").experience = 3;
    Member(run, "party.warrior").bonusAttack = 2;

    const int materials = run.materials;
    const std::vector<std::string> runes = run.runes;
    const int experience = Member(run, "party.warrior").experience;
    const int bonusAttack = Member(run, "party.warrior").bonusAttack;
    const int memberHealth = Member(run, "party.warrior").health;
    const std::vector<game::FSkillSlot> skills = Member(run, "party.warrior").skills;

    GiveHooks(registry, warrior,
              {
                  Sourced(MakeHook(EHookEvent::OnTurnStart, DamageSelfSpec(1)), "rune:probe0"),
                  Sourced(MakeHook(EHookEvent::OnTurnStart, HealSelfSpec(1)), "rune:probe1"),
                  Sourced(MakeHook(EHookEvent::OnTurnStart, HealTargetSpec(1)), "rune:probe2"),
                  Sourced(MakeHook(EHookEvent::OnTurnStart, StatusSelfSpec("status.buff")), "rune:probe3"),
                  Sourced(MakeHook(EHookEvent::OnTurnStart, StatusTargetSpec("status.debuff")), "rune:probe4"),
                  Sourced(MakeHook(EHookEvent::OnTurnStart, LogSpec("core log")), "rune:probe5"),
              });
    const int fired = game::FireHooks(registry, EHookEvent::OnTurnStart,
                                      game::FHookContext{.owner = warrior, .other = enemies[0]});
    CHECK(fired == 6); // every core kind ran (battle side effects only)
    CHECK(LogHasLine(Log(registry), "core log"));
    CHECK(registry.get<game::FActiveStatuses>(enemies[0]).effects.size() == 1);

    CHECK(run.materials == materials);
    CHECK(run.runes == runes);
    CHECK(Member(run, "party.warrior").experience == experience);
    CHECK(Member(run, "party.warrior").bonusAttack == bonusAttack);
    CHECK(Member(run, "party.warrior").health == memberHealth);
    CHECK(Member(run, "party.warrior").skills.size() == skills.size());
}

/// NoRunCurrencyEffectInBattle: a Run-scope currency effect is unreachable from battle — ApplyEffect
/// refuses it in a Battle context (currencies unchanged) and battle content cannot reference it at all.
void NoRunCurrencyEffectInBattle() {
    FUnitTest fixture{"norun"};
    auto& registry = fixture.registry();
    game::StartDungeonRun(registry, 5u, true);
    auto& run = registry.ctx().get<game::FDungeonRun>();
    run.materials = 5;

    game::RegisterEffect(registry, "test.gain_materials", FEffectScope::Run,
                         [](entt::registry& r, const game::FEffectContext&, const FEffectSpec&) {
                             if (auto* loaded = r.ctx().find<game::FDungeonRun>()) {
                                 loaded->materials += 100;
                             }
                         },
                         [](const FEffectSpec&) { return true; });

    const entt::entity unit = registry.create();
    FEffectSpec spec;
    spec.kind = "test.gain_materials";
    const game::FEffectContext battle{.scope = FEffectScope::Battle, .owner = unit};
    CHECK(!game::ApplyEffect(registry, battle, spec)); // refused: this kind is Run-scoped
    CHECK(run.materials == 5);                          // no farm through the battle scope

    FHook hook = Sourced(MakeHook(EHookEvent::OnRoomClear, LogSpec("x")), "skill:skill.strike");
    hook.effect.kind = "test.gain_materials";
    CHECK(!ValidatesHook(registry, hook)); // and battle content may not even name it
}

/// SaveLeaveDoesNotFarm: battle hooks change battle state only; save + continue round-trips the run
/// currencies and member health unchanged (no write→leave→continue farming loop).
void SaveLeaveDoesNotFarm() {
    FCombatTest fixture;
    ParkAll(fixture.registry);
    auto& registry = fixture.registry;
    const auto warrior = FindUnit(registry, "party.warrior");
    CHECK(warrior != entt::null);
    auto& run = registry.ctx().get<game::FDungeonRun>();
    run.materials = 9;
    run.runes = {"rune.power"};
    const int memberHealthBefore = Member(run, "party.warrior").health;

    GiveHooks(registry, warrior, {Sourced(MakeHook(EHookEvent::OnTurnStart, HealSelfSpec(5)), "rune:probe")});
    auto& health = registry.get<game::FHealth>(warrior);
    health.current = health.max - 10;
    CHECK(game::FireHooks(registry, EHookEvent::OnTurnStart, game::FHookContext{.owner = warrior}) == 1);
    CHECK(health.current == health.max - 5); // a real battle-side change happened

    CHECK(game::SaveDungeonRun(registry));
    CHECK(game::ContinueDungeonRun(registry)); // the "leave and continue" loop
    auto& reloaded = registry.ctx().get<game::FDungeonRun>();
    CHECK(reloaded.materials == 9);
    CHECK(reloaded.runes == std::vector<std::string>{"rune.power"});
    CHECK(Member(reloaded, "party.warrior").health == memberHealthBefore);
}

/// RunScopeEventEffectsOnly: a Run-scope effect applies only in a Run context — the battle context refuses
/// it (and vice versa), which is the runtime half of the scope separation.
void RunScopeEventEffectsOnly() {
    FUnitTest fixture{"runscope"};
    auto& registry = fixture.registry();
    game::StartDungeonRun(registry, 5u, true);

    bool ran = false;
    game::RegisterEffect(registry, "test.run_mark", FEffectScope::Run,
                         [&ran](entt::registry&, const game::FEffectContext&, const FEffectSpec&) { ran = true; },
                         [](const FEffectSpec&) { return true; });

    const entt::entity unit = registry.create();
    FEffectSpec runSpec;
    runSpec.kind = "test.run_mark";
    const game::FEffectContext run{.scope = FEffectScope::Run, .owner = unit};
    CHECK(game::ApplyEffect(registry, run, runSpec)); // Run scope + Run kind: allowed (WP-09 fires these)
    CHECK(ran);

    const game::FEffectContext battle{.scope = FEffectScope::Battle, .owner = unit};
    CHECK(!game::ApplyEffect(registry, battle, runSpec)); // the reverse of the previous test

    FEffectSpec battleSpec;
    battleSpec.kind = "damage_self";
    battleSpec.amount = 1;
    CHECK(!game::ApplyEffect(registry, run, battleSpec)); // a Battle kind cannot run in Run scope
}

// ---------------------------------------------------------------------------------------------
// Regression: hooks-free data behaves exactly as before
// ---------------------------------------------------------------------------------------------

/// NoDataNoChange: the shipped (hooks-free) content resolves and fights exactly as before — no hook ever
/// fires, and the damage is the same ComputeDamage path the preview used.
void NoDataNoChange() {
    FCombatTest fixture;
    ParkAll(fixture.registry);
    auto& registry = fixture.registry;
    const auto warrior = FindUnit(registry, "party.warrior");
    const auto enemies = Enemies(registry);
    CHECK(warrior != entt::null && !enemies.empty());
    Place(registry, warrior, 1, 1);
    Place(registry, enemies[0], 2, 1);

    const auto& content = registry.ctx().get<game::gameplay::FContentRegistry>();
    const auto resolved = game::ResolveSkillSlot(game::FSkillSlot{"skill.strike", {}}, content);
    CHECK(resolved.has_value());
    CHECK(resolved->hooks.empty());    // hooks are an optional field; the fixture authors none
    CHECK(resolved->lifeOnHit == 0);
    CHECK(resolved->runeIds.empty());

    const int before = registry.get<game::FHealth>(enemies[0]).current;
    const int expected = game::ComputeSkillDamage(
        registry.get<game::FStats>(warrior), 0, *resolved, registry.get<game::FStats>(enemies[0]));
    AttackOnce(registry, warrior, enemies[0], "skill.strike");

    CHECK(before - registry.get<game::FHealth>(enemies[0]).current == expected);
    CHECK(!LogHasLine(Log(registry), "[hook]")); // zero hook firing for hooks-free data
    const auto& active = registry.get<game::FActiveHooks>(warrior);
    CHECK(active.hooks.empty()); // the lazy build ran and found nothing to fire
}

/// TestsNeverTouchRealSave: every file this suite writes stays inside the GAME01P_SAVE_DIR override — a
/// plain `ctest` run exports none, so main() commits one before the first save can be written. Without it,
/// StartDungeonRun/SaveDungeonRun would target the user's real save and this fixture's cleanup would then
/// delete it.
void TestsNeverTouchRealSave() {
    const char* overrideDir = std::getenv("GAME01P_SAVE_DIR");
    CHECK(overrideDir != nullptr && *overrideDir != '\0');
    const std::filesystem::path root = std::filesystem::path(overrideDir).lexically_normal();

    FCombatTest fixture;
    const std::string savePath = fixture.registry.ctx().get<game::FDungeonSave>().path;
    CHECK(!savePath.empty());
    const std::filesystem::path relative =
        std::filesystem::path(savePath).lexically_normal().lexically_relative(root);
    CHECK(!relative.empty()); // empty = the save is not under the override directory at all
    CHECK(relative != std::filesystem::path(".."));
    CHECK(relative.native().compare(0, 2, std::filesystem::path("..").native()) != 0); // not an escape
}

} // namespace

int main() {
    // Save safety (review item 1): `ctest` exports no GAME01P_SAVE_DIR for feature tests, so outside
    // build-test.ps1 a plain run would have StartDungeonRun/SaveDungeonRun target — and fixture cleanup
    // delete — the user's real save under %APPDATA%\Game01P\EmberDungeon. Commit an override before any
    // dungeon service can touch disk. (_putenv_s/setenv: the same pattern as save_dir_override_tests.cpp.)
    if (const char* existing = std::getenv("GAME01P_SAVE_DIR"); existing == nullptr || *existing == '\0') {
        const std::filesystem::path overrideDir =
            std::filesystem::current_path() / "out" / "test-saves" / "effect_hooks_tests";
        std::error_code error;
        std::filesystem::create_directories(overrideDir, error);
        const std::string value = overrideDir.string();
#ifdef _WIN32
        _putenv_s("GAME01P_SAVE_DIR", value.c_str());
#else
        setenv("GAME01P_SAVE_DIR", value.c_str(), 1);
#endif
    }
    return game_test::RunTests("effect hooks", {
        {"TestsNeverTouchRealSave", TestsNeverTouchRealSave},
        {"HookSourceIsOrigin", HookSourceIsOrigin},
        {"DeadOwnerDoesNotFire", DeadOwnerDoesNotFire},
        {"HealDeadTargetIsNoOp", HealDeadTargetIsNoOp},
        {"SelfKillStopsRemainingHooks", SelfKillStopsRemainingHooks},
        {"CodeListenerFiresWithoutDataHook", CodeListenerFiresWithoutDataHook},
        {"CodeListenerOrder", CodeListenerOrder},
        {"OncePerTurnResetPerOwner", OncePerTurnResetPerOwner},
        {"HookOrderIsSlotThenRune", HookOrderIsSlotThenRune},
        {"HookDamageDoesNotRetrigger", HookDamageDoesNotRetrigger},
        {"NestedDepthCap", NestedDepthCap},
        {"OnKillFiresForKiller", OnKillFiresForKiller},
        {"OnDamageTakenOrder", OnDamageTakenOrder},
        {"OnDamageTakenRequiresSurvivor", OnDamageTakenRequiresSurvivor},
        {"OnKillRequiresLiveKiller", OnKillRequiresLiveKiller},
        {"StatusKillDoesNotFireOnKill", StatusKillDoesNotFireOnKill},
        {"HudEndTurnFiresOnTurnEnd", HudEndTurnFiresOnTurnEnd},
        {"FinalTurnEndNotFiredAfterOutcome", FinalTurnEndNotFiredAfterOutcome},
        {"TurnStartHookOnSkipTurn", TurnStartHookOnSkipTurn},
        {"HooksBuiltAfterLoadoutApplied", HooksBuiltAfterLoadoutApplied},
        {"HooksBuiltOncePerBattle", HooksBuiltOncePerBattle},
        {"LoadoutImmutableDuringBattle", LoadoutImmutableDuringBattle},
        {"OnRoomClearFiresForEachPartyMember", OnRoomClearFiresForEachPartyMember},
        {"OnRoomClearHealsBeforeCapture", OnRoomClearHealsBeforeCapture},
        {"OnRoomClearSkipsDeadMembers", OnRoomClearSkipsDeadMembers},
        {"ContinueDoesNotRefireRoomClear", ContinueDoesNotRefireRoomClear},
        {"LeechMovedToHook", LeechMovedToHook},
        {"LeechAiValueUnchanged", LeechAiValueUnchanged},
        {"LeechOnlyOnOwningSkill", LeechOnlyOnOwningSkill},
        {"StatusHookRefreshesSheet", StatusHookRefreshesSheet},
        {"PerKindParamValidation", PerKindParamValidation},
        {"UnknownKindFailsBoot", UnknownKindFailsBoot},
        {"ScopeMismatchFailsBoot", ScopeMismatchFailsBoot},
        {"EffectValidationRunsLast", EffectValidationRunsLast},
        {"EffectRegistrationBeforeValidation", EffectRegistrationBeforeValidation},
        {"RegistryIsCtxOwned", RegistryIsCtxOwned},
        {"ShutdownErasesEffectRegistry", ShutdownErasesEffectRegistry},
        {"NoPassiveTreeEdit", NoPassiveTreeEdit},
        {"BattleScopeCannotMutateRun", BattleScopeCannotMutateRun},
        {"NoRunCurrencyEffectInBattle", NoRunCurrencyEffectInBattle},
        {"SaveLeaveDoesNotFarm", SaveLeaveDoesNotFarm},
        {"RunScopeEventEffectsOnly", RunScopeEventEffectsOnly},
        {"NoDataNoChange", NoDataNoChange},
    });
}
