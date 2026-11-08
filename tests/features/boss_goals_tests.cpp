// WP-15a: multi-stage boss goals — the bosses.json catalog (map / phases / gate / parts), the parts gate
// (cap, preview canon, execution safety net, continued-after-room guard), the phase function and its
// one-line transition log, the BossDefeated producer (WP-20 cross contract) and boss-part spawning in the
// generated room document.
//
// Every expectation is a rule computed from the loaded data: 15a ships NO bosses.json, so every catalog
// here is written by the test into a temporary asset root (or handed straight to LoadBossCatalog). The
// boss slot / theme ids are derived from the loaded fixture sequence, never spelled as constants.

#define GAME01P_TEST_SUPPORT_CHECK
#include "../TestSupport.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <queue>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/AssetManager.h"
#include "ecs/components/FActionPoints.h"
#include "ecs/components/FActiveStatuses.h"
#include "ecs/components/FBossGoals.h"
#include "ecs/components/FDead.h"
#include "ecs/components/FFaction.h"
#include "ecs/components/FHealth.h"
#include "ecs/components/FSkillLoadout.h"
#include "ecs/components/FSpeed.h"
#include "ecs/components/FStats.h"
#include "ecs/components/FTurnParticipant.h"
#include "ecs/systems/ActionResolverSystem.h"
#include "ecs/systems/CombatOutcomeSystem.h"
#include "ecs/systems/FBossGoalSystem.h"
#include "ecs/systems/TurnSystem.h"
#include "gameplay/battle/FBattleSetup.h"
#include "gameplay/combat/FCombatApply.h"
#include "gameplay/combat/FCombatSheet.h"
#include "gameplay/combat/FCombatTypes.h"
#include "gameplay/combat/FSkillResolver.h"
#include "gameplay/data/FContentRegistry.h"
#include "gameplay/dungeon/FBossContent.h"
#include "gameplay/dungeon/FDungeonRun.h"
#include "gameplay/dungeon/FRewards.h"
#include "gameplay/dungeon/FRoomGenerator.h"
#include "gameplay/dungeon/FRunEventLog.h"
#include "gameplay/dungeon/FRunFlow.h"
#include "gameplay/dungeon/FRunSequence.h"
#include "gameplay/factory/FEntityDefinitions.h"
#include "gameplay/factory/FEntityFactory.h"
#include "gameplay/factory/FScene.h"
#include "gameplay/grid/FGridTerrain.h"
#include "gameplay/grid/FGridTypes.h"

namespace {

using nlohmann::json;

// --- scratch files ------------------------------------------------------------------------------

std::string UniqueStem(const char* stem) {
    static std::uint32_t serial = 0;
    std::filesystem::create_directories("out");
    return std::string(stem) + "-" + std::to_string(++serial) + "-" +
           std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
}

std::string UniquePath(const char* stem) { return "out/" + UniqueStem(stem) + ".json"; }

json ReadJsonFile(const std::string& path) {
    std::ifstream file(path);
    CHECK(file.good());
    json document;
    file >> document;
    return document;
}

/// The frozen fixture's dungeon document (party cells, generation block, sequence).
json FixtureDocument() { return ReadJsonFile(FixtureDataPath("dungeon.json")); }

// --- fixture content (the frozen gameplay data only — no dungeon boot) ----------------------------

const game::gameplay::FContentRegistry& FixtureContent() {
    static const game::gameplay::FContentRegistry loaded = [] {
        auto table = game::gameplay::FContentRegistry::load(game::FAssetManager{TestAssetRoots()});
        CHECK(table.has_value());
        return *table;
    }();
    return loaded;
}

game::FRunSequence FixtureSequence() {
    std::string error;
    auto sequence = game::FRunSequence::Load(FixtureDocument(), error);
    CHECK(sequence.has_value());
    return *sequence;
}

std::vector<std::string> ThemeIds() {
    const auto sequence = FixtureSequence();
    std::vector<std::string> ids;
    for (const auto& theme : sequence.themes) ids.push_back(theme.id);
    return ids;
}

/// The first (depth, battle ordinal) slot of the fixture sequence that carries `which` ("mid" / "final").
struct FBossSlot {
    int depth{1};
    int room{0};
    std::string slot;
};

FBossSlot BossSlotOf(const game::FRunSequence& sequence, std::string_view which) {
    for (int index = 0; index < sequence.TotalDungeons(); ++index) {
        const auto position = sequence.PositionOf(index);
        for (int ordinal = 0; ordinal < sequence.BattleCountAt(position); ++ordinal) {
            const auto* slot = sequence.BattleSlotAt(position, ordinal);
            if (slot != nullptr && slot->boss == which) return {index + 1, ordinal, slot->boss};
        }
    }
    throw std::runtime_error("the fixture sequence has no boss slot '" + std::string(which) + "'");
}

// --- bosses.json builders -----------------------------------------------------------------------

json PhaseRow(int hpAbove, bool partsGate) {
    json phase;
    phase["hp_above"] = hpAbove;
    if (partsGate) phase["gate"] = "parts";
    return phase;
}

json PartRow(std::string unit, std::string name, int count) {
    json row;
    row["unit"] = std::move(unit);
    row["name"] = std::move(name);
    row["count"] = count;
    return row;
}

json BossDefinition(std::string id, std::string name, std::string unit, std::vector<json> phases,
                    std::vector<json> parts = {}) {
    json boss;
    boss["id"] = std::move(id);
    boss["name"] = std::move(name);
    boss["unit"] = std::move(unit);
    boss["phases"] = json::array();
    for (auto& phase : phases) boss["phases"].push_back(std::move(phase));
    if (!parts.empty()) {
        boss["parts"] = json::array();
        for (auto& part : parts) boss["parts"].push_back(std::move(part));
    }
    return boss;
}

json BossCatalog(std::string theme, std::string slot, json boss) {
    json mapRow;
    mapRow["theme"] = std::move(theme);
    mapRow["slot"] = std::move(slot);
    mapRow["boss"] = boss["id"];
    json catalog;
    catalog["schema_version"] = game::kBossCatalogSchemaVersion;
    catalog["map"] = json::array({mapRow});
    catalog["bosses"] = json::array({boss});
    return catalog;
}

/// The 15a canonical workhorse: body `enemy.guardian`, one gated phase at 50%, three `enemy.imp` parts.
json TestGuardianBoss() {
    return BossDefinition("boss.test_guardian", "Test Guardian", "enemy.guardian",
                          {PhaseRow(50, true), PhaseRow(0, false)},
                          {PartRow("enemy.imp", "Test Part", 3)});
}

/// A temporary asset root ahead of the shipped one (edited documents + a test-authored bosses.json).
struct FTempAssets {
    std::filesystem::path root;

    explicit FTempAssets(const char* stem) {
        root = std::filesystem::path("out") / UniqueStem(stem);
        std::filesystem::create_directories(root / "data");
    }

    ~FTempAssets() {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }

    FTempAssets(const FTempAssets&) = delete;
    FTempAssets& operator=(const FTempAssets&) = delete;

    void Copy(const std::string& file) {
        std::error_code error;
        std::filesystem::copy_file(
            FixtureDataPath(file), root / "data" / file, std::filesystem::copy_options::overwrite_existing, error);
        CHECK(!error);
    }

    void CopyGameplayData() {
        for (const char* file : {"units.json", "skills.json", "runes.json", "statuses.json", "ai.json",
                                 "rewards.json", "passive-tree.json", "dungeon.json"}) {
            Copy(file);
        }
    }

    void Write(const std::string& file, const json& document) {
        std::ofstream out(root / "data" / file, std::ios::trunc);
        CHECK(out.good());
        out << document.dump(2);
    }
};

// --- a booted dungeon (a failing boot is reported, not asserted, so the boot-refusal tests can read it) --

struct FBoot {
    std::string checkpoint;
    entt::registry registry;
    std::string runSave;
    bool content{false};
    bool dungeon{false};

