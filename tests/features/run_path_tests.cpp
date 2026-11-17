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
#include "ecs/components/FActiveStatuses.h"
#include "ecs/components/FDead.h"
#include "gameplay/combat/FCombatQuery.h"
#include "gameplay/combat/FCombatTypes.h"
#include "gameplay/combat/FEliteRules.h"
#include "gameplay/combat/FStatusRules.h"
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
    return json::parse(R"json({
      "health_percent": 20,
      "affixes": [
        {"id": "affix.spell_fed", "display_name": "Spell-fed", "text": "Each Spell your party uses gives enemies +1 attack from the next round (up to +4)", "rule": "spell_fed", "amount": 1, "max_bonus": 4},
        {"id": "affix.thorned", "display_name": "Thorned", "text": "Melee skills cost the attacker 2 HP per enemy hit (never below 1 HP)", "rule": "thorned", "amount": 2},
        {"id": "affix.escalating", "display_name": "Escalating", "text": "From round 3, enemies gain +1 attack each round (up to +4)", "rule": "escalating", "amount": 1, "from_round": 3, "max_bonus": 4},
        {"id": "affix.unyielding", "display_name": "Unyielding", "text": "Enemies cannot be frozen or paralyzed", "rule": "unyielding", "statuses": ["status.freeze", "status.paralyze"]}
      ]
    })json");
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
    CHECK(sequence->elite.healthPercent == 20);
    CHECK(sequence->elite.affixes.size() == 4u);
    if (sequence->elite.affixes.size() == 4u) {
        const auto& rows = sequence->elite.affixes;
        CHECK(rows[0].id == "affix.spell_fed");
        CHECK(rows[0].displayName == "Spell-fed");
        CHECK(rows[0].rule == game::EEliteRule::SpellFed);
        CHECK(rows[0].amount == 1);
        CHECK(rows[0].maxBonus == 4);
        CHECK(rows[1].rule == game::EEliteRule::Thorned);
        CHECK(rows[1].amount == 2);
        CHECK(rows[2].rule == game::EEliteRule::Escalating);
        CHECK(rows[2].amount == 1);
        CHECK(rows[2].fromRound == 3);
        CHECK(rows[2].maxBonus == 4);
        CHECK(rows[3].rule == game::EEliteRule::Unyielding);
        CHECK(rows[3].statuses.size() == 2u);
        if (rows[3].statuses.size() == 2u) {
            CHECK(rows[3].statuses[0] == "status.freeze");
            CHECK(rows[3].statuses[1] == "status.paralyze");
        }
    }

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
    broken("unknown rule", [](json& e) { e["affixes"][0]["rule"] = "bogus"; });
    broken("spell_fed without max_bonus", [](json& e) { e["affixes"][0].erase("max_bonus"); });
    broken("spell_fed max_bonus below amount", [](json& e) { e["affixes"][0]["max_bonus"] = 0; });
    broken("thorned amount 0", [](json& e) { e["affixes"][1]["amount"] = 0; });
    broken("escalating from_round 0", [](json& e) { e["affixes"][2]["from_round"] = 0; });
    broken("unyielding empty statuses", [](json& e) { e["affixes"][3]["statuses"] = json::array(); });
    broken("duplicate id", [](json& e) { e["affixes"][1]["id"] = "affix.spell_fed"; });
    broken("health_percent -1", [](json& e) { e["health_percent"] = -1; });

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
        CHECK(state.affixId == affixes[game::EventPickHash(seed, 1, 1002) % affixes.size()].id);
        seen.insert(state.affixId);
    }
    CHECK(seen.size() == affixes.size());
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
// WP-44 — the elite battle state
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

// ---------------------------------------------------------------------------------------------
// WP-44 — the four elite rules
// ---------------------------------------------------------------------------------------------

constexpr const char* kSpellSkill = "skill.bolt";   // tags Spell
constexpr const char* kMeleeSkill = "skill.strike"; // tags Melee
constexpr const char* kPlainSkill = "skill.buff";   // neither

