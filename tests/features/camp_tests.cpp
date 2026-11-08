// WP-10: the camp as a materials-allocation decision — the shared price curves, the camp rune-offer sink
// (WP-17 API reuse), paid heal / node respec, the recovery budget, the chest risk trade, the run HP clamp,
// and the `run.ext.camp` save section. Every expectation is computed from the loaded data/rules: the WP-10
// table numbers are playtest variables, so the tests assert structure, never constants.

#define GAME01P_TEST_SUPPORT_CHECK
#include "../TestSupport.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <SDL3/SDL.h>
#include <imgui.h>
#include <nlohmann/json.hpp>
#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/spdlog.h>

#include "core/EngineState.h"
#include "core/Logger.h"
#include "core/ResourceManager.h"
#include "core/SDLDeleter.h"
#include "ecs/components/FFaction.h"
#include "ecs/components/FHealth.h"
#include "gameplay/battle/FBattleSetup.h"
#include "gameplay/data/FContentRegistry.h"
#include "gameplay/dungeon/FDungeonCamp.h"
#include "gameplay/dungeon/FDungeonRun.h"
#include "gameplay/dungeon/FDungeonSave.h"
#include "gameplay/dungeon/FRewards.h"
#include "gameplay/dungeon/FRunEventLog.h"
#include "gameplay/dungeon/FRunFlow.h"
#include "gameplay/factory/FEntityDefinitions.h"
#include "gameplay/factory/FEntityFactory.h"
#include "gameplay/factory/FScene.h"
#include "gameplay/grid/FGridTerrain.h"
#include "gameplay/grid/FGridTypes.h"
#include "presentation/FDungeonArt.h"
#include "presentation/FDungeonOverlay.h"
#include "presentation/FDungeonViews.h"
#include "presentation/FModalViewRegistry.h"

namespace {

using nlohmann::json;

constexpr std::uint32_t kSeed = 4242;

/// v1 sequence: three battle rooms per dungeon (WP-08 turns the count into data). The budget tests derive
/// their income from the balance keys and this structural count, never from the WP table.
constexpr int kBattlesPerDungeon = 3;

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

std::string ReadBytes(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    CHECK(file.good());
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

void WriteJsonFile(const std::string& path, const json& document) {
    std::ofstream file(path, std::ios::trunc);
    CHECK(file.good());
    file << document.dump(2);
}

/// Captures spdlog output while alive (the camp entry report line is the subject).
struct FLogCapture {
    std::ostringstream stream;
    std::shared_ptr<spdlog::logger> logger;

    FLogCapture() {
        logger = std::make_shared<spdlog::logger>(
            "camp-test-capture", std::make_shared<spdlog::sinks::ostream_sink_mt>(stream));
        logger->set_level(spdlog::level::trace);
        SetCurrentGameLogger(logger);
    }

    ~FLogCapture() { SetCurrentGameLogger(nullptr); }

    std::string Text() const { return stream.str(); }
};

// --- a booted registry with its own checkpoint and save slot -----------------------------------

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
    game::FCampProgress& progress() { return registry.ctx().get<game::FCampProgress>(); }
    const game::FCampTables& tables() { return registry.ctx().get<game::FCampTables>(); }
    game::FRewards& rewards() { return registry.ctx().get<game::FRewards>(); }
};

// --- a temporary asset root (invalid dungeon blocks, an authored preview hint) ------------------

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

    void CopyGameplayData(bool withDungeon = false) {
        // WP-14a: else the shipped encounters.json falls through the temp root and is validated against the
        // frozen fixture units (the six new enemies are absent there) — the empty fixture table keeps these
        // boots on the legacy generation path.
        for (const char* file : {"units.json", "skills.json", "runes.json", "statuses.json", "ai.json",
                                 "encounters.json"}) {
            Copy(file);
        }
        if (withDungeon) {
            Copy("dungeon.json");
            Copy("passive-tree.json");
            Copy("bosses.json");  // WP-15: else the shipped bosses.json is validated against fixture units
        }
    }

    void Write(const std::string& file, const json& document) {
        std::ofstream out(root / "data" / file, std::ios::trunc);
        CHECK(out.good());
        out << document.dump(2);
    }
};

/// Boots content + the dungeon definition: false means a loader rejected the content.
bool BootsDungeon(const std::vector<std::string>& roots, const std::string& checkpoint) {
    game_test::FTestRegistry loaded = game_test::LoadTestRegistry(roots, checkpoint);
    return loaded.content && loaded.dungeon;
}

// --- shared steps -------------------------------------------------------------------------------

void StartFresh(FBoot& boot) {
    game::StartRun(boot.registry, kSeed, true);
}

game::FRunMember& Member(game::FDungeonRun& run, std::string_view id) {
    const auto found = std::ranges::find(run.party, id, &game::FRunMember::id);
    CHECK(found != run.party.end());
    return *found;
}

/// A battle victory: the run is in the Reward phase and the room's offer decision has already run.
void WinRoom(FBoot& boot) {
    CHECK(game::ResolveRoomVictory(boot.registry, true).has_value());
    CHECK(boot.run().phase == game::EDungeonPhase::Reward);
}

/// Walks a fresh run to the camp through the real flow (three rooms, every offer skipped).
void WalkToCamp(FBoot& boot) {
    StartFresh(boot);
    WinRoom(boot);
    CHECK(game::ResolveRuneOffer(boot.registry, -1));
    CHECK(game::ApplyRunIntent(boot.registry, game::ERunIntent::ContinueReward).has_value());  // -> Event
    CHECK(game::ApplyRunIntent(boot.registry, game::ERunIntent::ChooseEventLoot).has_value());  // -> room 1
    WinRoom(boot);
    CHECK(game::ResolveRuneOffer(boot.registry, -1));
    CHECK(game::ApplyRunIntent(boot.registry, game::ERunIntent::ContinueReward).has_value());  // -> room 2
    WinRoom(boot);
    CHECK(game::ResolveRuneOffer(boot.registry, -1));
    CHECK(game::ApplyRunIntent(boot.registry, game::ERunIntent::ContinueReward).has_value());  // -> Camp
    CHECK(boot.run().phase == game::EDungeonPhase::Camp);
}

int DungeonIncome(FBoot& boot) {
    return game::DungeonBalance(boot.registry, "room_materials") * kBattlesPerDungeon +
           game::DungeonBalance(boot.registry, "event_materials");
}