    FBoot(std::string path, std::vector<std::string> roots) : checkpoint(std::move(path)) {
        game::FAssetManager::Initialize(registry, std::move(roots));
        content = game::gameplay::FGameplayServices::Initialize(registry);
        if (content) dungeon = game::InitializeDungeon(registry, checkpoint);
        if (dungeon) runSave = registry.ctx().get<game::FDungeonSave>().path;
    }

    ~FBoot() {
        std::error_code error;
        std::filesystem::remove(checkpoint, error);
        if (!runSave.empty()) {
            std::filesystem::remove(runSave, error);
            std::filesystem::remove(runSave + ".tmp", error);
        }
    }

    FBoot(const FBoot&) = delete;
    FBoot& operator=(const FBoot&) = delete;
};

// --- a real (manually fielded) battle at a boss slot --------------------------------------------

/// Reproduces FEmberBattleState::OnEnter's combat section (minus presentation): content -> dungeon ->
/// run at the requested slot -> battle scoped ctx -> SetupCombatUnits -> ApplyRunParty. The scene is kept
/// alive for the whole fixture (FScene owns every entity it created).
struct FBattle {
    std::unique_ptr<FTempAssets> assets;
    std::string checkpoint;
    entt::registry registry;
    std::string runSave;
    json document;
    int depth{1};
    int room{0};
    std::string bossId;
    std::unique_ptr<game::FScene> scene;

    /// `bosses` = the catalog to author into the temp root (nullptr = no bosses.json: every slot falls
    /// back). `bossSlot` == "" fields a non-boss battle (depth 1 room 0).
    FBattle(const char* stem, const json* bosses, std::string_view bossSlot, std::uint32_t seed = 4242) {
        assets = std::make_unique<FTempAssets>(stem);
        assets->CopyGameplayData();
        if (bosses != nullptr) assets->Write("bosses.json", *bosses);
        checkpoint = UniquePath(stem);
        game::FAssetManager::Initialize(registry, {assets->root.string(), kShipAssetRoot});
        CHECK(game::gameplay::FGameplayServices::Initialize(registry));
        CHECK(game::InitializeDungeon(registry, checkpoint));
        runSave = registry.ctx().get<game::FDungeonSave>().path;
        game::StartRun(registry, seed, true);

        auto& run = registry.ctx().get<game::FDungeonRun>();
        if (!bossSlot.empty()) {
            const auto slot = BossSlotOf(registry.ctx().get<game::FRunSequence>(), bossSlot);
            depth = slot.depth;
            room = slot.room;
        }
        run.depth = depth;
        run.room = room;

        document = game::BuildDungeonBattlefield(registry);
        CHECK(!document.empty());
        scene = std::make_unique<game::FScene>(registry, "BossGoalsTest");
        scene->Load();
        const auto battlefield = game::FEntityFactory::CreateBattlefield(*scene, document);
        CHECK(battlefield.has_value());
        registry.ctx().erase<game::FGridTerrain>();
        registry.ctx().emplace<game::FGridTerrain>(battlefield->terrain);
        registry.ctx().emplace<game::FGridMetrics>(battlefield->metrics);
        CHECK(game::SetupCombatUnits(registry, *battlefield, registry.ctx().get<game::gameplay::FContentRegistry>()));
        registry.ctx().emplace<game::FActionQueue>();
        registry.ctx().emplace<game::FCombatLog>();
        registry.ctx().emplace<game::FTurnRuntime>();
        registry.ctx().emplace<game::FCombatRuntime>();
        registry.ctx().emplace<game::FBattleOutcome>();
        game::ApplyRunParty(registry);
        if (const auto* marker = registry.ctx().find<game::FBossBattle>()) bossId = marker->bossId;
    }

    ~FBattle() {
        scene.reset();
        std::error_code error;
        std::filesystem::remove(checkpoint, error);
        if (!runSave.empty()) {
            std::filesystem::remove(runSave, error);
            std::filesystem::remove(runSave + ".tmp", error);
        }
    }

    FBattle(const FBattle&) = delete;
    FBattle& operator=(const FBattle&) = delete;

    game::FDungeonRun& run() { return registry.ctx().get<game::FDungeonRun>(); }

    [[nodiscard]] const json& generation() { return game::DungeonDefinition(registry).at("generation"); }

    /// The body (the only unit with FBossGoals) or entt::null for a fallback boss.
    entt::entity BossBody() {
        for (const auto entity : registry.view<game::FBossGoals>()) return entity;
        return entt::null;
    }

    std::vector<entt::entity> Parts() {
        std::vector<entt::entity> parts;
        const auto body = BossBody();
        if (body == entt::null) return parts;
        for (const auto [entity, part] : registry.view<const game::FBossPart>().each()) {
            if (part.body == body) parts.push_back(entity);
        }
        return parts;
    }

    std::vector<entt::entity> Enemies() {
        std::vector<entt::entity> enemies;
        for (const auto entity : registry.view<game::FEnemy, game::FHealth>()) enemies.push_back(entity);
        return enemies;
    }

    entt::entity EnemyNamed(std::string_view name) {
        for (const auto entity : registry.view<game::FUnitData, game::FHealth>()) {
            if (registry.all_of<game::FPartyMember>(entity)) continue;
            if (registry.get<game::FUnitData>(entity).definition.displayName == name) return entity;
        }
        return entt::null;
    }

    /// Parts first (the gate holds the body while they stand), then the body, then every remaining enemy.
    void KillAll() {
        for (const auto part : Parts()) (void)game::ApplyDamageToUnit(registry, part, 100000);
        if (const auto body = BossBody(); body != entt::null) {
            (void)game::ApplyDamageToUnit(registry, body, 100000);
        }
        for (const auto enemy : Enemies()) (void)game::ApplyDamageToUnit(registry, enemy, 100000);
    }

    void RunCos(int frames) {
        game::ecs::CombatOutcomeSystem outcomes;
        for (int frame = 0; frame < frames; ++frame) outcomes.update(registry, 0.016f);
    }
};

// --- minimal rigs (rules only: no content, no scene) --------------------------------------------

std::vector<game::FBossPhase> PhasesOf(std::vector<std::pair<int, bool>> rows) {
    std::vector<game::FBossPhase> phases;
    for (const auto& [hpAbove, partsGate] : rows) {
        game::FBossPhase phase;
        phase.hpAbove = hpAbove;
        phase.partsGate = partsGate;
        phases.push_back(std::move(phase));
    }
    return phases;
}

/// A body with FBossGoals + N parts, a Battle-phase run and a combat feed. Enough for the gate and phase
/// rules, which are pure reads of HP + parts + the run phase.
struct FGateRig {
    entt::registry registry;
    entt::entity body{entt::null};
    std::vector<entt::entity> parts;

    FGateRig(int maxHealth, std::vector<game::FBossPhase> phases, int partCount,
             game::EDungeonPhase phase = game::EDungeonPhase::Battle) {
        registry.ctx().emplace<game::FDungeonRun>().phase = phase;
        registry.ctx().emplace<game::FCombatLog>();
        body = registry.create();
        registry.emplace<game::FHealth>(body, game::FHealth{maxHealth, maxHealth});
        registry.emplace<game::FTurnParticipant>(body);
        game::FBossGoals goals;
        goals.bossId = "boss.test_guardian";
        goals.name = "Test Guardian";
        goals.phases = std::move(phases);
        registry.emplace<game::FBossGoals>(body, std::move(goals));
        for (int index = 0; index < partCount; ++index) {
            const auto part = registry.create();
            registry.emplace<game::FHealth>(part, game::FHealth{10, 10});
            registry.emplace<game::FBossPart>(part, game::FBossPart{body});
            parts.push_back(part);
        }
    }