/// One battle of the picked seed: the elite branch with `affixId` at slot 1, or (elite = false) the standard
/// branch of the same seed. `h` stays null when no seed carries that affix (the case then fails on the CHECK).
struct FEliteHarness {
    FPathBoot probe;
    std::uint32_t seed{kNoSeed};
    std::unique_ptr<FBattleHarness> h;

    FEliteHarness(const char* name, const char* affixId, bool elite = true)
        : probe(name, {"battle", "battle", "battle"}) {
        seed = PickSeed(probe.registry(), [&](const game::FRunPathState& s) {
            return s.hasFork && s.slot == 1 && s.affixId == affixId;
        });
        CHECK(seed != kNoSeed);
        if (seed == kNoSeed) return;
        h = std::make_unique<FBattleHarness>(seed, probe.assets.Roots());
        h->toRoomOne(elite ? game::ERunIntent::TakeFork : game::ERunIntent::ContinueReward);
    }

    entt::registry& registry() { return h->registry; }
};

const game::gameplay::FSkillContent* TaggedSkill(entt::registry& registry, const char* id, const char* tag) {
    const auto& content = registry.ctx().get<game::gameplay::FContentRegistry>();
    const auto* row = content.skills.find(id);
    CHECK(row != nullptr);
    if (row == nullptr) return nullptr;
    const bool has = std::find(row->tags.begin(), row->tags.end(), std::string(tag)) != row->tags.end();
    CHECK(has);
    return row;
}

entt::entity FirstWith(entt::registry& registry, bool party) {
    for (const auto entity : registry.view<game::FHealth>()) {
        if (party ? registry.all_of<game::FPartyMember>(entity) : registry.all_of<game::FEnemy>(entity)) {
            return entity;
        }
    }
    return entt::null;
}

void ShipEliteRules() {
    const auto dungeon = ReadJsonFile("assets/data/dungeon.json");
    const auto statuses = ReadJsonFile("assets/data/statuses.json");
    const auto& elite = dungeon.at("elite");
    CHECK(elite.at("health_percent").get<int>() == 20);
    CHECK(!elite.contains("reward_materials"));
    const auto& rows = elite.at("affixes");
    CHECK(rows.size() == 4u);
    if (rows.size() != 4u) return;
    CHECK(rows[0].at("id") == "affix.spell_fed");
    CHECK(rows[0].at("rule") == "spell_fed");
    CHECK(rows[0].at("amount").get<int>() == 1);
    CHECK(rows[0].at("max_bonus").get<int>() == 4);
    CHECK(rows[1].at("id") == "affix.thorned");
    CHECK(rows[1].at("rule") == "thorned");
    CHECK(rows[1].at("amount").get<int>() == 2);
    CHECK(rows[2].at("id") == "affix.escalating");
    CHECK(rows[2].at("rule") == "escalating");
    CHECK(rows[2].at("amount").get<int>() == 1);
    CHECK(rows[2].at("from_round").get<int>() == 3);
    CHECK(rows[2].at("max_bonus").get<int>() == 4);
    CHECK(rows[3].at("id") == "affix.unyielding");
    CHECK(rows[3].at("rule") == "unyielding");
    CHECK(rows[3].at("statuses").size() == 2u);
    for (const auto& id : rows[3].at("statuses")) {
        bool found = false;
        for (const auto& status : statuses.at("statuses")) {
            if (status.at("id") == id) found = true;
        }
        CHECK(found);
    }
}

void EliteBattleInstallsState() {
    for (const char* wanted : {"affix.spell_fed", "affix.thorned", "affix.escalating", "affix.unyielding"}) {
        FEliteHarness elite("path-f3e", wanted);
        FEliteHarness standard("path-f3s", wanted, false);
        if (!elite.h || !standard.h) continue;
        const auto* state = game::FindEliteBattle(elite.registry());
        CHECK(state != nullptr);
        if (state == nullptr) continue;
        CHECK(state->affix.id == wanted);
        CHECK(state->attackBonus == 0);
        CHECK(state->pendingBonus == 0);
        CHECK(game::FindEliteBattle(standard.registry()) == nullptr);
        const auto eliteEnemies = elite.h->enemies();
        const auto standardEnemies = standard.h->enemies();
        CHECK(!eliteEnemies.empty());
        CHECK(eliteEnemies.size() == standardEnemies.size());
        CHECK(elite.h->partyMaxHp() == standard.h->partyMaxHp());
        for (std::size_t i = 0; i < eliteEnemies.size() && i < standardEnemies.size(); ++i) {
            CHECK(eliteEnemies[i].hpMax == standardEnemies[i].hpMax + standardEnemies[i].hpMax * 20 / 100);
            CHECK(eliteEnemies[i].attack == standardEnemies[i].attack);
            CHECK(eliteEnemies[i].magic == standardEnemies[i].magic);
        }
    }
}

