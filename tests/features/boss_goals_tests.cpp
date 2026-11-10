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
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>
#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/spdlog.h>

#include "core/AssetManager.h"
#include "core/Logger.h"
#include "core/events/FEventBus.h"
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
#include "ecs/systems/TurnFlowSystem.h"
#include "ecs/systems/TurnSystem.h"
#include "gameplay/ai/FAiBlackboard.h"
#include "gameplay/ai/FAiBrain.h"
#include "gameplay/ai/FAiLeaves.h"
#include "gameplay/ai/FAiQueries.h"
#include "gameplay/ai/FAiTuning.h"
#include "gameplay/ai/FAiWorld.h"
#include "gameplay/battle/FBattleSetup.h"
#include "gameplay/combat/FCombatApply.h"
#include "gameplay/combat/FCombatSheet.h"
#include "gameplay/combat/FCombatTypes.h"
#include "gameplay/combat/FEffectHooks.h"
#include "gameplay/combat/FIntent.h"
#include "gameplay/combat/FSkillResolver.h"
#include "gameplay/combat/FStatusRules.h"
#include "gameplay/data/FContentRegistry.h"
#include "gameplay/data/FEffectContent.h"
#include "gameplay/data/FStatusContent.h"
#include "gameplay/dungeon/FBossContent.h"
#include "gameplay/dungeon/FDungeonRun.h"
#include "gameplay/dungeon/FRewards.h"
#include "gameplay/dungeon/FRoomGenerator.h"
#include "gameplay/dungeon/FRunEventLog.h"
#include "gameplay/dungeon/FRunFlow.h"
#include "gameplay/dungeon/FRunLog.h"
#include "gameplay/dungeon/FRunSequence.h"
#include "gameplay/factory/FEntityDefinitions.h"
#include "gameplay/factory/FEntityFactory.h"
#include "gameplay/factory/FScene.h"
#include "gameplay/grid/FGridTerrain.h"
#include "gameplay/grid/FGridTypes.h"
#include "states/FEmberBattleState.h"

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