    game::FBossGoals& Goals() { return registry.get<game::FBossGoals>(body); }
    void SetBodyHp(int hp) { registry.get<game::FHealth>(body).current = hp; }
    [[nodiscard]] int BodyHp() { return registry.get<game::FHealth>(body).current; }

    void Kill(entt::entity unit) { (void)game::ApplyDamageToUnit(registry, unit, 100000); }
    void KillParts() {
        for (const auto part : parts) Kill(part);
    }
};

int CountInLog(const entt::registry& registry, const char* needle) {
    const auto* log = registry.ctx().find<game::FCombatLog>();
    if (log == nullptr) return 0;
    int count = 0;
    for (const auto& line : log->lines) {
        if (line.find(needle) != std::string::npos) ++count;
    }
    return count;
}

/// A party hero (adjacent to the body) and a gated boss body with its parts — content + grid + turn
/// runtime: the resolver and the preview path can run end to end.
struct FMiniBattle {
    entt::registry registry;
    entt::entity actor{entt::null};
    entt::entity body{entt::null};
    std::vector<entt::entity> parts;

    FMiniBattle(int bodyMax, int gatedHpAbove, int partCount, int heroAttack = 200) {
        game::FAssetManager::Initialize(registry, TestAssetRoots());
        CHECK(game::gameplay::FGameplayServices::Initialize(registry));
        registry.ctx().emplace<game::FGridTerrain>();
        registry.ctx().emplace<game::FGridMetrics>();
        registry.ctx().emplace<game::FDungeonRun>().phase = game::EDungeonPhase::Battle;
        registry.ctx().emplace<game::FCombatLog>();
        registry.ctx().emplace<game::FCombatRuntime>();
        registry.ctx().emplace<game::FTurnRuntime>();
        registry.ctx().emplace<game::FActionQueue>();

        actor = Spawn({2, 2}, true, game::FStats{heroAttack, 0, 0, 0}, 100, 10, "Test Hero");
        registry.get<game::FSkillLoadout>(actor).slots.push_back(game::FSkillSlot{"skill.strike", {}});
        registry.get<game::FActionPoints>(actor) = game::FActionPoints{1, 1, 2, 2};

        body = Spawn({3, 2}, false, game::FStats{0, 0, 5, 0}, bodyMax, 2, "Test Guardian");
        game::FBossGoals goals;
        goals.bossId = "boss.test_guardian";
        goals.name = "Test Guardian";
        goals.phases = PhasesOf({{gatedHpAbove, true}, {0, false}});
        registry.emplace<game::FBossGoals>(body, std::move(goals));

        for (int index = 0; index < partCount; ++index) {
            const auto part = Spawn({5 + index, 6}, false, game::FStats{}, 10, 1, "Test Part");
            registry.emplace<game::FBossPart>(part, game::FBossPart{body});
            parts.push_back(part);
        }
    }

    entt::entity Spawn(game::FGridPosition cell, bool party, game::FStats stats, int maxHealth, int speed,
                       std::string name) {
        const auto entity = registry.create();
        if (party) {
            registry.emplace<game::FPartyMember>(entity);
        } else {
            registry.emplace<game::FEnemy>(entity);
        }
        game::FUnitDefinition definition;
        definition.id = party ? "party.test" : "enemy.test";
        definition.displayName = std::move(name);
        definition.faction = party ? game::EUnitFaction::Party : game::EUnitFaction::Enemy;
        registry.emplace<game::FUnitData>(entity, game::FUnitData{definition});
        registry.emplace<game::FStats>(entity, stats);
        registry.emplace<game::FHealth>(entity, game::FHealth{maxHealth, maxHealth});
        registry.emplace<game::FGridPosition>(entity, cell);
        registry.emplace<game::FActionPoints>(entity, game::FActionPoints{1, 1, 2, 2});
        registry.emplace<game::FSpeed>(entity, game::FSpeed{speed});
        registry.emplace<game::FTurnParticipant>(entity);
        registry.emplace<game::FActiveStatuses>(entity);
        // Every combat unit carries a (possibly empty) loadout: the resolver and the tests' equip step read
        // it with get<>, and a missing component is an entt assertion, not a nullopt.
        registry.emplace<game::FSkillLoadout>(entity);
        return entity;
    }

    const game::gameplay::FContentRegistry& Content() {
        return registry.ctx().get<game::gameplay::FContentRegistry>();
    }

    void SetBodyHp(int hp) { registry.get<game::FHealth>(body).current = hp; }
};

/// Turns pass until `actor` holds the live turn, then its token is returned (the resolver's validation).
game::FTurnToken StartTurnFor(entt::registry& registry, entt::entity actor) {
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

// --- run events ---------------------------------------------------------------------------------

std::vector<game::FRunEvent> RunEvents(entt::registry& registry, game::ERunEventKind kind) {
    std::vector<game::FRunEvent> found;
    if (const auto* log = registry.ctx().find<game::FRunEventLog>()) {
        for (const auto& event : log->tail) {
            if (event.kind == kind) found.push_back(event);
        }
    }
    return found;
}

std::vector<std::pair<int, std::string>> EventSequence(entt::registry& registry) {
    std::vector<std::pair<int, std::string>> sequence;
    if (const auto* log = registry.ctx().find<game::FRunEventLog>()) {
        for (const auto& event : log->tail) {
            sequence.emplace_back(static_cast<int>(event.kind), event.id);
        }
    }
    return sequence;
}

/// Run-flow steps whose refusal carries the reason: CHECK can only say "no value", so the reason becomes the
/// thrown message (a refused reward step names the open rune offer, a wrong phase, a save failure).
game::FRunResult MustResolveVictory(entt::registry& registry, bool victory) {
    auto result = game::ResolveRoomVictory(registry, victory);
    if (!result) throw std::runtime_error("ResolveRoomVictory refused: " + result.error());
    return *result;
}

game::FRunResult MustApplyIntent(entt::registry& registry, game::ERunIntent intent) {
    auto result = game::ApplyRunIntent(registry, intent);
    if (!result) throw std::runtime_error("ApplyRunIntent refused: " + result.error());
    return *result;
}

/// The reward step refuses to advance while a rune offer is open: pick the first card (the fixture's
/// rewards.json opens one on every room, boss rooms included).
void SettleRuneOffer(entt::registry& registry) {
    const auto* rewards = registry.ctx().find<game::FRewards>();
    if (rewards != nullptr && rewards->offer.active) CHECK(game::ResolveRuneOffer(registry, 0));
}

// --- room generation helpers (mirroring the fixture's crafted data) ------------------------------

/// Builds an FRoomGenConfig from a dungeon document's generation block (the same fields the game's
/// ParseRoomGeneration reads, re-derived here so a parser change cannot move these expectations).
game::gameplay::FRoomGenConfig ConfigFromDocument(const json& document, std::string_view family = "open") {
    const auto& generation = document.at("generation");
    game::gameplay::FRoomGenConfig config;
    for (const auto& name : generation.at("room_names")) config.names.push_back(name.get<std::string>());
    for (const auto& enemy : generation.at("enemies")) {
        config.enemies.push_back({enemy.at("unit").get<std::string>(),
                                  enemy.at("display_name").get<std::string>(),
                                  enemy.at("weight").get<int>()});
    }
    config.bossUnit = generation.at("boss_unit").get<std::string>();
    config.bossName = generation.at("boss_name").get<std::string>();
    config.enemyCount = {generation.at("enemy_count")[0].get<int>(), generation.at("enemy_count")[1].get<int>()};
    config.obstacleCount = {generation.at("obstacle_count")[0].get<int>(), generation.at("obstacle_count")[1].get<int>()};
    config.trapCount = {generation.at("trap_count")[0].get<int>(), generation.at("trap_count")[1].get<int>()};
    config.family = std::string(family);
    return config;
}

std::vector<std::pair<int, int>> PartyCellsFromDocument(const json& document) {
    std::vector<std::pair<int, int>> cells;
    for (const auto& member : document.at("party")) {
        cells.emplace_back(member.at("grid_x").get<int>(), member.at("grid_y").get<int>());
    }
    return cells;
}

/// Independent BFS: every unit (party + enemies) mutually reachable over non-wall cells, no shared cells.
bool AllUnitsReachable(const game::gameplay::FGeneratedRoom& room,
                       const std::vector<std::pair<int, int>>& party) {
    const int size = game::gameplay::FGeneratedRoom::kSize;
    std::vector<std::pair<int, int>> units = party;
    for (const auto& enemy : room.enemies) units.emplace_back(enemy.x, enemy.y);
    std::ranges::sort(units);
    CHECK(std::ranges::adjacent_find(units) == units.end());
    std::array<std::array<bool, game::gameplay::FGeneratedRoom::kSize>, game::gameplay::FGeneratedRoom::kSize> seen{};
    std::queue<std::pair<int, int>> open;
    open.push(units.front());
    seen[static_cast<std::size_t>(units.front().second)][static_cast<std::size_t>(units.front().first)] = true;
    while (!open.empty()) {
        const auto [x, y] = open.front();
        open.pop();
        for (const auto [dx, dy] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}}) {
            const int nx = x + dx;
            const int ny = y + dy;
            if (nx < 0 || ny < 0 || nx >= size || ny >= size || seen[static_cast<std::size_t>(ny)][static_cast<std::size_t>(nx)]) {
                continue;
            }
            if (room.terrain[static_cast<std::size_t>(ny)][static_cast<std::size_t>(nx)] == '#') continue;
            seen[static_cast<std::size_t>(ny)][static_cast<std::size_t>(nx)] = true;
            open.push({nx, ny});
        }
    }
    return std::ranges::all_of(units, [&](const std::pair<int, int>& unit) {
        return seen[static_cast<std::size_t>(unit.second)][static_cast<std::size_t>(unit.first)];
    });
}