void SpellFedQueuesUntilNextRound() {
    FEliteHarness e("path-f4", "affix.spell_fed");
    if (!e.h) return;
    auto& registry = e.registry();
    const auto* state = game::FindEliteBattle(registry);
    CHECK(state != nullptr);
    if (state == nullptr) return;
    const auto* spell = TaggedSkill(registry, kSpellSkill, "Spell");
    const auto* plain = TaggedSkill(registry, kPlainSkill, "Support");
    if (spell == nullptr || plain == nullptr) return;
    const auto& content = registry.ctx().get<game::gameplay::FContentRegistry>();
    const auto party = FirstWith(registry, true);
    const auto foe = FirstWith(registry, false);
    CHECK(party != entt::null);
    CHECK(foe != entt::null);
    const auto before = e.h->enemies();

    game::EliteOnSkillUsed(registry, party, *spell);
    game::EliteOnSkillUsed(registry, party, *spell);
    CHECK(state->pendingBonus == 2);
    CHECK(state->attackBonus == 0);
    const auto queued = e.h->enemies();
    for (std::size_t i = 0; i < before.size() && i < queued.size(); ++i) {
        CHECK(queued[i].attack == before[i].attack);
        CHECK(queued[i].magic == before[i].magic);
    }
    game::EliteOnSkillUsed(registry, party, *plain);
    game::EliteOnSkillUsed(registry, foe, *spell);
    CHECK(state->pendingBonus == 2);

    game::EliteOnRoundStarted(registry, content, 2);
    CHECK(state->attackBonus == 2);
    CHECK(state->pendingBonus == 0);
    auto after = e.h->enemies();
    for (std::size_t i = 0; i < before.size() && i < after.size(); ++i) {
        CHECK(after[i].attack == before[i].attack + 2);
        CHECK(after[i].magic == before[i].magic + 2);
    }
    game::EliteOnRoundStarted(registry, content, 2);
    CHECK(state->attackBonus == 2);

    for (int i = 0; i < 5; ++i) game::EliteOnSkillUsed(registry, party, *spell);
    CHECK(state->pendingBonus == 2);
    game::EliteOnRoundStarted(registry, content, 3);
    CHECK(state->attackBonus == 4);
    after = e.h->enemies();
    for (std::size_t i = 0; i < before.size() && i < after.size(); ++i) {
        CHECK(after[i].attack == before[i].attack + 4);
        CHECK(after[i].magic == before[i].magic + 4);
    }
    game::EliteOnSkillUsed(registry, party, *spell);
    CHECK(state->pendingBonus == 0);
}

void EscalatingFromRound() {
    FEliteHarness e("path-f5", "affix.escalating");
    if (!e.h) return;
    auto& registry = e.registry();
    const auto* state = game::FindEliteBattle(registry);
    CHECK(state != nullptr);
    if (state == nullptr) return;
    const auto* spell = TaggedSkill(registry, kSpellSkill, "Spell");
    if (spell == nullptr) return;
    const auto& content = registry.ctx().get<game::gameplay::FContentRegistry>();
    const auto before = e.h->enemies();
    game::EliteOnRoundStarted(registry, content, 1);
    game::EliteOnRoundStarted(registry, content, 2);
    CHECK(state->attackBonus == 0);
    game::EliteOnRoundStarted(registry, content, 3);
    CHECK(state->attackBonus == 1);
    game::EliteOnRoundStarted(registry, content, 3);
    CHECK(state->attackBonus == 1);
    const int expected[] = {2, 3, 4};
    for (std::uint64_t round = 4; round <= 6; ++round) {
        game::EliteOnRoundStarted(registry, content, round);
        CHECK(state->attackBonus == expected[round - 4]);
    }
    game::EliteOnRoundStarted(registry, content, 7);
    CHECK(state->attackBonus == 4);
    const auto after = e.h->enemies();
    for (std::size_t i = 0; i < before.size() && i < after.size(); ++i) {
        CHECK(after[i].attack == before[i].attack + 4);
        CHECK(after[i].magic == before[i].magic + 4);
    }
    game::EliteOnSkillUsed(registry, FirstWith(registry, true), *spell);
    CHECK(state->pendingBonus == 0);
}

