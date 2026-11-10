// WP-41: dungeon paths — the elite block, the seeded fork, the path state and its save section, the path
// strip, the fork panel and the autopilot policy. Temp-root documents start from the fixture dungeon.json and
// replace the first dungeon's rooms with the list each case states.

#define GAME01P_TEST_SUPPORT_CHECK
#include "../TestSupport.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <SDL3/SDL.h>
#include <imgui.h>
#include <nlohmann/json.hpp>

#include "core/AssetManager.h"
#include "ecs/components/FFaction.h"
#include "ecs/components/FHealth.h"
#include "ecs/components/FStats.h"
#include "ecs/systems/TurnSystem.h"
#include "core/EngineState.h"
#include "core/ResourceManager.h"
#include "core/SDLDeleter.h"
#include "dev/FRunAutopilot.h"
#include "gameplay/GameplayServices.h"
#include "gameplay/dungeon/FBattleObjective.h"
#include "gameplay/dungeon/FDungeonRun.h"
#include "gameplay/dungeon/FDungeonSave.h"
#include "gameplay/dungeon/FEncounterContent.h"
#include "gameplay/dungeon/FEventContent.h"
#include "gameplay/dungeon/FEventSection.h"
#include "gameplay/dungeon/FRewards.h"
#include "gameplay/dungeon/FRunEventLog.h"
#include "gameplay/dungeon/FRunFlow.h"
#include "gameplay/dungeon/FRunPath.h"
#include "gameplay/dungeon/FRunSequence.h"
#include "gameplay/dungeon/FSaveSections.h"
#include "states/FBaseState.h"
#include "states/FEmberBattleState.h"
#include "states/FEmberModalHostState.h"
#include "presentation/FDungeonArt.h"
#include "presentation/FDungeonOverlay.h"
#include "presentation/FDungeonViews.h"
#include "presentation/FModalViewRegistry.h"
#include "presentation/FPathStripView.h"

namespace {

using nlohmann::json;

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

json FixtureDoc(const char* file) { return ReadJsonFile(FixtureDataPath(file)); }

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
};

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

    void Write(const std::string& file, const json& document) {
        std::ofstream out(root / "data" / file, std::ios::trunc);
        CHECK(out.good());
        out << document.dump(2);
    }

    std::vector<std::string> Roots() const { return {root.string(), kTestFixtureRoot, kShipAssetRoot}; }
};

/// "battle" | "event" | "boss-mid" | "boss-final"
json RoomJson(std::string_view spec) {
    if (spec == "event") return {{"type", "event"}, {"event_pool", "common"}};
    json room = {{"type", "battle"}, {"family", "open"}};
    if (spec == "boss-mid") room["boss"] = "mid";
    if (spec == "boss-final") room["boss"] = "final";
    return room;
}

json EliteBlock() {
    return json::parse(R"({
      "reward_materials": 2,
      "affixes": [
        {"id": "affix.hardened", "display_name": "Hardened", "text": "Enemy max HP +30%", "health_percent": 30, "attack_bonus": 0},
        {"id": "affix.fierce", "display_name": "Fierce", "text": "Enemy attack +1", "health_percent": 0, "attack_bonus": 1}
      ]
    })");
}

json PathDungeon(const std::vector<std::string_view>& rooms, bool elite = true) {
    auto document = FixtureDoc("dungeon.json");
    json list = json::array();
    for (const auto spec : rooms) list.push_back(RoomJson(spec));
    document["themes"][0]["dungeons"][0]["rooms"] = list;
    if (elite) document["elite"] = EliteBlock();
    return document;
}

json PathEvent(const std::string& id, const std::string& tag) {
    return {{"id", id},
            {"title", "Title " + id},
            {"body", "Body " + id},
            {"tags", json::array({tag})},
            {"choices",
             json::array({{{"text", "Take " + id},
                           {"effects", json::array({{{"kind", "gain_materials"}, {"amount", 1}}})},
                           {"result_text", "Taken."}},
                          {{"text", "Skip " + id},
                           {"effects", json::array({{{"kind", "gain_materials"}, {"amount", 1}}})},
                           {"result_text", "Skipped."}}})}};
}

json PathEvents(bool mystery = true) {
    json events = json::array({PathEvent("event.p_common_a", "common"), PathEvent("event.p_common_b", "common")});
    if (mystery) {
        events.push_back(PathEvent("event.p_myst_a", "mystery"));
        events.push_back(PathEvent("event.p_myst_b", "mystery"));
    }
    return {{"schema_version", 1}, {"events", events}};
}

/// A booted registry over a temp root holding `rooms` as the first dungeon.
struct FPathBoot {
    FTempAssets assets;
    FBoot boot;

    FPathBoot(const char* stem, const std::vector<std::string_view>& rooms, bool elite = true, bool mystery = true)
        : assets(stem),
          boot((assets.Write("dungeon.json", PathDungeon(rooms, elite)),
                assets.Write("events.json", PathEvents(mystery)), UniquePath(stem)),
               assets.Roots()) {}