// ------------------------------------------------------------------------------------------------
// data: the bosses.json schema, map resolution and the boot refusals
// ------------------------------------------------------------------------------------------------

void BossMapResolvesByThemeSlot() {
    const auto themes = ThemeIds();
    CHECK(themes.size() >= 2);
    const auto document = BossCatalog(themes.front(), "mid", TestGuardianBoss());
    std::string error;
    const auto catalog = game::LoadBossCatalog(document, FixtureContent(), themes, error);
    CHECK(catalog.has_value());
    CHECK(error.empty());

    const auto* resolved = catalog->Resolve(themes.front(), "mid");
    CHECK(resolved != nullptr);
    CHECK(resolved->id == "boss.test_guardian");
    CHECK(resolved->name == "Test Guardian");
    CHECK(resolved->unit == "enemy.guardian");
    CHECK(resolved->phases.size() == 2);
    CHECK(resolved->phases.front().partsGate);
    CHECK(resolved->parts.size() == 1);
    CHECK(resolved->parts.front().unit == "enemy.imp");
    CHECK(resolved->parts.front().count == 3);
    CHECK(catalog->Find("boss.test_guardian") == resolved);
    // Only the authored (theme, slot) pair resolves; the other slot and the other theme are unmapped.
    CHECK(catalog->Resolve(themes.front(), "final") == nullptr);
    CHECK(catalog->Resolve(themes.back(), "mid") == nullptr);
}

void UnmappedBossFallsBack() {
    FBattle battle("boss-goals-fallback", nullptr, "mid");
    auto& registry = battle.registry;

    // 15a ships no bosses.json: the loader installs an EMPTY catalog (not "no catalog"), so every boss
    // slot takes the pre-WP-15 guardian path.
    const auto* catalog = game::FindBossCatalog(registry);
    CHECK(catalog != nullptr);
    CHECK(catalog->bosses.empty());
    CHECK(catalog->map.empty());

    const auto& generation = battle.generation();
    const std::string bossUnit = generation.at("boss_unit").get<std::string>();
    const std::string bossName = generation.at("boss_name").get<std::string>();

    const auto* marker = registry.ctx().find<game::FBossBattle>();
    CHECK(marker != nullptr);
    CHECK(marker->bossId == bossUnit);  // the fallback id is generation.boss_unit (WP-20 cross contract)
    CHECK(marker->slot == "mid");

    // The fielded boss really is the generation fallback unit, first in the enemy list.
    const auto& units = battle.document.at("units");
    const json* firstEnemy = nullptr;
    for (const auto& unit : units) {
        if (unit.at("faction").get<std::string>() == "enemy") {
            firstEnemy = &unit;
            break;
        }
    }
    CHECK(firstEnemy != nullptr);
    CHECK(firstEnemy->at("unit").get<std::string>() == bossUnit);
    CHECK(firstEnemy->at("display_name").get<std::string>() == bossName);
    const auto partySize = static_cast<std::size_t>(game::DungeonDefinition(registry).at("party").size());
    CHECK(units.size() == partySize + 3);  // boss + two escorts: the pre-WP-15 composition

    // Single phase means no goals attached and no parts — the guardian path is untouched.
    CHECK(registry.view<game::FBossGoals>().size() == 0);
    CHECK(registry.view<game::FBossPart>().size() == 0);
    const auto boss = battle.EnemyNamed(bossName);
    CHECK(boss != entt::null);
    CHECK(game::BossActivePhase(registry, boss) == -1);
}

void BadBossRefFailsBoot() {
    const auto themes = ThemeIds();
    const auto load = [&](const json& document, std::string& error) {
        return game::LoadBossCatalog(document, FixtureContent(), themes, error);
    };

    // An unknown boss id in the map.
    {
        auto document = BossCatalog(themes.front(), "mid", TestGuardianBoss());
        document["map"][0]["boss"] = "boss.missing";
        std::string error;
        const auto catalog = load(document, error);
        CHECK(!catalog.has_value());
        CHECK(!error.empty());
        CHECK(error.find("boss.missing") != std::string::npos);
    }
    // An unknown body unit.
    {
        auto boss = TestGuardianBoss();
        boss["unit"] = "enemy.missing";
        std::string error;
        const auto catalog = load(BossCatalog(themes.front(), "mid", boss), error);
        CHECK(!catalog.has_value());
        CHECK(error.find("enemy.missing") != std::string::npos);
    }
    // An unknown part unit.
    {
        auto boss = TestGuardianBoss();
        boss["parts"][0]["unit"] = "enemy.missing_part";
        std::string error;
        const auto catalog = load(BossCatalog(themes.front(), "mid", boss), error);
        CHECK(!catalog.has_value());
        CHECK(error.find("enemy.missing_part") != std::string::npos);
    }
    // A present-but-invalid document stops the boot (the whole run must not start on guessed content).
    {
        auto boss = TestGuardianBoss();
        boss["unit"] = "enemy.missing";
        FTempAssets assets("boss-goals-bad-ref");
        assets.CopyGameplayData();
        assets.Write("bosses.json", BossCatalog(themes.front(), "mid", boss));
        FBoot boot(UniquePath("boss-goals-bad-ref"), {assets.root.string(), kShipAssetRoot});
        CHECK(boot.content);
        CHECK(!boot.dungeon);
    }
}