void ThornedHurtsMeleeAttacker() {
    FEliteHarness e("path-f6t", "affix.thorned");
    FEliteHarness standard("path-f6s", "affix.thorned", false);
    if (!e.h || !standard.h) return;
    auto& registry = e.registry();
    const auto* state = game::FindEliteBattle(registry);
    CHECK(state != nullptr);
    if (state == nullptr) return;
    CHECK(TaggedSkill(registry, kMeleeSkill, "Melee") != nullptr);
    CHECK(TaggedSkill(registry, kSpellSkill, "Spell") != nullptr);
    const auto party = FirstWith(registry, true);
    const auto foe = FirstWith(registry, false);
    CHECK(party != entt::null);
    CHECK(foe != entt::null);
    if (party == entt::null || foe == entt::null) return;
    auto& health = registry.get<game::FHealth>(party);
    health.current = 10;
    const auto foeHp = registry.get<game::FHealth>(foe).current;
    game::EliteOnHit(registry, {.owner = party, .other = foe, .amount = 5, .skillId = kMeleeSkill});
    CHECK(health.current == 8);
    game::EliteOnHit(registry, {.owner = party, .other = foe, .amount = 5, .skillId = kSpellSkill});
    CHECK(health.current == 8);
    game::EliteOnHit(registry, {.owner = party, .other = foe, .amount = 0, .skillId = kMeleeSkill});
    CHECK(health.current == 8);
    game::EliteOnHit(registry, {.owner = foe, .other = party, .amount = 5, .skillId = kMeleeSkill});
    CHECK(health.current == 8);
    CHECK(registry.get<game::FHealth>(foe).current == foeHp);
    health.current = 2;
    game::EliteOnHit(registry, {.owner = party, .other = foe, .amount = 5, .skillId = kMeleeSkill});
    CHECK(health.current == 1);
    game::EliteOnHit(registry, {.owner = party, .other = foe, .amount = 5, .skillId = kMeleeSkill});
    CHECK(health.current == 1);
    CHECK(!registry.all_of<game::FDead>(party));

    auto& other = standard.registry();
    const auto otherParty = FirstWith(other, true);
    const auto otherFoe = FirstWith(other, false);
    if (otherParty == entt::null || otherFoe == entt::null) return;
    other.get<game::FHealth>(otherParty).current = 10;
    game::EliteOnHit(other, {.owner = otherParty, .other = otherFoe, .amount = 5, .skillId = kMeleeSkill});
    CHECK(other.get<game::FHealth>(otherParty).current == 10);
}

void UnyieldingBlocksControl() {
    FEliteHarness e("path-f7u", "affix.unyielding");
    FEliteHarness standard("path-f7s", "affix.unyielding", false);
    if (!e.h || !standard.h) return;
    int enemies = 0;
    for (const auto entity : e.registry().view<game::FEnemy>()) {
        ++enemies;
        const auto* active = e.registry().try_get<game::FActiveStatuses>(entity);
        CHECK(active != nullptr);
        if (active == nullptr) continue;
        CHECK(game::IsStatusImmune(*active, "status.freeze"));
        CHECK(game::IsStatusImmune(*active, "status.paralyze"));
        CHECK(std::count(active->immune.begin(), active->immune.end(), std::string("status.freeze")) == 1);
        CHECK(std::count(active->immune.begin(), active->immune.end(), std::string("status.paralyze")) == 1);
    }
    CHECK(enemies > 0);
    for (const auto entity : standard.registry().view<game::FEnemy>()) {
        const auto* active = standard.registry().try_get<game::FActiveStatuses>(entity);
        if (active == nullptr) continue;
        CHECK(!game::IsStatusImmune(*active, "status.freeze"));
        CHECK(!game::IsStatusImmune(*active, "status.paralyze"));
    }
}

