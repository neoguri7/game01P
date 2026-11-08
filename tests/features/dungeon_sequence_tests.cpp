// WP-08a: the dungeon sequence (dungeon.json v2) as data — themes -> dungeons -> room slots, the battle
// ordinal (`room`) that never moves when an event slot is inserted, boss placement from the slot `boss`
// field, the gap-event transitions, save/resume identity, the boot probe, the start-support skeleton (D-7)
// and the cycle scale (D-19). Every expectation is a rule computed from the loaded data: the WP-08 ship
// numbers are playtest variables, so the tests assert structure, never constants.

#define GAME01P_TEST_SUPPORT_CHECK
#include "../TestSupport.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <queue>
#include <sstream>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>
#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/spdlog.h>

#include "core/Logger.h"
#include "ecs/components/FBaseCombatStats.h"
#include "ecs/components/FFaction.h"
#include "ecs/components/FHealth.h"
#include "ecs/components/FStats.h"
#include "ecs/systems/ActionResolverSystem.h"
#include "ecs/systems/CombatOutcomeSystem.h"
#include "ecs/systems/TurnFlowSystem.h"
#include "ecs/systems/TurnSystem.h"
#include "gameplay/GameplayServices.h"
#include "gameplay/battle/FBattleSetup.h"
#include "gameplay/combat/FCombatSheet.h"
#include "gameplay/combat/FCombatTypes.h"
#include "gameplay/data/FContentRegistry.h"
#include "gameplay/dungeon/FBossContent.h"
#include "gameplay/dungeon/FDungeonRun.h"
#include "gameplay/dungeon/FDungeonCamp.h"
#include "gameplay/dungeon/FDungeonSave.h"
#include "gameplay/dungeon/FRewards.h"
#include "gameplay/dungeon/FRoomGenerator.h"
#include "gameplay/dungeon/FRunFlow.h"
#include "gameplay/dungeon/FRunSequence.h"
#include "gameplay/factory/FEntityDefinitions.h"
#include "gameplay/factory/FEntityFactory.h"
#include "gameplay/factory/FScene.h"
#include "states/FEmberBattleState.h"

namespace {

using nlohmann::json;

constexpr std::uint32_t kSeed = 4242;

// --- scratch files ------------------------------------------------------------------------------

std::string UniqueStem(const char* stem) {
    static std::uint32_t serial = 0;
    std::filesystem::create_directories("out");
    return std::string(stem) + "-" + std::to_string(++serial) + "-" +
           std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
}

std::string UniquePath(const char* stem) {
    return "out/" + UniqueStem(stem) + ".json";
}

json ReadJsonFile(const std::string& path) {
    std::ifstream file(path);
    CHECK(file.good());
    json document;
    file >> document;
    return document;
}

void WriteJsonFile(const std::string& path, const json& document) {
    std::ofstream file(path, std::ios::trunc);
    CHECK(file.good());
    file << document.dump(2);
}

/// The frozen fixture's dungeon document — the rules tests edit copies of it.
json FixtureDocument() { return ReadJsonFile(FixtureDataPath("dungeon.json")); }

/// Parses a sequence; every caller expects a valid document.
game::FRunSequence LoadSequence(const json& document) {
    std::string error;
    auto sequence = game::FRunSequence::Load(document, error);
    CHECK(sequence.has_value());
    CHECK(error.empty());
    return *sequence;
}

/// The WP rule transcription for the cycle scale (independent of CycleScaleFor): percents of the rows for
/// k = 1..cycle, the last row repeating.
game::FCycleScale ExpectedCycleScale(const game::FRunSequence& sequence, int cleared) {
    game::FCycleScale scale;
    const int total = sequence.TotalDungeons();
    if (total <= 0) return scale;
    const int cycle = std::max(0, cleared) / total;
    for (int k = 1; k <= cycle; ++k) {
        if (sequence.cycles.empty()) break;
        const std::size_t row = std::min<std::size_t>(static_cast<std::size_t>(k - 1), sequence.cycles.size() - 1);
        scale.healthPct += sequence.cycles[row].healthPct;
        scale.attackPct += sequence.cycles[row].attackPct;
    }
    return scale;
}

// --- logging capture (the gap-resume warning and the start-support warning) ---------------------

struct FLogCapture {
    std::ostringstream stream;
    std::shared_ptr<spdlog::logger> logger;

    FLogCapture() {
        logger = std::make_shared<spdlog::logger>(
            "sequence-test-capture", std::make_shared<spdlog::sinks::ostream_sink_mt>(stream));
        logger->set_level(spdlog::level::trace);
        SetCurrentGameLogger(logger);
    }

    ~FLogCapture() { SetCurrentGameLogger(nullptr); }

    std::string Text() const { return stream.str(); }
};

/// The `[room] family=...` metric lines in captured text (the boot probe's own lines never match).
int MetricLineCount(const std::string& text) {
    int count = 0;
    std::istringstream stream(text);
    for (std::string line; std::getline(stream, line);) {
        if (line.find("[room] family=") != std::string::npos) ++count;
    }
    return count;
}

// --- a booted registry with its own checkpoint and save slot ------------------------------------

struct FBoot {
    std::string checkpoint;
    entt::registry registry;
    std::string runSave;

    explicit FBoot(std::string path, std::vector<std::string> roots = TestAssetRoots())
        : checkpoint(std::move(path)) {
        game::FAssetManager::Initialize(registry, std::move(roots));
        CHECK(game::gameplay::FGameplayServices::Initialize(registry));
        CHECK(game::InitializeDungeon(registry, checkpoint));
        runSave = registry.ctx().get<game::FDungeonSave>().path;
    }

    ~FBoot() {
        std::error_code error;
        std::filesystem::remove(checkpoint, error);
        std::filesystem::remove(runSave, error);
        std::filesystem::remove(runSave + ".tmp", error);
    }

    FBoot(const FBoot&) = delete;
    FBoot& operator=(const FBoot&) = delete;

    game::FDungeonRun& run() { return registry.ctx().get<game::FDungeonRun>(); }
    game::FRewards& rewards() { return registry.ctx().get<game::FRewards>(); }
};

// --- a temporary asset root (edited dungeon documents) ------------------------------------------

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