json PhaseRow(int hpAbove, bool partsGate, std::vector<std::string> bodyImmunities = {}) {
    json phase;
    phase["hp_above"] = hpAbove;
    if (partsGate) phase["gate"] = "parts";
    if (!bodyImmunities.empty()) {
        phase["body_immunities"] = json::array();
        for (auto& statusId : bodyImmunities) phase["body_immunities"].push_back(std::move(statusId));
    }
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

/// The canonical workhorse: body `enemy.guardian`, one gated phase at 50%, three `enemy.imp` parts. Phase 1
/// ignores the fixture's three non-damage statuses (the WP-15b immune set the phase writes at setup and on
/// every transition); phase 2 (`hp_above` 0) ignores nothing.
json TestGuardianBoss() {
    return BossDefinition("boss.test_guardian", "Test Guardian", "enemy.guardian",
                          {PhaseRow(50, true, {"status.debuff", "status.freeze", "status.paralyze"}),
                           PhaseRow(0, false)},
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
        // bosses.json is part of the frozen fixture too: the fixture root is first in every boot, and the
        // shipped catalog names a part unit the fixture's units.json does not carry (an empty fixture
        // catalog keeps those boots on the single-phase fallback instead of failing validation).
        // encounters.json (WP-14a), like bosses.json: else the shipped encounters.json falls through and is
        // validated against the frozen fixture units (the six new enemies are absent there).
        for (const char* file : {"units.json", "skills.json", "runes.json", "statuses.json", "ai.json",
                                 "rewards.json", "passive-tree.json", "dungeon.json", "bosses.json",
                                 "encounters.json"}) {
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

/// Phases with a per-phase body-immunity row (WP-15b): {hp_above, parts gate, status ids the body ignores}.
std::vector<game::FBossPhase> PhasesWithImmunities(
    std::vector<std::tuple<int, bool, std::vector<std::string>>> rows) {
    std::vector<game::FBossPhase> phases;
    for (auto& [hpAbove, partsGate, immunities] : rows) {
        game::FBossPhase phase;
        phase.hpAbove = hpAbove;
        phase.partsGate = partsGate;
        phase.bodyImmunities = std::move(immunities);
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
        registry.emplace<game::FActiveStatuses>(body);
        registry.emplace<game::FTurnParticipant>(body);
        game::FBossGoals goals;
        goals.bossId = "boss.test_guardian";
        goals.name = "Test Guardian";
        goals.phases = std::move(phases);
        registry.emplace<game::FBossGoals>(body, std::move(goals));
        for (int index = 0; index < partCount; ++index) {
            const auto part = registry.create();
            registry.emplace<game::FHealth>(part, game::FHealth{10, 10});
            registry.emplace<game::FActiveStatuses>(part);
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

/// Captures spdlog output while alive — WP-15b's immunity choke point reports through LOG_INFO (the battle
/// feed has no line for it), so the block line is only observable here.
struct FLogCapture {
    std::ostringstream stream;
    std::shared_ptr<spdlog::logger> logger;

    FLogCapture() {
        logger = std::make_shared<spdlog::logger>(
            "boss-goals-test-capture", std::make_shared<spdlog::sinks::ostream_sink_mt>(stream));
        logger->set_level(spdlog::level::trace);
        SetCurrentGameLogger(logger);
    }

    ~FLogCapture() { SetCurrentGameLogger(nullptr); }

    FLogCapture(const FLogCapture&) = delete;
    FLogCapture& operator=(const FLogCapture&) = delete;

    [[nodiscard]] std::string Text() const { return stream.str(); }
};

std::vector<std::string> SortedCopy(std::vector<std::string> values) {
    std::ranges::sort(values);
    return values;
}

/// The unit's WP-15 immune set (sorted, so a JSON row order cannot move an assertion).
std::vector<std::string> ImmuneSet(const entt::registry& registry, entt::entity unit) {
    const auto* active = registry.try_get<game::FActiveStatuses>(unit);
    return active != nullptr ? SortedCopy(active->immune) : std::vector<std::string>{};
}

bool HoldsStatus(entt::registry& registry, entt::entity unit, std::string_view statusId) {
    const auto* active = registry.try_get<game::FActiveStatuses>(unit);
    if (active == nullptr) return false;
    return std::ranges::any_of(active->effects, [statusId](const game::FStatusEffect& effect) {
        return effect.statusId == statusId;
    });
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

    /// A heart linked to the body, placed where the test needs it (the AI-valuation scenario).
    entt::entity AddPart(game::FGridPosition cell, int maxHealth = 10) {
        const auto part = Spawn(cell, false, game::FStats{}, maxHealth, 1, "Test Part");
        registry.emplace<game::FBossPart>(part, game::FBossPart{body});
        parts.push_back(part);
        return part;
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

/// Runs one skill through the real action-queue path (the resolver applies the preview as-is).
void CastSkill(entt::registry& registry, entt::entity actor, entt::entity target, const std::string& skillId) {
    game::FActionRequest request;
    request.actor = actor;
    request.target = target;
    request.skillId = skillId;
    request.cell = registry.get<game::FGridPosition>(target);
    request.token = StartTurnFor(registry, actor);
    auto& points = registry.get<game::FActionPoints>(actor);
    points.skill = points.skillMax;
    registry.ctx().get<game::FActionQueue>().pending.push_back(request);
    game::ecs::ActionResolverSystem resolver;
    resolver.update(registry, 0.f);
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

// --- WP-15b: AI / intent / autoplay fixtures -----------------------------------------------------

/// The AI's best option for `actor` under the default filter (value per AP, AP required). The world is
/// rebuilt per call, so a mutation between two questions is visible.
struct FAiProbe {
    entt::registry& registry;
    const game::gameplay::FContentRegistry& content;
    game::ai::FAiTuning tuning;
    entt::entity actor{entt::null};

    [[nodiscard]] std::optional<game::ai::FSkillOption> Best() {
        const game::ai::FAiWorld world = game::ai::BuildAiWorld(registry, actor, content);
        game::ai::FAiBlackboard board;
        game::ai::FBtMemory memory;
        const game::ai::FSkillFilter filter;
        const game::ai::FBtContext ctx{registry, content, tuning, world, board, memory, actor,
                                       game::FTurnToken{1, 1, 1, actor}, false};
        return game::ai::FindBestOption(ctx, registry.get<game::FGridPosition>(actor), filter, true);
    }

    /// The phase condition the ai.json fragment guards on, asked of the same actor (WP-15b's leaf).
    bool PhaseAtLeast(int phase) {
        const game::ai::FAiWorld world = game::ai::BuildAiWorld(registry, actor, content);
        game::ai::FAiBlackboard board;
        game::ai::FBtMemory memory;
        const game::ai::FBtContext ctx{registry, content, tuning, world, board, memory, actor,
                                       game::FTurnToken{1, 1, 1, actor}, true};
        return game::ai::SelfBossPhaseAtLeast(ctx, phase);
    }
};

void CollectBtNodes(const json& node, std::vector<const json*>& out) {
    if (!node.is_object()) return;
    if (node.contains("type")) out.push_back(&node);
    if (const auto children = node.find("children"); children != node.end() && children->is_array()) {
        for (const auto& child : *children) CollectBtNodes(child, out);
    }
}

/// The UseSkill node of the ai.json tree that carries a `BossPhaseAtLeast` condition (the content-authored
/// phase rule — a fragment, or the profile tree itself), or nullptr when the data does not author one.
const json* PhaseFragmentUseSkill(const json& ai) {
    const auto scan = [](const json& node) -> const json* {
        std::vector<const json*> nodes;
        CollectBtNodes(node, nodes);
        const bool gatedPhase = std::ranges::any_of(nodes, [](const json* found) {
            return found->at("type").get<std::string>() == "BossPhaseAtLeast";
        });
        if (!gatedPhase) return nullptr;
        for (const json* found : nodes) {
            if (found->at("type").get<std::string>() == "UseSkill") return found;
        }
        return nullptr;
    };
    for (const auto& fragment : ai.at("fragments")) {
        if (const json* useSkill = scan(fragment.at("node"))) return useSkill;
    }
    for (const auto& profile : ai.at("profiles")) {
        if (const auto tree = profile.find("tree"); tree != profile.end()) {
            if (const json* useSkill = scan(*tree)) return useSkill;
        }
    }
    return nullptr;
}

/// The skill the fragment's UseSkill filter resolves to against the actor's own loadout (explicit `skills`
/// first, then `tags`) — derived from the data, never a hard-coded id.
std::string ExpectedPhaseSkill(const json& useSkill, const std::vector<std::string>& loadout,
                               const game::gameplay::FContentRegistry& content) {
    if (const auto skills = useSkill.find("skills"); skills != useSkill.end()) {
        for (const auto& id : *skills) {
            const std::string skillId = id.get<std::string>();
            if (std::ranges::find(loadout, skillId) != loadout.end()) return skillId;
        }
    }
    if (const auto tags = useSkill.find("tags"); tags != useSkill.end()) {
        std::vector<std::string> wanted;
        for (const auto& tag : *tags) wanted.push_back(tag.get<std::string>());
        for (const auto& skillId : loadout) {
            const auto* row = content.skills.find(skillId);
            if (row == nullptr) continue;
            for (const auto& tag : row->tags) {
                if (std::ranges::find(wanted, tag) != wanted.end()) return skillId;
            }
        }
    }
    return {};
}

/// The bosses.json map row for (theme, slot) — the data authority for a battle's expected boss id.
std::string MappedBossId(const json& document, std::string_view theme, std::string_view slot) {
    for (const auto& row : document.at("map")) {
        if (row.at("theme").get<std::string>() == std::string(theme) &&
            row.at("slot").get<std::string>() == std::string(slot)) {
            return row.at("boss").get<std::string>();
        }
    }
    return {};
}

/// The first floor cell within `range` of `target` that no unit stands on (a scenario input, like the HUD's
/// own placement of the party).
std::optional<game::FGridPosition> FreeCellNear(entt::registry& registry, game::FGridPosition target, int range) {
    std::vector<game::FGridPosition> occupied;
    for (const auto entity : registry.view<game::FGridPosition>()) {
        occupied.push_back(registry.get<game::FGridPosition>(entity));
    }
    const auto* terrain = registry.ctx().find<game::FGridTerrain>();
    for (int y = 0; y < game::kGridHeight; ++y) {
        for (int x = 0; x < game::kGridWidth; ++x) {
            const game::FGridPosition cell{x, y};
            if (std::abs(cell.x - target.x) + std::abs(cell.y - target.y) > range) continue;
            if (std::ranges::find(occupied, cell) != occupied.end()) continue;
            if (terrain != nullptr && terrain->at(x, y) != game::ETile::Floor) continue;
            return cell;
        }
    }
    return std::nullopt;
}

/// The shipped seed played for real: content, sequence and bosses.json from `assets/`, the first mid-boss
/// slot fielded directly (run depth/room = the sequence's slot) and the whole turn pipeline registered, so
/// the AI and autoplay tests run the game's own systems headless.
struct FBossRun {
    std::string checkpoint;
    entt::registry registry;
    std::string runSave;
    game::ecs::TurnSystem turns;
    game::ecs::TurnFlowSystem flow;
    game::ecs::ActionResolverSystem actions;
    game::ecs::CombatOutcomeSystem outcomes;
    FEmberBattleState battle;
    int steps{0};

    explicit FBossRun(const char* stem, std::uint32_t seed) : checkpoint(UniquePath(stem)) {
        game::FAssetManager::Initialize(registry, ShipAssetRoots());
        CHECK(game::gameplay::FGameplayServices::Initialize(registry));
        CHECK(game::InitializeDungeon(registry, checkpoint));
        runSave = registry.ctx().get<game::FDungeonSave>().path;
        game::InitializeEventBus(registry);  // the composition root's job; OnEnter does not create one
        turns.onRegister(registry);
        game::StartRun(registry, seed, true);
        auto& run = registry.ctx().get<game::FDungeonRun>();
        const auto slot = BossSlotOf(registry.ctx().get<game::FRunSequence>(), "mid");
        run.depth = slot.depth;
        run.room = slot.room;
        battle.OnEnter(registry);
    }

    ~FBossRun() {
        battle.OnExit(registry);
        std::error_code error;
        std::filesystem::remove(checkpoint, error);
        std::filesystem::remove(runSave, error);
        std::filesystem::remove(runSave + ".tmp", error);
    }

    FBossRun(const FBossRun&) = delete;
    FBossRun& operator=(const FBossRun&) = delete;

    game::FDungeonRun& run() { return registry.ctx().get<game::FDungeonRun>(); }

    entt::entity Body() {
        for (const auto entity : registry.view<game::FBossGoals>()) return entity;
        return entt::null;
    }

    std::vector<entt::entity> Hearts() {
        std::vector<entt::entity> hearts;
        const auto body = Body();
        if (body == entt::null) return hearts;
        for (const auto [entity, part] : registry.view<const game::FBossPart>().each()) {
            if (part.body == body) hearts.push_back(entity);
        }
        return hearts;
    }

    void step() {
        // Test-time acceleration (dungeon_sequence_tests' recipe): the outcome is turn based, so a larger dt
        // only carries the presentation timers to the same decisions in fewer frames. The frame boundary is
        // the Engine's own job (FEventBus::beginFrame before any system): without it the frame events pile up
        // and AccumulateRoomStats counts the same FRoundStarted again every frame.
        if (auto* bus = registry.ctx().find<game::FEventBus>()) bus->beginFrame();
        turns.update(registry, .5f);
        flow.update(registry, .5f);
        actions.update(registry, .5f);
        outcomes.update(registry, .5f);
        ++steps;
    }

    /// Auto-plays the fielded room to a decision. False = lost (a stall counts as a failure too).
    bool playBattle(int maxSteps = 6000) {
        registry.ctx().get<game::FCombatRuntime>().autoPlay = true;
        while (!registry.ctx().get<game::FBattleOutcome>().resolved && steps < maxSteps) step();
        const auto& outcome = registry.ctx().get<game::FBattleOutcome>();
        return outcome.resolved && outcome.victory;
    }
};

/// One AI decision for `actor` (a fresh turn serial per call: the brain treats it as a new turn).
std::optional<game::FBattleAction> Decide(entt::registry& registry, entt::entity actor, std::uint64_t serial,
                                          bool hostilePartySide) {
    game::FTurnToken token;
    token.sessionId = 1;
    token.roundNumber = serial;
    token.turnSerial = serial;
    token.actor = actor;
    return game::ai::RunBrain(
        registry, token, hostilePartySide, registry.ctx().get<game::gameplay::FContentRegistry>());
}

/// The skill id of an AI decision ("" for a move or no decision).
std::string SkillOf(const std::optional<game::FBattleAction>& action) {
    if (!action) return {};
    if (const auto* request = std::get_if<game::FActionRequest>(&*action)) return request->skillId;
    return {};
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
    FGateRig rig(110, PhasesWithImmunities({{50, true, {"status.freeze"}}, {0, false, {}}}), 2);
    rig.SetBodyHp(55);  // exactly the floor
    CHECK(game::BossPartsAlive(rig.registry, rig.body) == 2);
    CHECK(game::BossActivePhase(rig.registry, rig.body) == 0);
    CHECK(*game::BossGateFloor(rig.registry, rig.body) == 55);
    // The gate entry does not fire a phase change: the phase only moves when the last part dies.
    game::ecs::EvaluateBossGoals(rig.registry);
    CHECK(rig.Goals().activePhase == 0);
    // ... and the defense stays the active phase's row while the parts hold the phase (WP-15b).
    CHECK(ImmuneSet(rig.registry, rig.body) == std::vector<std::string>{"status.freeze"});
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
    CHECK(CountInLog(rig.registry, "first!") == 1);
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

// WP-39: the crit preview follows the boss gate cap. FMiniBattle(110, 50, 2, 20): hero attack 20 + strike
// power 4 - body defense 5 = 19 base damage; the gate floor is 55, so the allowance is current - 55.
void GatedBodyCritFollowsCap() {
    FMiniBattle battle(110, 50, 2, 20);
    battle.registry.ctx().get<game::FCombatRuntime>().critChancePercent = 15;
    const auto resolved = game::ResolveSkillSlot(game::FSkillSlot{"skill.strike", {}}, battle.Content());
    CHECK(resolved.has_value());
    const auto bodyCell = battle.registry.get<game::FGridPosition>(battle.body);
    const auto actorCell = battle.registry.get<game::FGridPosition>(battle.actor);
    const int floor = game::BossPhaseFloor(110, 50);
    const auto previewAt = [&](int hp) {
        battle.SetBodyHp(hp);
        const auto preview = game::PreviewSkill(battle.registry, battle.actor, *resolved, bodyCell, actorCell);
        CHECK(preview.has_value() && preview->targets.size() == 1);
        return preview->targets.front();
    };
    const auto roomy = previewAt(110);            // allowance 55 >= 2 * 19
    CHECK(roomy.damage == 19);
    CHECK(roomy.critDamage == 38);
    const auto tight = previewAt(floor + 30);     // 19 < allowance 30 < 38
    CHECK(tight.damage == 19);
    CHECK(tight.critDamage == 30);
    const auto shut = previewAt(floor);           // allowance 0 <= 19
    CHECK(shut.damage == 0);
    CHECK(shut.critDamage == 0);
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
    CHECK(CountInLog(registry, "Shielded!") == 1);
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
    CHECK(CountInLog(rig.registry, "Shielded!") == 1);  // one-per-battle, not one-per-hit
    // After the parts die the gate is gone and no further "shielded" line is written.
    rig.KillParts();
    (void)game::ApplyDamageToUnit(rig.registry, rig.body, 1);
    CHECK(CountInLog(rig.registry, "Shielded!") == 1);
}

/// WP-15c (critic F1): the battle feed shows one fixed-width line, so every player-facing boss line built from
/// the SHIPPED bosses.json must fit kBossFeedLineBudget, lead with the action and name the part. Derived from
/// the data (no literal names), so a longer authored name fails here instead of truncating on screen.
void GateGuidanceFitsOneLine() {
    const auto document = ReadJsonFile("assets/data/bosses.json");
    CHECK(!document.at("bosses").empty());
    for (const auto& boss : document.at("bosses")) {
        game::FBossGoals goals;
        goals.name = boss.value("name", boss.at("id").get<std::string>());
        int partCount = 0;
        if (boss.contains("parts")) {
            for (const auto& part : boss.at("parts")) partCount += part.at("count").get<int>();
            if (!boss.at("parts").empty()) {
                const auto& first = boss.at("parts").front();
                goals.partName = first.value("name", first.at("unit").get<std::string>());
            }
        }
        for (const auto& phase : boss.at("phases")) {
            game::FBossPhase row;
            row.notes = phase.value("notes", std::string{});
            goals.phases.push_back(row);
        }
        for (int alive = 1; alive <= std::max(1, partCount); ++alive) {
            const auto entry = game::BossGateEntryLine(goals, alive);
            const auto blocked = game::BossGateBlockedLine(goals, alive);
            CHECK(entry.size() <= game::kBossFeedLineBudget);
            CHECK(blocked.size() <= game::kBossFeedLineBudget);
            CHECK(entry.rfind("Break ", 0) == 0);  // the action comes first
            CHECK(blocked.find("Break ") != std::string::npos);
            if (!goals.partName.empty()) {
                CHECK(entry.find(goals.partName) != std::string::npos);
                CHECK(blocked.find(goals.partName) != std::string::npos);
            }
        }
        for (int phase = 1; phase < static_cast<int>(goals.phases.size()); ++phase) {
            const auto line = game::BossPhaseLine(goals, phase);
            CHECK(line.size() <= game::kBossFeedLineBudget);
            const auto& notes = goals.phases[static_cast<std::size_t>(phase)].notes;
            if (!notes.empty()) CHECK(line.find(notes) != std::string::npos);  // F9: the notes are shown
        }
    }
    // Without notes the transition keeps the plain wording.
    game::FBossGoals plain;
    plain.name = "Boss";
    plain.phases.resize(2);
    CHECK(game::BossPhaseLine(plain, 1) == "Boss enters phase 2.");
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

void NoDungeonRunBattleNoAppend() {
    // A registry without FDungeonRun must not append: AppendRunEvent would create a run log.
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

// ------------------------------------------------------------------------------------------------
// WP-15b: immunities (the FStatusRules choke point)
// ------------------------------------------------------------------------------------------------

void ImmunityBlocksSkillPath() {
    FMiniBattle battle(110, 50, 0);
    auto& registry = battle.registry;
    const std::string statusId = "status.debuff";
    const auto* definition = battle.Content().statuses.find(statusId);
    CHECK(definition != nullptr);
    const auto resolved = game::ResolveSkillSlot(game::FSkillSlot{"skill.debuff", {}}, battle.Content());
    CHECK(resolved.has_value());
    registry.get<game::FSkillLoadout>(battle.actor).slots.push_back(game::FSkillSlot{"skill.debuff", {}});
    registry.get<game::FActiveStatuses>(battle.body).immune = {statusId};

    const auto aim = registry.get<game::FGridPosition>(battle.body);
    const auto origin = registry.get<game::FGridPosition>(battle.actor);

    // Preview = canon (WP-15b rules): an immune target's preview carries no status, so the resolver has none
    // to hand to the application point.
    auto preview = game::PreviewSkill(registry, battle.actor, *resolved, aim, origin);
    CHECK(preview.has_value());
    CHECK(preview->targets.size() == 1);
    CHECK(preview->targets.front().unit == battle.body);
    CHECK(preview->targets.front().statuses.empty());

    CastSkill(registry, battle.actor, battle.body, "skill.debuff");
    CHECK(!HoldsStatus(registry, battle.body, statusId));

    // The application point every path shares reports the block once (the resolver even reaching it does not
    // depend on the preview filter, so the line is pinned here).
    {
        FLogCapture capture;
        game::FBattleRng rng{1234u};
        CHECK(!game::TryApplyStatus(registry.get<game::FActiveStatuses>(battle.body), *definition, 0, 0, rng));
        CHECK(!HoldsStatus(registry, battle.body, statusId));
        CHECK(capture.Text().find("blocked (immune)") != std::string::npos);
    }

    // The same skill on a non-immune unit applies: the choke point is the immune set, not the skill.
    registry.get<game::FActiveStatuses>(battle.body).immune.clear();
    preview = game::PreviewSkill(registry, battle.actor, *resolved, aim, origin);
    CHECK(preview.has_value());
    CHECK(preview->targets.front().statuses.size() == 1);
    CastSkill(registry, battle.actor, battle.body, "skill.debuff");
    CHECK(HoldsStatus(registry, battle.body, statusId));
}

void ImmunityBlocksRuneHookPath() {
    // The rune-hook entry (FEffectHooks' apply_status_self) goes through ApplyStatus, which is the same
    // choke point — no call-site edit, so an immune body ignores a hook-granted status too.
    FBoot boot(UniquePath("boss-goals-hook-immune"), TestAssetRoots());
    CHECK(boot.content);
    auto& registry = boot.registry;
    const std::string statusId = "status.debuff";

    const auto field = [&](std::vector<std::string> immune) {
        const auto unit = registry.create();
        registry.emplace<game::FHealth>(unit, game::FHealth{20, 20});
        registry.emplace<game::FActiveStatuses>(unit).immune = std::move(immune);
        game::gameplay::FHook hook;
        hook.event = game::gameplay::EHookEvent::OnTurnStart;
        hook.eventName = "on_turn_start";
        hook.effect.kind = std::string(game::gameplay::kEffectKindApplyStatusSelf);
        hook.effect.statusId = statusId;
        hook.id = "hook.test_immune";
        game::FActiveHooks hooks;
        hooks.hooks.push_back(game::FActiveHook{std::move(hook), "skill.test"});
        registry.emplace<game::FActiveHooks>(unit, std::move(hooks));
        return unit;
    };

    const auto immuneUnit = field({statusId});
    const auto plainUnit = field({});
    {
        FLogCapture capture;
        CHECK(game::FireHooks(registry, game::gameplay::EHookEvent::OnTurnStart,
                              game::FHookContext{.owner = immuneUnit}) == 1);
        CHECK(!HoldsStatus(registry, immuneUnit, statusId));
        CHECK(capture.Text().find("blocked (immune)") != std::string::npos);
    }
    CHECK(game::FireHooks(registry, game::gameplay::EHookEvent::OnTurnStart,
                          game::FHookContext{.owner = plainUnit}) == 1);
    CHECK(HoldsStatus(registry, plainUnit, statusId));
}

void ImmunityBeforeRollNoRng() {
    const auto& content = FixtureContent();
    const auto* paralyze = content.statuses.find("status.paralyze");
    CHECK(paralyze != nullptr);
    CHECK(game::IsOnApplyControl(*paralyze));  // the rolling kind: immunity must skip the roll

    // Every seed: the roll exists (a clean unit would sometimes apply), yet the immune unit consumes nothing.
    for (std::uint32_t seed = 1; seed <= 64; ++seed) {
        game::FActiveStatuses active;
        active.immune = {paralyze->id};
        game::FBattleRng rng{seed};
        const std::uint32_t before = rng.state;
        CHECK(!game::TryApplyStatus(active, *paralyze, 0, 0, rng));
        CHECK(rng.state == before);
        CHECK(active.effects.empty());
    }

    // Same seed twice: an identical outcome and an identical RNG state (immunity is a pure no-op).
    const auto run = [paralyze](std::uint32_t seed, bool immune) {
        game::FActiveStatuses active;
        if (immune) active.immune = {paralyze->id};
        game::FBattleRng rng{seed};
        const bool applied = game::TryApplyStatus(active, *paralyze, 0, 0, rng);
        return std::pair<bool, std::uint32_t>{applied, rng.state};
    };
    CHECK(run(9876u, true) == run(9876u, true));
    CHECK(!run(9876u, true).first);
    CHECK(run(9876u, true).second == game::FBattleRng{9876u}.state);
    CHECK(run(9876u, false).second != game::FBattleRng{9876u}.state);  // the control rolls exactly once
}

void ImmuneSetFromBossPhase() {
    const auto catalog = BossCatalog(ThemeIds().front(), "mid", TestGuardianBoss());
    FBattle battle("boss-goals-immune-set", &catalog, "mid");
    auto& registry = battle.registry;
    const auto body = battle.BossBody();
    CHECK(body != entt::null);
    const auto& goals = registry.get<game::FBossGoals>(body);
    CHECK(!goals.phases.front().bodyImmunities.empty());

    // Setup writes the active phase's data row: the body ignores exactly those ids.
    CHECK(ImmuneSet(registry, body) == SortedCopy(goals.phases.front().bodyImmunities));
    const auto* active = registry.try_get<game::FActiveStatuses>(body);
    CHECK(active != nullptr);
    for (const auto& statusId : goals.phases.front().bodyImmunities) {
        CHECK(game::IsStatusImmune(*active, statusId));
    }

    // Hearts and escorts ignore nothing (the row is the body's).
    const auto parts = battle.Parts();
    CHECK(parts.size() == 3);
    for (const auto part : parts) CHECK(ImmuneSet(registry, part).empty());
    for (const auto enemy : battle.Enemies()) {
        if (enemy != body) CHECK(ImmuneSet(registry, enemy).empty());
    }
}

void ImmuneSetUpdatedOnTransition() {
    FGateRig rig(110, PhasesWithImmunities({{50, true, {"status.freeze"}}, {0, false, {}}}), 2);
    rig.SetBodyHp(55);
    game::ecs::EvaluateBossGoals(rig.registry);
    CHECK(rig.Goals().activePhase == 0);
    CHECK(ImmuneSet(rig.registry, rig.body) == std::vector<std::string>{"status.freeze"});

    // The last heart dies at the floor: the defense changes AT the transition (phase 2 ignores nothing).
    rig.KillParts();
    CHECK(game::BossActivePhase(rig.registry, rig.body) == 1);
    game::ecs::EvaluateBossGoals(rig.registry);
    CHECK(rig.Goals().activePhase == 1);
    CHECK(ImmuneSet(rig.registry, rig.body).empty());
}

// ------------------------------------------------------------------------------------------------
// WP-15b: the telegraph contract (defense now, action next round)
// ------------------------------------------------------------------------------------------------

void CommittedIntentSurvivesTransition() {
    const auto catalog = BossCatalog(ThemeIds().front(), "mid", TestGuardianBoss());
    FBattle battle("boss-goals-intent-transition", &catalog, "mid");
    auto& registry = battle.registry;
    const auto& content = registry.ctx().get<game::gameplay::FContentRegistry>();

    // One round commit through the event bus (the shape TurnSystem queues for FRoundStarted).
    game::InitializeEventBus(registry);
    auto& bus = registry.ctx().get<game::FEventBus>();
    bus.beginFrame();
    game::FTurnSnapshot round;
    round.state = game::ETurnState::Running;
    round.sessionId = 7;
    round.roundNumber = 1;
    bus.queueFrame<game::FRoundStarted>(game::FRoundStarted{round});
    game::CommitEnemyIntents(registry, content);

    const auto body = battle.BossBody();
    CHECK(body != entt::null);
    auto* store = registry.ctx().find<game::FIntentStore>();
    CHECK(store != nullptr);
    const auto committedEntry = store->intents.find(body);
    CHECK(committedEntry != store->intents.end());
    const game::FIntent committed = committedEntry->second;

    // Mid-round transition: the last heart dies at the body's floor.
    const auto floor = game::BossGateFloor(registry, body);
    CHECK(floor.has_value());
    registry.get<game::FHealth>(body).current = *floor;
    for (const auto part : battle.Parts()) (void)game::ApplyDamageToUnit(registry, part, 100000);
    CHECK(game::BossActivePhase(registry, body) == 1);
    game::ecs::EvaluateBossGoals(registry);
    CHECK(registry.get<game::FBossGoals>(body).activePhase == 1);

    // The declared intent is neither replaced nor re-aimed by the transition ...
    const auto after = store->intents.find(body);
    CHECK(after != store->intents.end());
    CHECK(after->second.skillId == committed.skillId);
    CHECK(after->second.aim.x == committed.aim.x && after->second.aim.y == committed.aim.y);
    CHECK(after->second.target == committed.target);
    CHECK(after->second.mode == committed.mode);
    CHECK(after->second.expectedAtCommit == committed.expectedAtCommit);
    // ... and still validates against the live world (only WP-11's own rules may invalidate it).
    const auto evaluation = game::EvaluateIntent(registry, body, content);
    CHECK(evaluation.verdict != game::EIntentVerdict::Invalidated);
}

void PhaseBehaviorTakesEffectNextRound() {
    FGateRig rig(110, PhasesWithImmunities({{50, true, {"status.freeze"}}, {0, false, {}}}), 2);
    auto& stats = rig.registry.ctx().emplace<game::FRoomBattleStats>();
    stats.rounds = 4;
    rig.SetBodyHp(55);
    game::ecs::EvaluateBossGoals(rig.registry);  // first evaluation: phase 0
    rig.KillParts();
    game::ecs::EvaluateBossGoals(rig.registry);  // the transition lands mid-round 4

    auto& goals = rig.Goals();
    CHECK(goals.activePhase == 1);
    CHECK(goals.phaseRound == 4);
    CHECK(goals.behaviorPhase == 0);  // action changes only from the next round's plan

    // More evaluations in the same round: still 0.
    game::ecs::EvaluateBossGoals(rig.registry);
    CHECK(rig.Goals().behaviorPhase == 0);

    // The counter moves past the transition round: the phase behavior is promoted.
    stats.rounds = 5;
    game::ecs::EvaluateBossGoals(rig.registry);
    CHECK(rig.Goals().behaviorPhase == 1);
    CHECK(rig.Goals().activePhase == 1);
}

void PhaseGuardSelectsSkill() {
    // The phase rule lives in the SHIPPED ai.json (the content task authors it); the expected skill is derived
    // from that document against the boss's own loadout — never a hard-coded id.
    const auto ai = ReadJsonFile("assets/data/ai.json");
    const json* useSkill = PhaseFragmentUseSkill(ai);
    CHECK(useSkill != nullptr);

    FBossRun run("boss-goals-phase-skill", 5u);
    auto& registry = run.registry;
    const auto body = run.Body();
    CHECK(body != entt::null);
    const auto& content = registry.ctx().get<game::gameplay::FContentRegistry>();

    std::vector<std::string> loadout;
    for (const auto& slot : registry.get<game::FSkillLoadout>(body).slots) loadout.push_back(slot.skillId);
    const std::string expected = ExpectedPhaseSkill(*useSkill, loadout, content);
    if (expected.empty()) {
        throw std::runtime_error(
            "ai.json's BossPhaseAtLeast tree has no `skills`/`tags` filter this test can derive a skill from; "
            "author the phase UseSkill with one of them");
    }

    // Clear the field down to the body vs. one adjacent hero, so the decision is a skill (not an approach).
    // Collect first: ApplyDamageToUnit mutates the registry, and a view must not be iterated across that.
    std::vector<entt::entity> others;
    for (const auto entity : registry.view<game::FHealth>()) {
        if (entity == body || registry.all_of<game::FPartyMember>(entity)) continue;
        others.push_back(entity);
    }
    for (const auto entity : others) (void)game::ApplyDamageToUnit(registry, entity, 100000);
    entt::entity hero = entt::null;
    for (const auto entity : registry.view<game::FPartyMember, game::FGridPosition>()) {
        hero = entity;
        break;
    }
    CHECK(hero != entt::null);
    const auto free = FreeCellNear(registry, registry.get<game::FGridPosition>(body), 1);
    CHECK(free.has_value());
    registry.get<game::FGridPosition>(hero) = *free;

    auto& goals = registry.get<game::FBossGoals>(body);
    registry.get<game::FActiveStatuses>(body).effects.clear();
    auto& points = registry.get<game::FActionPoints>(body);
    // Rooted for this decision: the question is which SKILL the phase fragment picks, not where the boss
    // walks — see the fragment's Succeeder[Engage] (it declines and the sequence falls through to UseSkill).
    points.move = 0;
    points.skill = points.skillMax;

    // The leaf the fragment guards on: false in phase 1's behavior, true once promoted.
    FAiProbe leaf{registry, content, {}, body};
    goals.behaviorPhase = 0;
    CHECK(!leaf.PhaseAtLeast(2));
    const auto phaseOneDecision = Decide(registry, body, 1, true);

    goals.behaviorPhase = 1;
    CHECK(leaf.PhaseAtLeast(2));
    const auto phaseTwoDecision = Decide(registry, body, 2, true);

    // The phase fragment (first in the selector) forces its skill once promoted ...
    CHECK(SkillOf(phaseTwoDecision) == expected);
    // ... and is not taken before that (the normal fight plans something else — if the two ever collide the
    // fragment is indistinguishable from the default plan and must be re-authored).
    CHECK(SkillOf(phaseOneDecision) != expected);
}

// ------------------------------------------------------------------------------------------------
// WP-15b: AI valuation through the gate
// ------------------------------------------------------------------------------------------------

void AiValuationGateAware() {
    // One heart already lives (far away), so the parts gate holds the body at its floor.
    FMiniBattle battle(110, 50, 1);
    auto& registry = battle.registry;
    FAiProbe probe{registry, battle.Content(), {}, battle.actor};
    probe.tuning.focusPercent = 0;  // the value is exactly the landed HP (no wounded-target bonus)

    // At the floor the body is worth 0 → the option drops out entirely.
    battle.SetBodyHp(55);
    const auto floor = game::BossGateFloor(registry, battle.body);
    CHECK(floor.has_value());
    CHECK(*floor == 55);
    CHECK(!probe.Best().has_value());

    // A heart beside the caster is the better target: the shielded body contributes nothing.
    const auto heart = battle.AddPart({2, 3});
    const auto onHeart = probe.Best();
    CHECK(onHeart.has_value());
    CHECK(onHeart->target == heart);
    CHECK(onHeart->value > 0);

    // Above the floor only the gate's allowance counts (current - floor), never a kill.
    registry.get<game::FGridPosition>(heart) = game::FGridPosition{0, 0};
    battle.SetBodyHp(62);
    CHECK(game::BossGateFloor(registry, battle.body).has_value());  // the heart still holds the gate
    const auto onBody = probe.Best();
    CHECK(onBody.has_value());
    CHECK(onBody->target == battle.body);
    CHECK(onBody->value == 62 - 55);
    CHECK(!onBody->kills);

    // The gate gone (no heart alive) the same body is a kill again — the cap is the only thing softening it.
    for (const auto part : battle.parts) (void)game::ApplyDamageToUnit(registry, part, 100000);
    CHECK(!game::BossGateFloor(registry, battle.body).has_value());
    const auto withoutGate = probe.Best();
    CHECK(withoutGate.has_value());
    CHECK(withoutGate->target == battle.body);
    CHECK(withoutGate->kills);
}

// ------------------------------------------------------------------------------------------------
// WP-15b: the shipped seed played for real
// ------------------------------------------------------------------------------------------------

void AutoplayKillsPartsFirst() {
    const auto bosses = ReadJsonFile("assets/data/bosses.json");
    for (const std::uint32_t seed : {1u, 2u, 3u}) {
        FBossRun run("boss-goals-autoplay", seed);
        auto& registry = run.registry;
        const auto body = run.Body();
        CHECK(body != entt::null);
        const auto hearts = run.Hearts();
        CHECK(!hearts.empty());

        // The seeded map row is the authority for the reported id (no literal boss id in the test).
        const auto* sequence = game::FindRunSequence(registry);
        CHECK(sequence != nullptr);
        auto& state = run.run();
        const auto position = sequence->PositionOf(state.depth - 1);
        const std::string theme = sequence->themes[position.theme].id;
        const std::string expectedBoss = MappedBossId(bosses, theme, "mid");
        CHECK(!expectedBoss.empty());

        std::vector<int> heartDiedAt(hearts.size(), -1);
        int bodyDiedAt = -1;
        registry.ctx().get<game::FCombatRuntime>().autoPlay = true;
        while (!registry.ctx().get<game::FBattleOutcome>().resolved && run.steps < 6000) {
            run.step();
            // Sampled every frame: while any heart lives the body never dips under its floor.
            const auto floor = game::BossGateFloor(registry, body);
            if (floor && game::BossUnitAlive(registry, body) && game::BossPartsAlive(registry, body) > 0) {
                CHECK(registry.get<game::FHealth>(body).current >= *floor);
            }
            for (std::size_t index = 0; index < hearts.size(); ++index) {
                if (heartDiedAt[index] < 0 && !game::BossUnitAlive(registry, hearts[index])) {
                    heartDiedAt[index] = run.steps;
                }
            }
            if (bodyDiedAt < 0 && !game::BossUnitAlive(registry, body)) bodyDiedAt = run.steps;
        }

        const auto& outcome = registry.ctx().get<game::FBattleOutcome>();
        CHECK(outcome.resolved && outcome.victory);

        int lastHeart = -1;
        for (const int died : heartDiedAt) {
            CHECK(died >= 0);  // every heart died
            lastHeart = std::max(lastHeart, died);
        }
        CHECK(lastHeart >= 0);
        CHECK(bodyDiedAt > lastHeart);  // ... before the body

        const auto events = RunEvents(registry, game::ERunEventKind::BossDefeated);
        CHECK(events.size() == 1);
        CHECK(events.front().id == expectedBoss);  // the seeded id, exactly once

        std::cout << "  seed " << seed << ": boss " << expectedBoss << ", hearts " << hearts.size()
                  << ", steps " << run.steps << ", body died at frame " << bodyDiedAt << " (last heart "
                  << lastHeart << ")\n";
    }
}

void BossPartsAutoCompleteBudget() {
    // Report only: the round count is a tuning metric (the cap is 60), never a pass/fail threshold. Only a
    // loss or a stall at the cap fails the test.
    for (const std::uint32_t seed : {1u, 2u}) {
        FBossRun run("boss-goals-budget", seed);
        CHECK(run.Body() != entt::null);
        const bool victory = run.playBattle();
        const auto* stats = run.registry.ctx().find<game::FRoomBattleStats>();
        const int rounds = stats != nullptr ? stats->rounds : 0;
        std::cout << "  seed " << seed << ": rounds " << rounds << " / " << game::kMaxBattleRounds
                  << ", steps " << run.steps << ", hearts " << run.Hearts().size() << '\n';
        CHECK(victory);
    }
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
        {"GatedBodyCritFollowsCap", GatedBodyCritFollowsCap},
        {"GatedHitLogsZero", GatedHitLogsZero},
        {"ContinuedBossRoomShowsAllDefeated", ContinuedBossRoomShowsAllDefeated},
        {"GateLiftsAfterPartsDie", GateLiftsAfterPartsDie},
        {"GateBlockedLogOnce", GateBlockedLogOnce},
        {"GateGuidanceFitsOneLine", GateGuidanceFitsOneLine},
        {"NoFrameOrderDependence", NoFrameOrderDependence},
        {"SinglePhaseBossUnchanged", SinglePhaseBossUnchanged},
        {"FallbackBossStillAppends", FallbackBossStillAppends},
        {"NoDungeonRunBattleNoAppend", NoDungeonRunBattleNoAppend},
        {"ContinuedRoomNoReAppend", ContinuedRoomNoReAppend},
        {"BossDefeatedAppendedOnce", BossDefeatedAppendedOnce},
        {"MidBossAppendsBeforeCamp", MidBossAppendsBeforeCamp},
        {"FinalBossDoesNotEndRun", FinalBossDoesNotEndRun},
        {"BossPartsSpawnInDocument", BossPartsSpawnInDocument},
        {"GoalCompletionDeterministic", GoalCompletionDeterministic},
        {"ImmunityBlocksSkillPath", ImmunityBlocksSkillPath},
        {"ImmunityBlocksRuneHookPath", ImmunityBlocksRuneHookPath},
        {"ImmunityBeforeRollNoRng", ImmunityBeforeRollNoRng},
        {"ImmuneSetFromBossPhase", ImmuneSetFromBossPhase},
        {"ImmuneSetUpdatedOnTransition", ImmuneSetUpdatedOnTransition},
        {"CommittedIntentSurvivesTransition", CommittedIntentSurvivesTransition},
        {"PhaseBehaviorTakesEffectNextRound", PhaseBehaviorTakesEffectNextRound},
        {"PhaseGuardSelectsSkill", PhaseGuardSelectsSkill},
        {"AiValuationGateAware", AiValuationGateAware},
        {"AutoplayKillsPartsFirst", AutoplayKillsPartsFirst},
        {"BossPartsAutoCompleteBudget", BossPartsAutoCompleteBudget},
    });
}
