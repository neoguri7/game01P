// WP-39 combat stakes: party crits (resolver + preview), seeded battle objectives (loader, pick, failure
// rules, victory payout), the first-contact record and the battle wiring.
//
// Battles are built the way pipeline_tests.cpp builds them (dungeon run -> battlefield -> units); runs with
// the run log are booted the way run_log_tests.cpp boots them. Fixture assets keep the numbers frozen;
// the cases that read the SHIPPED values (F7, F8, F9, F16) use ShipAssetRoots().

#define GAME01P_TEST_SUPPORT_CHECK
#include "../TestSupport.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <entt/entt.hpp>
#include <nlohmann/json.hpp>

#include "ecs/components/FActionPoints.h"
#include "ecs/components/FActiveStatuses.h"
#include "ecs/components/FDead.h"
#include "ecs/components/FFaction.h"
#include "ecs/components/FGridPosition.h"
#include "ecs/components/FHealth.h"
#include "ecs/components/FSkillLoadout.h"
#include "ecs/components/FStats.h"
#include "ecs/systems/ActionResolverSystem.h"
#include "ecs/systems/TurnSystem.h"
#include "gameplay/battle/FBattleSetup.h"
#include "gameplay/combat/FBattleEvents.h"
#include "gameplay/combat/FCombatApply.h"
#include "gameplay/combat/FCombatQuery.h"
#include "gameplay/combat/FCombatRules.h"
#include "gameplay/combat/FCombatSheet.h"
#include "gameplay/combat/FCombatTypes.h"
#include "gameplay/combat/FSkillApCap.h"
#include "gameplay/combat/FSkillResolver.h"
#include "gameplay/data/FContentRegistry.h"
#include "gameplay/dungeon/FBattleObjective.h"
#include "gameplay/dungeon/FDungeonRun.h"
#include "gameplay/dungeon/FDungeonSave.h"
#include "gameplay/dungeon/FRunFlow.h"
#include "gameplay/dungeon/FRunLog.h"
#include "gameplay/factory/FEntityFactory.h"
#include "gameplay/grid/FGridTerrain.h"
#include "gameplay/grid/FGridTypes.h"

namespace {

using nlohmann::json;

// ---------------------------------------------------------------------------------------------
// Helpers (copied from pipeline_tests.cpp / tempo_skills_tests.cpp / run_log_tests.cpp)
// ---------------------------------------------------------------------------------------------

std::vector<entt::entity> Enemies(entt::registry& registry)
{
    std::vector<entt::entity> enemies;
    for (const auto entity : registry.view<game::FEnemy, game::FUnitData>()) enemies.push_back(entity);
    return enemies;
}

entt::entity FindUnit(entt::registry& registry, std::string_view id)
{
    for (const auto entity : registry.view<game::FUnitData>()) {
        if (registry.get<game::FUnitData>(entity).definition.id == id) return entity;
    }
    return entt::null;
}

void Place(entt::registry& registry, entt::entity unit, int x, int y)
{
    registry.get<game::FGridPosition>(unit) = {x, y};
}

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

/// Dungeon (Ember) battle setup, the same order as FEmberBattleState::OnEnter's combat section.
struct FFixture {
    entt::registry registry;
    std::unique_ptr<game::FScene> scene;
    std::filesystem::path checkpoint;
    std::filesystem::path runSave;