    /// Everything FGameplayServices + InitializeDungeon read; the dungeon is overwritten per test.
    void CopyGameplayData() {
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

/// Boots content + the dungeon definition over a temp root; false means a loader rejected the document.
bool BootsDocument(const json& document, const char* stem) {
    FTempAssets assets(stem);
    assets.CopyGameplayData();
    assets.Write("dungeon.json", document);
    auto loaded = game_test::LoadTestRegistry({assets.root.string(), kShipAssetRoot}, UniquePath(stem));
    return loaded.content && loaded.dungeon;
}

// --- room generation inputs (the fixture's crafted data, not the game's loader-private parse) ----

/// Builds an FRoomGenConfig from a dungeon document's generation block. Mirrors the fields the game's
/// ParseRoomGeneration reads — deliberately re-derived in the test so a change to the game-side parser
/// cannot move these expectations.
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

// --- a real battle harness (autoplay through the sequence) --------------------------------------

struct FBattleHarness {
    entt::registry registry;
    game::ecs::TurnSystem turns;
    game::ecs::TurnFlowSystem flow;
    game::ecs::ActionResolverSystem actions;
    game::ecs::CombatOutcomeSystem outcomes;
    FEmberBattleState battle;
    std::string checkpoint;
    std::string runSave;

    explicit FBattleHarness(std::uint32_t seed, std::vector<std::string> roots = TestAssetRoots())
        : checkpoint(UniquePath("sequence-battle")) {
        game::FAssetManager::Initialize(registry, std::move(roots));
        CHECK(game::gameplay::FGameplayServices::Initialize(registry));
        CHECK(game::InitializeDungeon(registry, checkpoint));
        runSave = registry.ctx().get<game::FDungeonSave>().path;
        turns.onRegister(registry);
        game::StartRun(registry, seed, true);
        battle.OnEnter(registry);
    }

    ~FBattleHarness() {
        battle.OnExit(registry);
        std::error_code error;
        std::filesystem::remove(checkpoint, error);
        std::filesystem::remove(runSave, error);
        std::filesystem::remove(runSave + ".tmp", error);
    }

    FBattleHarness(const FBattleHarness&) = delete;
    FBattleHarness& operator=(const FBattleHarness&) = delete;

    game::FDungeonRun& run() { return registry.ctx().get<game::FDungeonRun>(); }

    void step() {
        // Test-time acceleration: the battle outcome is turn/AP-based, not frame-based, so a larger dt only
        // carries the presentation/watch timers to the same decisions in fewer frames (keeps the suite well
        // inside the 120 s ctest timeout while playing 3 seeds through three dungeons).
        turns.update(registry, .5f);
        flow.update(registry, .5f);
        actions.update(registry, .5f);
        outcomes.update(registry, .5f);
    }

    /// Auto-plays the fielded room to a decision. False = lost (or stalled, which is a failure too).
    bool playBattle() {
        registry.ctx().get<game::FCombatRuntime>().autoPlay = true;
        int steps = 0;
        while (!registry.ctx().get<game::FBattleOutcome>().resolved && steps++ < 6000) step();
        totalSteps += steps;
        const auto& outcome = registry.ctx().get<game::FBattleOutcome>();
        return outcome.resolved && outcome.victory;
    }

    int totalSteps{0};  // report metric: how many frames the seed's autoplay consumed

    /// The app layer's next-room handoff: consume the advance request through the battle state (as the real
    /// state machine does) and field the next room.
    void nextRoom() {
        CHECK((battle.Update(registry, 0) == FStateTransition{EStateChangeKind::Root, std::string(kEmberBattleId)}));
        battle.OnExit(registry);
        battle.OnEnter(registry);
    }

    /// True when the fielded room holds an enemy with this display name. FUnitData keeps the placement
    /// id/display name (the unit template id lives only in the battlefield document), and the generation
    /// data gives the boss its own display name — that is the boss identity the field can be checked for.
    [[nodiscard]] bool hasEnemyNamed(std::string_view displayName) {
        for (const auto entity : registry.view<game::FUnitData>()) {
            if (registry.all_of<game::FPartyMember>(entity)) continue;
            if (registry.get<game::FUnitData>(entity).definition.displayName == displayName) return true;
        }
        return false;
    }

    /// The boss unit's display name as authored in `generation.boss_name` (derived, never a test constant).
    [[nodiscard]] std::string bossDisplayName() {
        return game::DungeonDefinition(registry).at("generation").at("boss_name").get<std::string>();
    }
};

/// Fields the current room document and refreshes the sheets; returns the sorted enemy (max HP, attack)
/// pairs — the same seed/ordinal must produce the same pairs no matter where the event slots sit.
std::vector<std::pair<int, int>> EnemySheets(FBoot& boot, const json& document) {
    game::FScene scene(boot.registry, "SequenceTest");
    scene.Load();
    const auto battlefield = game::FEntityFactory::CreateBattlefield(scene, document);
    CHECK(battlefield.has_value());
    const auto& content = boot.registry.ctx().get<game::gameplay::FContentRegistry>();
    CHECK(game::SetupCombatUnits(boot.registry, *battlefield, content));
    game::RefreshAllCombatSheets(boot.registry, content);
    std::vector<std::pair<int, int>> sheets;
    for (const auto entity : boot.registry.view<game::FBaseCombatStats>()) {
        if (boot.registry.all_of<game::FPartyMember>(entity)) continue;
        sheets.emplace_back(boot.registry.get<game::FHealth>(entity).max,
                            boot.registry.get<game::FStats>(entity).attack);
    }
    std::ranges::sort(sheets);
    return sheets;
}

// ------------------------------------------------------------------------------------------------
// sequence rules
// ------------------------------------------------------------------------------------------------

void SequencePositions() {
    const auto ship = LoadSequence(ReadJsonFile("assets/data/dungeon.json"));
    const int total = ship.TotalDungeons();
    CHECK(total >= 2);

    // An independent flattening of the authored themes: cleared -> (theme, dungeonInTheme) follows it in
    // order and wraps with cycle = cleared / total (D-19 — the themes loop, the cycle grows).
    std::vector<std::pair<std::size_t, std::size_t>> flat;
    for (std::size_t theme = 0; theme < ship.themes.size(); ++theme) {
        for (std::size_t dungeon = 0; dungeon < ship.themes[theme].dungeons.size(); ++dungeon) {
            flat.emplace_back(theme, dungeon);
        }
    }
    CHECK(static_cast<int>(flat.size()) == total);
    for (int cleared = 0; cleared < 2 * total + 3; ++cleared) {
        const auto position = ship.PositionOf(cleared);
        CHECK(position.cycle == ship.RunCycle(cleared));
        CHECK(position.cycle == cleared / total);
        const auto expected = flat[static_cast<std::size_t>(cleared % total)];
        CHECK(position.theme == expected.first && position.dungeonInTheme == expected.second);
        CHECK(!ship.DungeonAt(position).name.empty());
        CHECK(ship.BattleCountAt(position) >= 1);
    }
    // Immutable inputs: random access by PositionOf never mutates the authored themes.
    CHECK(ship.TotalDungeons() == total);
}

void MidAndFinalBoss() {
    const auto ship = LoadSequence(ReadJsonFile("assets/data/dungeon.json"));
    CHECK(ship.themes.size() >= 2);
    for (std::size_t theme = 0; theme < ship.themes.size(); ++theme) {
        const std::size_t dungeons = ship.themes[theme].dungeons.size();
        CHECK(dungeons >= 3);
        const auto third = ship.PositionOf(static_cast<int>(theme * dungeons + 2));
        const auto last = ship.PositionOf(static_cast<int>((theme + 1) * dungeons - 1));
        CHECK(third.theme == theme && third.dungeonInTheme == 2);
        CHECK(last.theme == theme && last.dungeonInTheme == dungeons - 1);
        // The third dungeon ends on the mid boss, the theme's last dungeon on the final boss.
        CHECK(ship.IsBossBattle(third, ship.BattleCountAt(third) - 1));
        CHECK(ship.BattleSlotAt(third, ship.BattleCountAt(third) - 1)->boss == "mid");
        CHECK(ship.IsBossBattle(last, ship.BattleCountAt(last) - 1));
        CHECK(ship.BattleSlotAt(last, ship.BattleCountAt(last) - 1)->boss == "final");
    }
}

void DungeonsOneTwoHaveNoBoss() {
    const auto ship = LoadSequence(ReadJsonFile("assets/data/dungeon.json"));
    for (std::size_t theme = 0; theme < ship.themes.size(); ++theme) {
        const std::size_t dungeons = ship.themes[theme].dungeons.size();
        for (std::size_t dungeon = 0; dungeon < std::min<std::size_t>(2, dungeons); ++dungeon) {
            const auto position = ship.PositionOf(static_cast<int>(theme * dungeons + dungeon));
            for (int ordinal = 0; ordinal < ship.BattleCountAt(position); ++ordinal) {
                CHECK(!ship.IsBossBattle(position, ordinal));
                CHECK(ship.BattleSlotAt(position, ordinal)->boss.empty());
            }
        }
    }
}

void MidBossAtThirdDungeon() {
    const auto ship = LoadSequence(ReadJsonFile("assets/data/dungeon.json"));
    for (std::size_t theme = 0; theme < ship.themes.size(); ++theme) {
        const std::size_t dungeons = ship.themes[theme].dungeons.size();
        CHECK(dungeons >= 3);
        const auto position = ship.PositionOf(static_cast<int>(theme * dungeons + 2));
        const int battles = ship.BattleCountAt(position);
        CHECK(ship.IsBossBattle(position, battles - 1));
        CHECK(ship.BattleSlotAt(position, battles - 1)->boss == "mid");
        for (int ordinal = 0; ordinal + 1 < battles; ++ordinal) {
            CHECK(!ship.IsBossBattle(position, ordinal));  // exactly one boss, and it closes the dungeon
        }
    }
}

void FinalBossAtThemeEnd() {
    const auto ship = LoadSequence(ReadJsonFile("assets/data/dungeon.json"));
    for (std::size_t theme = 0; theme < ship.themes.size(); ++theme) {
        const std::size_t dungeons = ship.themes[theme].dungeons.size();
        const auto position = ship.PositionOf(static_cast<int>((theme + 1) * dungeons - 1));
        const int battles = ship.BattleCountAt(position);
        CHECK(ship.BattleSlotAt(position, battles - 1)->boss == "final");
        CHECK(ship.IsBossBattle(position, battles - 1));
        for (int ordinal = 0; ordinal + 1 < battles; ++ordinal) {
            CHECK(!ship.IsBossBattle(position, ordinal));
        }
    }
}

void RoomCountVaries() {
    // Ship: the confirmed §3 shape (3 battles + 1-2 events) comes from the slots, not from a code constant.
    const auto ship = LoadSequence(ReadJsonFile("assets/data/dungeon.json"));
    for (int index = 0; index < ship.TotalDungeons(); ++index) {
        const auto position = ship.PositionOf(index);
        const auto& rooms = ship.DungeonAt(position).rooms;
        int battles = 0;
        int events = 0;
        for (const auto& slot : rooms) (slot.type == "battle" ? battles : events) += 1;
        CHECK(ship.BattleCountAt(position) == battles);
        CHECK(battles == 3);
        CHECK(events >= 1 && events <= 2);
        CHECK(rooms.front().type == "battle" && rooms.back().type == "battle");
    }

    // A test-only shape the ship data does not use: battle 4 + event 2 — the walk follows the slots.
    auto edited = FixtureDocument();
    edited["themes"][0]["dungeons"][0]["rooms"] = json::array({
        {{"type", "battle"}, {"family", "open"}},
        {{"type", "event"}, {"event_pool", "common"}},
        {{"type", "battle"}, {"family", "open"}},
        {{"type", "event"}, {"event_pool", "common"}},
        {{"type", "battle"}, {"family", "open"}},
        {{"type", "battle"}, {"family", "open"}}});
    const auto sequence = LoadSequence(edited);
    const auto position = sequence.PositionOf(0);
    CHECK(sequence.BattleCountAt(position) == 4);
    CHECK(sequence.BattleSlotAt(position, 3) != nullptr);
    CHECK(sequence.BattleSlotAt(position, 4) == nullptr);
    CHECK(sequence.GapEvent(position, 0) != nullptr);
    CHECK(sequence.GapEvent(position, 1) != nullptr);
    CHECK(sequence.GapEvent(position, 2) == nullptr);
    CHECK(sequence.GapEvent(position, 3) == nullptr);

    // A single-battle dungeon is the smallest legal shape and still ends in a camp.
    auto shortDungeon = FixtureDocument();
    shortDungeon["themes"][0]["dungeons"][0]["rooms"] = json::array({{{"type", "battle"}, {"family", "open"}}});
    const auto shortSequence = LoadSequence(shortDungeon);
    CHECK(shortSequence.BattleCountAt(shortSequence.PositionOf(0)) == 1);
    CHECK(shortSequence.GapEvent(shortSequence.PositionOf(0), 0) == nullptr);
}

void EventSlotsOnlyInInternalGaps() {
    const auto base = FixtureDocument();
    const auto edit = [&base](const auto& change) {
        auto document = base;
        change(document);
        return document;
    };
    const std::vector<std::pair<const char*, json>> cases = {
        {"first slot non-battle",
         edit([](json& d) {
             d["themes"][0]["dungeons"][0]["rooms"][0]["type"] = "event";
             d["themes"][0]["dungeons"][0]["rooms"][1]["type"] = "battle";
         })},
        {"last slot non-battle",
         edit([](json& d) { d["themes"][0]["dungeons"][0]["rooms"].back()["type"] = "event"; })},
        {"two non-battle slots adjacent",
         edit([](json& d) {
             d["themes"][0]["dungeons"][0]["rooms"][1]["type"] = "event";
             d["themes"][0]["dungeons"][0]["rooms"][2]["type"] = "event";
         })},
        {"unknown slot type",
         edit([](json& d) { d["themes"][0]["dungeons"][0]["rooms"][1]["type"] = "shop"; })},
    };
    for (const auto& [label, document] : cases) {
        std::string error;
        const bool loaded = game::FRunSequence::Load(document, error).has_value();
        if (loaded) std::cout << "    accepted invalid sequence: " << label << '\n';
        CHECK(!loaded);
        CHECK(!error.empty());
        CHECK(!BootsDocument(document, "sequence-gap"));
    }
}

void GapAfterFirstBattle() {
    const auto ship = LoadSequence(ReadJsonFile("assets/data/dungeon.json"));
    const auto first = ship.PositionOf(0);
    const auto* gap = ship.GapEvent(first, 0);
    CHECK(gap != nullptr && gap->type == "event" && !gap->eventPool.empty());
    CHECK(ship.GapEvent(first, 1) == nullptr);  // ship dungeon 1's second battle is followed by a battle

    // The gap position varies per dungeon (that is the data freedom the ordinal model needs): at least one
    // ship dungeon puts its event after the second battle.
    bool secondGapExists = false;
    for (int index = 0; index < ship.TotalDungeons(); ++index) {
        const auto position = ship.PositionOf(index);
        if (ship.BattleCountAt(position) >= 2 && ship.GapEvent(position, 0) == nullptr &&
            ship.GapEvent(position, 1) != nullptr) {
            secondGapExists = true;
            break;
        }
    }
    CHECK(secondGapExists);

    // The last battle never has a gap — the camp follows it.
    const auto last = ship.PositionOf(ship.TotalDungeons() - 1);
    CHECK(ship.GapEvent(last, ship.BattleCountAt(last) - 1) == nullptr);
}

void CampAfterLastBattleOnly() {
    FBoot boot(UniquePath("sequence-camp-last"));
    game::StartRun(boot.registry, kSeed, true);
    auto& run = boot.run();
    const auto* sequence = game::FindRunSequence(boot.registry);
    CHECK(sequence != nullptr);
    const auto position = sequence->PositionOf(run.depth - 1);
    const int battles = sequence->BattleCountAt(position);
    CHECK(battles >= 2);
    for (int room = 0; room < battles; ++room) {
        CHECK(game::ResolveRoomVictory(boot.registry, true).has_value());
        CHECK(run.phase == game::EDungeonPhase::Reward);
        if (boot.rewards().offer.active) CHECK(game::ResolveRuneOffer(boot.registry, -1));
        CHECK(game::ApplyRunIntent(boot.registry, game::ERunIntent::ContinueReward).has_value());
        if (room + 1 == battles) {
            // The camp opens after the dungeon's last battle — never after an earlier one.
            CHECK(run.phase == game::EDungeonPhase::Camp);
            CHECK(run.cleared == run.depth);
        } else if (sequence->GapEvent(position, room) != nullptr) {
            CHECK(run.phase == game::EDungeonPhase::Event);
            CHECK(game::ApplyRunIntent(boot.registry, game::ERunIntent::ChooseEventLoot).has_value());
            CHECK(run.phase == game::EDungeonPhase::Battle && run.room == room + 1);
        } else {
            CHECK(run.phase == game::EDungeonPhase::Battle && run.room == room + 1);
        }
        CHECK(run.phase != game::EDungeonPhase::Camp || room + 1 == battles);
    }
}

// ------------------------------------------------------------------------------------------------
// loader enforcement / boot probe
// ------------------------------------------------------------------------------------------------

void BootProbeCoversTuples() {
    for (const auto& roots : {TestAssetRoots(), ShipAssetRoots()}) {
        FBoot boot(UniquePath("sequence-probe"), roots);
        const auto& probe = boot.registry.ctx().get<game::FDungeonBootProbe>();
        const auto* sequence = game::FindRunSequence(boot.registry);
        CHECK(sequence != nullptr);
        // WP-15: the probe key also carries the resolved boss (bosses.json (theme, slot) mapping, else the
        // generation.boss_unit fallback) — a mapped boss with parts is a different generation class.
        const auto* catalog = game::FindBossCatalog(boot.registry);
        CHECK(catalog != nullptr);
        const std::string fallbackBoss =
            game::DungeonDefinition(boot.registry).at("generation").at("boss_unit").get<std::string>();
        std::vector<std::tuple<std::string, std::string, std::string>> tuples;
        for (int index = 0; index < sequence->TotalDungeons(); ++index) {
            const auto position = sequence->PositionOf(index);
            for (int ordinal = 0; ordinal < sequence->BattleCountAt(position); ++ordinal) {
                const auto* slot = sequence->BattleSlotAt(position, ordinal);
                CHECK(slot != nullptr);
                std::string boss;
                if (!slot->boss.empty()) {
                    const auto* def = catalog->Resolve(sequence->themes[position.theme].id, slot->boss);
                    boss = def != nullptr ? def->id : fallbackBoss;
                }
                const std::tuple<std::string, std::string, std::string> tuple{slot->family, slot->boss, boss};
                if (std::ranges::find(tuples, tuple) == tuples.end()) tuples.push_back(tuple);
            }
        }
        CHECK(probe.tuples == static_cast<int>(tuples.size()));
        CHECK(probe.iterations == probe.tuples * 3);
        CHECK(probe.tuples >= 2);  // at least one normal room and one boss identity
    }
}

void BadFamilyFailsBoot() {
    const auto base = FixtureDocument();
    // An unknown family name is refused with the accepted set named.
    {
        auto document = base;
        document["themes"][0]["dungeons"][0]["rooms"][0]["family"] = "banana";
        std::string error;
        CHECK(!game::FRunSequence::Load(document, error).has_value());
        CHECK(error.find("banana") != std::string::npos);
        CHECK(error.find("open_variant") != std::string::npos);  // the message names the accepted set
        CHECK(!BootsDocument(document, "sequence-bad-family"));
    }
    // open_variant is a valid family now (WP-08b) — the slot stores it (the heavy set is a separate rule,
    // see FamilyStrengthRespected).
    {
        auto document = base;
        document["themes"][0]["dungeons"][0]["rooms"][0]["family"] = "open_variant";
        const auto sequence = LoadSequence(document);
        const auto* slot = sequence.BattleSlotAt(sequence.PositionOf(0), 0);
        CHECK(slot != nullptr && slot->family == "open_variant");
    }
    // Missing family stays legal (missing = "open").
    auto noFamily = base;
    noFamily["themes"][0]["dungeons"][0]["rooms"][0].erase("family");
    const auto noFamilySequence = LoadSequence(noFamily);
    const auto* slot = noFamilySequence.BattleSlotAt(noFamilySequence.PositionOf(0), 0);
    CHECK(slot != nullptr && slot->family == "open");
}

// ------------------------------------------------------------------------------------------------
// room families (WP-08b): the golden open layout, invariants, strength enforcement, metrics
// ------------------------------------------------------------------------------------------------

void OpenFamilyUnchanged() {
    // Golden: the `open` family must keep the pre-WP-08b generator byte-identical — the same RNG draw order,
    // so every seed that generated a room before still generates that exact room. The values were captured
    // from the generator as committed at f2b9aa8, with this exact input: the fixture config, party cells
    // (3,9)(4,9)(6,9)(7,9), request depth=1 room=0, seeds 1-3 x boss off/on. If this fails, the open draw
    // order moved — not the fixture.
    const auto document = FixtureDocument();
    const auto config = ConfigFromDocument(document, "open");
    const auto cells = PartyCellsFromDocument(document);
    CHECK((cells == std::vector<std::pair<int, int>>{{3, 9}, {4, 9}, {6, 9}, {7, 9}}));

    struct FEnemyGolden {
        const char* unit;
        int x;
        int y;
    };
    struct FCase {
        std::uint32_t seed;
        bool boss;
        const char* name;
        std::array<const char*, 12> terrain;
        std::vector<FEnemyGolden> enemies;
    };
    const std::vector<FCase> golden = {
        {1, false, "The sunken vault",
         {"............", "............", "............", "............", "............", ".......#....",
          "......^#....", "............", "............", "............", "............", "............"},
         {{"enemy.imp", 9, 1}, {"enemy.imp", 2, 3}, {"enemy.imp_shaman", 1, 1}, {"enemy.imp", 1, 2}}},
        {1, true, "The sunken vault",
         {"............", "............", "............", ".........##.", "........^...", ".......^....",
          "............", "............", "............", "............", "............", "............"},
         {{"enemy.guardian", 9, 1}, {"enemy.imp_archer", 2, 3}, {"enemy.imp", 1, 1}}},
        {2, false, "Whispering cistern",
         {"............", "............", "............", "....^...^...", "............", ".........#..",
          ".........#..", "............", ".....^......", "............", "............", "............"},
         {{"enemy.imp", 4, 2}, {"enemy.imp", 7, 1}, {"enemy.imp_archer", 10, 1}, {"enemy.imp", 7, 2}}},
        {2, true, "Whispering cistern",
         {"............", "............", "............", "....^...^...", "............", "...##....#..",
          "...#.....#..", "............", ".....^......", "............", "............", "............"},
         {{"enemy.guardian", 4, 2}, {"enemy.imp", 7, 1}, {"enemy.imp", 10, 1}}},
        {3, false, "The sunken vault",
         {"............", "............", "............", "......#.....", "..###.......", "....^.......",
          "............", "..^.........", "......^.....", "............", "............", "............"},
         {{"enemy.imp", 5, 1}, {"enemy.imp", 8, 3}, {"enemy.imp_archer", 10, 2}, {"enemy.imp_shaman", 4, 1}}},
        {3, true, "The sunken vault",
         {"............", "............", "............", "............", "..###.......", ".^..........",
          "............", "............", "......^.....", "............", "............", "............"},
         {{"enemy.guardian", 5, 1}, {"enemy.imp_shaman", 8, 3}, {"enemy.imp", 10, 2}}},
    };
    for (const auto& test : golden) {
        const auto room = game::gameplay::GenerateRoom(config, {test.seed, 1, 0, test.boss, cells});
        CHECK(room.name == test.name);
        for (int y = 0; y < game::gameplay::FGeneratedRoom::kSize; ++y) {
            CHECK(room.terrain[static_cast<std::size_t>(y)] == test.terrain[static_cast<std::size_t>(y)]);
        }
        CHECK(room.enemies.size() == test.enemies.size());
        for (std::size_t i = 0; i < test.enemies.size(); ++i) {
            CHECK(room.enemies[i].unit == test.enemies[i].unit);
            CHECK(room.enemies[i].x == test.enemies[i].x && room.enemies[i].y == test.enemies[i].y);
        }
    }
}

/// Independent BFS: every unit (party + enemies) mutually reachable over non-wall cells, and no two units
/// on one cell (the generation contract).
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
    seen[units.front().second][units.front().first] = true;
    while (!open.empty()) {
        const auto [x, y] = open.front();
        open.pop();
        for (const auto [dx, dy] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}}) {
            const int nx = x + dx;
            const int ny = y + dy;
            if (nx < 0 || ny < 0 || nx >= size || ny >= size || seen[ny][nx]) continue;
            if (room.terrain[static_cast<std::size_t>(ny)][static_cast<std::size_t>(nx)] == '#') continue;
            seen[ny][nx] = true;
            open.push({nx, ny});
        }
    }
    return std::ranges::all_of(units, [&](const std::pair<int, int>& unit) {
        return seen[unit.second][unit.first];
    });
}