/// The five reference prices at an arbitrary curve state (production step functions on loaded data).
game::FCampPrices PricesAt(const game::FCampTables& tables, const game::FDungeonRecovery& recovery,
                           int upgrades, int draws) {
    game::FCampPrices prices;
    prices.upgrade = game::UpgradeStepCost(tables, static_cast<std::uint32_t>(upgrades));
    prices.nextUpgrade = game::UpgradeStepCost(tables, static_cast<std::uint32_t>(upgrades + 1));
    prices.heal = recovery.campHealCost;
    prices.respec = tables.respecCost;
    prices.offer = game::RuneDrawStepCost(tables, static_cast<std::uint32_t>(draws));
    return prices;
}

struct FReferenceCamp {
    int held{0};          // materials at the camp's entry (after the dungeon's income)
    int upgrades{0};
    int draws{0};
    int basket{0};
    int cheapestTwo{0};
    int spent{0};
};

/// Simulates the WP-10 §2 reference path (every event room takes the cache): the price of the whole
/// basket, the two cheapest services and the simulated purchases at each camp. The purchase *choices* are
/// scenario inputs; the asserted facts are the invariants computed through the production price rules.
std::vector<FReferenceCamp> ReferenceCampWalk(const game::FCampTables& tables,
                                              const game::FDungeonRecovery& recovery, int income, int camps) {
    struct FBuys {
        int upgrades;
        int draws;
        bool heal;
    };
    static constexpr FBuys kPath[] = {
        {2, 0, true}, {1, 0, true}, {1, 0, false}, {1, 1, false},
        {1, 0, false}, {0, 1, true}, {0, 1, false}, {1, 0, false},
    };
    std::vector<FReferenceCamp> walk;
    int upgrades = 0;
    int draws = 0;
    int held = 0;
    const int campsToWalk = std::min(camps, static_cast<int>(std::size(kPath)));
    for (int camp = 0; camp < campsToWalk; ++camp) {
        held += income;
        const game::FCampPrices prices = PricesAt(tables, recovery, upgrades, draws);
        FReferenceCamp entry;
        entry.held = held;
        entry.upgrades = upgrades;
        entry.draws = draws;
        entry.basket = game::CampBasketTotal(prices);
        entry.cheapestTwo = game::CampCheapestTwo(prices);
        int spent = 0;
        for (int step = 0; step < kPath[camp].upgrades; ++step) {
            spent += game::UpgradeStepCost(tables, static_cast<std::uint32_t>(upgrades + step));
        }
        if (kPath[camp].heal) spent += recovery.campHealCost;
        for (int step = 0; step < kPath[camp].draws; ++step) {
            spent += game::RuneDrawStepCost(tables, static_cast<std::uint32_t>(draws + step));
        }
        entry.spent = spent;
        upgrades += kPath[camp].upgrades;
        draws += kPath[camp].draws;
        held -= spent;
        walk.push_back(entry);
    }
    return walk;
}

// --- UI capture (the event/camp labels are read from the real views) ---------------------------

/// Fields the run party like FEmberBattleState::OnEnter's combat section (the scene stays alive in the
/// returned holder for the caller's scope). Used by the tests that need real fielded party entities.
std::unique_ptr<game::FScene> FieldParty(FBoot& boot) {
    auto& registry = boot.registry;
    const auto document = game::BuildDungeonBattlefield(registry);
    auto scene = std::make_unique<game::FScene>(registry, "CampTest");
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
    return scene;
}

entt::entity FindEntity(entt::registry& registry, std::string_view id) {
    for (const auto entity : registry.view<game::FPartyMember, game::FUnitData>()) {
        if (registry.get<game::FUnitData>(entity).definition.id == id) return entity;
    }
    return entt::null;
}

std::vector<std::string> RenderModalLogs(entt::registry& registry, const std::vector<std::string>& modalIds) {
    std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> surface(
        SDL_CreateSurface(1280, 800, SDL_PIXELFORMAT_RGBA8888), &SDL_DestroySurface);
    CHECK(surface != nullptr);
    FRendererPtr renderer{SDL_CreateSoftwareRenderer(surface.get())};
    CHECK(renderer != nullptr);

    auto* window = registry.ctx().find<game::FEngineWindowState>();
    if (window == nullptr) window = &registry.ctx().emplace<game::FEngineWindowState>();
    window->width = 1280;
    window->height = 800;
    auto* resources = registry.ctx().find<game::FResourceManager>();
    if (resources == nullptr) resources = &registry.ctx().emplace<game::FResourceManager>();
    resources->init(renderer.get());
    CHECK(game::InitializeDungeonArt(registry));

    std::unique_ptr<ImGuiContext, decltype(&ImGui::DestroyContext)> context(
        ImGui::CreateContext(), &ImGui::DestroyContext);
    auto& io = ImGui::GetIO();
    io.DisplaySize = {1280, 800};
    io.DeltaTime = 1.f / 60.f;
    io.Fonts->Build();
    game::InitializeDungeonUi(registry);

    std::vector<std::string> captured;
    for (const auto& modalId : modalIds) {
        const std::string logPath = UniquePath("camp-ui") + ".log";
        for (int frame = 0; frame < 3; ++frame) {
            ImGui::NewFrame();
            if (frame == 2) ImGui::LogToFile(0, logPath.c_str());
            const auto* view = game::FindModalView(registry, modalId);
            CHECK(view != nullptr);
            view->render(registry);
            if (frame == 2) ImGui::LogFinish();
            ImGui::Render();
        }
        captured.push_back(ReadBytes(logPath));
        std::error_code error;
        std::filesystem::remove(logPath, error);
    }
    return captured;
}

// ---------------------------------------------------------------------------------------------
// The budget (WP-10 §2)
// ---------------------------------------------------------------------------------------------

void BudgetFromRules() {
    FBoot boot(UniquePath("camp-budget-rules"));
    const game::FCampTables& tables = boot.tables();
    const game::FDungeonRecovery& recovery = game::DungeonRecovery(boot.registry);
    const auto walk = ReferenceCampWalk(tables, recovery, DungeonIncome(boot), 8);
    CHECK(walk.size() == 8u);
    for (std::size_t camp = 0; camp < walk.size(); ++camp) {
        const FReferenceCamp& state = walk[camp];
        std::cout << "  C" << (camp + 1) << " held " << state.held << " basket " << state.basket
                  << " cheapest_two " << state.cheapestTwo << " spent " << state.spent << '\n';
        // The structure the gate measures: the whole basket is out of reach, the two cheapest are not.
        CHECK(state.basket > state.held);
        CHECK(state.held >= state.cheapestTwo);
        CHECK(state.held >= state.spent);
    }
}