void BadGateRejectedAtBoot() {
    const auto themes = ThemeIds();

    // Anything but "parts" is refused (data says WHAT, code interprets HOW — no condition DSL).
    {
        auto boss = TestGuardianBoss();
        boss["phases"][0]["gate"] = "hp";
        std::string error;
        const auto catalog = game::LoadBossCatalog(BossCatalog(themes.front(), "mid", boss), FixtureContent(), themes, error);
        CHECK(!catalog.has_value());
        CHECK(!error.empty());
        FTempAssets assets("boss-goals-bad-gate");
        assets.CopyGameplayData();
        assets.Write("bosses.json", BossCatalog(themes.front(), "mid", boss));
        FBoot boot(UniquePath("boss-goals-bad-gate"), {assets.root.string(), kShipAssetRoot});
        CHECK(boot.content);
        CHECK(!boot.dungeon);
    }
    // hp_above must be strictly descending ...
    {
        auto boss = TestGuardianBoss();
        boss["phases"] = json::array({PhaseRow(50, true), PhaseRow(50, false), PhaseRow(0, false)});
        std::string error;
        CHECK(!game::LoadBossCatalog(BossCatalog(themes.front(), "mid", boss), FixtureContent(), themes, error).has_value());
        CHECK(!error.empty());
    }
    // ... and the last phase must be 0.
    {
        auto boss = TestGuardianBoss();
        boss["phases"] = json::array({PhaseRow(50, true), PhaseRow(20, false)});
        std::string error;
        CHECK(!game::LoadBossCatalog(BossCatalog(themes.front(), "mid", boss), FixtureContent(), themes, error).has_value());
        CHECK(!error.empty());
    }
    // A gated phase needs a positive threshold and at least one part to gate on.
    {
        auto boss = TestGuardianBoss();
        boss["phases"] = json::array({PhaseRow(0, true)});
        std::string error;
        CHECK(!game::LoadBossCatalog(BossCatalog(themes.front(), "mid", boss), FixtureContent(), themes, error).has_value());
    }
    {
        auto boss = TestGuardianBoss();
        boss.erase("parts");
        std::string error;
        CHECK(!game::LoadBossCatalog(BossCatalog(themes.front(), "mid", boss), FixtureContent(), themes, error).has_value());
    }
}

void SecondGateRejectedAtBoot() {
    const auto themes = ThemeIds();
    auto boss = TestGuardianBoss();
    boss["phases"] = json::array({PhaseRow(75, true), PhaseRow(50, true), PhaseRow(0, false)});
    std::string error;
    const auto catalog = game::LoadBossCatalog(BossCatalog(themes.front(), "mid", boss), FixtureContent(), themes, error);
    CHECK(!catalog.has_value());
    CHECK(!error.empty());

    FTempAssets assets("boss-goals-two-gates");
    assets.CopyGameplayData();
    assets.Write("bosses.json", BossCatalog(themes.front(), "mid", boss));
    FBoot boot(UniquePath("boss-goals-two-gates"), {assets.root.string(), kShipAssetRoot});
    CHECK(boot.content);
    CHECK(!boot.dungeon);
}

// ------------------------------------------------------------------------------------------------
// phase function
// ------------------------------------------------------------------------------------------------

void GateFloorIsMaxGatedThreshold() {
    // Ungated 75 / gated 50 / 0: the floor is the GATED threshold (only one gate per boss is legal).
    FGateRig rig(120, PhasesOf({{75, false}, {50, true}, {0, false}}), 2);
    const auto floor = game::BossGateFloor(rig.registry, rig.body);
    CHECK(floor.has_value());
    CHECK(*floor == game::BossPhaseFloor(120, 50));
    CHECK(*floor == 60);
    CHECK(*floor != game::BossPhaseFloor(120, 75));

    // The floor tracks the live max HP (never a cached number): 110 -> 55.
    FGateRig smaller(110, PhasesOf({{75, false}, {50, true}, {0, false}}), 1);
    CHECK(game::BossGateFloor(smaller.registry, smaller.body) == game::BossPhaseFloor(110, 50));
    CHECK(*game::BossGateFloor(smaller.registry, smaller.body) == 55);
}

void FloorRoundsUp() {
    CHECK(game::BossPhaseFloor(110, 50) == 55);
    CHECK(game::BossPhaseFloor(101, 50) == 51);
    CHECK(game::BossPhaseFloor(110, 0) == 0);
    // ceil definition, independent of the implementation's integer form: floor is the smallest n with
    // n * 100 >= max * pct.
    for (int max = 1; max <= 200; ++max) {
        for (int pct = 1; pct <= 100; ++pct) {
            const int floor = game::BossPhaseFloor(max, pct);
            CHECK(floor >= 1);
            CHECK(floor * 100 >= max * pct);
            CHECK((floor - 1) * 100 < max * pct);
        }
    }
}

void PhaseHeldWhilePartsLive() {
    FGateRig rig(110, PhasesOf({{50, true}, {0, false}}), 2);
    rig.SetBodyHp(55);  // exactly the floor
    CHECK(game::BossPartsAlive(rig.registry, rig.body) == 2);
    CHECK(game::BossActivePhase(rig.registry, rig.body) == 0);
    CHECK(*game::BossGateFloor(rig.registry, rig.body) == 55);
    // The gate entry does not fire a phase change: the phase only moves when the last part dies.
    game::ecs::EvaluateBossGoals(rig.registry);
    CHECK(rig.Goals().activePhase == 0);
}

void TransitionOnLastPartDeath() {
    FGateRig rig(100, PhasesOf({{50, true}, {0, false}}), 3);
    rig.SetBodyHp(50);  // the floor
    CHECK(game::BossActivePhase(rig.registry, rig.body) == 0);
    rig.Kill(rig.parts[0]);
    CHECK(game::BossActivePhase(rig.registry, rig.body) == 0);
    rig.Kill(rig.parts[1]);
    CHECK(game::BossActivePhase(rig.registry, rig.body) == 0);
    rig.Kill(rig.parts[2]);
    CHECK(game::BossActivePhase(rig.registry, rig.body) == 1);
    CHECK(game::BossPartsAlive(rig.registry, rig.body) == 0);
}

void TransitionLoggedOnce() {
    FGateRig rig(100, PhasesOf({{50, true}, {0, false}}), 2);
    rig.SetBodyHp(50);

    // The first evaluation records the phase and announces the guard (one line).
    game::ecs::EvaluateBossGoals(rig.registry);
    CHECK(CountInLog(rig.registry, "is guarded by") == 1);
    CHECK(CountInLog(rig.registry, "enters phase 2") == 0);

    rig.Kill(rig.parts[0]);
    game::ecs::EvaluateBossGoals(rig.registry);
    CHECK(CountInLog(rig.registry, "enters phase 2") == 0);

    rig.Kill(rig.parts[1]);
    for (int frame = 0; frame < 40; ++frame) game::ecs::EvaluateBossGoals(rig.registry);
    CHECK(rig.Goals().activePhase == 1);
    CHECK(CountInLog(rig.registry, "enters phase 2") == 1);
}

// ------------------------------------------------------------------------------------------------
// the parts gate
// ------------------------------------------------------------------------------------------------

void PartsGateCapsBodyAtThreshold() {
    FGateRig rig(110, PhasesOf({{50, true}, {0, false}}), 3);
    CHECK(!game::ApplyDamageToUnit(rig.registry, rig.body, 9999));
    CHECK(rig.BodyHp() == 55);  // min(incoming, max - floor): the floor, never below
    CHECK(!rig.registry.all_of<game::FDead>(rig.body));
    CHECK(rig.registry.all_of<game::FTurnParticipant>(rig.body));
    CHECK(rig.Goals().gateBlockLogged);  // the first blocked hit is reported once
    // The floor is not a one-time clamp: a second huge hit lands nothing.
    CHECK(!game::ApplyDamageToUnit(rig.registry, rig.body, 9999));
    CHECK(rig.BodyHp() == 55);
}