void FamiliesKeepInvariants() {
    const auto document = FixtureDocument();
    const auto cells = PartyCellsFromDocument(document);
    const int size = game::gameplay::FGeneratedRoom::kSize;
    for (const char* family : {"open", "open_variant", "corridor", "cover", "split"}) {
        const auto config = ConfigFromDocument(document, family);
        for (std::uint32_t seed = 1; seed <= 50; ++seed) {
            for (const bool boss : {false, true}) {
                const auto room = game::gameplay::GenerateRoom(config, {seed, 1, 0, boss, cells});
                // Deterministic for a given config and request (the header contract): the same input twice.
                const auto again = game::gameplay::GenerateRoom(config, {seed, 1, 0, boss, cells});
                CHECK(room.terrain == again.terrain);
                CHECK(room.enemies.size() == again.enemies.size());
                for (std::size_t i = 0; i < room.enemies.size(); ++i) {
                    CHECK(room.enemies[i].unit == again.enemies[i].unit);
                    CHECK(room.enemies[i].x == again.enemies[i].x && room.enemies[i].y == again.enemies[i].y);
                }
                CHECK(!room.enemies.empty());
                // The map is open: the outermost ring is never built on (walls, traps or units).
                for (int i = 0; i < size; ++i) {
                    CHECK(room.terrain[static_cast<std::size_t>(i)].front() == '.');
                    CHECK(room.terrain[static_cast<std::size_t>(i)].back() == '.');
                    CHECK(room.terrain.front()[static_cast<std::size_t>(i)] == '.');
                    CHECK(room.terrain.back()[static_cast<std::size_t>(i)] == '.');
                }
                const auto spawnable = [&](int x, int y) {
                    const char cell = room.terrain[static_cast<std::size_t>(y)][static_cast<std::size_t>(x)];
                    return cell == '.' || cell == '^';  // floor or trap — never a wall
                };
                int walls = 0;
                for (const auto& row : room.terrain) {
                    walls += static_cast<int>(std::ranges::count(row, '#'));
                }
                for (const auto& [x, y] : cells) CHECK(spawnable(x, y));
                for (const auto& enemy : room.enemies) {
                    // The spawn-band contract holds in every family.
                    CHECK(enemy.y >= game::gameplay::kRoomEnemyFirstRow && enemy.y <= game::gameplay::kRoomEnemyLastRow);
                    CHECK(spawnable(enemy.x, enemy.y));
                }
                CHECK(AllUnitsReachable(room, cells));
                if (std::string_view(family) == "open" || std::string_view(family) == "open_variant") {
                    // D-23: light families never pass the obstacle budget.
                    CHECK(walls <= config.obstacleCount.max);
                }
                if (boss) CHECK(room.enemies.front().unit == config.bossUnit);
            }
        }
    }
}