void CampBudgetForcesAllocation() {
    FBoot boot(UniquePath("camp-budget-allocation"));
    StartFresh(boot);
    const game::FCampTables& tables = boot.tables();
    const game::FDungeonRecovery& recovery = game::DungeonRecovery(boot.registry);
    const auto walk = ReferenceCampWalk(tables, recovery, DungeonIncome(boot), 4);
    CHECK(walk.size() == 4u);
    const FReferenceCamp& camp4 = walk[3];

    auto& run = boot.run();
    run.phase = game::EDungeonPhase::Camp;
    run.materials = camp4.held;
    boot.progress().upgradesTotal = static_cast<std::uint32_t>(camp4.upgrades);
    boot.progress().runeDrawsTotal = static_cast<std::uint32_t>(camp4.draws);
    boot.progress().campDepth = static_cast<std::uint32_t>(run.depth);
    boot.progress().runeDrawsThisCamp = 0;
    for (auto& member : run.party) member.health = 1;   // someone to heal (the floor: the chest rule's min)
    Member(run, "party.warrior").allocatedNodes = {"node.might"};
    Member(run, "party.warrior").passivePoints = 0;

    const game::FCampPrices prices = game::CampPricesOf(boot.registry);
    CHECK(game::CampBasketTotal(prices) == camp4.basket);
    CHECK(game::CampCheapestTwo(prices) == camp4.cheapestTwo);
    CHECK(camp4.basket > run.materials);           // buying everything at once is impossible
    CHECK(run.materials >= camp4.cheapestTwo);     // the two cheapest stay reachable

    // And the two cheapest services really are buyable at this state (heal + the camp rune offer here).
    const game::FCampSession session = game::BuildCampSession(boot.registry);
    const int before = run.materials;
    CHECK(!game::PayDungeonCampHeal(boot.registry).has_value());
    CHECK(!game::PurchaseCampRuneOffer(boot.registry).has_value());
    CHECK(run.materials == before - session.paidHealCost - session.runeOfferCost);
    CHECK(run.materials >= 0);
    CHECK(game::CampBasketTotal(prices) > run.materials);  // the whole basket is still out of reach
    std::cout << "  camp 4: held " << before << " (heal " << session.paidHealCost << " + offer "
              << session.runeOfferCost << ") left " << run.materials << '\n';
}

void ShrinePathTradesBuyingPower() {
    FBoot boot(UniquePath("camp-shrine-trade"));
    const game::FCampTables& tables = boot.tables();
    const game::FDungeonRecovery& recovery = game::DungeonRecovery(boot.registry);
    const int boxIncome = DungeonIncome(boot);
    const int shrineIncome =
        game::DungeonBalance(boot.registry, "room_materials") * kBattlesPerDungeon;  // the shrine heals
    CHECK(shrineIncome < boxIncome);  // structural: the cache pays, the shrine heals
    const game::FCampPrices prices = PricesAt(tables, recovery, 6, 1);  // the WP-10 §2 C6 state
    const int cheapestTwo = game::CampCheapestTwo(prices);
    std::cout << "  shrine income " << shrineIncome << " vs cheapest two at C6 " << cheapestTwo
              << " (deficit " << cheapestTwo - shrineIncome << ")\n";
    // Report metric, not a gate (WP-10 §2): the shrine path's shorter balance is the intended
    // materials-vs-healing trade; the reference-path invariant is asserted in BudgetFromRules only.
    CHECK(cheapestTwo > 0);
}

// --- the recovery budget (WP-10 §3 / D-18) ------------------------------------------------------

void KoFractionFromData() {
    FBoot boot(UniquePath("camp-ko-fraction"));
    // The value really is the authored data: the recovery struct initialises to zero and carries no copy.
    const double fraction = game::DungeonRecovery(boot.registry).koRecoverFraction;
    CHECK(game::DungeonDefinition(boot.registry)["recovery"]["ko_recover_fraction"].get<double>() == fraction);
    const auto expected = [fraction](int maxHealth) {
        return std::max(1, static_cast<int>(std::floor(static_cast<double>(maxHealth) * fraction)));
    };
    CHECK(game::KoRecoverFloor(140, fraction) == expected(140));
    CHECK(game::KoRecoverFloor(100, fraction) == expected(100));
    CHECK(game::KoRecoverFloor(90, fraction) == expected(90));
    // The floor never drops below 1: an empty bar would be a soft KO.
    CHECK(game::KoRecoverFloor(1, fraction) == 1);
    CHECK(game::KoRecoverFloor(0, fraction) == 1);
    std::cout << "  ko floor: 140->" << game::KoRecoverFloor(140, fraction) << " 100->"
              << game::KoRecoverFloor(100, fraction) << " 90->" << game::KoRecoverFloor(90, fraction) << '\n';

    // End to end on a fielded entity: CaptureRunParty brings a 1 HP member up to the data floor.
    StartFresh(boot);
    auto scene = FieldParty(boot);
    const entt::entity warrior = FindEntity(boot.registry, "party.warrior");
    CHECK(warrior != entt::null);
    auto& health = boot.registry.get<game::FHealth>(warrior);
    health.current = 1;
    game::CaptureRunParty(boot.registry);
    const int floorValue = game::KoRecoverFloor(health.max, fraction);
    CHECK(floorValue > 1);  // the case only proves the floor engaged while 1 sits below it
    CHECK(Member(boot.run(), "party.warrior").health == floorValue);
    (void)scene;
}

void AttritionBudgetPerUnit() {
    FBoot boot(UniquePath("camp-attrition"));
    StartFresh(boot);
    const game::FDungeonRecovery& recovery = game::DungeonRecovery(boot.registry);
    const int freeBudget =
        game::DungeonBalance(boot.registry, "room_heal") * kBattlesPerDungeon + recovery.campAutoHeal;
    // Per unit u: room_heal x battles + camp_auto <= free_heal_cap_fraction x max HP(u). Free means
    // non-optional, unconditional healing — the shrine (a choice) and the KO floor (damage-dependent)
    // stay out of the invariant and are report metrics.
    for (const auto& member : boot.run().party) {
        const int maxHealth = game::DungeonMemberMaxHealth(boot.registry, member);
        std::cout << "  " << member.id << " free budget " << freeBudget << " <= "
                  << static_cast<int>(recovery.freeHealCapFraction * maxHealth) << " (max HP " << maxHealth << ")\n";
        CHECK(static_cast<double>(freeBudget) <= recovery.freeHealCapFraction * maxHealth);
    }
}

// --- run HP rules and the chest trade (WP-10 §4) ------------------------------------------------