void GatedBodyPreviewZeroDamage() {
    FMiniBattle battle(110, 50, 2);
    const auto resolved = game::ResolveSkillSlot(game::FSkillSlot{"skill.strike", {}}, battle.Content());
    CHECK(resolved.has_value());
    const auto bodyCell = battle.registry.get<game::FGridPosition>(battle.body);
    const auto actorCell = battle.registry.get<game::FGridPosition>(battle.actor);
    const int floor = game::BossPhaseFloor(110, 50);

    // At the floor the preview is a REAL zero (the cap sits after ComputeDamage's minimum-1).
    battle.SetBodyHp(floor);
    auto preview = game::PreviewSkill(battle.registry, battle.actor, *resolved, bodyCell, actorCell);
    CHECK(preview.has_value());
    CHECK(preview->targets.size() == 1);
    const auto& blocked = preview->targets.front();
    CHECK(blocked.unit == battle.body);
    CHECK(blocked.damage == 0);
    CHECK(blocked.healthAfter == floor);
    CHECK(!blocked.lethal);
    CHECK(blocked.blockedByGate);
    CHECK(blocked.offense == 200);  // the formula terms stay the uncapped ones
    CHECK(blocked.power == resolved->power);

    // Above the floor the same big hit previews exactly current - floor (the gate's allowance).
    battle.SetBodyHp(110);
    preview = game::PreviewSkill(battle.registry, battle.actor, *resolved, bodyCell, actorCell);
    CHECK(preview.has_value());
    CHECK(preview->targets.size() == 1);
    const auto& capped = preview->targets.front();
    CHECK(capped.damage == 110 - floor);
    CHECK(capped.healthAfter == floor);
    CHECK(!capped.lethal);
    CHECK(capped.blockedByGate);
}

void GatedHitLogsZero() {
    FMiniBattle battle(110, 50, 2);
    const int floor = game::BossPhaseFloor(110, 50);
    battle.SetBodyHp(floor);
    const auto resolved = game::ResolveSkillSlot(game::FSkillSlot{"skill.strike", {}}, battle.Content());
    CHECK(resolved.has_value());
    auto& registry = battle.registry;

    // The real resolver path: the preview is applied as-is (no resolver edit — C-R3).
    game::FActionRequest request;
    request.actor = battle.actor;
    request.target = battle.body;
    request.skillId = "skill.strike";
    request.cell = registry.get<game::FGridPosition>(battle.body);
    request.token = StartTurnFor(registry, battle.actor);
    registry.ctx().get<game::FActionQueue>().pending.push_back(request);

    game::ecs::ActionResolverSystem resolver;
    resolver.update(registry, 0.f);

    CHECK(registry.get<game::FHealth>(battle.body).current == floor);
    CHECK(!registry.all_of<game::FDead>(battle.body));

    const auto& log = registry.ctx().get<game::FCombatLog>();
    int zeroLines = 0;
    for (const auto& line : log.lines) {
        CHECK(line.find("knocked out") == std::string::npos);
        CHECK(line.find(" / KO") == std::string::npos);
        if (line.find("-0 HP") != std::string::npos) ++zeroLines;
    }
    CHECK(zeroLines >= 1);  // the applied number is 0 and the feed says so
    CHECK(CountInLog(registry, "is shielded") == 1);
}

void ContinuedBossRoomShowsAllDefeated() {
    // A saved-phase continue (continuedAfterRoom) rebuilds the field with run.phase != Battle and then
    // kill-alls every enemy. The gate must be skipped there or the body would survive in a cleared room.
    FGateRig rig(110, PhasesOf({{50, true}, {0, false}}), 3, game::EDungeonPhase::Reward);
    rig.SetBodyHp(55);
    CHECK(!game::BossGateFloor(rig.registry, rig.body).has_value());
    CHECK(game::BossBodyDamageCap(rig.registry, rig.body, 77) == 77);
    CHECK(game::ApplyDamageToUnit(rig.registry, rig.body, 9999));
    CHECK(rig.registry.all_of<game::FDead>(rig.body));
    CHECK(rig.BodyHp() == 0);
}

void GateLiftsAfterPartsDie() {
    FGateRig rig(110, PhasesOf({{50, true}, {0, false}}), 2);
    rig.SetBodyHp(55);
    CHECK(game::BossGateFloor(rig.registry, rig.body).has_value());
    rig.KillParts();
    CHECK(!game::BossGateFloor(rig.registry, rig.body).has_value());
    CHECK(game::BossBodyDamageCap(rig.registry, rig.body, 9999) == 9999);
    CHECK(game::ApplyDamageToUnit(rig.registry, rig.body, 9999));
    CHECK(rig.registry.all_of<game::FDead>(rig.body));
    CHECK(game::BossActivePhase(rig.registry, rig.body) == 1);
}

void GateBlockedLogOnce() {
    FGateRig rig(110, PhasesOf({{50, true}, {0, false}}), 2);
    for (int hit = 0; hit < 3; ++hit) (void)game::ApplyDamageToUnit(rig.registry, rig.body, 9999);
    CHECK(rig.BodyHp() == 55);
    CHECK(CountInLog(rig.registry, "is shielded") == 1);  // one-per-battle, not one-per-hit
    // After the parts die the gate is gone and no further "shielded" line is written.
    rig.KillParts();
    (void)game::ApplyDamageToUnit(rig.registry, rig.body, 1);
    CHECK(CountInLog(rig.registry, "is shielded") == 1);
}

void NoFrameOrderDependence() {
    // The cap derives the live phase from HP + parts; the cached activePhase is never read by rules.
    FGateRig evaluated(110, PhasesOf({{50, true}, {0, false}}), 2);
    game::ecs::EvaluateBossGoals(evaluated.registry);  // cache = 0

    FGateRig never(110, PhasesOf({{50, true}, {0, false}}), 2);  // cache stays -1
    FGateRig stale(110, PhasesOf({{50, true}, {0, false}}), 2);
    stale.Goals().activePhase = 1;  // a wrong/stale cache

    for (FGateRig* rig : {&evaluated, &never, &stale}) {
        (void)game::ApplyDamageToUnit(rig->registry, rig->body, 9999);
        CHECK(rig->BodyHp() == 55);
        CHECK(game::BossActivePhase(rig->registry, rig->body) == 0);
    }
}

// ------------------------------------------------------------------------------------------------
// the BossDefeated producer (WP-20 cross contract) and the fallback regression
// ------------------------------------------------------------------------------------------------

void SinglePhaseBossUnchanged() {
    // No bosses.json at all: the fallback boss is a single-phase unit with no goals and no gate.
    FBattle battle("boss-goals-single", nullptr, "mid");
    CHECK(battle.BossBody() == entt::null);
    CHECK(battle.registry.view<game::FBossPart>().size() == 0);

    const auto& generation = battle.generation();
    const auto boss = battle.EnemyNamed(generation.at("boss_name").get<std::string>());
    CHECK(boss != entt::null);
    CHECK(!battle.registry.all_of<game::FBossGoals>(boss));
    CHECK(game::ApplyDamageToUnit(battle.registry, boss, 9999));  // no cap: a lethal hit kills outright
    CHECK(battle.registry.all_of<game::FDead>(boss));

    battle.KillAll();
    battle.RunCos(1);
    const auto& outcome = battle.registry.ctx().get<game::FBattleOutcome>();
    CHECK(outcome.resolved);
    CHECK(outcome.victory);
}

void FallbackBossStillAppends() {
    FBattle battle("boss-goals-fallback-append", nullptr, "mid");
    const auto fightId = battle.generation().at("boss_unit").get<std::string>();
    CHECK(battle.bossId == fightId);

    battle.KillAll();
    battle.RunCos(3);
    const auto events = RunEvents(battle.registry, game::ERunEventKind::BossDefeated);
    CHECK(events.size() == 1);
    CHECK(events.front().id == fightId);  // an unmapped slot still reports generation.boss_unit
}