void FamilyStrengthRespected() {
    const auto base = FixtureDocument();
    // A heavy family on a normal battle slot is refused; the message names the slot and the rule.
    for (const char* family : {"corridor", "cover", "split"}) {
        auto document = base;
        document["themes"][0]["dungeons"][0]["rooms"][0]["family"] = family;
        std::string error;
        CHECK(!game::FRunSequence::Load(document, error).has_value());
        CHECK(error.find("heavy") != std::string::npos);
        CHECK(error.find("light") != std::string::npos);
        CHECK(error.find("slot 1") != std::string::npos);
        CHECK(!BootsDocument(document, "sequence-heavy-normal"));
    }
    // Heavy on a boss slot stays refused while the data flag is false (the shipped state, D-23 §2).
    auto bossHeavy = base;
    bossHeavy["themes"][0]["dungeons"][2]["rooms"].back()["family"] = "cover";  // d3 closes on the mid boss
    {
        std::string error;
        CHECK(!game::FRunSequence::Load(bossHeavy, error).has_value());
        CHECK(error.find("boss") != std::string::npos);
        CHECK(error.find("boss_rooms_allow_heavy") != std::string::npos);
        CHECK(error.find("slot 4") != std::string::npos);
        CHECK(!BootsDocument(bossHeavy, "sequence-heavy-boss-off"));
    }
    // The data flip opens exactly that: the same document boots with the flag true (heavy generation is
    // part of the boot probe too).
    auto allowed = bossHeavy;
    allowed["family_rules"] = {{"boss_rooms_allow_heavy", true}};
    CHECK(LoadSequence(allowed).bossRoomsAllowHeavy);
    CHECK(BootsDocument(allowed, "sequence-heavy-boss-on"));
    // Light families are fine on a boss slot.
    auto lightBoss = base;
    lightBoss["themes"][0]["dungeons"][2]["rooms"].back()["family"] = "open_variant";
    CHECK(BootsDocument(lightBoss, "sequence-light-boss"));
}