void RunHpClampedOnHeal() {
    FBoot boot(UniquePath("camp-hp-clamp"));
    StartFresh(boot);
    auto& run = boot.run();
    const int roomHeal = game::DungeonBalance(boot.registry, "room_heal");
    CHECK(roomHeal > 2);  // the clamp is only observable when the heal could overshoot
    for (auto& member : run.party) {
        member.health = game::DungeonMemberMaxHealth(boot.registry, member) - 2;
    }
    CHECK(game::ResolveRoomVictory(boot.registry, true).has_value());
    for (const auto& member : run.party) {
        const int maxHealth = game::DungeonMemberMaxHealth(boot.registry, member);
        CHECK(member.health == std::min(maxHealth - 2 + roomHeal, maxHealth));
        CHECK(member.health == maxHealth);  // room heal 4 > deficit 2: clamped exactly at the max
    }
}

void MemberSentinelSkipsHeal() {
    FBoot boot(UniquePath("camp-hp-sentinel"));
    StartFresh(boot);
    auto& run = boot.run();
    const int roomHeal = game::DungeonBalance(boot.registry, "room_heal");
    run.party[0].health = -1;  // unclaimed = full
    run.party[1].health = 10;
    CHECK(game::ResolveRoomVictory(boot.registry, true).has_value());
    CHECK(run.party[0].health == -1);  // the room heal never materialises the sentinel
    CHECK(run.party[1].health == std::min(10 + roomHeal, game::DungeonMemberMaxHealth(boot.registry, run.party[1])));

    // The paid camp heal skips it too, and paying is the only way to heal outside the free entries.
    run.phase = game::EDungeonPhase::Camp;
    run.materials = 100;
    run.party[2].health = 1;
    CHECK(!game::PayDungeonCampHeal(boot.registry).has_value());
    CHECK(run.party[0].health == -1);
}

void UnknownMemberIsNotClamped() {
    FBoot boot(UniquePath("camp-unknown-member"));
    StartFresh(boot);
    auto& run = boot.run();
    // A member whose unit template is missing: its max HP is unknown (logged once) — never 0 + bonuses.
    run.party.push_back(game::FRunMember{.id = "party.ghost", .displayName = "Ghost", .health = 50});
    CHECK(game::DungeonMemberMaxHealth(boot.registry, run.party.back()) <= 0);
    CHECK(game::ChestHpLossFor(boot.registry, run.party.back()) == 0);  // no cost against an unknown max
    const int roomHeal = game::DungeonBalance(boot.registry, "room_heal");
    CHECK(game::ResolveRoomVictory(boot.registry, true).has_value());
    CHECK(run.party.back().health == 50 + roomHeal);  // the add-only heal, not a clamp toward 0
    // The paid heal skips it too: with everyone else a full sentinel it refuses instead of charging.
    run.phase = game::EDungeonPhase::Camp;
    run.materials = 100;
    for (std::size_t index = 0; index + 1 < run.party.size(); ++index) run.party[index].health = -1;
    CHECK(game::ValidateDungeonCampHeal(boot.registry).has_value());
    CHECK(run.materials == 100);
}

void ChestCostNeverKills() {
    FBoot boot(UniquePath("camp-chest-floor"));
    StartFresh(boot);
    auto& run = boot.run();
    const double fraction = game::DungeonRecovery(boot.registry).eventChestHpCostFraction;
    for (auto& member : run.party) member.health = 1;  // everyone at the 1 HP floor
    for (const auto& member : run.party) CHECK(game::ChestHpLossFor(boot.registry, member) == 0);
    CHECK(game::ApplyChestHpCost(boot.registry) == 0);
    for (const auto& member : run.party) CHECK(member.health == 1);  // cost 0 applied, nobody dies

    // A full member pays the data fraction (capped at maxHp - 1) and stays alive.
    const int maxHealth = game::DungeonMemberMaxHealth(boot.registry, run.party[1]);
    run.party[1].health = maxHealth;
    const int loss = game::ChestHpLossFor(boot.registry, run.party[1]);
    CHECK(loss == std::min(static_cast<int>(std::floor(static_cast<double>(maxHealth) * fraction)), maxHealth - 1));
    CHECK(loss >= 0);
    CHECK(game::ApplyChestHpCost(boot.registry) == loss);
    CHECK(run.party[1].health == maxHealth - loss);
    CHECK(run.party[1].health >= 1);
}

void ChestChoiceCostsHp() {
    FBoot boot(UniquePath("camp-chest-choice"));
    StartFresh(boot);
    WinRoom(boot);
    CHECK(game::ResolveRuneOffer(boot.registry, -1));
    CHECK(game::ApplyRunIntent(boot.registry, game::ERunIntent::ContinueReward).has_value());
    auto& run = boot.run();
    CHECK(run.phase == game::EDungeonPhase::Event);

    // Deterministic HP: member 0 sits at the 1 HP floor (it pays nothing), the rest are full.
    for (std::size_t index = 0; index < run.party.size(); ++index) {
        run.party[index].health =
            index == 0 ? 1 : game::DungeonMemberMaxHealth(boot.registry, run.party[index]);
    }
    std::vector<int> losses;
    for (const auto& member : run.party) losses.push_back(game::ChestHpLossFor(boot.registry, member));
    CHECK(losses[0] == 0);

    // The choice states every member's loss before it is taken (the same helper the flow applies).
    const auto logs = RenderModalLogs(boot.registry, {std::string(game::kRunModalEvent)});
    CHECK(logs.size() == 1u);
    CHECK(logs[0].find("costs party HP") != std::string::npos);
    CHECK(logs[0].find("HP cost:") != std::string::npos);
    for (std::size_t index = 0; index < run.party.size(); ++index) {
        CHECK(logs[0].find(game::MemberName(run.party[index]) + " " + std::to_string(losses[index])) !=
              std::string::npos);
    }

    const int materials = run.materials;
    CHECK(game::ApplyRunIntent(boot.registry, game::ERunIntent::ChooseEventLoot).has_value());
    CHECK(run.materials == materials + game::DungeonBalance(boot.registry, "event_materials"));
    for (std::size_t index = 0; index < run.party.size(); ++index) {
        const int before =
            index == 0 ? 1 : game::DungeonMemberMaxHealth(boot.registry, run.party[index]);
        CHECK(run.party[index].health == before - losses[index]);  // displayed loss == applied loss
    }
    CHECK(run.message.find("HP -") != std::string::npos);  // the result wording states the cost
}