    entt::registry& registry() { return boot.registry; }
    game::FDungeonRun& run() { return boot.run(); }
};

constexpr std::uint32_t kNoSeed = 0;

/// Loops seeds from 1 until `wanted` holds for the derived state; 0 = none below 200.
template <typename Pred>
std::uint32_t PickSeed(entt::registry& registry, Pred wanted) {
    for (std::uint32_t seed = 1; seed < 200; ++seed) {
        game::StartRun(registry, seed, true);
        if (wanted(game::EnsureRunPath(registry))) return seed;
    }
    return kNoSeed;
}

void StartSeed(FPathBoot& path, std::uint32_t seed) { game::StartRun(path.registry(), seed, true); }

void WinRoom(FPathBoot& path) {
    CHECK(game::ResolveRoomVictory(path.registry(), true).has_value());
    CHECK(path.run().phase == game::EDungeonPhase::Reward);
}

void SkipOfferIfActive(FPathBoot& path) {
    auto* rewards = path.registry().ctx().find<game::FRewards>();
    CHECK(rewards != nullptr);
    if (rewards->offer.active) CHECK(game::ResolveRuneOffer(path.registry(), -1));
}

// ---------------------------------------------------------------------------------------------
// F1 — the elite block
// ---------------------------------------------------------------------------------------------

void EliteBlockLoadsAndValidates() {
    const auto good = PathDungeon({"battle", "battle", "battle"});
    std::string error;
    const auto sequence = game::FRunSequence::Load(good, error);
    CHECK(sequence.has_value());
    CHECK(sequence->elite.rewardMaterials == 2);
    CHECK(sequence->elite.affixes.size() == 2u);
    CHECK(sequence->elite.affixes[0].id == "affix.hardened");
    CHECK(sequence->elite.affixes[0].displayName == "Hardened");
    CHECK(sequence->elite.affixes[0].text == "Enemy max HP +30%");
    CHECK(sequence->elite.affixes[0].healthPercent == 30);
    CHECK(sequence->elite.affixes[0].attackBonus == 0);
    CHECK(sequence->elite.affixes[1].id == "affix.fierce");
    CHECK(sequence->elite.affixes[1].healthPercent == 0);
    CHECK(sequence->elite.affixes[1].attackBonus == 1);

    const auto broken = [&](const char* label, auto&& mutate) {
        auto document = good;
        mutate(document["elite"]);
        std::string message;
        const auto loaded = game::FRunSequence::Load(document, message);
        if (loaded.has_value() || message.empty()) std::cout << "  broken copy loaded: " << label << "\n";
        CHECK(!loaded.has_value());
        CHECK(!message.empty());
    };
    broken("missing id", [](json& e) { e["affixes"][0].erase("id"); });
    broken("missing display_name", [](json& e) { e["affixes"][0].erase("display_name"); });
    broken("missing text", [](json& e) { e["affixes"][0].erase("text"); });
    broken("health -1", [](json& e) { e["affixes"][0]["health_percent"] = -1; });
    broken("attack -1", [](json& e) { e["affixes"][1]["attack_bonus"] = -1; });
    broken("both zero", [](json& e) {
        e["affixes"][0]["health_percent"] = 0;
        e["affixes"][0]["attack_bonus"] = 0;
    });
    broken("duplicate id", [](json& e) { e["affixes"][1]["id"] = "affix.hardened"; });
    broken("reward -1", [](json& e) { e["reward_materials"] = -1; });

    auto none = good;
    none.erase("elite");
    const auto loaded = game::FRunSequence::Load(none, error);
    CHECK(loaded.has_value());
    CHECK(loaded->elite.affixes.empty());
}

// ---------------------------------------------------------------------------------------------
// F2 / F3 — the seeded fork
// ---------------------------------------------------------------------------------------------

void ForkSlotIsSeededAndEligible() {
    {
        FPathBoot path("path-slot", {"battle", "event", "battle", "boss-mid"});
        std::set<int> slots;
        for (std::uint32_t seed = 1; seed <= 40; ++seed) {
            StartSeed(path, seed);
            const auto& state = game::EnsureRunPath(path.registry());
            CHECK(state.hasFork);
            CHECK(state.choice == -1);
            const int expected = std::array{1, 2}[game::EventPickHash(seed, 1, 1001) % 2];
            CHECK(state.slot == expected);
            slots.insert(state.slot);
            if (state.slot == 1) {
                CHECK(!state.battle);
                CHECK(state.battleOrdinal == 0);
            } else {
                CHECK(state.battle);
                CHECK(state.battleOrdinal == 1);
            }
        }
        CHECK(slots.size() == 2u);
    }
    {
        FPathBoot path("path-slot-noelite", {"battle", "event", "battle", "boss-mid"}, false, true);
        for (std::uint32_t seed = 1; seed <= 40; ++seed) {
            StartSeed(path, seed);
            const auto& state = game::EnsureRunPath(path.registry());
            CHECK(state.hasFork);
            CHECK(state.slot == 1);
        }
    }
    {
        FPathBoot path("path-slot-none", {"battle", "event", "battle", "boss-mid"}, false, false);
        for (std::uint32_t seed = 1; seed <= 10; ++seed) {
            StartSeed(path, seed);
            CHECK(!game::EnsureRunPath(path.registry()).hasFork);
        }
    }
}