void FamilyMetricsLogged() {
    // The metric line is written by a real battle build (BuildDungeonBattlefield in the battle phase) and
    // never by the boot probe; the content is the pure MeasureRoom of the same generated room.
    FLogCapture capture;
    FBoot boot(UniquePath("sequence-metrics"));
    game::StartRun(boot.registry, kSeed, true);
    CHECK(boot.run().phase == game::EDungeonPhase::Battle && boot.run().room == 0);
    (void)game::BuildDungeonBattlefield(boot.registry);

    const auto& data = game::DungeonDefinition(boot.registry);
    const auto config = ConfigFromDocument(data, "open");
    const auto cells = PartyCellsFromDocument(data);
    const auto room = game::gameplay::GenerateRoom(config, {boot.run().seed, boot.run().depth, boot.run().room,
                                                            false, cells});
    const auto metrics = game::gameplay::MeasureRoom(room, cells);
    char reachable[16];
    std::snprintf(reachable, sizeof(reachable), "%.2f", metrics.reachableRatio);
    const std::string expected = "[room] family=open strength=light walls=" + std::to_string(metrics.walls) +
                                 " reachable=" + reachable + " min_path=" + std::to_string(metrics.minPath) +
                                 " cover=" + std::to_string(metrics.coverCells) +
                                 " traps=" + std::to_string(metrics.traps);
    CHECK(capture.Text().find(expected) != std::string::npos);
    CHECK(MetricLineCount(capture.Text()) == 1);
    CHECK(capture.Text().find("[room] probe tuples=") != std::string::npos);  // the probe line is separate

    // The second battle of the fixture dungeon is open_variant: the same rule reports its family/strength.
    boot.run().room = 1;
    (void)game::BuildDungeonBattlefield(boot.registry);
    CHECK(capture.Text().find("[room] family=open_variant strength=light walls=") != std::string::npos);
    CHECK(MetricLineCount(capture.Text()) == 2);

    // A non-battle build (saved-phase continue) fields the room without logging the metric.
    CHECK(game::ResolveRoomVictory(boot.registry, true).has_value());
    CHECK(boot.run().phase == game::EDungeonPhase::Reward);
    (void)game::BuildDungeonBattlefield(boot.registry);
    CHECK(MetricLineCount(capture.Text()) == 2);
}

void FamilyMetricsDistribution() {
    // A report, not a gate: the emergent shape of each family over 50 seeds. The only assertions are that
    // the metrics were computed and that the light variant shows a genuinely different distribution from
    // the classic layout (the WP-08 acceptance for light variants).
    const auto document = FixtureDocument();
    const auto cells = PartyCellsFromDocument(document);
    struct FMetric {
        double sum{0};
        double min{1e9};
        double max{-1e9};
        int count{0};
        void Add(double value) {
            sum += value;
            min = std::min(min, value);
            max = std::max(max, value);
            ++count;
        }
        [[nodiscard]] double Mean() const { return count > 0 ? sum / count : 0.0; }
    };
    struct FMetricSet {
        FMetric walls, reachable, minPath, cover, traps;
    };
    const auto measure = [&](const char* family) {
        const auto config = ConfigFromDocument(document, family);
        FMetricSet set;
        for (std::uint32_t seed = 1; seed <= 50; ++seed) {
            const auto room = game::gameplay::GenerateRoom(config, {seed, 1, 0, false, cells});
            const auto metrics = game::gameplay::MeasureRoom(room, cells);
            set.walls.Add(metrics.walls);
            set.reachable.Add(metrics.reachableRatio);
            set.minPath.Add(metrics.minPath);
            set.cover.Add(metrics.coverCells);
            set.traps.Add(metrics.traps);
        }
        return set;
    };
    const auto report = [](const char* family, const FMetricSet& set) {
        const auto row = [](const char* name, const FMetric& metric) {
            std::ostringstream line;
            line << " " << name << "=" << metric.Mean() << " (" << metric.min << ".." << metric.max << ")";
            return line.str();
        };
        std::cout << "[metrics] family=" << family << " seeds=50" << row("walls", set.walls)
                  << row("reachable", set.reachable) << row("min_path", set.minPath) << row("cover", set.cover)
                  << row("traps", set.traps) << '\n';
    };

    std::map<std::string, FMetricSet> sets;
    for (const char* family : {"open", "open_variant", "corridor", "cover", "split"}) {
        sets[family] = measure(family);
        report(family, sets[family]);
        const auto& set = sets[family];
        CHECK(set.walls.count == 50 && set.reachable.count == 50 && set.minPath.count == 50 &&
              set.cover.count == 50 && set.traps.count == 50);  // every metric was computed
        CHECK(set.walls.min >= 0);
        CHECK(set.reachable.min >= 0.0 && set.reachable.max <= 1.0);
        CHECK(set.minPath.min >= 0);  // the invariants hold: nothing is unreachable
    }

    const auto& classic = sets["open"];
    const auto& variant = sets["open_variant"];
    const bool differs = std::abs(classic.walls.Mean() - variant.walls.Mean()) > 1e-9 ||
                         std::abs(classic.reachable.Mean() - variant.reachable.Mean()) > 1e-9 ||
                         std::abs(classic.minPath.Mean() - variant.minPath.Mean()) > 1e-9 ||
                         std::abs(classic.cover.Mean() - variant.cover.Mean()) > 1e-9 ||
                         std::abs(classic.traps.Mean() - variant.traps.Mean()) > 1e-9;
    CHECK(differs);  // the light variant is a different distribution, not a relabelled classic
}

// ------------------------------------------------------------------------------------------------
// save / resume
// ------------------------------------------------------------------------------------------------

void SaveRoomRangeFollowsBattles() {
    FBoot boot(UniquePath("sequence-room-range"));
    game::StartRun(boot.registry, kSeed, true);
    auto& run = boot.run();
    const auto* sequence = game::FindRunSequence(boot.registry);
    CHECK(sequence != nullptr);
    const int last = sequence->BattleCountAt(sequence->PositionOf(run.depth - 1)) - 1;
    CHECK(last >= 1);

    // The last battle ordinal round-trips through the save.
    run.room = last;
    CHECK(game::SaveDungeonRun(boot.registry));
    boot.registry.ctx().erase<game::FDungeonRun>();
    CHECK(game::ContinueDungeonRun(boot.registry));
    CHECK(boot.run().room == last && boot.run().phase == game::EDungeonPhase::Battle);

    // One past the last battle is refused by the sequence-checked loader (the static cap alone would pass).
    auto& replaced = boot.run();
    replaced.room = last + 1;
    CHECK(game::SaveDungeonRun(boot.registry));
    boot.registry.ctx().erase<game::FDungeonRun>();
    CHECK(!game::ContinueDungeonRun(boot.registry));
    CHECK(!boot.registry.ctx().get<game::FDungeonSave>().error.empty());
}