void EliteHudLine() {
    {
        FEliteHarness standard("path-f8s", "affix.thorned", false);
        if (standard.h) CHECK(game::EliteRuleHudLine(standard.registry()).empty());
    }
    const auto endsWith = [](const std::string& text, const std::string& tail) {
        return text.size() >= tail.size() && text.compare(text.size() - tail.size(), tail.size(), tail) == 0;
    };
    {
        FEliteHarness e("path-f8t", "affix.thorned");
        if (!e.h) return;
        auto& registry = e.registry();
        const auto* state = game::FindEliteBattle(registry);
        CHECK(state != nullptr);
        if (state == nullptr) return;
        const std::string line = game::EliteRuleHudLine(registry);
        const std::string head = "Thorned: Melee skills cost the attacker 2 HP per enemy hit (never below 1 HP) ";
        CHECK(line.rfind(head, 0) == 0);
        std::string skills = game::EliteAffectedSkills(registry, state->affix);
        CHECK(!skills.empty() && skills.back() == ' ');
        if (!skills.empty()) skills.pop_back();
        CHECK(line == head + skills);
        const bool none = skills == "Your party has no Melee skills.";
        CHECK(skills.rfind("Your Melee skills: ", 0) == 0 || none);
        const auto rows = game::PartySkillRows(registry);
        std::string expectedList;
        for (const auto* row : rows) {
            if (std::find(row->tags.begin(), row->tags.end(), std::string("Melee")) == row->tags.end()) continue;
            if (!expectedList.empty()) expectedList += ", ";
            expectedList += row->displayName;
        }
        if (expectedList.empty()) {
            CHECK(none);
        } else {
            CHECK(skills == "Your Melee skills: " + expectedList + ".");
        }

        auto& run = registry.ctx().get<game::FDungeonRun>();
        run.phase = game::EDungeonPhase::Reward;
        CHECK(game::EliteRuleHudLine(registry).empty());
        run.phase = game::EDungeonPhase::Battle;
        CHECK(!game::EliteRuleHudLine(registry).empty());
    }
    {
        FEliteHarness e("path-f8e", "affix.escalating");
        if (!e.h) return;
        const auto* state = game::FindEliteBattle(e.registry());
        CHECK(state != nullptr);
        if (state != nullptr) CHECK(game::EliteAffectedSkills(e.registry(), state->affix).empty());
    }
    {
        FEliteHarness e("path-f8f", "affix.spell_fed");
        if (!e.h) return;
        auto& registry = e.registry();
        const auto* spell = TaggedSkill(registry, kSpellSkill, "Spell");
        if (spell == nullptr) return;
        const auto& content = registry.ctx().get<game::gameplay::FContentRegistry>();
        CHECK(endsWith(game::EliteRuleHudLine(registry), "  |  enemy attack +0"));
        game::EliteOnSkillUsed(registry, FirstWith(registry, true), *spell);
        CHECK(endsWith(game::EliteRuleHudLine(registry), "  |  enemy attack +0 (+1 next round)"));
        game::EliteOnRoundStarted(registry, content, 2);
        CHECK(endsWith(game::EliteRuleHudLine(registry), "  |  enemy attack +1"));
    }
}