void RunMaxHpMatchesCombatSheet() {
    FBoot boot(UniquePath("camp-maxhp"));
    StartFresh(boot);
    auto& registry = boot.registry;
    auto& run = boot.run();
    auto scene = FieldParty(boot);  // the fielded entities live for this test's scope

    // A bought HP bonus and a passive node must flow into both derivations identically.
    auto& member = Member(run, "party.warrior");
    member.bonusHealth = 9;
    member.allocatedNodes = {"node.vitality"};
    game::ApplyRunParty(registry);
    const entt::entity warrior = FindEntity(registry, "party.warrior");
    CHECK(warrior != entt::null);
    CHECK(registry.get<game::FHealth>(warrior).max == game::DungeonMemberMaxHealth(registry, member));
    (void)scene;
}

// --- the camp counters and the save section (WP-10 §1) ------------------------------------------

void CampSectionRoundTrip() {
    FBoot boot(UniquePath("camp-section-roundtrip"));
    StartFresh(boot);
    // The save parser requires cleared == depth - 1 (or == depth in Camp): keep the document coherent.
    boot.run().depth = 2;
    boot.run().cleared = 1;
    boot.progress().upgradesTotal = 3;
    boot.progress().runeDrawsTotal = 1;
    boot.progress().campDepth = 2;
    boot.progress().runeDrawsThisCamp = 1;
    CHECK(game::SaveDungeonRun(boot.registry));

    // The unpack must run before the run exists (the continue path unpacks sections first).
    boot.registry.ctx().erase<game::FDungeonRun>();
    CHECK(game::ContinueDungeonRun(boot.registry));
    const game::FCampProgress& restored = boot.progress();
    CHECK(restored.upgradesTotal == 3u);
    CHECK(restored.runeDrawsTotal == 1u);
    CHECK(restored.campDepth == 2u);
    CHECK(restored.runeDrawsThisCamp == 1u);
    CHECK(boot.run().depth == 2);
}

void CampDepthResetsThisCamp() {
    FBoot boot(UniquePath("camp-depth-reset"));
    StartFresh(boot);
    auto& run = boot.run();
    auto& progress = boot.progress();
    progress.campDepth = 1;
    progress.runeDrawsThisCamp = 2;
    progress.runeDrawsTotal = 2;
    run.depth = 1;
    CHECK(game::CampRuneDrawsThisCamp(progress, 1) == 2u);
    CHECK(game::BuildCampSession(boot.registry).runeDrawsThisCamp == 2u);
    CHECK(!game::BuildCampSession(boot.registry).runeOfferDiscounted);  // this camp's special is spent
    // Leaving bumps the depth: the next camp reads zero without a reset write.
    run.depth = 2;
    CHECK(game::CampRuneDrawsThisCamp(progress, 2) == 0u);
    CHECK(game::BuildCampSession(boot.registry).runeOfferDiscounted);
    CHECK(progress.runeDrawsThisCamp == 2u);  // the stored value is untouched (derived reset)
}

void UpgradesTotalCountsPurchases() {
    FBoot boot(UniquePath("camp-upgrades-total"));
    StartFresh(boot);
    auto& run = boot.run();
    auto& progress = boot.progress();
    const game::FCampTables& tables = boot.tables();
    run.phase = game::EDungeonPhase::Camp;
    run.materials = 100;
    CHECK(progress.upgradesTotal == 0u);
    const int firstCost = game::UpgradeCostOf(tables, progress);
    CHECK(!game::UpgradeDungeonStat(boot.registry, 0, 0).has_value());
    CHECK(progress.upgradesTotal == 1u);
    CHECK(run.materials == 100 - firstCost);
    CHECK(run.party[0].bonusAttack == 1);
    CHECK(game::UpgradeCostOf(tables, progress) == game::UpgradeStepCost(tables, 1u));
    // The price follows the stored counter, never the bonus fields (a reward effect may move those).
    run.party[0].bonusAttack += 5;
    CHECK(game::UpgradeCostOf(tables, progress) == game::UpgradeStepCost(tables, 1u));
}

void CampDrawCapPersists() {
    FBoot boot(UniquePath("camp-draw-cap"));
    StartFresh(boot);
    auto& run = boot.run();
    run.phase = game::EDungeonPhase::Camp;
    run.materials = 100;
    const int cap = boot.tables().runeOfferCampCap;
    CHECK(cap >= 1);  // the fixture enables the sink; cap 0 is the documented rollback
    for (int draw = 0; draw < cap; ++draw) {
        CHECK(!game::PurchaseCampRuneOffer(boot.registry).has_value());
        CHECK(game::ResolveRuneOffer(boot.registry, -1));
    }
    CHECK(boot.progress().runeDrawsTotal == static_cast<std::uint32_t>(cap));
    CHECK(boot.progress().runeDrawsThisCamp == static_cast<std::uint32_t>(cap));
    const auto problem = game::ValidateCampRuneOfferPurchase(boot.registry);
    CHECK(problem.has_value());
    const int materials = run.materials;
    CHECK(game::PurchaseCampRuneOffer(boot.registry).has_value());  // refused with a reason
    CHECK(run.materials == materials);                             // and no charge

    // The curve index and the per-camp cap survive save/continue.
    CHECK(game::SaveDungeonRun(boot.registry));
    boot.registry.ctx().erase<game::FDungeonRun>();
    CHECK(game::ContinueDungeonRun(boot.registry));
    const game::FCampProgress& restored = boot.progress();
    CHECK(restored.runeDrawsTotal == static_cast<std::uint32_t>(cap));
    CHECK(restored.campDepth == 1u);
    CHECK(game::ValidateCampRuneOfferPurchase(boot.registry).has_value());  // still capped at this camp
}