    explicit FFixture(std::uint32_t seed = 1234)
    {
        std::filesystem::create_directories("out");
        checkpoint = std::filesystem::path("out") / ("stakes-checkpoint-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".json");
        game::FAssetManager::Initialize(registry, TestAssetRoots());
        CHECK(game::gameplay::FGameplayServices::Initialize(registry));
        CHECK(game::InitializeDungeon(registry, checkpoint.string()));
        runSave = registry.ctx().get<game::FDungeonSave>().path;
        game::StartDungeonRun(registry, seed, true);

        const auto document = game::BuildDungeonBattlefield(registry);
        scene = std::make_unique<game::FScene>(registry, "StakesTest");
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

/// A party attacker next to an enemy on an open floor, both on a big HP pool so no hit is lethal.
struct FDuel {
    FFixture fixture;
    entt::entity hero{entt::null};
    entt::entity foe{entt::null};

    FDuel()
    {
        auto& registry = fixture.registry;
        ParkAll(registry);
        hero = FindUnit(registry, "party.warrior");
        const auto enemies = Enemies(registry);
        CHECK(hero != entt::null && !enemies.empty());
        foe = enemies[0];
        Place(registry, hero, 1, 1);
        Place(registry, foe, 2, 1);
        for (const auto unit : {hero, foe}) {
            auto& health = registry.get<game::FHealth>(unit);
            health.max = 1000;
            health.current = 1000;
            auto& slots = registry.get<game::FSkillLoadout>(unit).slots;
            if (std::ranges::find(slots, "skill.strike", &game::FSkillSlot::skillId) == slots.end()) {
                slots.push_back(game::FSkillSlot{"skill.strike", {}});
            }
        }
    }

    entt::registry& registry() { return fixture.registry; }
    game::FCombatRuntime& combat() { return fixture.registry.ctx().get<game::FCombatRuntime>(); }
    int Hp(entt::entity unit) { return registry().get<game::FHealth>(unit).current; }

    [[nodiscard]] game::FResolvedSkill Resolved(const std::string& skillId)
    {
        const auto& content = registry().ctx().get<game::gameplay::FContentRegistry>();
        const auto resolved = game::ResolveSkillSlot(game::FSkillSlot{skillId, {}}, content);
        CHECK(resolved.has_value());
        return *resolved;
    }

    /// The preview of `skillId` from `actor` onto `target`'s cell.
    [[nodiscard]] game::FSkillPreview Preview(entt::entity actor, entt::entity target, const std::string& skillId)
    {
        const auto resolved = Resolved(skillId);
        const auto preview = game::PreviewSkill(registry(), actor, resolved, registry().get<game::FGridPosition>(target),
                                                registry().get<game::FGridPosition>(actor));
        CHECK(preview.has_value());
        return *preview;
    }

    /// Resolves `skillId` through the real resolver path.
    void Resolve(entt::entity actor, entt::entity target, const std::string& skillId)
    {
        auto& points = registry().get<game::FActionPoints>(actor);
        points.skill = points.skillMax;
        game::FActionRequest request;
        request.actor = actor;
        request.target = target;
        request.skillId = skillId;
        request.cell = registry().get<game::FGridPosition>(target);
        request.token = StartTurnFor(registry(), actor);
        registry().ctx().get<game::FActionQueue>().pending.push_back(request);
        game::ecs::ActionResolverSystem resolver;
        resolver.update(registry(), 0.f);
    }
};

bool LogContains(entt::registry& registry, std::string_view needle)
{
    const auto& log = registry.ctx().get<game::FCombatLog>();
    return std::ranges::any_of(log.lines, [&](const std::string& line) { return line.find(needle) != std::string::npos; });
}

// --- temp asset roots ------------------------------------------------------------------------

struct FTempRoot {
    std::filesystem::path root;

    explicit FTempRoot(std::string_view label)
    {
        static int counter = 0;
        const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
        root = std::filesystem::temp_directory_path() /
               ("game01p-wp39-" + std::string{label} + "-" + std::to_string(ticks) + "-" + std::to_string(counter++));
        std::error_code error;
        std::filesystem::remove_all(root, error);
        CHECK(std::filesystem::create_directories(root / "data"));
    }

    ~FTempRoot()
    {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }

    FTempRoot(const FTempRoot&) = delete;
    FTempRoot& operator=(const FTempRoot&) = delete;
};

json ReadJsonFile(const std::filesystem::path& path)
{
    std::ifstream file(path);
    CHECK(file.good());
    json document;
    file >> document;
    return document;
}

void WriteJsonFile(const std::filesystem::path& path, const json& document)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << document.dump(2);
    file.flush();
    CHECK(file.good());
}

json FixtureDungeon() { return ReadJsonFile(FixtureDataPath("dungeon.json")); }

std::vector<std::string> RootsWith(const FTempRoot& temp)
{
    std::vector<std::string> roots{temp.root.string()};
    const std::vector<std::string> base = TestAssetRoots();
    roots.insert(roots.end(), base.begin(), base.end());
    return roots;
}

/// Loads a temp dungeon document through LoadBattleObjectives / LoadTempoTuning.
std::optional<game::FBattleObjectiveTable> ObjectivesFrom(const json& dungeon)
{
    FTempRoot temp{"objectives"};
    WriteJsonFile(temp.root / "data" / "dungeon.json", dungeon);
    const game::FAssetManager assets{RootsWith(temp)};
    return game::LoadBattleObjectives(assets);
}

std::optional<game::gameplay::FTempoTuning> TuningFrom(const json& dungeon)
{
    FTempRoot temp{"tuning"};
    WriteJsonFile(temp.root / "data" / "dungeon.json", dungeon);
    const game::FAssetManager assets{RootsWith(temp)};
    return game::gameplay::LoadTempoTuning(assets);
}

// --- a booted run with the run log (as run_log_tests.cpp's FBoot) -----------------------------

struct FBoot {
    std::string directory;
    entt::registry registry;
    std::string runSave;

    explicit FBoot(std::vector<std::string> roots = TestAssetRoots())
    {
        static std::uint32_t serial = 0;
        directory = (std::filesystem::path("out") /
                     ("stakes-" + std::to_string(++serial) + "-" +
                      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())))
                        .string();
        std::filesystem::create_directories(directory);
        game::FAssetManager::Initialize(registry, std::move(roots));
        CHECK(game::gameplay::FGameplayServices::Initialize(registry));
        CHECK(game::InitializeDungeon(registry, directory + "/checkpoint.json"));
        runSave = registry.ctx().get<game::FDungeonSave>().path;
    }

    FBoot(const FBoot&) = delete;
    FBoot& operator=(const FBoot&) = delete;

    game::FDungeonRun& run() { return registry.ctx().get<game::FDungeonRun>(); }

    /// StartRun + the first battlefield: the run is then in the Battle phase.
    void EnterBattle()
    {
        game::StartRun(registry, 4242, true);
        CHECK(!game::BuildDungeonBattlefield(registry).empty());
        CHECK(run().phase == game::EDungeonPhase::Battle);
    }
};

json LastRoomEnd(entt::registry& registry)
{
    const auto& events = registry.ctx().get<game::FRunLog>().events;
    for (auto it = events.rbegin(); it != events.rend(); ++it) {
        if (it->is_object() && it->value("t", std::string{}) == "room_end") return *it;
    }
    throw std::runtime_error("no room_end line in the run log");
}

game::FBattleObjectiveDef Unbroken()
{
    return game::FBattleObjectiveDef{"objective.unbroken", "Unbroken", "No ally goes down",
                                     game::EBattleObjectiveKind::NoAllyDown, 0, 1};
}

// ---------------------------------------------------------------------------------------------
// F1-F5: the crit
// ---------------------------------------------------------------------------------------------

void ZeroChanceNeverCrits()
{
    FDuel duel;
    CHECK(duel.combat().critChancePercent == 0);
    const auto preview = duel.Preview(duel.hero, duel.foe, "skill.strike");
    CHECK(preview.targets.size() == 1);
    CHECK(preview.targets.front().critDamage == 0);
    CHECK(preview.critChancePercent == 0);
    const int damage = preview.targets.front().damage;
    CHECK(damage > 0);
    const int before = duel.Hp(duel.foe);
    duel.Resolve(duel.hero, duel.foe, "skill.strike");
    CHECK(before - duel.Hp(duel.foe) == damage);
}

void PreviewCarriesCrit()
{
    FDuel duel;
    duel.combat().critChancePercent = 15;
    duel.combat().critMultiplierPercent = 200;
    const auto party = duel.Preview(duel.hero, duel.foe, "skill.strike");
    CHECK(party.targets.size() == 1);
    CHECK(party.targets.front().damage > 0);
    CHECK(party.targets.front().critDamage == 2 * party.targets.front().damage);
    CHECK(party.critChancePercent == 15);

    const auto enemy = duel.Preview(duel.foe, duel.hero, "skill.strike");
    CHECK(enemy.targets.size() == 1);
    CHECK(enemy.targets.front().critDamage == 0);
    CHECK(enemy.critChancePercent == 0);
}

void CritRollDealsCritDamage()
{
    FDuel duel;
    duel.combat().critChancePercent = 100;
    duel.registry().ctx().emplace<game::FBattleEvents>();
    duel.registry().ctx().emplace<game::FRoomBattleStats>();
    const auto preview = duel.Preview(duel.hero, duel.foe, "skill.strike");
    const int critDamage = preview.targets.front().critDamage;
    CHECK(critDamage > preview.targets.front().damage);
    const int before = duel.Hp(duel.foe);
    duel.Resolve(duel.hero, duel.foe, "skill.strike");
    CHECK(before - duel.Hp(duel.foe) == critDamage);

    const auto& events = duel.registry().ctx().get<game::FBattleEvents>().pending;
    bool flagged = false;
    for (const auto& event : events) {
        if (event.kind != game::FBattleEvent::EKind::Skill) continue;
        for (const auto& target : event.targets) flagged = flagged || target.crit;
    }
    CHECK(flagged);
    CHECK(LogContains(duel.registry(), " / CRIT"));
    CHECK(LogContains(duel.registry(), "-" + std::to_string(critDamage) + " HP"));
    CHECK(duel.registry().ctx().get<game::FRoomBattleStats>().crits == 1);
}

void EnemiesNeverCrit()
{
    FDuel duel;
    duel.combat().critChancePercent = 50;
    const auto preview = duel.Preview(duel.foe, duel.hero, "skill.strike");
    const int damage = preview.targets.front().damage;
    const std::uint32_t critState = duel.combat().critRng.state;
    const int before = duel.Hp(duel.hero);
    duel.Resolve(duel.foe, duel.hero, "skill.strike");
    CHECK(before - duel.Hp(duel.hero) == damage);
    CHECK(duel.combat().critRng.state == critState);
}

void CritUsesItsOwnRng()
{
    FDuel duel;
    duel.combat().critChancePercent = 50;
    std::string chosen;
    for (const auto unit : duel.registry().view<game::FPartyMember, game::FSkillLoadout>()) {
        for (const auto& slot : duel.registry().get<game::FSkillLoadout>(unit).slots) {
            const auto resolved = duel.Resolved(slot.skillId);
            if (chosen.empty() && resolved.power > 0 && resolved.statuses.empty() && resolved.target != "ally" &&
                resolved.target != "self") {
                chosen = slot.skillId;
            }
        }
    }
    CHECK(!chosen.empty());  // the hero loadout carries at least one damaging skill without statuses
    auto& slots = duel.registry().get<game::FSkillLoadout>(duel.hero).slots;
    if (std::ranges::find(slots, chosen, &game::FSkillSlot::skillId) == slots.end()) {
        slots.push_back(game::FSkillSlot{chosen, {}});
    }
    const std::uint32_t rngState = duel.combat().rng.state;
    const std::uint32_t critState = duel.combat().critRng.state;
    duel.Resolve(duel.hero, duel.foe, chosen);
    CHECK(duel.combat().rng.state == rngState);
    CHECK(duel.combat().critRng.state != critState);
}

// ---------------------------------------------------------------------------------------------
// F7: tuning
// ---------------------------------------------------------------------------------------------

void CritTuningLoads()
{
    {
        const game::FAssetManager assets{ShipAssetRoots()};
        const auto tuning = game::gameplay::LoadTempoTuning(assets);
        CHECK(tuning.has_value());
        CHECK(tuning->critChancePercent == 15);
        CHECK(tuning->critMultiplierPercent == 200);
    }
    json dungeon = FixtureDungeon();
    dungeon["balance"]["crit_chance_percent"] = 101;
    CHECK(!TuningFrom(dungeon).has_value());
    dungeon = FixtureDungeon();
    dungeon["balance"]["crit_multiplier_percent"] = 99;
    CHECK(!TuningFrom(dungeon).has_value());
    dungeon = FixtureDungeon();
    dungeon["balance"].erase("crit_chance_percent");
    dungeon["balance"].erase("crit_multiplier_percent");
    const auto plain = TuningFrom(dungeon);
    CHECK(plain.has_value());
    CHECK(plain->critChancePercent == 0);
    CHECK(plain->critMultiplierPercent == 200);
}

// ---------------------------------------------------------------------------------------------
// F8-F10: objectives
// ---------------------------------------------------------------------------------------------

void ObjectivesLoad()
{
    {
        const game::FAssetManager assets{ShipAssetRoots()};
        const auto table = game::LoadBattleObjectives(assets);
        CHECK(table.has_value());
        CHECK(table->rows.size() == 2);
        const auto& unbroken = table->rows[0];
        CHECK(unbroken.id == "objective.unbroken");
        CHECK(unbroken.kind == game::EBattleObjectiveKind::NoAllyDown);
        CHECK(unbroken.rewardMaterials == 1);
        const auto& swift = table->rows[1];
        CHECK(swift.id == "objective.swift");
        CHECK(swift.kind == game::EBattleObjectiveKind::WinByRound);
        CHECK(swift.rounds == 5);
        CHECK(swift.rewardMaterials == 1);
    }
    // No key = an empty table (the frozen fixture has none).
    {
        const auto table = ObjectivesFrom(FixtureDungeon());
        CHECK(table.has_value());
        CHECK(table->rows.empty());
    }

    const json good = {{"id", "objective.a"}, {"display_name", "A"}, {"text", "t"}, {"kind", "no_ally_down"},
                       {"reward_materials", 1}};
    const auto with = [&](json rows) {
        json dungeon = FixtureDungeon();
        dungeon["battle_objectives"] = std::move(rows);
        return ObjectivesFrom(dungeon);
    };
    const auto edited = [&](const char* key, json value) {
        json row = good;
        row[key] = std::move(value);
        return with(json::array({row}));
    };
    CHECK(with(json::array({good})).has_value());          // the base row is valid
    CHECK(!with(json::object()).has_value());              // value not an array
    CHECK(!with(json::array({1})).has_value());            // a row that is not an object
    CHECK(!edited("kind", "nope").has_value());            // unknown kind
    CHECK(!edited("id", "").has_value());                  // empty id
    CHECK(!with(json::array({good, good})).has_value());   // repeated id
    {
        json swift = good;
        swift["kind"] = "win_by_round";
        CHECK(!with(json::array({swift})).has_value());    // win_by_round without rounds
        swift["rounds"] = 0;
        CHECK(!with(json::array({swift})).has_value());    // rounds 0
        swift["rounds"] = 5;
        CHECK(with(json::array({swift})).has_value());     // the valid counterpart
    }
    CHECK(!edited("reward_materials", 11).has_value());    // reward out of range
    CHECK(!edited("surprise", 1).has_value());             // unknown key
}

void ObjectivePickIsSeeded()
{
    const game::FAssetManager assets{ShipAssetRoots()};
    const auto table = game::LoadBattleObjectives(assets);
    CHECK(table.has_value() && table->rows.size() == 2);
    CHECK(game::PickBattleObjective(*table, 12345u, 3, 1) == game::PickBattleObjective(*table, 12345u, 3, 1));
    bool seenUnbroken = false;
    bool seenSwift = false;
    for (int depth = 1; depth <= 8; ++depth) {
        for (int room = 0; room <= 2; ++room) {
            const game::FBattleObjectiveDef* pick = game::PickBattleObjective(*table, 12345u, depth, room);
            CHECK(pick != nullptr);
            seenUnbroken = seenUnbroken || pick->id == "objective.unbroken";
            seenSwift = seenSwift || pick->id == "objective.swift";
        }
    }
    CHECK(seenUnbroken && seenSwift);
    CHECK(game::PickBattleObjective(game::FBattleObjectiveTable{}, 12345u, 1, 0) == nullptr);
}

void ObjectiveFailureRules()
{
    const game::FBattleObjectiveDef unbroken = Unbroken();
    CHECK(!game::IsBattleObjectiveFailed(unbroken, 9, false));
    CHECK(game::IsBattleObjectiveFailed(unbroken, 1, true));
    const game::FBattleObjectiveDef swift{"objective.swift", "Swift", "Win by round 5",
                                          game::EBattleObjectiveKind::WinByRound, 5, 1};
    CHECK(!game::IsBattleObjectiveFailed(swift, 5, true));   // anyAllyDown is irrelevant to Swift
    CHECK(game::IsBattleObjectiveFailed(swift, 6, false));
    CHECK(!game::IsBattleObjectiveFailed(swift, 1, false));
}

// ---------------------------------------------------------------------------------------------
// F11-F12: victory payout
// ---------------------------------------------------------------------------------------------

bool EndsWith(const std::string& text, const std::string& suffix)
{
    return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
}

void VictoryPaysMetObjective()
{
    {
        FBoot boot;
        boot.EnterBattle();
        auto& registry = boot.registry;
        const int materials = boot.run().materials;
        const int roomMaterials = game::DungeonBalance(registry, "room_materials");
        registry.ctx().insert_or_assign(game::FBattleObjective{Unbroken()});
        registry.ctx().insert_or_assign(game::FRoomBattleStats{.rounds = 3, .ko = false, .active = true});
        CHECK(game::ResolveRoomVictory(registry, true).has_value());
        CHECK(boot.run().materials == materials + roomMaterials + 1);
        CHECK(EndsWith(boot.run().message, "Objective met: Unbroken (+1 material)."));
        const auto& objective = registry.ctx().get<game::FBattleObjective>();
        CHECK(objective.settled && objective.met);
        const json line = LastRoomEnd(registry);
        CHECK(line.contains("objective"));
        CHECK(line["objective"]["id"] == "objective.unbroken");
        CHECK(line["objective"]["met"] == true);
    }
    {
        FBoot boot;
        boot.EnterBattle();
        auto& registry = boot.registry;
        const int materials = boot.run().materials;
        const int roomMaterials = game::DungeonBalance(registry, "room_materials");
        registry.ctx().insert_or_assign(game::FBattleObjective{Unbroken()});
        registry.ctx().insert_or_assign(game::FRoomBattleStats{.rounds = 3, .ko = true, .active = true});
        CHECK(game::ResolveRoomVictory(registry, true).has_value());
        CHECK(boot.run().materials == materials + roomMaterials);
        CHECK(EndsWith(boot.run().message, "Objective missed: Unbroken."));
        const auto& objective = registry.ctx().get<game::FBattleObjective>();
        CHECK(objective.settled && !objective.met);
        const json line = LastRoomEnd(registry);
        CHECK(line.contains("objective"));
        CHECK(line["objective"]["met"] == false);
    }
}

void NoObjectiveNoBonus()
{
    {
        FBoot boot;
        boot.EnterBattle();
        auto& registry = boot.registry;
        const int materials = boot.run().materials;
        const int roomMaterials = game::DungeonBalance(registry, "room_materials");
        CHECK(game::ResolveRoomVictory(registry, true).has_value());
        CHECK(boot.run().materials == materials + roomMaterials);
        CHECK(boot.run().message.find("Objective") == std::string::npos);
        CHECK(!LastRoomEnd(registry).contains("objective"));
    }
    {
        FBoot boot;
        boot.EnterBattle();
        auto& registry = boot.registry;
        const int materials = boot.run().materials;
        const int roomMaterials = game::DungeonBalance(registry, "room_materials");
        registry.ctx().insert_or_assign(game::FBattleObjective{Unbroken()});
        registry.ctx().insert_or_assign(game::FRoomBattleStats{.rounds = 3, .ko = false, .active = false});
        CHECK(game::ResolveRoomVictory(registry, true).has_value());
        CHECK(boot.run().materials == materials + roomMaterials);
        CHECK(boot.run().message.find("Objective") == std::string::npos);
        CHECK(!LastRoomEnd(registry).contains("objective"));
    }
}

// ---------------------------------------------------------------------------------------------
// F13: first contact
// ---------------------------------------------------------------------------------------------

entt::entity Spawn(entt::registry& registry, bool party, game::FGridPosition cell, int maxHealth)
{
    const auto entity = registry.create();
    if (party) registry.emplace<game::FPartyMember>(entity);
    else registry.emplace<game::FEnemy>(entity);
    game::FUnitDefinition definition;
    definition.id = party ? "party.test" : "enemy.test";
    registry.emplace<game::FUnitData>(entity, game::FUnitData{definition});
    registry.emplace<game::FHealth>(entity, game::FHealth{maxHealth, maxHealth});
    registry.emplace<game::FGridPosition>(entity, cell);
    return entity;
}

void FirstContactRecordedOnce()
{
    entt::registry registry;
    const auto hero = Spawn(registry, true, {2, 2}, 10);
    const auto foe = Spawn(registry, false, {5, 2}, 10);
    (void)hero;
    game::FRoomBattleStats stats;
    stats.hpStart["party.test"] = 10;
    stats.hpMin["party.test"] = 10;
    stats.rounds = 0;
    game::UpdateFirstContact(registry, stats);
    CHECK(stats.firstAdjacentRound == 0);
    CHECK(stats.firstDamageRound == 0);

    Place(registry, foe, 3, 2);
    stats.rounds = 2;
    game::UpdateFirstContact(registry, stats);
    CHECK(stats.firstAdjacentRound == 2);
    CHECK(stats.firstDamageRound == 0);

    registry.get<game::FHealth>(foe).current = 6;
    game::UpdateFirstContact(registry, stats);
    CHECK(stats.firstDamageRound == 2);

    stats.rounds = 4;
    game::UpdateFirstContact(registry, stats);
    CHECK(stats.firstAdjacentRound == 2);
    CHECK(stats.firstDamageRound == 2);

    // A party unit that lost HP alone (enemy untouched) sets the damage round.
    {
        entt::registry other;
        Spawn(other, true, {2, 2}, 10);
        Spawn(other, false, {8, 8}, 10);
        game::FRoomBattleStats hurt;
        hurt.rounds = 3;
        hurt.hpStart["party.test"] = 10;
        hurt.hpMin["party.test"] = 7;
        game::UpdateFirstContact(other, hurt);
        CHECK(hurt.firstDamageRound == 3);
        CHECK(hurt.firstAdjacentRound == 0);
    }
    // A dead adjacent enemy is not contact.
    {
        entt::registry other;
        Spawn(other, true, {2, 2}, 10);
        const auto corpse = Spawn(other, false, {3, 2}, 10);
        other.emplace<game::FDead>(corpse);
        game::FRoomBattleStats quiet;
        quiet.rounds = 2;
        game::UpdateFirstContact(other, quiet);
        CHECK(quiet.firstAdjacentRound == 0);
    }
}

// ---------------------------------------------------------------------------------------------
// F16: the battle wiring
// ---------------------------------------------------------------------------------------------

void SetStakesEnv(const char* value)
{
#ifdef _WIN32
    _putenv_s("GAME01P_STAKES", value);
#else
    setenv("GAME01P_STAKES", value, 1);
#endif
}

void StakesWireIntoBattle()
{
    FBoot boot(ShipAssetRoots());
    boot.EnterBattle();
    auto& registry = boot.registry;
    registry.ctx().insert_or_assign(game::FCombatRuntime{});
    registry.ctx().get<game::FCombatRuntime>().rng.state = 1234;
    registry.ctx().insert_or_assign(game::FCombatLog{});
    game::ApplyBattleStakes(registry);

    const auto& combat = registry.ctx().get<game::FCombatRuntime>();
    CHECK(combat.critChancePercent == 15);
    CHECK(combat.critMultiplierPercent == 200);
    CHECK(combat.critRng.state != 0);
    CHECK(combat.critRng.state != combat.rng.state);

    const game::FAssetManager assets{ShipAssetRoots()};
    const auto table = game::LoadBattleObjectives(assets);
    CHECK(table.has_value());
    const game::FBattleObjectiveDef* expected =
        game::PickBattleObjective(*table, boot.run().seed, boot.run().depth, boot.run().room);
    CHECK(expected != nullptr);
    const auto* objective = registry.ctx().find<game::FBattleObjective>();
    CHECK(objective != nullptr);
    CHECK(objective->def.id == expected->id);
    CHECK(!objective->settled);
    const auto& log = registry.ctx().get<game::FCombatLog>();
    CHECK(std::ranges::any_of(log.lines, [](const std::string& line) { return line.rfind("Objective: ", 0) == 0; }));

    SetStakesEnv("0");
    CHECK(!game::StakesEnabled());
    SetStakesEnv("1");
    CHECK(game::StakesEnabled());
    SetStakesEnv("0");  // the value the test environment sets
}

} // namespace

int main()
{
    return game_test::RunTests("combat_stakes", {
        {"ZeroChanceNeverCrits", ZeroChanceNeverCrits},
        {"PreviewCarriesCrit", PreviewCarriesCrit},
        {"CritRollDealsCritDamage", CritRollDealsCritDamage},
        {"EnemiesNeverCrit", EnemiesNeverCrit},
        {"CritUsesItsOwnRng", CritUsesItsOwnRng},
        {"CritTuningLoads", CritTuningLoads},
        {"ObjectivesLoad", ObjectivesLoad},
        {"ObjectivePickIsSeeded", ObjectivePickIsSeeded},
        {"ObjectiveFailureRules", ObjectiveFailureRules},
        {"VictoryPaysMetObjective", VictoryPaysMetObjective},
        {"NoObjectiveNoBonus", NoObjectiveNoBonus},
        {"FirstContactRecordedOnce", FirstContactRecordedOnce},
        {"StakesWireIntoBattle", StakesWireIntoBattle},
    });
}