void ForestBattleNoAppend() {
    // A forest-like registry (no FDungeonRun) must not append: AppendRunEvent would create a run log.
    entt::registry registry;
    registry.ctx().emplace<game::FTurnRuntime>();
    registry.ctx().emplace<game::FBattleOutcome>();
    registry.ctx().emplace<game::FBossBattle>(game::FBossBattle{"enemy.guardian", "mid"});

    const auto hero = registry.create();
    registry.emplace<game::FPartyMember>(hero);
    registry.emplace<game::FHealth>(hero, game::FHealth{10, 10});
    const auto enemy = registry.create();
    registry.emplace<game::FEnemy>(enemy);
    registry.emplace<game::FHealth>(enemy, game::FHealth{5, 5});
    CHECK(game::ApplyDamageToUnit(registry, enemy, 99));

    game::ecs::CombatOutcomeSystem outcomes;
    outcomes.update(registry, 0.f);
    const auto& outcome = registry.ctx().get<game::FBattleOutcome>();
    CHECK(outcome.resolved);
    CHECK(outcome.victory);
    CHECK(registry.ctx().find<game::FRunEventLog>() == nullptr);
    CHECK(RunEvents(registry, game::ERunEventKind::BossDefeated).empty());
}

void ContinuedRoomNoReAppend() {
    // continuedAfterRoom sets resolved itself, so COS early-returns and the boss event never re-fires.
    entt::registry registry;
    registry.ctx().emplace<game::FTurnRuntime>();
    registry.ctx().emplace<game::FDungeonRun>().phase = game::EDungeonPhase::Reward;
    registry.ctx().emplace<game::FBossBattle>(game::FBossBattle{"boss.test_guardian", "mid"});
    auto& outcome = registry.ctx().emplace<game::FBattleOutcome>();
    outcome.resolved = true;
    outcome.victory = true;

    const auto hero = registry.create();
    registry.emplace<game::FPartyMember>(hero);
    registry.emplace<game::FHealth>(hero, game::FHealth{10, 10});
    const auto enemy = registry.create();
    registry.emplace<game::FEnemy>(enemy);
    registry.emplace<game::FHealth>(enemy, game::FHealth{5, 5});
    CHECK(game::ApplyDamageToUnit(registry, enemy, 99));

    game::ecs::CombatOutcomeSystem outcomes;
    for (int frame = 0; frame < 5; ++frame) outcomes.update(registry, 0.f);
    CHECK(RunEvents(registry, game::ERunEventKind::BossDefeated).empty());
}

void BossDefeatedAppendedOnce() {
    const auto themes = ThemeIds();
    const auto catalog = BossCatalog(themes.front(), "mid", TestGuardianBoss());

    FBattle boss("boss-goals-append-once", &catalog, "mid");
    CHECK(boss.bossId == "boss.test_guardian");
    boss.KillAll();
    boss.RunCos(10);  // many frames: the resolved guard makes the append exactly once
    const auto events = RunEvents(boss.registry, game::ERunEventKind::BossDefeated);
    CHECK(events.size() == 1);
    CHECK(events.front().id == boss.bossId);
    CHECK(boss.registry.ctx().get<game::FBattleOutcome>().victory);

    // The same catalog on a NON-boss battle appends nothing (the marker decides, not the catalog).
    FBattle plain("boss-goals-append-plain", &catalog, "");
    CHECK(!plain.registry.ctx().contains<game::FBossBattle>());
    plain.KillAll();
    plain.RunCos(3);
    CHECK(plain.registry.ctx().get<game::FBattleOutcome>().victory);
    CHECK(RunEvents(plain.registry, game::ERunEventKind::BossDefeated).empty());
}

void MidBossAppendsBeforeCamp() {
    const auto themes = ThemeIds();
    const auto catalog = BossCatalog(themes.front(), "mid", TestGuardianBoss());
    FBattle battle("boss-goals-mid-camp", &catalog, "mid");
    const int bossDepth = battle.run().depth;
    const int bossRoom = battle.run().room;
    CHECK(battle.run().phase == game::EDungeonPhase::Battle);

    battle.KillAll();
    battle.RunCos(5);
    auto events = RunEvents(battle.registry, game::ERunEventKind::BossDefeated);
    CHECK(events.size() == 1);
    // COS appends at the resolving frame — the run still holds the boss battle's depth/room.
    CHECK(events.front().depth == bossDepth);
    CHECK(events.front().room == bossRoom);

    (void)MustResolveVictory(battle.registry, true);
    CHECK(battle.run().phase == game::EDungeonPhase::Reward);

    SettleRuneOffer(battle.registry);
    (void)MustApplyIntent(battle.registry, game::ERunIntent::ContinueReward);
    CHECK(battle.run().phase == game::EDungeonPhase::Camp);  // the mid boss closes its dungeon
    CHECK(battle.run().cleared == bossDepth);

    // The event is unchanged by the later transition (it was born from the battle, not the camp).
    events = RunEvents(battle.registry, game::ERunEventKind::BossDefeated);
    CHECK(events.size() == 1);
    CHECK(events.front().depth == bossDepth && events.front().room == bossRoom);
}

void FinalBossDoesNotEndRun() {
    const auto themes = ThemeIds();
    const auto catalog = BossCatalog(themes.front(), "final", TestGuardianBoss());
    FBattle battle("boss-goals-final", &catalog, "final");
    CHECK(battle.bossId == "boss.test_guardian");
    const int bossDepth = battle.run().depth;
    CHECK(battle.run().phase == game::EDungeonPhase::Battle);

    battle.KillAll();
    battle.RunCos(3);
    CHECK(RunEvents(battle.registry, game::ERunEventKind::BossDefeated).size() == 1);
    CHECK(RunEvents(battle.registry, game::ERunEventKind::RunEnded).empty());

    // The normal room-victory handoff: D-19 end_on_final_boss is false, so the run continues.
    (void)MustResolveVictory(battle.registry, true);
    CHECK(battle.run().phase == game::EDungeonPhase::Reward);
    CHECK(battle.run().phase != game::EDungeonPhase::Defeat);

    SettleRuneOffer(battle.registry);
    (void)MustApplyIntent(battle.registry, game::ERunIntent::ContinueReward);
    CHECK(battle.run().phase == game::EDungeonPhase::Camp);  // the theme's last dungeon cleared
    CHECK(battle.run().cleared == bossDepth);
    CHECK(RunEvents(battle.registry, game::ERunEventKind::RunEnded).empty());
    CHECK(RunEvents(battle.registry, game::ERunEventKind::BossDefeated).size() == 1);
}

// ------------------------------------------------------------------------------------------------
// spawn
// ------------------------------------------------------------------------------------------------