void SinkPurchaseUsesOfferApi() {
    FBoot boot(UniquePath("camp-sink-api"));
    StartFresh(boot);
    auto& run = boot.run();
    auto& rewards = boot.rewards();
    run.phase = game::EDungeonPhase::Camp;
    run.materials = 100;

    const auto drawIndex = rewards.nextDrawIndex;
    const auto peek = game::PeekRuneCandidates(boot.registry);
    CHECK(peek.size() >= static_cast<std::size_t>(std::max(1, rewards.tables.minLegalCandidates)));
    const int cost = game::BuildCampSession(boot.registry).runeOfferCost;
    CHECK(!rewards.offer.active);
    CHECK(!game::PurchaseCampRuneOffer(boot.registry).has_value());
    CHECK(rewards.offer.active);
    CHECK(rewards.offer.candidates == peek);             // the offer is exactly the previewed WP-17 draw
    CHECK(rewards.nextDrawIndex == drawIndex + 1u);      // RollRuneCandidates consumed one draw index
    CHECK(run.materials == 100 - cost);
    for (const auto& candidate : rewards.offer.candidates) {
        CHECK(!game::RuneFittingSkillNames(boot.registry, candidate).empty());  // same legality gate
    }
    const auto& log = boot.registry.ctx().get<game::FRunEventLog>();
    CHECK(!log.tail.empty());
    CHECK(log.tail.back().kind == game::ERunEventKind::CampSpent);
    CHECK(log.tail.back().id == "rune_offer");
    CHECK(log.tail.back().amount == cost);
    CHECK(log.tail.back().member == -1);

    // Descending waits for the offer decision (the same guard ContinueReward has); a skip refunds.
    CHECK(!game::ApplyRunIntent(boot.registry, game::ERunIntent::LeaveCamp).has_value());
    CHECK(boot.run().depth == 1 && boot.run().phase == game::EDungeonPhase::Camp);
    const int refund = boot.rewards().tables.skipRefundMaterials;
    const int beforeSkip = boot.run().materials;
    CHECK(game::ResolveRuneOffer(boot.registry, -1));
    CHECK(boot.run().materials == beforeSkip + refund);
    CHECK(game::ApplyRunIntent(boot.registry, game::ERunIntent::LeaveCamp).has_value());
    CHECK(boot.run().depth == 2);
}

void UpgradeCurveMonotonic() {
    FBoot boot(UniquePath("camp-curve"));
    const game::FCampTables& tables = boot.tables();
    const auto monotonic = [](const std::vector<int>& steps) {
        CHECK(!steps.empty());
        for (std::size_t index = 0; index < steps.size(); ++index) {
            CHECK(steps[index] >= 1);
            if (index > 0) CHECK(steps[index] >= steps[index - 1]);
        }
    };
    monotonic(tables.upgradeCostSteps);
    monotonic(tables.runeOfferDrawCostSteps);
    // Out-of-range reads pin to the last step: the curve never wraps or resets.
    CHECK(game::UpgradeStepCost(tables, 0) == tables.upgradeCostSteps.front());
    CHECK(game::UpgradeStepCost(tables, 1000) == tables.upgradeCostSteps.back());
    CHECK(game::RuneDrawStepCost(tables, 1000) == tables.runeOfferDrawCostSteps.back());

    StartFresh(boot);
    boot.run().phase = game::EDungeonPhase::Camp;
    boot.run().materials = 1000;
    CHECK(game::UpgradeCostOf(tables, boot.progress()) == tables.upgradeCostSteps.front());
    CHECK(!game::UpgradeDungeonStat(boot.registry, 0, 0).has_value());
    const auto after = std::min<std::size_t>(1, tables.upgradeCostSteps.size() - 1);
    CHECK(game::UpgradeCostOf(tables, boot.progress()) == tables.upgradeCostSteps[after]);
}

void CostCurveAndSpecial() {
    FBoot boot(UniquePath("camp-special"));
    StartFresh(boot);
    auto& run = boot.run();
    const game::FCampTables& tables = boot.tables();
    run.phase = game::EDungeonPhase::Camp;
    run.materials = 1000;

    CHECK(game::UpgradeCostOf(tables, boot.progress()) == game::UpgradeStepCost(tables, 0u));
    CHECK(!game::UpgradeDungeonStat(boot.registry, 0, 0).has_value());
    CHECK(game::UpgradeCostOf(tables, boot.progress()) == game::UpgradeStepCost(tables, 1u));

    // The special discount applies to the first camp rune offer only.
    CHECK(game::BuildCampSession(boot.registry).runeOfferDiscounted);
    const int first = game::BuildCampSession(boot.registry).runeOfferCost;
    CHECK(first == std::max(0, game::RuneDrawStepCost(tables, 0u) - tables.specialDiscount));
    CHECK(!game::PurchaseCampRuneOffer(boot.registry).has_value());
    CHECK(game::ResolveRuneOffer(boot.registry, -1));
    CHECK(!game::BuildCampSession(boot.registry).runeOfferDiscounted);
    CHECK(game::BuildCampSession(boot.registry).runeOfferCost == game::RuneDrawStepCost(tables, 1u));

    // The discount never makes a price negative.
    game::FCampTables synthetic = tables;
    synthetic.runeOfferDrawCostSteps = {1};
    synthetic.specialDiscount = 5;
    const game::FCampProgress empty;
    CHECK(game::RuneDrawCostOf(synthetic, empty, 1) == 0);
}

// --- camp purchases: heal, respec, the action limit --------------------------------------------

void PaidHealIsOptional() {
    FBoot boot(UniquePath("camp-paid-heal"));
    StartFresh(boot);
    auto& run = boot.run();
    const game::FDungeonRecovery& recovery = game::DungeonRecovery(boot.registry);
    run.phase = game::EDungeonPhase::Camp;
    run.materials = 50;

    // A full party is never charged: the arrival heal is the free part, healing past it is a choice.
    for (auto& member : run.party) member.health = game::DungeonMemberMaxHealth(boot.registry, member);
    CHECK(game::ValidateDungeonCampHeal(boot.registry).has_value());
    CHECK(game::PayDungeonCampHeal(boot.registry).has_value());
    CHECK(run.materials == 50);
    // Sentinel members count as full too.
    for (auto& member : run.party) member.health = -1;
    CHECK(game::ValidateDungeonCampHeal(boot.registry).has_value());

    run.party[1].health = 1;
    CHECK(!game::ValidateDungeonCampHeal(boot.registry).has_value());
    CHECK(!game::PayDungeonCampHeal(boot.registry).has_value());
    CHECK(run.materials == 50 - recovery.campHealCost);
    const int maxHealth = game::DungeonMemberMaxHealth(boot.registry, run.party[1]);
    const int amount = maxHealth * recovery.campHealPercent / 100;  // the data rule, no code floor
    CHECK(amount > 0);
    CHECK(run.party[1].health == std::min(1 + amount, maxHealth));
    CHECK(run.party[0].health == -1);  // the sentinel was skipped
}