void AffixIsSeeded() {
    FPathBoot path("path-affix", {"battle", "battle", "battle"});
    std::set<std::string> seen;
    const auto& affixes = path.registry().ctx().get<game::FRunSequence>().elite.affixes;
    for (std::uint32_t seed = 1; seed <= 40; ++seed) {
        StartSeed(path, seed);
        const auto& state = game::EnsureRunPath(path.registry());
        CHECK(state.hasFork);
        CHECK(state.battle);
        CHECK(state.affixId == affixes[game::EventPickHash(seed, 1, 1002) % 2].id);
        seen.insert(state.affixId);
    }
    CHECK(seen.size() == 2u);
}

// ---------------------------------------------------------------------------------------------
// Flow helpers
// ---------------------------------------------------------------------------------------------

std::uint32_t SeedWithSlot(FPathBoot& path, int slot) {
    const auto seed = PickSeed(path.registry(), [&](const game::FRunPathState& s) { return s.hasFork && s.slot == slot; });
    CHECK(seed != kNoSeed);
    return seed;
}

/// Room 0 won, its offer skipped: the reward modal that precedes slot 1.
void ReachRewardStep(FPathBoot& path) {
    WinRoom(path);
    SkipOfferIfActive(path);
}

bool Apply(FPathBoot& path, game::ERunIntent intent, int choice = -1) {
    return game::ApplyRunIntent(path.registry(), intent, choice).has_value();
}

// ---------------------------------------------------------------------------------------------
// F4 — continue takes the standard branch, TakeFork the variant
// ---------------------------------------------------------------------------------------------

void ContinueTakesStandardTakeForkTakesVariant() {
    {
        FPathBoot path("path-f4a", {"battle", "battle", "battle"});
        SeedWithSlot(path, 1);
        ReachRewardStep(path);
        CHECK(game::IsForkPending(path.registry()));
        CHECK(Apply(path, game::ERunIntent::ContinueReward));
        CHECK(game::EnsureRunPath(path.registry()).choice == 0);
        CHECK(path.run().room == 1);
        CHECK(path.run().phase == game::EDungeonPhase::Battle);
        CHECK(!game::IsForkPending(path.registry()));
    }
    {
        FPathBoot path("path-f4b", {"battle", "battle", "battle"});
        SeedWithSlot(path, 1);
        ReachRewardStep(path);
        CHECK(Apply(path, game::ERunIntent::TakeFork));
        CHECK(game::EnsureRunPath(path.registry()).choice == 1);
        CHECK(path.run().room == 1);
        CHECK(path.run().phase == game::EDungeonPhase::Battle);
    }
    {
        FPathBoot path("path-f4c", {"battle", "battle", "battle"});
        SeedWithSlot(path, 1);
        WinRoom(path);
        CHECK(path.registry().ctx().get<game::FRewards>().offer.active);  // the fixture chance is 1.0
        CHECK(!Apply(path, game::ERunIntent::TakeFork));
        CHECK(game::EnsureRunPath(path.registry()).choice == -1);
        CHECK(path.run().phase == game::EDungeonPhase::Reward);
    }
    {
        FPathBoot path("path-f4d", {"battle", "battle", "battle"});
        SeedWithSlot(path, 1);
        CHECK(!Apply(path, game::ERunIntent::TakeFork));
        CHECK(game::EnsureRunPath(path.registry()).choice == -1);
    }
    {
        FPathBoot path("path-f4e", {"battle", "event", "battle"});
        SeedWithSlot(path, 2);
        ReachRewardStep(path);
        CHECK(!game::IsForkPending(path.registry()));  // the fork is the battle after the event
        CHECK(Apply(path, game::ERunIntent::ContinueReward));
        CHECK(path.run().phase == game::EDungeonPhase::Event);
        CHECK(!Apply(path, game::ERunIntent::TakeFork));  // unresolved event
        CHECK(Apply(path, game::ERunIntent::ChooseEventOption, 0));
        CHECK(Apply(path, game::ERunIntent::TakeFork));
        CHECK(game::EnsureRunPath(path.registry()).choice == 1);
        CHECK(path.run().room == 1);
        CHECK(path.run().phase == game::EDungeonPhase::Battle);
    }
}

// ---------------------------------------------------------------------------------------------
// F5 — the affix scales enemies only
// ---------------------------------------------------------------------------------------------

struct FBattleHarness {
    entt::registry registry;
    game::ecs::TurnSystem turns;
    FEmberBattleState battle;
    std::string checkpoint;
    std::string runSave;