void OldSaveRoom2Loads() {
    FBoot boot(UniquePath("sequence-old-room"));
    game::StartRun(boot.registry, kSeed, true);
    auto document = ReadJsonFile(boot.runSave);
    // Exactly the pre-change shape: schema 1, no `run.ext`, room 2 = "the third battle".
    document["schema_version"] = 1;
    document["run"].erase("ext");
    document["run"]["depth"] = 1;
    document["run"]["cleared"] = 0;
    document["run"]["room"] = 2;
    document["run"]["phase"] = "battle";
    document["run"]["room_name"] = "The broken gate";
    WriteJsonFile(boot.runSave, document);

    boot.registry.ctx().erase<game::FDungeonRun>();
    CHECK(game::ContinueDungeonRun(boot.registry));
    const auto& run = boot.run();
    CHECK(run.depth == 1 && run.cleared == 0 && run.room == 2);  // identity: the ordinal survives
    CHECK(run.phase == game::EDungeonPhase::Battle);
}

void EventGapResumeSkipsWhenMissing() {
    // Control: the gap after battle 0 exists — the resume keeps the saved event step.
    {
        FBoot boot(UniquePath("sequence-gap-control"));
        game::StartRun(boot.registry, kSeed, true);
        boot.run().phase = game::EDungeonPhase::Event;
        CHECK(game::SaveDungeonRun(boot.registry));
        boot.registry.ctx().erase<game::FDungeonRun>();
        CHECK(game::ContinueDungeonRun(boot.registry));
        CHECK(boot.run().phase == game::EDungeonPhase::Event && boot.run().room == 0);
    }

    // Content changed: dungeon 1 now runs four battles in a row — no event in the saved gap.
    {
        FTempAssets assets("sequence-gap-missing");
        assets.CopyGameplayData();
        auto document = FixtureDocument();
        document["themes"][0]["dungeons"][0]["rooms"][1] = {{"type", "battle"}, {"family", "open"}};
        assets.Write("dungeon.json", document);

        FBoot boot(UniquePath("sequence-gap-missing-run"), {assets.root.string(), kShipAssetRoot});
        game::StartRun(boot.registry, kSeed, true);
        boot.run().phase = game::EDungeonPhase::Event;
        CHECK(game::SaveDungeonRun(boot.registry));

        FLogCapture capture;
        boot.registry.ctx().erase<game::FDungeonRun>();
        CHECK(game::ContinueDungeonRun(boot.registry));
        const auto& resumed = boot.run();
        // Advanced exactly like a reward step with no event: the next battle, once — not a replayed battle.
        // The advance request is cleared with it: the continue path enters ember.battle itself, so a pending
        // request would root the battle state again and build this room a second time.
        CHECK(resumed.phase == game::EDungeonPhase::Battle && resumed.room == 1);
        CHECK(!resumed.advanceRequested);
        CHECK(capture.Text().find("no longer exists") != std::string::npos);
        CHECK(ReadJsonFile(boot.runSave)["run"]["room"] == 1);  // the normalized state was saved
    }
}

void BattleOrdinalStableAcrossEventSlots() {
    // The same (seed, depth, room) must produce the same room and the same rune draw whether or not event
    // slots sit between the battles: `room` is the battle ordinal, never a slot index.
    FTempAssets assetsA("sequence-ordinal-a");
    FTempAssets assetsB("sequence-ordinal-b");
    assetsA.CopyGameplayData();
    assetsB.CopyGameplayData();
    auto documentA = FixtureDocument();
    documentA["themes"][0]["dungeons"][0]["rooms"] = json::array({
        {{"type", "battle"}, {"family", "open"}},
        {{"type", "battle"}, {"family", "open"}},
        {{"type", "battle"}, {"family", "open"}}});
    auto documentB = FixtureDocument();
    documentB["themes"][0]["dungeons"][0]["rooms"] = json::array({
        {{"type", "battle"}, {"family", "open"}},
        {{"type", "event"}, {"event_pool", "common"}},
        {{"type", "battle"}, {"family", "open"}},
        {{"type", "event"}, {"event_pool", "common"}},
        {{"type", "battle"}, {"family", "open"}}});
    assetsA.Write("dungeon.json", documentA);
    assetsB.Write("dungeon.json", documentB);

    FBoot bootA(UniquePath("sequence-ordinal-a-run"), {assetsA.root.string(), kShipAssetRoot});
    FBoot bootB(UniquePath("sequence-ordinal-b-run"), {assetsB.root.string(), kShipAssetRoot});
    for (FBoot* boot : {&bootA, &bootB}) {
        game::StartRun(boot->registry, kSeed, true);
        boot->run().depth = 1;
        boot->run().room = 1;
    }

    // Same generated room: one document built from the same ordinal rules.
    const auto roomA = game::BuildDungeonBattlefield(bootA.registry);
    const auto roomB = game::BuildDungeonBattlefield(bootB.registry);
    CHECK(!roomA.empty() && roomA == roomB);

    // Same rune draw: the reward stream is (seed, depth, room, draw) — event slots do not shift it.
    const auto candidatesA = game::PeekRuneCandidates(bootA.registry);
    const auto candidatesB = game::PeekRuneCandidates(bootB.registry);
    CHECK(!candidatesA.empty() && candidatesA == candidatesB);

    // Same combat scale for the same ordinal.
    CHECK(EnemySheets(bootA, roomA) == EnemySheets(bootB, roomB));
}

// ------------------------------------------------------------------------------------------------
// cycle scale (D-19)
// ------------------------------------------------------------------------------------------------

void CycleScalesFromRules() {
    const auto ship = LoadSequence(ReadJsonFile("assets/data/dungeon.json"));
    const int total = ship.TotalDungeons();

    FBoot boot(UniquePath("sequence-cycle"), ShipAssetRoots());
    game::StartRun(boot.registry, kSeed, true);
    auto& run = boot.run();

    // Cycle 0 (still inside the first loop): no scaling.
    CHECK(game::CycleScaleFor(boot.registry).healthPct == 0);
    CHECK(game::CycleScaleFor(boot.registry).attackPct == 0);

    // One full loop: the rule transcription of the table.
    run.depth = total + 1;
    run.cleared = total;
    CHECK(ship.RunCycle(run.cleared) == 1);
    const auto oneLoop = game::CycleScaleFor(boot.registry);
    const auto expectedOne = ExpectedCycleScale(ship, total);
    CHECK(oneLoop.healthPct == expectedOne.healthPct && oneLoop.attackPct == expectedOne.attackPct);
    if (!ship.cycles.empty()) {
        CHECK(oneLoop.healthPct == ship.cycles.front().healthPct);  // the first loop reads the first row
    }

    // Two loops: the last row repeats (a one-row table reads as 2x).
    run.depth = 2 * total + 1;
    run.cleared = 2 * total;
    const auto twoLoops = game::CycleScaleFor(boot.registry);
    const auto expectedTwo = ExpectedCycleScale(ship, 2 * total);
    CHECK(twoLoops.healthPct == expectedTwo.healthPct && twoLoops.attackPct == expectedTwo.attackPct);
    if (ship.cycles.size() == 1) {
        CHECK(twoLoops.healthPct == 2 * ship.cycles.front().healthPct);
    }

    // The consumption point: FCombatSheet ③ adds the cycle term on top of the (capped) depth term.
    run.depth = total + 1;
    run.cleared = total;
    const auto document = game::BuildDungeonBattlefield(boot.registry);
    game::FScene scene(boot.registry, "SequenceTest");
    scene.Load();
    const auto battlefield = game::FEntityFactory::CreateBattlefield(scene, document);
    CHECK(battlefield.has_value());
    const auto& content = boot.registry.ctx().get<game::gameplay::FContentRegistry>();
    CHECK(game::SetupCombatUnits(boot.registry, *battlefield, content));
    const int healthPerDepth = game::DungeonBalance(boot.registry, "enemy_health_per_depth");
    const int attackPerDepth = game::DungeonBalance(boot.registry, "enemy_attack_per_depth");
    const int extra = std::min(run.depth - 1, 100);  // the FCombatSheet depth cap
    const auto cycle = ExpectedCycleScale(ship, run.cleared);
    bool sawEnemy = false;
    for (const auto entity : boot.registry.view<game::FBaseCombatStats>()) {
        if (boot.registry.all_of<game::FPartyMember>(entity)) continue;
        const game::FBaseCombatStats base = boot.registry.get<game::FBaseCombatStats>(entity);
        game::RefreshCombatSheet(boot.registry, entity, content);
        const int beforeCycleHealth = base.maxHealth + extra * healthPerDepth + run.room * 5;
        CHECK(boot.registry.get<game::FHealth>(entity).max ==
              beforeCycleHealth + beforeCycleHealth * cycle.healthPct / 100);
        const int beforeCycleAttack = base.stats.attack + extra * attackPerDepth + run.room;
        CHECK(boot.registry.get<game::FStats>(entity).attack ==
              beforeCycleAttack + beforeCycleAttack * cycle.attackPct / 100);
        sawEnemy = true;
    }
    CHECK(sawEnemy);
    CHECK(cycle.healthPct > 0 && cycle.attackPct > 0);  // the ship table really scales (otherwise vacuous)

    // An empty table is the rollback lever: cycle 1 with no rows scales nothing.
    {
        FTempAssets assets("sequence-cycle-empty");
        assets.CopyGameplayData();
        auto document = FixtureDocument();
        document["cycles"] = json::array();
        assets.Write("dungeon.json", document);
        FBoot emptyBoot(UniquePath("sequence-cycle-empty-run"), {assets.root.string(), kShipAssetRoot});
        game::StartRun(emptyBoot.registry, kSeed, true);
        emptyBoot.run().depth = total + 1;
        emptyBoot.run().cleared = total;
        const auto emptyScale = game::CycleScaleFor(emptyBoot.registry);
        CHECK(emptyScale.healthPct == 0 && emptyScale.attackPct == 0);
    }
}