void RespecReallocatesNodes() {
    FBoot boot(UniquePath("camp-respec"));
    StartFresh(boot);
    auto& run = boot.run();
    run.phase = game::EDungeonPhase::Camp;
    run.materials = 100;
    auto& member = run.party[0];
    member.allocatedNodes = {"node.might", "node.vitality"};
    member.passivePoints = 0;
    member.bonusAttack = 2;  // a bought stat: never refunded
    boot.progress().upgradesTotal = 3;
    // A node that grants max HP: after the reset a full-HP member sits exactly at the lower max.
    const int withNode = game::DungeonMemberMaxHealth(boot.registry, member);
    member.health = withNode;  // full HP while the vitality node is allocated

    const int cost = boot.tables().respecCost;
    CHECK(!game::RespecDungeonNodes(boot.registry).has_value());
    CHECK(member.allocatedNodes.empty());
    CHECK(member.passivePoints == 2);
    CHECK(member.bonusAttack == 2);
    CHECK(boot.progress().upgradesTotal == 3u);  // the curve counter is untouched
    CHECK(run.materials == 100 - cost);
    const int withoutNode = game::DungeonMemberMaxHealth(boot.registry, member);
    CHECK(withoutNode < withNode);        // the reallocated node really granted max HP
    CHECK(member.health == withoutNode);  // clamped to the post-respec max, not left above it
    const auto& log = boot.registry.ctx().get<game::FRunEventLog>();
    CHECK(log.tail.back().kind == game::ERunEventKind::CampSpent);
    CHECK(log.tail.back().id == "respec" && log.tail.back().amount == cost);

    // Nothing allocated / not enough materials: refused with no change.
    CHECK(game::ValidateDungeonRespec(boot.registry).has_value());
    run.materials = 0;
    member.allocatedNodes = {"node.might"};
    CHECK(game::ValidateDungeonRespec(boot.registry).has_value());
}

void UnlimitedActionsByDefault() {
    FBoot boot(UniquePath("camp-unlimited"));
    StartFresh(boot);
    auto& run = boot.run();
    run.phase = game::EDungeonPhase::Camp;
    run.materials = 10000;
    CHECK(boot.tables().actionsPerCamp == 0);  // D-17 A: the constraint is materials, declared as 0 actions
    // Six paid actions in one camp — well past any action-budget design; all are allowed.
    for (int purchase = 0; purchase < 6; ++purchase) {
        CHECK(!game::UpgradeDungeonStat(boot.registry, 0, 0).has_value());
    }
    CHECK(run.party[0].bonusAttack == 6);
    CHECK(game::ValidateDungeonStatUpgrade(boot.registry, 0, 0).has_value());  // now the stat cap, not an action cap
}

void CampSpentLogged() {
    FBoot boot(UniquePath("camp-spent-log"));
    StartFresh(boot);
    auto& run = boot.run();
    run.phase = game::EDungeonPhase::Camp;
    run.materials = 1000;
    for (auto& member : run.party) member.health = 1;
    run.party[0].allocatedNodes = {"node.might"};
    auto& log = boot.registry.ctx().get<game::FRunEventLog>();
    const auto spentCount = [&log] {
        return static_cast<int>(std::count_if(log.tail.begin(), log.tail.end(), [](const game::FRunEvent& event) {
            return event.kind == game::ERunEventKind::CampSpent;
        }));
    };
    CHECK(spentCount() == 0);

    const int upgradeCost = game::BuildCampSession(boot.registry).upgradeCost;
    CHECK(!game::UpgradeDungeonStat(boot.registry, 0, 0).has_value());
    CHECK(spentCount() == 1);
    CHECK(log.tail.back().id == "upgrade.attack");
    CHECK(log.tail.back().amount == upgradeCost);
    CHECK(log.tail.back().member == 0);

    const int healCost = game::BuildCampSession(boot.registry).paidHealCost;
    CHECK(!game::PayDungeonCampHeal(boot.registry).has_value());
    CHECK(log.tail.back().id == "heal" && log.tail.back().amount == healCost && log.tail.back().member == -1);

    const int respecCost = game::BuildCampSession(boot.registry).respecCost;
    CHECK(!game::RespecDungeonNodes(boot.registry).has_value());
    CHECK(log.tail.back().id == "respec" && log.tail.back().amount == respecCost && log.tail.back().member == -1);

    const int offerCost = game::BuildCampSession(boot.registry).runeOfferCost;
    CHECK(!game::PurchaseCampRuneOffer(boot.registry).has_value());
    CHECK(log.tail.back().id == "rune_offer" && log.tail.back().amount == offerCost &&
          log.tail.back().member == -1);

    CHECK(spentCount() == 4);  // one purchase = one line, never an aggregate
}

// --- camp entry / report ------------------------------------------------------------------------

void CampAutoHealOnceOnEntry() {
    FBoot boot(UniquePath("camp-auto-heal"));
    StartFresh(boot);
    auto& run = boot.run();
    WinRoom(boot);
    CHECK(game::ResolveRuneOffer(boot.registry, -1));
    CHECK(game::ApplyRunIntent(boot.registry, game::ERunIntent::ContinueReward).has_value());  // -> Event
    CHECK(game::ApplyRunIntent(boot.registry, game::ERunIntent::ChooseEventRest).has_value());  // -> room 1
    WinRoom(boot);
    CHECK(game::ResolveRuneOffer(boot.registry, -1));
    CHECK(game::ApplyRunIntent(boot.registry, game::ERunIntent::ContinueReward).has_value());  // -> room 2
    WinRoom(boot);
    CHECK(game::ResolveRuneOffer(boot.registry, -1));
    CHECK(run.phase == game::EDungeonPhase::Reward);

    for (auto& member : run.party) member.health = 10;
    const int autoHeal = game::DungeonRecovery(boot.registry).campAutoHeal;
    CHECK(game::ApplyRunIntent(boot.registry, game::ERunIntent::ContinueReward).has_value());  // -> Camp
    CHECK(run.phase == game::EDungeonPhase::Camp);
    for (const auto& member : run.party) {
        CHECK(member.health == std::min(10 + autoHeal, game::DungeonMemberMaxHealth(boot.registry, member)));
    }

    // Once per entry: continuing at the camp does not heal again.
    std::vector<int> healths;
    for (const auto& member : run.party) healths.push_back(member.health);
    CHECK(game::SaveDungeonRun(boot.registry));
    boot.registry.ctx().erase<game::FDungeonRun>();
    CHECK(game::ContinueDungeonRun(boot.registry));
    auto& restored = boot.run();  // the erased reference above is dangling: re-fetch after the swap
    CHECK(restored.phase == game::EDungeonPhase::Camp);
    for (std::size_t index = 0; index < restored.party.size(); ++index) {
        CHECK(restored.party[index].health == healths[index]);
    }
}