    FBattleHarness(std::uint32_t seed, std::vector<std::string> roots)
        : checkpoint(UniquePath("path-battle")) {
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

    /// Win room 0, skip its offer, take the given intent and field the next room.
    void toRoomOne(game::ERunIntent intent) {
        CHECK(game::ResolveRoomVictory(registry, true).has_value());
        if (registry.ctx().get<game::FRewards>().offer.active) CHECK(game::ResolveRuneOffer(registry, -1));
        CHECK(game::ApplyRunIntent(registry, intent).has_value());
        CHECK((battle.Update(registry, 0) == FStateTransition{EStateChangeKind::Root, std::string(kEmberBattleId)}));
        battle.OnExit(registry);
        battle.OnEnter(registry);
    }

    struct FEnemy {
        int hpMax{0};
        int attack{0};
        int magic{0};
    };

    std::vector<FEnemy> enemies() {
        std::vector<FEnemy> out;
        for (const auto entity : registry.view<game::FHealth, game::FStats>()) {
            if (registry.all_of<game::FPartyMember>(entity)) continue;
            const auto& health = registry.get<game::FHealth>(entity);
            const auto& stats = registry.get<game::FStats>(entity);
            out.push_back({health.max, stats.attack, stats.magicAttack});
        }
        return out;
    }

    int partyMaxHp() {
        int total = 0;
        for (const auto entity : registry.view<game::FHealth, game::FPartyMember>()) {
            total += registry.get<game::FHealth>(entity).max;
        }
        return total;
    }
};

void EliteAffixScalesEnemies() {
    for (const char* wanted : {"affix.hardened", "affix.fierce"}) {
        FPathBoot probe("path-f5probe", {"battle", "battle", "battle"});
        const auto seed = PickSeed(probe.registry(), [&](const game::FRunPathState& s) {
            return s.hasFork && s.slot == 1 && s.affixId == wanted;
        });
        CHECK(seed != kNoSeed);

        FBattleHarness elite(seed, probe.assets.Roots());
        elite.toRoomOne(game::ERunIntent::TakeFork);
        const auto* plan = game::FindRoomPlan(elite.registry);
        CHECK(plan != nullptr);
        CHECK(plan->elite);
        CHECK(plan->affixId == wanted);
        const auto eliteEnemies = elite.enemies();

        FBattleHarness standard(seed, probe.assets.Roots());
        standard.toRoomOne(game::ERunIntent::ContinueReward);
        const auto* standardPlan = game::FindRoomPlan(standard.registry);
        CHECK(standardPlan != nullptr);
        CHECK(!standardPlan->elite);
        const auto standardEnemies = standard.enemies();

        CHECK(!eliteEnemies.empty());
        CHECK(eliteEnemies.size() == standardEnemies.size());
        CHECK(elite.partyMaxHp() == standard.partyMaxHp());
        for (std::size_t i = 0; i < eliteEnemies.size() && i < standardEnemies.size(); ++i) {
            if (std::string_view(wanted) == "affix.hardened") {
                CHECK(eliteEnemies[i].hpMax == standardEnemies[i].hpMax + standardEnemies[i].hpMax * 30 / 100);
                CHECK(eliteEnemies[i].attack == standardEnemies[i].attack);
            } else {
                CHECK(eliteEnemies[i].hpMax == standardEnemies[i].hpMax);
                CHECK(eliteEnemies[i].attack == standardEnemies[i].attack + 1);
                CHECK(eliteEnemies[i].magic == standardEnemies[i].magic + 1);
            }
        }
    }
}

// ---------------------------------------------------------------------------------------------
// F6 — the elite payout
// ---------------------------------------------------------------------------------------------

/// The rewards document of the fixture plus the shipped WP-40 rarity tables, so `boosted` exists.
json TieredRewards() {
    auto document = FixtureDoc("rewards.json");
    document["rarity_tables"] = ReadJsonFile("assets/data/rewards.json").at("rarity_tables");
    return document;
}

struct FTieredPathBoot {
    FTempAssets assets;
    FBoot boot;