// ------------------------------------------------------------------------------------------------
// start support / low starts (D-7, D-21)
// ------------------------------------------------------------------------------------------------

void StartAtLowerDungeon() {
    FBoot boot(UniquePath("sequence-start-lower"));
    auto& checkpoint = boot.registry.ctx().get<game::FDungeonCheckpoint>();
    checkpoint.cleared = 4;
    const auto* sequence = game::FindRunSequence(boot.registry);
    CHECK(sequence != nullptr);
    // The expectation is computed from the authored fields, never through the production helper the
    // implementation calls. The ship data must make the support observable at index 2 — otherwise the
    // equality below would hold vacuously (0 == 0).
    const int perDungeon = sequence->startSupport.materialsPerDungeon;
    const int cap = sequence->startSupport.materialsCap;
    const auto support = [perDungeon, cap](int index) { return std::min(index * perDungeon, cap); };
    CHECK(perDungeon > 0 && cap > 0);
    CHECK(support(2) > 0);

    game::StartRunAt(boot.registry, kSeed, 2);
    {
        const auto& run = boot.run();
        CHECK(run.seed == kSeed && run.depth == 3 && run.cleared == 2);
        CHECK(run.materials == support(2));
        CHECK(run.phase == game::EDungeonPhase::Battle && run.room == 0);
    }

    // Deeper than the checkpoint: clamped to it (D-21 — the checkpoint bounds the choice).
    game::StartRunAt(boot.registry, kSeed, 99);
    {
        const auto& run = boot.run();
        CHECK(run.depth == 5 && run.cleared == 4);
        CHECK(run.materials == support(4));
    }
    CHECK(checkpoint.cleared == 4);  // the checkpoint never goes down

    // StartRun(seed, fresh=false) is exactly the checkpoint start.
    game::StartRun(boot.registry, kSeed, false);
    CHECK(boot.run().depth == 5 && boot.run().cleared == 4);

    // A fresh start ignores the checkpoint and grants no support.
    game::StartRunAt(boot.registry, kSeed, 0);
    CHECK(boot.run().depth == 1 && boot.run().cleared == 0);
    CHECK(boot.run().materials == 0);
}

void StartSupportAppliedAtDepth() {
    // (a) With pity on, a start index > 0 seeds the pity streak so the first monster room is forced.
    {
        FTempAssets assets("sequence-support");
        assets.CopyGameplayData();
        auto rewardDocument = ReadJsonFile(FixtureDataPath("rewards.json"));
        rewardDocument["rune_offer"]["chance"] = 0.0;  // a plain roll can never open an offer
        rewardDocument["rune_offer"]["pity_threshold"] = 2;
        assets.Write("rewards.json", rewardDocument);
        FBoot boot(UniquePath("sequence-support-run"), {assets.root.string(), kShipAssetRoot});
        auto& checkpoint = boot.registry.ctx().get<game::FDungeonCheckpoint>();
        checkpoint.cleared = 2;
        const auto* sequence = game::FindRunSequence(boot.registry);
        CHECK(sequence != nullptr);
        CHECK(sequence->startSupport.runeOffers == 1);  // D-7 is on in the shipped data
        // Computed from the authored fields (not the production helper): the materials rule is
        // min(index x per-dungeon, cap).
        const int supportMaterials = std::min(
            2 * sequence->startSupport.materialsPerDungeon, sequence->startSupport.materialsCap);
        CHECK(supportMaterials > 0);

        game::StartRunAt(boot.registry, kSeed, 2);
        CHECK(boot.run().materials == supportMaterials);
        const auto& rewards = boot.rewards();
        CHECK(rewards.tables.pityThreshold > 0);
        CHECK(rewards.pity == rewards.tables.pityThreshold);  // the force is armed by the start support

        // The armed streak really forces the next monster room, although chance is 0.
        CHECK(game::ResolveRoomVictory(boot.registry, true).has_value());
        CHECK(boot.rewards().offer.active);

        // A fresh start neither grants materials nor arms pity.
        game::StartRun(boot.registry, kSeed + 1, true);
        CHECK(boot.run().materials == 0);
        CHECK(boot.rewards().pity == 0);
    }

    // (b) Pity off: the support cannot force the offer — it stays off instead of silently doing nothing.
    {
        FBoot boot(UniquePath("sequence-support-off"));
        auto& checkpoint = boot.registry.ctx().get<game::FDungeonCheckpoint>();
        checkpoint.cleared = 2;
        FLogCapture capture;
        game::StartRunAt(boot.registry, kSeed, 2);
        const auto* sequence = game::FindRunSequence(boot.registry);
        CHECK(sequence != nullptr);
        CHECK(boot.rewards().tables.pityThreshold == 0);
        CHECK(boot.rewards().pity == 0);  // nothing was forced
        CHECK(boot.run().materials ==
              std::min(2 * sequence->startSupport.materialsPerDungeon, sequence->startSupport.materialsCap));
        CHECK(capture.Text().find("cannot be forced") != std::string::npos);
    }
}

// ------------------------------------------------------------------------------------------------
// fixture / flags
// ------------------------------------------------------------------------------------------------

void FixtureDungeonV2Loads() {
    FBoot boot(UniquePath("sequence-fixture-v2"));
    const auto* sequence = game::FindRunSequence(boot.registry);
    CHECK(sequence != nullptr);
    CHECK(sequence->themes.size() >= 2);
    for (const auto& theme : sequence->themes) CHECK(!theme.dungeons.empty());
    CHECK(sequence->TotalDungeons() >= 2);
    CHECK(boot.registry.ctx().get<game::FDungeonBootProbe>().tuples >= 1);
}

void EndOnFinalBossFlagDefaultsFalse() {
    const auto fixture = FixtureDocument();
    std::string error;
    CHECK(game::FRunSequence::Load(fixture, error).has_value());  // authored false
    auto withoutRun = fixture;
    withoutRun.erase("run");
    CHECK(game::FRunSequence::Load(withoutRun, error).has_value());  // missing block = default false
    auto withoutFlag = fixture;
    withoutFlag["run"].erase("end_on_final_boss");
    CHECK(game::FRunSequence::Load(withoutFlag, error).has_value());  // missing key = default false
}

void EndOnFinalBossTrueRejectedUntilConsumer() {
    const auto fixture = FixtureDocument();
    auto enabled = fixture;
    enabled["run"]["end_on_final_boss"] = true;
    std::string error;
    CHECK(!game::FRunSequence::Load(enabled, error).has_value());
    CHECK(error.find("WP-19b") != std::string::npos);  // the refusal names the missing consumer
    CHECK(!BootsDocument(enabled, "sequence-end-flag"));

    auto broken = fixture;
    broken["run"]["end_on_final_boss"] = "yes";
    CHECK(!game::FRunSequence::Load(broken, error).has_value());
}