void HoardRatioReported() {
    FBoot boot(UniquePath("camp-hoard"));
    FLogCapture capture;
    WalkToCamp(boot);
    const std::string text = capture.Text();
    const game::FCampPrices prices = game::CampPricesOf(boot.registry);
    const int cheapestTwo = game::CampCheapestTwo(prices);
    CHECK(text.find("[camp] hoard_ratio=") != std::string::npos);
    CHECK(text.find("materials=" + std::to_string(boot.run().materials)) != std::string::npos);
    CHECK(text.find("cheapest_two=" + std::to_string(cheapestTwo)) != std::string::npos);
    // The ratio is the materials over the two cheapest services — a tuning input, never a gate.
    std::ostringstream ratio;
    ratio.precision(2);
    ratio << std::fixed
          << (cheapestTwo > 0 ? static_cast<double>(boot.run().materials) / cheapestTwo : 0.0);
    CHECK(text.find("hoard_ratio=" + ratio.str()) != std::string::npos);
    std::cout << "  " << text.substr(text.find("[camp]"), text.find('\n', text.find("[camp]")) - text.find("[camp]"))
              << '\n';
}

// --- the conditional next-dungeon preview (WP-08 data skeleton) ---------------------------------

void PreviewUsesSequenceWhenPresent() {
    // Without the hint the camp invents no preview.
    {
        FBoot boot(UniquePath("camp-preview-none"));
        StartFresh(boot);
        boot.run().phase = game::EDungeonPhase::Camp;
        const auto logs = RenderModalLogs(boot.registry, {std::string(game::kRunModalCamp)});
        CHECK(logs.size() == 1u);
        CHECK(logs[0].find("Next dungeon:") == std::string::npos);
    }
    // With the WP-08 skeleton authored, the camp shows exactly that line.
    {
        FTempAssets assets("camp-preview-hint");
        assets.CopyGameplayData(true);
        assets.Copy("rewards.json");
        auto dungeon = ReadJsonFile(FixtureDataPath("dungeon.json"));
        dungeon["next_dungeon_hint"] = "Hall of echoes";
        assets.Write("dungeon.json", dungeon);
        FBoot boot(UniquePath("camp-preview-hint"), {assets.root.string(), kShipAssetRoot});
        StartFresh(boot);
        boot.run().phase = game::EDungeonPhase::Camp;
        const auto logs = RenderModalLogs(boot.registry, {std::string(game::kRunModalCamp)});
        CHECK(logs.size() == 1u);
        CHECK(logs[0].find("Next dungeon: Hall of echoes") != std::string::npos);
    }
}

// --- data validation ----------------------------------------------------------------------------

void CampDataValidationRejects() {
    FTempAssets assets("camp-invalid");
    assets.CopyGameplayData(true);
    assets.Copy("rewards.json");
    const auto base = ReadJsonFile(FixtureDataPath("dungeon.json"));
    const auto edit = [&base](const auto& change) {
        auto document = base;
        change(document);
        return document;
    };
    const std::vector<std::pair<const char*, json>> cases = {
        {"missing camp block", edit([](json& d) { d.erase("camp"); })},
        {"missing recovery block", edit([](json& d) { d.erase("recovery"); })},
        {"empty upgrade curve", edit([](json& d) { d["camp"]["upgrade_cost_steps"] = json::array(); })},
        {"zero cost step", edit([](json& d) { d["camp"]["upgrade_cost_steps"] = json::array({1, 0, 4}); })},
        {"decreasing curve", edit([](json& d) { d["camp"]["upgrade_cost_steps"] = json::array({4, 2}); })},
        {"negative cap", edit([](json& d) { d["camp"]["rune_offer_camp_cap"] = -1; })},
        {"negative discount", edit([](json& d) { d["camp"]["special_discount"] = -2; })},
        {"recovery fraction above one", edit([](json& d) { d["recovery"]["ko_recover_fraction"] = 1.5; })},
        {"chest fraction below zero", edit([](json& d) { d["recovery"]["event_chest_hp_cost_fraction"] = -0.1; })},
        {"heal percent above 100", edit([](json& d) { d["recovery"]["camp_heal_percent"] = 101; })},
    };
    for (const auto& [label, document] : cases) {
        assets.Write("dungeon.json", document);
        const std::string checkpoint = UniquePath("camp-invalid-checkpoint");
        const bool booted = BootsDungeon({assets.root.string()}, checkpoint);
        if (booted) std::cout << "    accepted invalid dungeon.json: " << label << '\n';
        CHECK(!booted);
    }
    assets.Write("dungeon.json", base);
    CHECK(BootsDungeon({assets.root.string()}, UniquePath("camp-valid-checkpoint")));
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
            (std::filesystem::current_path() / "out" / "test-saves" / "camp_tests").string();
        std::error_code error;
        std::filesystem::create_directories(path, error);
        _putenv_s("GAME01P_SAVE_DIR", path.c_str());
    }

    return game_test::RunTests("camp", {
        {"CampBudgetForcesAllocation", CampBudgetForcesAllocation},
        {"BudgetFromRules", BudgetFromRules},
        {"KoFractionFromData", KoFractionFromData},
        {"ChestCostNeverKills", ChestCostNeverKills},
        {"ChestChoiceCostsHp", ChestChoiceCostsHp},
        {"RunHpClampedOnHeal", RunHpClampedOnHeal},
        {"MemberSentinelSkipsHeal", MemberSentinelSkipsHeal},
        {"UnknownMemberIsNotClamped", UnknownMemberIsNotClamped},
        {"RunMaxHpMatchesCombatSheet", RunMaxHpMatchesCombatSheet},
        {"AttritionBudgetPerUnit", AttritionBudgetPerUnit},
        {"ShrinePathTradesBuyingPower", ShrinePathTradesBuyingPower},
        {"CampSectionRoundTrip", CampSectionRoundTrip},
        {"CampDepthResetsThisCamp", CampDepthResetsThisCamp},
        {"CampDrawCapPersists", CampDrawCapPersists},
        {"UpgradesTotalCountsPurchases", UpgradesTotalCountsPurchases},
        {"SinkPurchaseUsesOfferApi", SinkPurchaseUsesOfferApi},
        {"UpgradeCurveMonotonic", UpgradeCurveMonotonic},
        {"RespecReallocatesNodes", RespecReallocatesNodes},
        {"CampSpentLogged", CampSpentLogged},
        {"CampAutoHealOnceOnEntry", CampAutoHealOnceOnEntry},
        {"PaidHealIsOptional", PaidHealIsOptional},
        {"HoardRatioReported", HoardRatioReported},
        {"UnlimitedActionsByDefault", UnlimitedActionsByDefault},
        {"CostCurveAndSpecial", CostCurveAndSpecial},
        {"PreviewUsesSequenceWhenPresent", PreviewUsesSequenceWhenPresent},
        {"CampDataValidationRejects", CampDataValidationRejects},
    });
}