void EliteFeedLines() {
    {
        FEliteHarness e("path-f11e", "affix.escalating");
        if (!e.h) return;
        auto& registry = e.registry();
        if (registry.ctx().find<game::FCombatLog>() == nullptr) registry.ctx().emplace<game::FCombatLog>();
        auto& log = *registry.ctx().find<game::FCombatLog>();
        const auto& content = registry.ctx().get<game::gameplay::FContentRegistry>();
        game::EliteOnRoundStarted(registry, content, 3);
        CHECK(!log.lines.empty());
        if (!log.lines.empty()) CHECK(log.lines.back() == "Escalating: enemies +1 attack (now +1)");
        const auto count = log.lines.size();
        game::EliteOnRoundStarted(registry, content, 3);
        CHECK(log.lines.size() == count);
    }
    {
        FEliteHarness e("path-f11t", "affix.thorned");
        if (!e.h) return;
        auto& registry = e.registry();
        if (registry.ctx().find<game::FCombatLog>() == nullptr) registry.ctx().emplace<game::FCombatLog>();
        auto& log = *registry.ctx().find<game::FCombatLog>();
        const auto party = FirstWith(registry, true);
        const auto foe = FirstWith(registry, false);
        if (party == entt::null || foe == entt::null) return;
        registry.get<game::FHealth>(party).current = 10;
        game::EliteOnHit(registry, {.owner = party, .other = foe, .amount = 5, .skillId = kMeleeSkill});
        CHECK(!log.lines.empty());
        if (!log.lines.empty()) {
            CHECK(log.lines.back() == game::UnitDisplayName(registry, party) + " takes 2 damage (Thorned)");
        }
        const auto count = log.lines.size();
        game::EliteOnHit(registry, {.owner = party, .other = foe, .amount = 5, .skillId = kSpellSkill});
        CHECK(log.lines.size() == count);
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

/// The fixture runes with tiers (the frozen file has none): every row uncommon except fire, cold and lightning.
json TieredRunes() {
    auto document = FixtureDoc("runes.json");
    for (auto& row : document.at("runes")) {
        const std::string id = row.at("id").get<std::string>();
        const bool common = id == "rune.fire" || id == "rune.cold" || id == "rune.lightning";
        row["rarity"] = common ? "common" : "uncommon";
    }
    return document;
}

struct FTieredPathBoot {
    FTempAssets assets;
    FBoot boot;

    FTieredPathBoot(const char* stem, const std::vector<std::string_view>& rooms)
        : assets(stem),
          boot((assets.Write("dungeon.json", PathDungeon(rooms)), assets.Write("events.json", PathEvents()),
                assets.Write("rewards.json", TieredRewards()), assets.Write("runes.json", TieredRunes()),
                UniquePath(stem)),
               assets.Roots()) {}
};

void EliteVictoryPaysOffer() {
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
    const auto isCommon = [](const std::string& id) {
        return id == "rune.fire" || id == "rune.cold" || id == "rune.lightning";
    };
    FTieredPathBoot probe("path-f6probe", {"battle", "battle", "battle"});
    const auto seed = PickSeed(probe.boot.registry, [](const game::FRunPathState& s) { return s.hasFork && s.slot == 1; });
    CHECK(seed != kNoSeed);
    int eliteGain = -1;
    int standardGain = -2;
    {
        FTieredPathBoot boot("path-f6a", {"battle", "battle", "battle"});
        reachRoomOne(boot, seed, game::ERunIntent::TakeFork);
        const int before = boot.boot.run().materials;
        CHECK(game::ResolveRoomVictory(boot.boot.registry, true).has_value());
        eliteGain = boot.boot.run().materials - before;
        const auto& rewards = boot.boot.registry.ctx().get<game::FRewards>();
        CHECK(rewards.offer.active);
        CHECK(rewards.offer.rarityTable == "boss");
        CHECK(!rewards.offer.candidates.empty());
        for (const auto& id : rewards.offer.candidates) CHECK(!isCommon(id));
        CHECK(rewards.pity == 0);
        CHECK(boot.boot.run().message.find("Elite defeated") != std::string::npos);
        CHECK(boot.boot.run().message.find("materials") == std::string::npos);
    }
    {
        FTieredPathBoot boot("path-f6b", {"battle", "battle", "battle"});
        reachRoomOne(boot, seed, game::ERunIntent::ContinueReward);
        const int before = boot.boot.run().materials;
        CHECK(game::ResolveRoomVictory(boot.boot.registry, true).has_value());
        standardGain = boot.boot.run().materials - before;
        CHECK(standardGain == game::DungeonBalance(boot.boot.registry, "room_materials"));
        CHECK(!boot.boot.registry.ctx().get<game::FRewards>().offer.active);
    }
    CHECK(eliteGain == standardGain);

    // Dry pool: every uncommon rune but one is owned.
    const auto allRunes = FixtureDoc("runes.json").at("runes");
    const auto ownAllButOne = [&](FTieredPathBoot& boot) {
        game::StartRun(boot.boot.registry, seed, true);
        std::string spare;
        for (const auto& row : allRunes) {
            const std::string id = row.at("id").get<std::string>();
            if (isCommon(id)) continue;
            if (spare.empty()) {
                spare = id;
                continue;
            }
            boot.boot.run().runes.push_back(id);
        }
    };
    {
        FTieredPathBoot boot("path-f6c", {"battle", "battle", "battle"});
        ownAllButOne(boot);
        const auto strict = game::RollRuneCandidates(boot.boot.registry, game::kRarityTableBoss, true);
        CHECK(strict.size() <= 2u);
        for (const auto& id : strict) CHECK(!isCommon(id));
    }
    {
        FTieredPathBoot boot("path-f6d", {"battle", "battle", "battle"});
        ownAllButOne(boot);
        const auto loose = game::RollRuneCandidates(boot.boot.registry, game::kRarityTableBoss, false);
        bool hasCommon = false;
        for (const auto& id : loose) hasCommon = hasCommon || isCommon(id);
        CHECK(hasCommon);
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
        return s.hasFork && s.slot == 2 && s.affixId == "affix.unyielding";
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
        CHECK(strip.forkAlt.has_value() && strip.forkAlt->label == "Elite: Unyielding");
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
        CHECK(strip.nodes[2].label == "Elite: Unyielding");
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
        return s.hasFork && s.slot == 2 && s.affixId == "affix.unyielding";
    });
    CHECK(seed != kNoSeed);
    const std::string text =
        CaptureFrame(path.registry(), [&] { game::DrawPathStrip(path.registry(), 1268.f, 44.f, 1.f); });
    CHECK(Has(text, "[Battle]"));
    CHECK(Has(text, "Event"));
    CHECK(Has(text, "Battle / Elite: Unyielding"));
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
        CHECK(Has(text, "Enemy max HP +20%"));
        CHECK(Has(text, "uncommon or better"));
        CHECK(!Has(text, "boosted"));
        CHECK(!Has(text, "+2 materials"));
        {
            const auto& sequence = path.registry().ctx().get<game::FRunSequence>();
            const auto& state = game::EnsureRunPath(path.registry());
            const game::FEliteAffix* affix = nullptr;
            for (const auto& row : sequence.elite.affixes) {
                if (row.id == state.affixId) affix = &row;
            }
            CHECK(affix != nullptr);
            const auto options = game::ForkOptions(path.registry());
            CHECK(options.size() == 2u);
            if (affix != nullptr && options.size() == 2u) {
                CHECK(options[1].detail == "Enemy max HP +20%. " + affix->text + ". " +
                                               game::EliteAffectedSkills(path.registry(), *affix) +
                                               "Reward: rune offer (uncommon or better)");
            }
        }
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
        {"ShipEliteRules", ShipEliteRules},
        {"EliteBattleInstallsState", EliteBattleInstallsState},
        {"SpellFedQueuesUntilNextRound", SpellFedQueuesUntilNextRound},
        {"EscalatingFromRound", EscalatingFromRound},
        {"ThornedHurtsMeleeAttacker", ThornedHurtsMeleeAttacker},
        {"UnyieldingBlocksControl", UnyieldingBlocksControl},
        {"EliteHudLine", EliteHudLine},
        {"EliteFeedLines", EliteFeedLines},
        {"EliteVictoryPaysOffer", EliteVictoryPaysOffer},
        {"MysteryForkPicksMysteryPool", MysteryForkPicksMysteryPool},
        {"ForkSurvivesSaveAndContentChange", ForkSurvivesSaveAndContentChange},
        {"HostAcceptsTakeFork", HostAcceptsTakeFork},
        {"StripMirrorsSlotsAndPhase", StripMirrorsSlotsAndPhase},
        {"StripDrawsLabels", StripDrawsLabels},
        {"AutopilotForkPolicy", AutopilotForkPolicy},
        {"ForkPanelShowsBothOptions", ForkPanelShowsBothOptions},
    });
}