    FTieredPathBoot(const char* stem, const std::vector<std::string_view>& rooms)
        : assets(stem),
          boot((assets.Write("dungeon.json", PathDungeon(rooms)), assets.Write("events.json", PathEvents()),
                assets.Write("rewards.json", TieredRewards()), UniquePath(stem)),
               assets.Roots()) {}
};

void EliteVictoryPaysOfferAndMaterials() {
    const auto reachRoomOne = [](FTieredPathBoot& boot, std::uint32_t seed, game::ERunIntent intent) {
        game::StartRun(boot.boot.registry, seed, true);
        CHECK(game::ResolveRoomVictory(boot.boot.registry, true).has_value());
        auto& rewards = boot.boot.registry.ctx().get<game::FRewards>();
        if (rewards.offer.active) CHECK(game::ResolveRuneOffer(boot.boot.registry, -1));
        CHECK(game::ApplyRunIntent(boot.boot.registry, intent).has_value());
        CHECK(boot.boot.run().room == 1);
        auto& tuned = boot.boot.registry.ctx().get<game::FRewards>();
        tuned.tables.chance = 0.0;
        tuned.tables.pityThreshold = 3;
        tuned.pity = 1;
        boot.boot.registry.ctx().erase<game::FBattleObjective>();
    };
    FTieredPathBoot probe("path-f6probe", {"battle", "battle", "battle"});
    const auto seed = PickSeed(probe.boot.registry, [](const game::FRunPathState& s) { return s.hasFork && s.slot == 1; });
    CHECK(seed != kNoSeed);
    {
        FTieredPathBoot boot("path-f6a", {"battle", "battle", "battle"});
        reachRoomOne(boot, seed, game::ERunIntent::TakeFork);
        const int before = boot.boot.run().materials;
        CHECK(game::ResolveRoomVictory(boot.boot.registry, true).has_value());
        const auto& rewards = boot.boot.registry.ctx().get<game::FRewards>();
        CHECK(boot.boot.run().materials - before == game::DungeonBalance(boot.boot.registry, "room_materials") + 2);
        CHECK(rewards.offer.active);
        CHECK(rewards.offer.rarityTable == "boosted");
        CHECK(rewards.pity == 0);
        CHECK(boot.boot.run().message.find("Elite defeated: +2 materials") != std::string::npos);
    }
    {
        FTieredPathBoot boot("path-f6b", {"battle", "battle", "battle"});
        reachRoomOne(boot, seed, game::ERunIntent::ContinueReward);
        const int before = boot.boot.run().materials;
        CHECK(game::ResolveRoomVictory(boot.boot.registry, true).has_value());
        CHECK(boot.boot.run().materials - before == game::DungeonBalance(boot.boot.registry, "room_materials"));
        CHECK(!boot.boot.registry.ctx().get<game::FRewards>().offer.active);
    }
}

// ---------------------------------------------------------------------------------------------
// F7 — the "?" pool
// ---------------------------------------------------------------------------------------------

void MysteryForkPicksMysteryPool() {
    FPathBoot path("path-f7", {"battle", "event", "battle"}, false, true);
    std::set<std::string> tags;
    for (std::uint32_t seed = 1; seed <= 40; ++seed) {
        StartSeed(path, seed);
        CHECK(game::EnsureRunPath(path.registry()).slot == 1);
        ReachRewardStep(path);
        CHECK(Apply(path, game::ERunIntent::TakeFork));
        CHECK(path.run().phase == game::EDungeonPhase::Event);
        const auto instance = game::CurrentEventInstance(path.registry());
        CHECK(instance.has_value());
        std::vector<game::FEventDef> mixed;
        for (const auto& event : game::EventCatalog(path.registry())) {
            if (game::EventInPool(event, "common") || game::EventInPool(event, "mystery")) mixed.push_back(event);
        }
        const auto expected = game::PickEventId(mixed, "", {}, seed, 1, 0);
        CHECK(expected.has_value());
        CHECK(instance->eventId == *expected);
        const auto def = game::FindEventDef(path.registry(), instance->eventId);
        CHECK(def.has_value());
        CHECK(game::EventInPool(*def, "common") || game::EventInPool(*def, "mystery"));
        tags.insert(game::EventInPool(*def, "mystery") ? "mystery" : "common");
    }
    CHECK(tags.size() == 2u);

    StartSeed(path, 7);
    ReachRewardStep(path);
    const auto options = game::ForkOptions(path.registry());
    CHECK(options.size() == 2u);
    const auto named = game::PickEventId(game::EventCatalog(path.registry()), "common", {}, 7, 1, 0);
    CHECK(named.has_value());
    const auto namedDef = game::FindEventDef(path.registry(), *named);
    CHECK(namedDef.has_value());
    CHECK(options[0].title == "Event: " + namedDef->title);
    for (const auto& choice : namedDef->choices) CHECK(options[0].detail.find(choice.text) != std::string::npos);
    CHECK(options[1].title == "? Unknown event");
    CHECK(Apply(path, game::ERunIntent::ContinueReward));
    const auto picked = game::CurrentEventInstance(path.registry());
    CHECK(picked.has_value());
    CHECK(picked->eventId == *named);
    CHECK(game::EventInPool(*game::FindEventDef(path.registry(), picked->eventId), "common"));
}

// ---------------------------------------------------------------------------------------------
// F8 — save and content change
// ---------------------------------------------------------------------------------------------

void ForgetRun(FPathBoot& path) {
    path.registry().ctx().erase<game::FDungeonRun>();
    path.registry().ctx().erase<game::FEventInstance>();
    path.registry().ctx().erase<game::FEventRunState>();
    path.registry().ctx().get<game::FRunPathState>() = {};
}

void ForkSurvivesSaveAndContentChange() {
    {  // (a) battle fork
        FPathBoot path("path-f8a", {"battle", "battle", "battle"});
        SeedWithSlot(path, 1);
        const auto saved = game::EnsureRunPath(path.registry());
        ReachRewardStep(path);
        CHECK(Apply(path, game::ERunIntent::TakeFork));
        CHECK(Apply(path, game::ERunIntent::ChooseEventRest) == false);  // wrong phase, nothing changes
        CHECK(game::ResolveRoomVictory(path.registry(), true).has_value());
        CHECK(path.run().phase == game::EDungeonPhase::Reward);
        ForgetRun(path);
        CHECK(game::ContinueDungeonRun(path.registry()));
        const auto& state = game::EnsureRunPath(path.registry());
        CHECK(state.hasFork);
        CHECK(state.slot == saved.slot);
        CHECK(state.battle == saved.battle);
        CHECK(state.affixId == saved.affixId);
        CHECK(state.choice == 1);
        CHECK(game::IsEliteBattle(path.registry(), 1));
        (void)game::BuildDungeonBattlefield(path.registry());  // the plan is stamped when the room is fielded
        const auto* plan = game::FindRoomPlan(path.registry());
        CHECK(plan != nullptr && plan->elite);
    }
    {  // (b) event fork
        FPathBoot path("path-f8b", {"battle", "event", "battle"}, false, true);
        StartSeed(path, 5);
        ReachRewardStep(path);
        CHECK(Apply(path, game::ERunIntent::TakeFork));
        const auto before = game::CurrentEventInstance(path.registry());
        CHECK(before.has_value());
        ForgetRun(path);
        CHECK(game::ContinueDungeonRun(path.registry()));
        const auto after = game::CurrentEventInstance(path.registry());
        CHECK(after.has_value());
        CHECK(after->eventId == before->eventId);
        CHECK(game::EnsureRunPath(path.registry()).choice == 1);
    }
    {  // (c) content change: the elite block is gone
        FPathBoot path("path-f8c", {"battle", "battle", "battle"});
        SeedWithSlot(path, 1);
        ReachRewardStep(path);
        CHECK(Apply(path, game::ERunIntent::TakeFork));
        CHECK(game::ResolveRoomVictory(path.registry(), true).has_value());
        FTempAssets other("path-f8c-other");
        other.Write("dungeon.json", PathDungeon({"battle", "battle", "battle"}, false));
        other.Write("events.json", PathEvents(false));
        entt::registry second;
        game::FAssetManager::Initialize(second, other.Roots());
        CHECK(game::gameplay::FGameplayServices::Initialize(second));
        CHECK(game::InitializeDungeon(second, path.boot.checkpoint));
        CHECK(game::ContinueDungeonRun(second));
        CHECK(!game::EnsureRunPath(second).hasFork);
        CHECK(!game::IsEliteBattle(second, 1));
    }
    {  // (d) a malformed section
        FPathBoot path("path-f8d", {"battle", "battle", "battle"});
        SeedWithSlot(path, 1);
        ReachRewardStep(path);
        CHECK(Apply(path, game::ERunIntent::TakeFork));
        json saved = ReadJsonFile(path.boot.runSave);
        saved["run"]["ext"]["path"] = {{"section_version", 1}, {"choice", 5}};
        {
            std::ofstream out(path.boot.runSave, std::ios::trunc);
            CHECK(out.good());
            out << saved.dump(2);
        }
        ForgetRun(path);
        CHECK(game::ContinueDungeonRun(path.registry()));
        CHECK(game::EnsureRunPath(path.registry()).choice == -1);
    }
    {  // (e) a new run of the same seed starts undecided
        FPathBoot path("path-f8e", {"battle", "battle", "battle"});
        const auto seed = SeedWithSlot(path, 1);
        ReachRewardStep(path);
        CHECK(Apply(path, game::ERunIntent::TakeFork));
        CHECK(game::EnsureRunPath(path.registry()).choice == 1);
        StartSeed(path, seed);
        CHECK(game::EnsureRunPath(path.registry()).choice == -1);
    }
}

// ---------------------------------------------------------------------------------------------
// F19 — the modal host accepts the fork intent
// ---------------------------------------------------------------------------------------------

void HostAcceptsTakeFork() {
    {
        FPathBoot path("path-f19a", {"battle", "battle", "battle"});
        SeedWithSlot(path, 1);
        ReachRewardStep(path);
        FEmberModalHostState host(std::string(game::kRunModalReward), EHostMode::RunPhase);
        game::PostRunIntent(path.registry(), game::ERunIntent::TakeFork);
        host.Update(path.registry(), 0.f);
        CHECK(game::EnsureRunPath(path.registry()).choice == 1);
        CHECK(path.run().room == 1);
    }
    {
        FPathBoot path("path-f19b", {"battle", "event", "battle"});
        SeedWithSlot(path, 2);
        ReachRewardStep(path);
        CHECK(Apply(path, game::ERunIntent::ContinueReward));
        CHECK(Apply(path, game::ERunIntent::ChooseEventOption, 0));
        FEmberModalHostState host(std::string(game::kRunModalEvent), EHostMode::RunPhase);
        game::PostRunIntent(path.registry(), game::ERunIntent::TakeFork);
        host.Update(path.registry(), 0.f);
        CHECK(game::EnsureRunPath(path.registry()).choice == 1);
        CHECK(path.run().room == 1);
    }
}

// ---------------------------------------------------------------------------------------------
// F9 / F10 / F11 — the strip and the fork panel
// ---------------------------------------------------------------------------------------------

void StripMirrorsSlotsAndPhase() {
    FPathBoot path("path-f9", {"battle", "event", "battle", "boss-final"});
    const auto seed = PickSeed(path.registry(), [](const game::FRunPathState& s) {
        return s.hasFork && s.slot == 2 && s.affixId == "affix.hardened";
    });
    CHECK(seed != kNoSeed);
    {
        const auto strip = game::BuildPathStrip(path.registry());
        CHECK(strip.nodes.size() == 5u);
        CHECK(strip.nodes[0].label == "Battle");
        CHECK(strip.nodes[1].label == "Event");
        CHECK(strip.nodes[2].label == "Battle");
        CHECK(strip.nodes[3].label.starts_with("Boss"));
        CHECK(strip.nodes[3].kind == game::EPathNodeKind::Boss);
        CHECK(strip.nodes[4].label == "Camp");
        CHECK(strip.nodes[0].current && !strip.nodes[0].done);
        CHECK(strip.forkNode == 2);
        CHECK(strip.forkAlt.has_value() && strip.forkAlt->label == "Elite: Hardened");
    }
    WinRoom(path);
    {
        const auto strip = game::BuildPathStrip(path.registry());
        CHECK(strip.nodes[0].done && strip.nodes[0].current);
    }
    SkipOfferIfActive(path);
    CHECK(Apply(path, game::ERunIntent::ContinueReward));
    {
        const auto strip = game::BuildPathStrip(path.registry());
        CHECK(strip.nodes[1].current);
        CHECK(strip.nodes[0].done);
    }
    CHECK(Apply(path, game::ERunIntent::ChooseEventOption, 0));
    CHECK(Apply(path, game::ERunIntent::TakeFork));
    {
        const auto strip = game::BuildPathStrip(path.registry());
        CHECK(strip.nodes[2].label == "Elite: Hardened");
        CHECK(strip.nodes[2].kind == game::EPathNodeKind::Elite);
        CHECK(strip.nodes[2].current);
        CHECK(!strip.forkAlt.has_value());
    }
    {
        FPathBoot other("path-f9b", {"battle", "event", "battle", "boss-final"});
        StartSeed(other, seed);
        WinRoom(other);
        SkipOfferIfActive(other);
        CHECK(Apply(other, game::ERunIntent::ContinueReward));
        CHECK(Apply(other, game::ERunIntent::ChooseEventOption, 0));
        CHECK(Apply(other, game::ERunIntent::ContinueEvent));
        const auto strip = game::BuildPathStrip(other.registry());
        CHECK(strip.nodes[2].label == "Battle");
        CHECK(!strip.forkAlt.has_value());
        other.run().phase = game::EDungeonPhase::Camp;
        const auto camp = game::BuildPathStrip(other.registry());
        CHECK(camp.nodes[4].label == "Camp" && camp.nodes[4].current);
        for (int i = 0; i < 4; ++i) CHECK(camp.nodes[static_cast<std::size_t>(i)].done);
    }
}

/// Runs `draw` for three ImGui frames and returns what frame 3 logged (the RenderModalLogs pattern).
std::string CaptureFrame(entt::registry& registry, const std::function<void()>& draw) {
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
    const std::string logPath = UniquePath("path-ui") + ".log";
    for (int frame = 0; frame < 3; ++frame) {
        ImGui::NewFrame();
        if (frame == 2) ImGui::LogToFile(0, logPath.c_str());
        draw();
        if (frame == 2) ImGui::LogFinish();
        ImGui::Render();
    }
    std::ifstream in(logPath, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    std::error_code error;
    std::filesystem::remove(logPath, error);
    return text;
}

std::string CaptureModal(entt::registry& registry, std::string_view id) {
    return CaptureFrame(registry, [&] {
        const auto* view = game::FindModalView(registry, id);
        CHECK(view != nullptr);
        view->render(registry);
    });
}

bool Has(const std::string& text, std::string_view needle) { return text.find(needle) != std::string::npos; }

void StripDrawsLabels() {
    FPathBoot path("path-f10", {"battle", "event", "battle", "boss-final"});
    const auto seed = PickSeed(path.registry(), [](const game::FRunPathState& s) {
        return s.hasFork && s.slot == 2 && s.affixId == "affix.hardened";
    });
    CHECK(seed != kNoSeed);
    const std::string text =
        CaptureFrame(path.registry(), [&] { game::DrawPathStrip(path.registry(), 1268.f, 44.f, 1.f); });
    CHECK(Has(text, "[Battle]"));
    CHECK(Has(text, "Event"));
    CHECK(Has(text, "Battle / Elite: Hardened"));
    CHECK(Has(text, "Boss"));
    CHECK(Has(text, "Camp"));
    CHECK(Has(text, ">"));
}

void ForkPanelShowsBothOptions() {
    {
        FPathBoot path("path-f11a", {"battle", "battle", "battle"});
        SeedWithSlot(path, 1);
        ReachRewardStep(path);
        const std::string text = CaptureModal(path.registry(), game::kRunModalReward);
        const auto at = text.find("CHOOSE A PATH");
        CHECK(at != std::string::npos);
        CHECK(Has(text, "Party HP "));
        CHECK(Has(text, "Materials "));
        CHECK(Has(text, "Elite: "));
        CHECK(Has(text, "Reward: rune offer (boosted), +2 materials"));
        CHECK(Has(text, "Standard rewards"));
        if (at != std::string::npos) CHECK(!Has(text.substr(at), "Continue"));
    }
    {
        FPathBoot path("path-f11b", {"battle", "event", "battle"});
        SeedWithSlot(path, 2);
        ReachRewardStep(path);
        CHECK(Apply(path, game::ERunIntent::ContinueReward));
        CHECK(Apply(path, game::ERunIntent::ChooseEventOption, 0));
        const std::string text = CaptureModal(path.registry(), game::kRunModalEvent);
        CHECK(Has(text, "CHOOSE A PATH"));
        CHECK(Has(text, "Elite: "));
    }
    {
        FPathBoot path("path-f11c", {"battle", "event", "battle"}, false, true);
        StartSeed(path, 3);
        ReachRewardStep(path);
        const std::string text = CaptureModal(path.registry(), game::kRunModalReward);
        CHECK(Has(text, "? Unknown event"));
        CHECK(Has(text, "Event: "));
    }
}

// ---------------------------------------------------------------------------------------------
// F20 — the autopilot fork policy
// ---------------------------------------------------------------------------------------------

void AutopilotForkPolicy() {
    for (const bool variant : {true, false}) {
        FPathBoot path("path-f20", {"battle", "battle", "battle"});
        SeedWithSlot(path, 1);
        ReachRewardStep(path);
        const auto policy = game::dev::ParseAutopilotPolicy(variant ? "fork=variant" : std::string_view(game::dev::kDefaultAutopilotPolicy));
        CHECK(policy.has_value());
        game::dev::FRunAutopilot pilot{*policy};
        for (int tick = 0; tick < 10 && path.run().phase == game::EDungeonPhase::Reward; ++tick) {
            const auto status = pilot.tick(path.registry(), std::string(game::kRunModalReward));
            CHECK(status != game::dev::EAutopilotStatus::Stuck);
            FEmberModalHostState host(std::string(game::kRunModalReward), EHostMode::RunPhase);
            host.Update(path.registry(), 1.f / 60.f);
        }
        CHECK(game::EnsureRunPath(path.registry()).choice == (variant ? 1 : 0));
        CHECK(path.run().room == 1);
    }
}

} // namespace

int main() {
    std::cout.setf(std::ios::unitbuf);
    const char* saveDir = nullptr;
#ifdef _MSC_VER
#pragma warning(suppress: 4996)
#endif
    saveDir = std::getenv("GAME01P_SAVE_DIR");
    if (saveDir == nullptr || *saveDir == '\0') {
        const std::string path = (std::filesystem::current_path() / "out" / "test-saves" / "run_path_tests").string();
        std::error_code error;
        std::filesystem::create_directories(path, error);
        _putenv_s("GAME01P_SAVE_DIR", path.c_str());
    }

    return game_test::RunTests("run_path", {
        {"EliteBlockLoadsAndValidates", EliteBlockLoadsAndValidates},
        {"ForkSlotIsSeededAndEligible", ForkSlotIsSeededAndEligible},
        {"AffixIsSeeded", AffixIsSeeded},
        {"ContinueTakesStandardTakeForkTakesVariant", ContinueTakesStandardTakeForkTakesVariant},
        {"EliteAffixScalesEnemies", EliteAffixScalesEnemies},
        {"EliteVictoryPaysOfferAndMaterials", EliteVictoryPaysOfferAndMaterials},
        {"MysteryForkPicksMysteryPool", MysteryForkPicksMysteryPool},
        {"ForkSurvivesSaveAndContentChange", ForkSurvivesSaveAndContentChange},
        {"HostAcceptsTakeFork", HostAcceptsTakeFork},
        {"StripMirrorsSlotsAndPhase", StripMirrorsSlotsAndPhase},
        {"StripDrawsLabels", StripDrawsLabels},
        {"AutopilotForkPolicy", AutopilotForkPolicy},
        {"ForkPanelShowsBothOptions", ForkPanelShowsBothOptions},
    });
}