void BossPartsSpawnInDocument() {
    const auto document = FixtureDocument();
    const auto cells = PartyCellsFromDocument(document);
    const std::string partUnit = "enemy.imp";
    const std::string partName = "Test Part";
    constexpr int kPartCount = 3;

    auto config = ConfigFromDocument(document);
    config.bossParts = {game::gameplay::FBossPartSpawn{partUnit, partName, kPartCount}};
    auto plain = config;
    plain.bossParts.clear();

    const int size = game::gameplay::FGeneratedRoom::kSize;
    for (const char* family : {"open", "open_variant", "corridor", "cover", "split"}) {
        config.family = family;
        plain.family = family;
        for (std::uint32_t seed = 1; seed <= 8; ++seed) {
            const game::gameplay::FRoomRequest request{seed, 3, 2, true, cells};
            const auto room = game::gameplay::GenerateRoom(config, request);
            const auto before = game::gameplay::GenerateRoom(plain, request);

            // Parts consume NO random draws: the classic room is byte-identical (golden safe).
            CHECK(room.terrain == before.terrain);
            CHECK(room.enemies.size() == before.enemies.size() + static_cast<std::size_t>(kPartCount));
            CHECK(!before.enemies.empty());
            CHECK(before.enemies.front().unit == config.bossUnit);
            for (std::size_t index = 0; index < before.enemies.size(); ++index) {
                CHECK(room.enemies[index].unit == before.enemies[index].unit);
                CHECK(room.enemies[index].displayName == before.enemies[index].displayName);
                CHECK(room.enemies[index].x == before.enemies[index].x);
                CHECK(room.enemies[index].y == before.enemies[index].y);
            }

            // The parts come last, inside the spawn band, on plain floor, boss-nearest first.
            const auto boss = room.enemies.front();
            std::vector<std::pair<int, int>> seen;
            std::tuple<int, int, int> previous{-1, -1, -1};
            for (std::size_t index = before.enemies.size(); index < room.enemies.size(); ++index) {
                const auto& part = room.enemies[index];
                CHECK(part.unit == partUnit);
                CHECK(part.displayName == partName);
                CHECK(part.y >= game::gameplay::kRoomEnemyFirstRow);
                CHECK(part.y <= game::gameplay::kRoomEnemyLastRow);
                CHECK(part.x >= 1 && part.x <= size - 2);
                CHECK(room.terrain[static_cast<std::size_t>(part.y)][static_cast<std::size_t>(part.x)] == '.');
                CHECK(std::ranges::find(seen, std::pair{part.x, part.y}) == seen.end());
                seen.emplace_back(part.x, part.y);
                const std::tuple<int, int, int> key{
                    std::abs(part.x - boss.x) + std::abs(part.y - boss.y), part.y, part.x};
                CHECK(previous < key);
                previous = key;
            }
            CHECK(AllUnitsReachable(room, cells));
        }
    }

    // The battle side: the catalog mapped to the first theme's mid slot is attached by SetupCombatUnits.
    const auto catalog = BossCatalog(ThemeIds().front(), "mid", TestGuardianBoss());
    FBattle battle("boss-goals-parts", &catalog, "mid");
    const auto* marker = battle.registry.ctx().find<game::FBossBattle>();
    CHECK(marker != nullptr);
    CHECK(marker->bossId == "boss.test_guardian");
    CHECK(marker->slot == "mid");

    const auto body = battle.BossBody();
    CHECK(body != entt::null);
    CHECK(battle.registry.get<game::FUnitData>(body).definition.displayName == "Test Guardian");
    const auto parts = battle.Parts();
    CHECK(parts.size() == static_cast<std::size_t>(kPartCount));
    for (const auto part : parts) {
        CHECK(part != body);
        CHECK(battle.registry.get<game::FBossPart>(part).body == body);
        CHECK(battle.registry.get<game::FUnitData>(part).definition.displayName == partName);
    }

    // The generator appends parts after the escorts: the document's last three enemies are the parts.
    const auto partySize = static_cast<std::size_t>(game::DungeonDefinition(battle.registry).at("party").size());
    const auto& units = battle.document.at("units");
    CHECK(units.size() == partySize + 3 + static_cast<std::size_t>(kPartCount));  // boss room: boss + 2 escorts
    for (std::size_t index = units.size() - static_cast<std::size_t>(kPartCount); index < units.size(); ++index) {
        CHECK(units[index].at("unit").get<std::string>() == partUnit);
        CHECK(units[index].at("display_name").get<std::string>() == partName);
    }
}

void GoalCompletionDeterministic() {
    // Same seed twice: identical room (part cells included) and an identical BossDefeated sequence.
    const auto document = FixtureDocument();
    const auto cells = PartyCellsFromDocument(document);
    auto config = ConfigFromDocument(document);
    config.bossParts = {game::gameplay::FBossPartSpawn{"enemy.imp", "Test Part", 3}};
    const game::gameplay::FRoomRequest request{77, 3, 2, true, cells};
    const auto first = game::gameplay::GenerateRoom(config, request);
    const auto second = game::gameplay::GenerateRoom(config, request);
    CHECK(first.terrain == second.terrain);
    CHECK(first.enemies.size() == second.enemies.size());
    for (std::size_t index = 0; index < first.enemies.size(); ++index) {
        CHECK(first.enemies[index].unit == second.enemies[index].unit);
        CHECK(first.enemies[index].x == second.enemies[index].x);
        CHECK(first.enemies[index].y == second.enemies[index].y);
    }
    CHECK(first.enemies.size() == 3 + 3);  // boss + 2 escorts + 3 parts

    const auto catalog = BossCatalog(ThemeIds().front(), "mid", TestGuardianBoss());
    FBattle left("boss-goals-determinism-a", &catalog, "mid", 77);
    FBattle right("boss-goals-determinism-b", &catalog, "mid", 77);
    left.KillAll();
    right.KillAll();
    left.RunCos(3);
    right.RunCos(3);
    const auto leftEvents = EventSequence(left.registry);
    const auto rightEvents = EventSequence(right.registry);
    CHECK(leftEvents == rightEvents);
    CHECK(leftEvents.size() == 1);
    CHECK(leftEvents.front().first == static_cast<int>(game::ERunEventKind::BossDefeated));
    CHECK(leftEvents.front().second == "boss.test_guardian");
}

}  // namespace

int main() {
    // Per-case progress lines flush before a crash/abort (an entt assertion kills the process, so the
    // buffered runner output would otherwise be lost and the failing case unknown).
    std::cout.setf(std::ios::unitbuf);
    // Save safety: outside build-test.ps1 no GAME01P_SAVE_DIR is exported, so a bare run must never touch
    // the user's real save (a defeat deletes it). Commit an override before any dungeon service hits disk.
    if (const char* existing = std::getenv("GAME01P_SAVE_DIR"); existing == nullptr || *existing == '\0') {
        const std::filesystem::path overrideDir =
            std::filesystem::current_path() / "out" / "test-saves" / "boss_goals_tests";
        std::error_code error;
        std::filesystem::create_directories(overrideDir, error);
        const std::string value = overrideDir.string();
#ifdef _WIN32
        _putenv_s("GAME01P_SAVE_DIR", value.c_str());
#else
        setenv("GAME01P_SAVE_DIR", value.c_str(), 1);
#endif
    }
    return game_test::RunTests("boss goals", {
        {"BossMapResolvesByThemeSlot", BossMapResolvesByThemeSlot},
        {"UnmappedBossFallsBack", UnmappedBossFallsBack},
        {"BadBossRefFailsBoot", BadBossRefFailsBoot},
        {"BadGateRejectedAtBoot", BadGateRejectedAtBoot},
        {"SecondGateRejectedAtBoot", SecondGateRejectedAtBoot},
        {"GateFloorIsMaxGatedThreshold", GateFloorIsMaxGatedThreshold},
        {"FloorRoundsUp", FloorRoundsUp},
        {"PhaseHeldWhilePartsLive", PhaseHeldWhilePartsLive},
        {"TransitionOnLastPartDeath", TransitionOnLastPartDeath},
        {"TransitionLoggedOnce", TransitionLoggedOnce},
        {"PartsGateCapsBodyAtThreshold", PartsGateCapsBodyAtThreshold},
        {"GatedBodyPreviewZeroDamage", GatedBodyPreviewZeroDamage},
        {"GatedHitLogsZero", GatedHitLogsZero},
        {"ContinuedBossRoomShowsAllDefeated", ContinuedBossRoomShowsAllDefeated},
        {"GateLiftsAfterPartsDie", GateLiftsAfterPartsDie},
        {"GateBlockedLogOnce", GateBlockedLogOnce},
        {"NoFrameOrderDependence", NoFrameOrderDependence},
        {"SinglePhaseBossUnchanged", SinglePhaseBossUnchanged},
        {"FallbackBossStillAppends", FallbackBossStillAppends},
        {"ForestBattleNoAppend", ForestBattleNoAppend},
        {"ContinuedRoomNoReAppend", ContinuedRoomNoReAppend},
        {"BossDefeatedAppendedOnce", BossDefeatedAppendedOnce},
        {"MidBossAppendsBeforeCamp", MidBossAppendsBeforeCamp},
        {"FinalBossDoesNotEndRun", FinalBossDoesNotEndRun},
        {"BossPartsSpawnInDocument", BossPartsSpawnInDocument},
        {"GoalCompletionDeterministic", GoalCompletionDeterministic},
    });
}