// ------------------------------------------------------------------------------------------------
// autoplay: the first boss is dungeon 3's mid boss
// ------------------------------------------------------------------------------------------------

void AutoCompleteSeesFirstBossAtDungeonThree() {
    // The three seeds really play from dungeon 1 through dungeon 3's mid boss. Two test-local inputs
    // shorten the walk without skipping any of it:
    //  - the sequence comes from the shipped document (byte-identical, including the d3 `boss:"mid"` slot);
    //    only `generation.enemy_count` narrows to 1 monster per monster room — encounter composition is
    //    WP-14's subject, and the shipped 3-4 monsters put this autoplay far over the suite's time budget
    //    in a debug build. Boss rooms are unaffected: the generator always fields the boss + 2.
    //  - gameplay content comes from the frozen fixture, so a future ship balance change cannot silently
    //    move this test's runtime.
    FTempAssets assets("sequence-autoplay");
    assets.CopyGameplayData();
    auto document = ReadJsonFile("assets/data/dungeon.json");
    document["generation"]["enemy_count"] = json::array({1, 1});
    assets.Write("dungeon.json", document);

    for (const std::uint32_t seed : {1u, 2u, 3u}) {
        FBattleHarness harness(seed, {assets.root.string(), kShipAssetRoot});
        auto& registry = harness.registry;
        auto& run = harness.run();
        const auto* sequence = game::FindRunSequence(registry);
        CHECK(sequence != nullptr);
        const std::string bossName = harness.bossDisplayName();
        CHECK(!harness.hasEnemyNamed(bossName));  // dungeon 1 starts boss-less

        bool reachedMidBoss = false;
        for (int guard = 0; guard < 400 && !(reachedMidBoss && run.phase == game::EDungeonPhase::Camp); ++guard) {
            switch (run.phase) {
            case game::EDungeonPhase::Battle: {
                const auto position = sequence->PositionOf(run.depth - 1);
                const bool bossBattle = sequence->IsBossBattle(position, run.room);
                if (run.depth <= 2) {
                    // Dungeons 1-2 have no boss slot: never the boss unit, never a boss identity.
                    CHECK(!bossBattle);
                    CHECK(!harness.hasEnemyNamed(bossName));
                }
                if (run.depth == 3 && run.room == 2) {
                    CHECK(bossBattle && sequence->BattleSlotAt(position, run.room)->boss == "mid");
                    CHECK(harness.hasEnemyNamed(bossName));
                    reachedMidBoss = true;
                }
                CHECK(harness.playBattle());
                CHECK((harness.battle.Update(registry, 0) ==
                       FStateTransition{EStateChangeKind::Push, std::string(game::kRunModalReward)}));
                break;
            }
            case game::EDungeonPhase::Reward: {
                if (registry.ctx().get<game::FRewards>().offer.active) {
                    CHECK(game::ResolveRuneOffer(registry, 0));
                }
                CHECK(game::ApplyRunIntent(registry, game::ERunIntent::ContinueReward).has_value());
                if (run.phase == game::EDungeonPhase::Battle) harness.nextRoom();
                break;
            }
            case game::EDungeonPhase::Event: {
                // Rest: the autoplay needs to survive three dungeons, and the choice is a scenario input.
                CHECK(game::ApplyRunIntent(registry, game::ERunIntent::ChooseEventRest).has_value());
                CHECK(run.phase == game::EDungeonPhase::Battle);
                harness.nextRoom();
                break;
            }
            case game::EDungeonPhase::Camp: {
                CHECK(run.cleared == run.depth);
                if (run.cleared >= 3) break;
                // Spend the camp on reinforcement and healing so the deeper dungeons stay survivable; the
                // choices are scenario inputs, the asserts stay rule computations.
                (void)game::PayDungeonCampHeal(registry);
                const auto& growth = registry.ctx().get<game::FDungeonGrowth>();
                auto& progress = registry.ctx().get<game::FCampProgress>();
                const auto& tables = registry.ctx().get<game::FCampTables>();
                for (;;) {
                    if (run.materials < game::UpgradeCostOf(tables, progress)) break;
                    bool bought = false;
                    for (std::size_t member = 0; member < run.party.size() && !bought; ++member) {
                        for (std::size_t index = 0; index < growth.statUpgrades.size(); ++index) {
                            if (!game::UpgradeDungeonStat(registry, member, index).has_value()) {
                                bought = true;
                                break;
                            }
                        }
                    }
                    if (!bought) break;
                }
                CHECK(game::ApplyRunIntent(registry, game::ERunIntent::LeaveCamp).has_value());
                CHECK(run.phase == game::EDungeonPhase::Battle);
                harness.nextRoom();
                break;
            }
            case game::EDungeonPhase::Defeat:
                CHECK(false);  // the run must survive the three dungeons at every seed
                break;
            }
        }
        CHECK(reachedMidBoss);
        CHECK(run.depth == 3 && run.cleared == 3 && run.phase == game::EDungeonPhase::Camp);
        CHECK(sequence->PositionOf(run.cleared - 1).dungeonInTheme == 2);  // dungeon 3 of theme 1
        CHECK(sequence->RunCycle(run.cleared - 1) == 0);
        std::cout << "  seed " << seed << ": first boss (dungeon 3 mid) reached, checkpoint " << run.cleared
                  << ", autoplay frames " << harness.totalSteps << '\n';
    }
}

} // namespace

int main() {
    // Feature tests get no per-test save directory from CMake: isolate every save under the build tree so a
    // run can never touch %APPDATA%\Game01P\EmberDungeon.
    const char* saveDir = nullptr;
#ifdef _MSC_VER
#pragma warning(suppress: 4996)  // std::getenv: read-only, no buffer to overflow
#endif
    saveDir = std::getenv("GAME01P_SAVE_DIR");
    if (saveDir == nullptr || *saveDir == '\0') {
        const std::string path =
            (std::filesystem::current_path() / "out" / "test-saves" / "dungeon_sequence_tests").string();
        std::error_code error;
        std::filesystem::create_directories(path, error);
#ifdef _WIN32
        _putenv_s("GAME01P_SAVE_DIR", path.c_str());
#else
        setenv("GAME01P_SAVE_DIR", path.c_str(), 1);
#endif
    }

    return game_test::RunTests("dungeon sequence", {
        {"SequencePositions", SequencePositions},
        {"MidAndFinalBoss", MidAndFinalBoss},
        {"DungeonsOneTwoHaveNoBoss", DungeonsOneTwoHaveNoBoss},
        {"MidBossAtThirdDungeon", MidBossAtThirdDungeon},
        {"FinalBossAtThemeEnd", FinalBossAtThemeEnd},
        {"RoomCountVaries", RoomCountVaries},
        {"EventSlotsOnlyInInternalGaps", EventSlotsOnlyInInternalGaps},
        {"GapAfterFirstBattle", GapAfterFirstBattle},
        {"CampAfterLastBattleOnly", CampAfterLastBattleOnly},
        {"BootProbeCoversTuples", BootProbeCoversTuples},
        {"BadFamilyFailsBoot", BadFamilyFailsBoot},
        {"OpenFamilyUnchanged", OpenFamilyUnchanged},
        {"FamiliesKeepInvariants", FamiliesKeepInvariants},
        {"FamilyStrengthRespected", FamilyStrengthRespected},
        {"FamilyMetricsLogged", FamilyMetricsLogged},
        {"FamilyMetricsDistribution", FamilyMetricsDistribution},
        {"SaveRoomRangeFollowsBattles", SaveRoomRangeFollowsBattles},
        {"OldSaveRoom2Loads", OldSaveRoom2Loads},
        {"EventGapResumeSkipsWhenMissing", EventGapResumeSkipsWhenMissing},
        {"BattleOrdinalStableAcrossEventSlots", BattleOrdinalStableAcrossEventSlots},
        {"CycleScalesFromRules", CycleScalesFromRules},
        {"StartAtLowerDungeon", StartAtLowerDungeon},
        {"StartSupportAppliedAtDepth", StartSupportAppliedAtDepth},
        {"FixtureDungeonV2Loads", FixtureDungeonV2Loads},
        {"AutoCompleteSeesFirstBossAtDungeonThree", AutoCompleteSeesFirstBossAtDungeonThree},
        {"EndOnFinalBossFlagDefaultsFalse", EndOnFinalBossFlagDefaultsFalse},
        {"EndOnFinalBossTrueRejectedUntilConsumer", EndOnFinalBossTrueRejectedUntilConsumer},
    });
}
