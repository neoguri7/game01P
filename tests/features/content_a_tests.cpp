// WP-22 content tests: the twelve new skills, the five new runes, the two new engine archetypes, the
// derived tag-vocabulary gate, the units.json pools with the first-4 default loadout, and the two report
// models (ignition timing; capped-loadout full-dungeon auto-run with an uncapped full-pool baseline).
//
// Structure:
//   * data/boot tests on SHIP content (ShipAssetRoots): counts, cross-references, the derived vocabulary,
//     the temp-document boot failures, the engine archetypes;
//   * real-resolver tests on SHIP content — every new rune hook fires through the actual OnHit/OnKill/
//     OnDamageTaken seams of ActionResolverSystem (hand-built battles, the tempo/ignition recipes);
//   * pool/loadout tests — pool covers, size minimums, the BuildLoadout first-4 cap and the
//     placement.skills override regression;
//   * AI contract — an enemy injected with new skills acts through the existing behavior tree;
//   * two report tests (printed numbers, never gates — R3-review #1):
//       IgnitionTimingSimulated: pure seeded Monte Carlo, first dungeon where an engine is establishable;
//       CappedLoadoutReport: one full dungeon auto-run, capped first-4 (3 seeds) + full pool (seed 1).
//
// The ship content is the subject: tests/fixtures/** is never edited (its content stays the frozen
// WP-16-era snapshot), so every resolver test here loads ShipAssetRoots().

#define GAME01P_TEST_SUPPORT_CHECK
#include "../TestSupport.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/AssetManager.h"
#include "core/events/FEventBus.h"
#include "ecs/components/FActionPoints.h"
#include "ecs/components/FActiveStatuses.h"
#include "ecs/components/FBaseCombatStats.h"
#include "ecs/components/FDead.h"
#include "ecs/components/FFaction.h"
#include "ecs/components/FGridPosition.h"
#include "ecs/components/FHealth.h"
#include "ecs/components/FPosition.h"
#include "ecs/components/FSkillLoadout.h"
#include "ecs/components/FSpeed.h"
#include "ecs/components/FStats.h"
#include "ecs/components/FTag.h"
#include "ecs/components/FTurnParticipant.h"
#include "ecs/systems/ActionResolverSystem.h"
#include "ecs/systems/CombatOutcomeSystem.h"
#include "ecs/systems/TurnFlowSystem.h"
#include "ecs/systems/TurnSystem.h"
#include "gameplay/battle/FBattleSetup.h"
#include "gameplay/combat/FBattleCommand.h"
#include "gameplay/combat/FBattleEvents.h"
#include "gameplay/combat/FEffectHooks.h"
#include "gameplay/combat/FSkillResolver.h"
#include "gameplay/combat/FStatusRules.h"
#include "gameplay/data/FContentRegistry.h"
#include "gameplay/data/FEngineContent.h"
#include "gameplay/dungeon/FDungeonRun.h"
#include "gameplay/dungeon/FRewards.h"
#include "gameplay/dungeon/FRunSequence.h"
#include "gameplay/factory/FEntityDefinitions.h"
#include "gameplay/grid/FBattlefieldData.h"
#include "gameplay/grid/FGridTerrain.h"
#include "gameplay/grid/FGridTypes.h"
#include "states/FEmberBattleState.h"

namespace {

using game::ECommandReject;
using game::FCommandResult;
using game::FTurnToken;

// ---------------------------------------------------------------------------------------------
// Filesystem / data helpers (the tempo_skills_tests recipe)
// ---------------------------------------------------------------------------------------------

/// Always the override build-test.ps1 / CMake set (WP-00b), never the user's real save.
std::filesystem::path SaveDirRoot() {
    if (const char* overrideDir = std::getenv("GAME01P_SAVE_DIR");
        overrideDir != nullptr && *overrideDir != '\0') {
        return std::filesystem::path(overrideDir);
    }
    return std::filesystem::current_path() / "out" / "test-saves" / "content_a_tests";
}

std::filesystem::path MakeCheckpointPath(std::string_view label) {
    const std::filesystem::path root = SaveDirRoot();
    std::error_code error;
    std::filesystem::create_directories(root, error);
    static int counter = 0;
    return root / ("content-a-" + std::string{label} + "-" +
                   std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
                   std::to_string(counter++) + ".json");
}

void RemoveCheckpointFiles(const std::filesystem::path& checkpoint) {
    std::error_code error;
    std::filesystem::remove(checkpoint, error);
    std::filesystem::remove(checkpoint.string() + ".run.json", error);
    std::filesystem::remove(checkpoint.string() + ".run.json.tmp", error);
}

/// A unique throwaway asset root with a data/ subdirectory, removed when it goes out of scope.
struct FTempRoot {
    std::filesystem::path root;

    explicit FTempRoot(std::string_view label) {
        static int counter = 0;
        const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
        root = std::filesystem::temp_directory_path() /
               ("game01p-wp22-" + std::string{label} + "-" + std::to_string(ticks) + "-" + std::to_string(counter++));
        std::error_code error;
        std::filesystem::remove_all(root, error);
        CHECK(std::filesystem::create_directories(root / "data"));
    }

    ~FTempRoot() {
        std::error_code error;
        std::filesystem::remove_all(root, error);
    }

    FTempRoot(const FTempRoot&) = delete;
    FTempRoot& operator=(const FTempRoot&) = delete;
};

nlohmann::json ReadJsonFile(const std::filesystem::path& path) {
    std::ifstream file(path);
    CHECK(file.good());
    nlohmann::json document;
    file >> document;
    return document;
}

void WriteJsonFile(const std::filesystem::path& path, const nlohmann::json& document) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << document.dump(2);
    file.flush();
    CHECK(file.good());
}

// ---------------------------------------------------------------------------------------------
// Content helpers (ship content unless stated otherwise)
// ---------------------------------------------------------------------------------------------

[[nodiscard]] game::gameplay::FContentRegistry ShipContent() {
    const game::FAssetManager assets{ShipAssetRoots()};
    std::optional<game::gameplay::FContentRegistry> content = game::gameplay::FContentRegistry::load(assets);
    CHECK(content.has_value());
    return std::move(*content);
}

[[nodiscard]] std::vector<std::string> PoolOf(
    const game::gameplay::FContentRegistry& content, std::string_view unitId) {
    const game::gameplay::FUnitContent* unit = content.units.find(unitId);
    CHECK(unit != nullptr);
    return unit->skillIds;
}

[[nodiscard]] std::vector<const game::gameplay::FSkillContent*> SkillsOrDefaultFour(
    const game::gameplay::FContentRegistry& content, std::string_view unitId) {
    std::vector<const game::gameplay::FSkillContent*> skills;
    const std::vector<std::string> pool = PoolOf(content, unitId);
    for (std::size_t index = 0; index < pool.size() && index < 4; ++index) {
        const game::gameplay::FSkillContent* skill = content.skills.find(pool[index]);
        CHECK(skill != nullptr);
        skills.push_back(skill);
    }
    return skills;
}

/// Every requires_all/any tag must be inside the derived vocabulary (ship content):
/// skills.tags ∪ runes.adds_tags — the FContentRegistry gate mirrored at the data level.
[[nodiscard]] std::set<std::string> DerivedTagVocabulary(const game::gameplay::FContentRegistry& content) {
    std::set<std::string> known;
    for (const game::gameplay::FSkillContent& skill : content.skills.rows) {
        known.insert(skill.tags.begin(), skill.tags.end());
    }
    for (const game::gameplay::FRuneContent& rune : content.runes.rows) {
        known.insert(rune.addsTags.begin(), rune.addsTags.end());
    }
    return known;
}

[[nodiscard]] bool SlotIsLegal(
    game::FSkillSlot slot, const game::gameplay::FContentRegistry& content) {
    return !game::ValidateSkillSlot(slot, content).has_value();
}

[[nodiscard]] game::FSkillSlot Slot(std::string skillId, std::vector<std::string> runes = {}) {
    return game::FSkillSlot{std::move(skillId), std::move(runes)};
}

[[nodiscard]] bool HasStatus(entt::registry& registry, entt::entity unit, std::string_view statusId) {
    const auto* active = registry.try_get<game::FActiveStatuses>(unit);
    if (active == nullptr) {
        return false;
    }
    return std::ranges::any_of(active->effects, [&](const game::FStatusEffect& effect) {
        return std::string_view{effect.statusId} == statusId;
    });
}

[[nodiscard]] game::FStatusEffect* FindStatus(
    entt::registry& registry, entt::entity unit, std::string_view statusId) {
    auto* active = registry.try_get<game::FActiveStatuses>(unit);
    if (active == nullptr) {
        return nullptr;
    }
    for (game::FStatusEffect& effect : active->effects) {
        if (std::string_view{effect.statusId} == statusId) {
            return &effect;
        }
    }
    return nullptr;
}

const game::FCombatLog& Log(entt::registry& registry) { return registry.ctx().get<game::FCombatLog>(); }

[[nodiscard]] int CountLines(const std::vector<std::string>& lines, std::string_view needle) {
    return static_cast<int>(std::ranges::count_if(
        lines, [&](const std::string& line) { return line.find(needle) != std::string::npos; }));
}

// ---------------------------------------------------------------------------------------------
// Hand-built battle fixture (the tempo_skills_tests recipe; ship content so the new rows exist)
// ---------------------------------------------------------------------------------------------

/// Content + the minimal battle contexts on an empty grid. Every unit is 50 HP; the party hits for
/// `offense + power - 2` and the enemies for `6 + power - 2`, so the tests can name exact numbers.
struct FContentFixture {
    std::filesystem::path checkpoint;
    game_test::FTestRegistry loaded;
    entt::entity actor{entt::null};
    entt::entity foe{entt::null};
    entt::entity foe2{entt::null};
    entt::entity foe3{entt::null};

    explicit FContentFixture(std::string_view label, std::vector<std::string> roots = ShipAssetRoots())
        : checkpoint(MakeCheckpointPath(label)),
          loaded(game_test::LoadTestRegistry(std::move(roots), checkpoint.string())) {
        CHECK(loaded.content);
        CHECK(loaded.dungeon);
        auto& r = registry();
        r.ctx().emplace<game::FCombatLog>();
        r.ctx().emplace<game::FActionQueue>();
        r.ctx().emplace<game::FTurnRuntime>();
        r.ctx().emplace<game::FEventBus>();
        r.ctx().emplace<game::FCombatRuntime>();
        r.ctx().emplace<game::FBattleOutcome>();
        r.ctx().emplace<game::FGridMetrics>();
        r.ctx().emplace<game::FGridTerrain>().moveRange = 2;
        r.ctx().emplace<game::FBattleEvents>();
        r.ctx().emplace<game::FCommandResults>();
        r.ctx().emplace<game::FCommandOutbox>();
        // A bare run: some gameplay readers guard on it (engine seed guard, sheet refresh branch).
        auto& run = r.ctx().emplace<game::FDungeonRun>();
        run.seed = 4242u;
        run.depth = 1;
        run.room = 0;
        actor = MakeUnit("Adam", 5, 5, true, game::FStats{10, 10, 2, 2}, {Slot("skill.strike")});
        foe = MakeUnit("Bela", 5, 4, false, game::FStats{6, 6, 2, 2});
        foe2 = MakeUnit("Cato", 6, 4, false, game::FStats{6, 6, 2, 2});
        foe3 = MakeUnit("Dora", 7, 4, false, game::FStats{6, 6, 2, 2});
        r.emplace<game::FTurnParticipant>(actor);
    }

    ~FContentFixture() { RemoveCheckpointFiles(checkpoint); }

    FContentFixture(const FContentFixture&) = delete;
    FContentFixture& operator=(const FContentFixture&) = delete;

    entt::registry& registry() { return loaded.registry; }

    entt::entity MakeUnit(std::string_view tag, int x, int y, bool party, game::FStats stats,
                          std::vector<game::FSkillSlot> slots = {}) {
        auto& r = registry();
        const entt::entity unit = r.create();
        r.emplace<game::FTag>(unit, game::FTag{std::string(tag)});
        r.emplace<game::FStats>(unit, stats);
        r.emplace<game::FHealth>(unit, game::FHealth{50, 50});
        r.emplace<game::FGridPosition>(unit, game::FGridPosition{x, y});
        r.emplace<FPosition>(unit, FPosition{0.f, 0.f});
        r.emplace<game::FActionPoints>(unit, game::FActionPoints{1, 1, 2, 2});
        r.emplace<game::FSpeed>(unit, game::FSpeed{10});
        r.emplace<game::FActiveStatuses>(unit);
        r.emplace<game::FSkillLoadout>(unit, game::FSkillLoadout{std::move(slots)});
        if (party) {
            r.emplace<game::FPartyMember>(unit);
        } else {
            r.emplace<game::FEnemy>(unit);
        }
        return unit;
    }

    /// Replaces a unit's loadout; a stale lazily-built FActiveHooks list is dropped so the next dispatch
    /// rebuilds it from the new slots.
    void SetSlots(entt::entity unit, std::vector<game::FSkillSlot> slots) {
        registry().emplace_or_replace<game::FSkillLoadout>(unit, game::FSkillLoadout{std::move(slots)});
        if (registry().all_of<game::FActiveHooks>(unit)) {
            registry().remove<game::FActiveHooks>(unit);
        }
    }

    /// Starts the session; the actor is the only participant, so it always opens.
    FTurnToken StartTurn() {
        auto& runtime = registry().ctx().get<game::FTurnRuntime>();
        CHECK(runtime.controller.start(registry(), 20261108u).has_value());
        const auto current = runtime.controller.snapshot().currentTurn;
        CHECK(current.has_value());
        CHECK(current->actor == actor);
        return *current;
    }

    /// Direct push (the pre-API path) + one resolver drain; returns the recorded result.
    FCommandResult Tackle(const FTurnToken& token, entt::entity target, std::string_view skillId) {
        auto& r = registry();
        r.ctx().get<game::FActionQueue>().pending.push_back(
            game::FActionRequest{actor, target, std::string{skillId}, token, {}});
        game::ecs::ActionResolverSystem resolver;
        resolver.update(r, 0.f);
        const auto& recent = r.ctx().get<game::FCommandResults>().recent;
        CHECK(!recent.empty());
        return recent.back();
    }

    [[nodiscard]] const game::gameplay::FContentRegistry& Content() {
        return loaded.registry.ctx().get<game::gameplay::FContentRegistry>();
    }
};

// ---------------------------------------------------------------------------------------------
// Data / boot tests
// ---------------------------------------------------------------------------------------------

/// Skills only enemies use (WP-52: imp_firebomb + the nine new rows); no party pool carries them.
const std::set<std::string> kEnemyOnlySkills{"skill.imp_firebomb", "skill.mend", "skill.bite", "skill.pounce",
                                             "skill.detonate", "skill.war_drum", "skill.ash_bolt", "skill.hex",
                                             "skill.venom_spit", "skill.ground_slam"};

/// SkillsLoadAndRegister: ship content carries 37 rows — 27 player skills (all referenced by some
/// party-class unit pool) + 10 enemy-only rows; every cross-reference resolves.
void SkillsLoadAndRegister() {
    const game::gameplay::FContentRegistry content = ShipContent();
    CHECK(content.skills.rows.size() == 37);

    std::set<std::string> partyPools;
    for (const std::string_view unitId : {"char.warrior", "char.archer", "char.mage", "char.healer"}) {
        for (const std::string& skillId : PoolOf(content, unitId)) {
            partyPools.insert(skillId);
        }
    }
    CHECK(partyPools.size() == 27);

    std::set<std::string> playerRows;
    int enemyOnlyRows = 0;
    for (const game::gameplay::FSkillContent& skill : content.skills.rows) {
        CHECK(skill.apCost >= 1 && skill.apCost <= 2); // D-1: skill_ap_cap 2
        CHECK(!skill.displayName.empty());
        CHECK(!skill.tags.empty());
        if (!skill.appliesStatus.empty()) {
            CHECK(content.statuses.find(skill.appliesStatus) != nullptr);
        }
        if (kEnemyOnlySkills.count(skill.id) == 1) {
            ++enemyOnlyRows;
        } else {
            playerRows.insert(skill.id);
        }
    }
    CHECK(enemyOnlyRows == 10);
    CHECK(playerRows == partyPools); // every player row is equippable by some character pool
    for (const std::string& enemyOnly : kEnemyOnlySkills) {
        CHECK(partyPools.count(enemyOnly) == 0); // an enemy-only row never leaks into a party pool
    }

    for (const std::string& skillId : {"skill.hook", "skill.bash", "skill.charge", "skill.brace",
                                       "skill.mark", "skill.pin", "skill.piercing", "skill.storm",
                                       "skill.frost_lance", "skill.flame_field", "skill.renew",
                                       "skill.static"}) {
        CHECK(content.skills.find(skillId) != nullptr);
    }
    const game::gameplay::FUnitContent* shaman = content.units.find("enemy.imp_shaman");
    CHECK(shaman != nullptr);
    CHECK(std::ranges::find(shaman->skillIds, "skill.imp_firebomb") != shaman->skillIds.end());
}

/// RunesLoadAndRegister: 24 rows, every one drop-weighted, every status reference resolves, and the
/// D-12 shock wording carries the +20%p paralysis-chance note in the display name.
void RunesLoadAndRegister() {
    const game::gameplay::FContentRegistry content = ShipContent();
    CHECK(content.runes.rows.size() == 24);
    for (const game::gameplay::FRuneContent& rune : content.runes.rows) {
        CHECK(rune.dropWeight >= 1);
        if (!rune.statusOnHit.empty()) {
            CHECK(content.statuses.find(rune.statusOnHit) != nullptr);
        }
        if (!rune.statusId.empty()) {
            CHECK(content.statuses.find(rune.statusId) != nullptr);
        }
    }
    for (const std::string& runeId : {"rune.chain", "rune.frostpath", "rune.combust", "rune.guard",
                                      "rune.impact"}) {
        CHECK(content.runes.find(runeId) != nullptr);
    }
    const game::gameplay::FRuneContent* shock = content.runes.find("rune.shock");
    CHECK(shock != nullptr);
    CHECK(shock->displayName == "Amplified Shock (+20%p paralysis chance)");
    CHECK(shock->statusMagnitudeDelta == 20); // the number the wording documents
}

/// AllRunesHaveLegalLoadout: every rune fits at least one skill slot, searched against the REAL
/// ValidateSkillSlot over rune x skill x up-to-sockets combos (R19: a rune nobody can equip would be
/// dead content that the vocabulary gate cannot see).
void AllRunesHaveLegalLoadout() {
    const game::gameplay::FContentRegistry content = ShipContent();
    std::vector<std::string> dead;
    for (const game::gameplay::FRuneContent& rune : content.runes.rows) {
        bool found = false;
        for (const game::gameplay::FSkillContent& skill : content.skills.rows) {
            if (found || skill.runeSockets < 1) {
                continue;
            }
            if (SlotIsLegal({skill.id, {rune.id}}, content)) {
                found = true;
                continue;
            }
            if (skill.runeSockets < 2) {
                continue;
            }
            for (const game::gameplay::FRuneContent& partner : content.runes.rows) {
                if (partner.id == rune.id) {
                    continue;
                }
                if (SlotIsLegal({skill.id, {rune.id, partner.id}}, content)) {
                    found = true;
                    break;
                }
            }
            if (found || skill.runeSockets < 3) {
                continue;
            }
            for (std::size_t a = 0; a < content.runes.rows.size() && !found; ++a) {
                const game::gameplay::FRuneContent& second = content.runes.rows[a];
                if (second.id == rune.id) {
                    continue;
                }
                for (std::size_t b = a + 1; b < content.runes.rows.size() && !found; ++b) {
                    const game::gameplay::FRuneContent& third = content.runes.rows[b];
                    if (third.id == rune.id) {
                        continue;
                    }
                    if (SlotIsLegal({skill.id, {rune.id, second.id, third.id}}, content)) {
                        found = true;
                    }
                }
            }
        }
        if (!found) {
            dead.push_back(rune.id);
        }
    }
    for (const std::string& runeId : dead) {
        std::cout << "[FAIL] no legal loadout for rune " << runeId << '\n';
    }
    CHECK(dead.empty());
}

/// RuneRequiresTagsAreKnown: on ship content every requires_all/any tag is inside the derived vocabulary
/// (skills.tags ∪ runes.adds_tags) — the data-side mirror of the FContentRegistry gate.
void RuneRequiresTagsAreKnown() {
    const game::gameplay::FContentRegistry content = ShipContent();
    const std::set<std::string> known = DerivedTagVocabulary(content);
    int checked = 0;
    for (const game::gameplay::FRuneContent& rune : content.runes.rows) {
        for (const std::string& tag : rune.requiresAllTags) {
            ++checked;
            CHECK(known.count(tag) == 1);
        }
        for (const std::string& tag : rune.requiresAnyTags) {
            ++checked;
            CHECK(known.count(tag) == 1);
        }
    }
    CHECK(checked >= 10); // the walk really looked at the authored gates
}

/// UnknownRequireTagFailsBoot: an authored requires_all/any tag outside the vocabulary fails the boot
/// (temp-document injection); a vacuous excludes tag only warns; the frozen fixture still boots.
void UnknownRequireTagFailsBoot() {
    const nlohmann::json base = ReadJsonFile("assets/data/runes.json");
    const auto findRune = [](nlohmann::json& document, std::string_view id) -> nlohmann::json& {
        for (nlohmann::json& rune : document["runes"]) {
            if (rune.value("id", std::string{}) == id) {
                return rune;
            }
        }
        throw std::runtime_error("rune not found in the ship document");
    };
    const auto boots = [&](const auto& edit) {
        FTempRoot temp{"rune-tags"};
        nlohmann::json document = base;
        edit(document);
        WriteJsonFile(temp.root / "data" / "runes.json", document);
        const std::vector<std::string> roots{temp.root.string(), kShipAssetRoot};
        return game::gameplay::FContentRegistry::load(game::FAssetManager{roots}).has_value();
    };

    CHECK(boots([](nlohmann::json&) {})); // the unmodified copy boots
    CHECK(!boots([&](nlohmann::json& document) {
        findRune(document, "rune.guard")["requires_all_tags"] = std::vector<std::string>{"NotATag"};
    }));
    CHECK(!boots([&](nlohmann::json& document) {
        findRune(document, "rune.impact")["requires_any_tags"] = std::vector<std::string>{"NotATag"};
    }));
    CHECK(boots([&](nlohmann::json& document) {
        findRune(document, "rune.guard")["excludes_tags"] = std::vector<std::string>{"NotATag"};
    }));

    // The frozen fixture content (never edited) still boots with the vocabulary gate in place.
    const game::FAssetManager fixtureAssets{TestAssetRoots()};
    CHECK(game::gameplay::FContentRegistry::load(fixtureAssets).has_value());
}

/// ShippedRunesHaveRarityTable (WP-40 F1): every shipped rune carries the rarity of the plan table.
void ShippedRunesHaveRarityTable() {
    const nlohmann::json document = ReadJsonFile("assets/data/runes.json");
    const std::map<std::string, std::string> expected{
        {"rune.fire", "common"},       {"rune.cold", "common"},        {"rune.lightning", "common"},
        {"rune.power", "common"},      {"rune.swift", "common"},       {"rune.leech", "common"},
        {"rune.inferno", "common"},    {"rune.glacial", "common"},     {"rune.area", "uncommon"},
        {"rune.concentrated", "uncommon"}, {"rune.range", "uncommon"}, {"rune.duration", "uncommon"},
        {"rune.ember", "uncommon"},    {"rune.frostbite", "uncommon"}, {"rune.shock", "uncommon"},
        {"rune.precision", "uncommon"}, {"rune.multishot", "rare"},    {"rune.pierce", "rare"},
        {"rune.zone", "rare"},         {"rune.chain", "rare"},         {"rune.frostpath", "rare"},
        {"rune.combust", "rare"},      {"rune.guard", "rare"},         {"rune.impact", "rare"}};
    std::map<std::string, std::string> seen;
    for (const nlohmann::json& rune : document["runes"]) {
        seen[rune.value("id", std::string{})] = rune.value("rarity", std::string{"<absent>"});
    }
    CHECK(seen.size() == 24u);
    for (const auto& [id, rarity] : expected) {
        CHECK(seen.count(id) == 1);
        CHECK(seen[id] == rarity);
    }
    int common = 0;
    int uncommon = 0;
    int rare = 0;
    for (const auto& [id, rarity] : seen) {
        common += rarity == "common" ? 1 : 0;
        uncommon += rarity == "uncommon" ? 1 : 0;
        rare += rarity == "rare" ? 1 : 0;
    }
    CHECK(common == 8);
    CHECK(uncommon == 8);
    CHECK(rare == 8);
}

/// UnknownRuneRarityFailsBoot (WP-40 F2): an unknown rarity fails the boot; absent reads as common.
void UnknownRuneRarityFailsBoot() {
    const nlohmann::json base = ReadJsonFile("assets/data/runes.json");
    const auto boots = [&](const auto& edit) {
        FTempRoot temp{"rune-rarity"};
        nlohmann::json document = base;
        edit(document);
        WriteJsonFile(temp.root / "data" / "runes.json", document);
        const std::vector<std::string> roots{temp.root.string(), kShipAssetRoot};
        return game::gameplay::FContentRegistry::load(game::FAssetManager{roots}).has_value();
    };
    CHECK(boots([](nlohmann::json&) {}));
    CHECK(!boots([](nlohmann::json& document) { document["runes"][0]["rarity"] = "epic"; }));
    CHECK(boots([](nlohmann::json& document) { document["runes"][0].erase("rarity"); }));

    const game::FAssetManager fixtureAssets{TestAssetRoots()};
    const auto fixture = game::gameplay::FContentRegistry::load(fixtureAssets);
    CHECK(fixture.has_value());
    if (fixture.has_value()) {
        CHECK(!fixture->runes.rows.empty());
        for (const game::gameplay::FRuneContent& rune : fixture->runes.rows) {
            CHECK(rune.rarity == game::gameplay::ERuneRarity::Common);
        }
    }
}

/// AlternativePathExists: engine.ember_kindled's alternative pair (rune.fire + rune.duration) is legal
/// together in one slot of some skill with at least two sockets — R3 P20's "same trigger, second combo".
void AlternativePathExists() {
    const game::gameplay::FContentRegistry content = ShipContent();
    CHECK(content.runes.find("rune.fire") != nullptr);
    CHECK(content.runes.find("rune.duration") != nullptr);
    const game::gameplay::FEngineContent* kindled = content.engines.find("engine.ember_kindled");
    CHECK(kindled != nullptr);
    CHECK((kindled->requiresRunes == std::vector<std::string>{"rune.fire", "rune.duration"}));
    CHECK(kindled->triggerStatusId == "status.burn");

    bool legal = false;
    std::string witness;
    for (const game::gameplay::FSkillContent& skill : content.skills.rows) {
        if (skill.runeSockets < 2) {
            continue;
        }
        if (SlotIsLegal({skill.id, {"rune.fire", "rune.duration"}}, content)) {
            legal = true;
            witness = skill.id;
            break;
        }
    }
    CHECK(legal);
    std::cout << "[report] ember_kindled pair fire+duration is equippable on " << witness << '\n';
}

/// ArchetypesUseExistingVocabulary: all four engines stand on shipped runes and shipped statuses, and
/// every trigger uses the one existing kind (WP-25 vocabulary; no new kind, no system change).
void ArchetypesUseExistingVocabulary() {
    const game::gameplay::FContentRegistry content = ShipContent();
    CHECK(content.engines.rows.size() == 4);

    std::vector<std::string> ids;
    for (const game::gameplay::FEngineContent& engine : content.engines.rows) {
        CHECK(!engine.requiresRunes.empty());
        CHECK(engine.triggerKind == game::gameplay::kEngineTriggerStatusOnTarget);
        CHECK(!engine.log.empty());
        for (const std::string& runeId : engine.requiresRunes) {
            CHECK(content.runes.find(runeId) != nullptr);
        }
        CHECK(content.statuses.find(engine.triggerStatusId) != nullptr);
        ids.push_back(engine.id);
    }
    std::ranges::sort(ids);
    CHECK((ids == std::vector<std::string>{"engine.ember", "engine.ember_kindled", "engine.frost",
                                           "engine.shock"}));

    const game::gameplay::FEngineContent* shock = content.engines.find("engine.shock");
    CHECK(shock != nullptr);
    CHECK((shock->requiresRunes == std::vector<std::string>{"rune.lightning", "rune.shock"}));
    CHECK(shock->triggerStatusId == "status.paralyze");
    const game::gameplay::FEngineContent* kindled = content.engines.find("engine.ember_kindled");
    CHECK(kindled != nullptr);
    CHECK(kindled->triggerStatusId == "status.burn"); // same trigger as engine.ember, different pair
}

/// NoStackTrigger: every engine judges a pre-hit status presence on the one trigger kind — the schema has
/// no stack/count condition and no log carries stacking wording.
void NoStackTrigger() {
    const game::gameplay::FContentRegistry content = ShipContent();
    for (const game::gameplay::FEngineContent& engine : content.engines.rows) {
        CHECK(engine.triggerKind == game::gameplay::kEngineTriggerStatusOnTarget);
        std::string lowered = engine.log;
        std::ranges::transform(lowered, lowered.begin(), [](unsigned char ch) {
            return static_cast<char>(std::tolower(ch));
        });
        CHECK(lowered.find("stack") == std::string::npos);
        CHECK(lowered.find("3x") == std::string::npos);
    }
    // The trigger schema itself: the closed field list has no stack/count condition to author.
    for (const game::FContentField& field : game::gameplay::kEngineTriggerFields) {
        const std::string key{field.key};
        CHECK(key.find("stack") == std::string::npos);
        CHECK(key.find("count") == std::string::npos);
    }
}

// ---------------------------------------------------------------------------------------------
// Ignition timing simulation (report only — R3-review #1)
// ---------------------------------------------------------------------------------------------

/// The offer settings, read from ship rewards.json (never typed in).
struct FOfferConfig {
    double chance{0.0};
    int pityThreshold{0};
    int count{0};
    int minLegalCandidates{0};
    std::string bossOffer;
};

[[nodiscard]] FOfferConfig ReadOfferConfig() {
    const nlohmann::json rewards = ReadJsonFile("assets/data/rewards.json");
    FOfferConfig config;
    config.count = rewards.at("rune_offer").at("count").get<int>();
    config.chance = rewards.at("rune_offer").at("chance").get<double>();
    config.pityThreshold = rewards.at("rune_offer").at("pity_threshold").get<int>();
    config.minLegalCandidates = rewards.at("rune_offer").at("min_legal_candidates").get<int>();
    config.bossOffer = rewards.at("boss_offer").get<std::string>();
    return config;
}

struct FIgnitionSimResult {
    int runs{0};
    int never{0};                                  // no engine established within the dungeon cap
    std::vector<int> firstDungeon;                 // established runs only: the first dungeon number
    std::map<std::string, int> firstEngineCount;   // which engine established first, per run
};

/// A pure, seeded, deterministic Monte Carlo model of the rune-offer economy. Fixed assumptions (all read
/// from ship data, none typed in):
///  * offer chance and pity threshold and card count — rewards.json (0.7 / 2 / 3)
///  * battles and boss slots per dungeon — the shipped dungeon.json v2 sequence (WP-28 A4): for dungeon
///    d, `PositionOf(d - 1)` + `BattleCountAt` + `IsBossBattle`; the themes cycle after the last authored
///    dungeon. The sequence's event slots between battles offer no runes.
///  * a boss battle opens a forced offer iff `boss_offer != "none"` (ship "guaranteed"); with "none" the
///    boss room opens nothing and never rolls chance/pity (FRewards::RollRoomOffer parity). "extra"'s
///    second offer after the first decision is not modelled (no ship value uses it).
///  * D-7 start support does NOT force the first offer here: FRunFlow::StartRunAt seeds the pity streak
///    only for a run starting below the checkpoint (startIndex > 0), and this sim models a fresh start
///    (index 0) — the first monster room rolls chance/pity like every other.
///  * candidate pool = every rune the CAPPED DEFAULT party can equip on its base skill alone
///    (FRewards::RuneFitsSkills semantics, mirrored with the real ValidateSkillSlot). Since the M-FUN S2
///    pool reorder the defaults carry Fire (firebolt), Cold (frost_lance) and Lightning (static), so all
///    24 runes are offerable — pinned by EveryRuneIsOfferLegalForStartingParty.
///  * draw: weighted (runes.json drop_weight) without repeat from the unowned pool, then at most one
///    owned duplicate card (FRewards::CandidatesFor semantics)
///  * pick policy (greedy): a rune that completes an owned engine pair first, else any engine piece,
///    else the highest weight; ties break on id
///  * an engine is established when both runes are owned AND one default-loadout skill with >= 2 sockets
///    accepts both together (ValidateSkillSlot)
///  * 24-rune content pool, camp purchases ignored, cap = 10 dungeons
FIgnitionSimResult RunIgnitionSim(
    const game::gameplay::FContentRegistry& content,
    const game::FRunSequence& sequence,
    const std::vector<const game::gameplay::FSkillContent*>& defaults,
    const FOfferConfig& config, int runs, int dungeonCap, std::uint32_t seed) {
    const auto weightOf = [](const game::gameplay::FRuneContent& rune) {
        return std::max(1, rune.dropWeight);
    };
    const auto pairLegal = [&](const std::string& first, const std::string& second) {
        for (const game::gameplay::FSkillContent* skill : defaults) {
            if (skill->runeSockets < 2) {
                continue;
            }
            if (SlotIsLegal({skill->id, {first, second}}, content)) {
                return true;
            }
        }
        return false;
    };

    // The legal candidate pool for the capped default party (FRewards::RuneFitsSkills rule).
    std::vector<const game::gameplay::FRuneContent*> legalPool;
    for (const game::gameplay::FRuneContent& rune : content.runes.rows) {
        bool fits = false;
        for (const game::gameplay::FSkillContent* skill : defaults) {
            if (skill->runeSockets < 1) {
                continue;
            }
            if (SlotIsLegal({skill->id, {rune.id}}, content)) {
                fits = true;
                break;
            }
        }
        if (fits) {
            legalPool.push_back(&rune);
        }
    }

    // Engines in data order, each with its pair legality precomputed once.
    struct FEnginePair {
        std::string id;
        std::string first;
        std::string second;
        bool establishable{false};
    };
    std::vector<FEnginePair> enginePairs;
    for (const game::gameplay::FEngineContent& engine : content.engines.rows) {
        if (engine.requiresRunes.size() != 2) {
            continue;
        }
        enginePairs.push_back(FEnginePair{engine.id, engine.requiresRunes[0], engine.requiresRunes[1],
                                          pairLegal(engine.requiresRunes[0], engine.requiresRunes[1])});
    }
    for (const FEnginePair& pair : enginePairs) {
        std::cout << "[report]   engine " << pair.id << " pair [" << pair.first << " + " << pair.second << "] "
                  << (pair.establishable ? "is pair-legal" : "is NOT pair-legal")
                  << " on the capped default loadout (establishable once both runes are owned)\n";
    }

    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> unit01(0.0, 1.0);
    const auto pickWeighted = [&](const std::vector<const game::gameplay::FRuneContent*>& pool) {
        int total = 0;
        for (const game::gameplay::FRuneContent* rune : pool) {
            total += weightOf(*rune);
        }
        std::uniform_int_distribution<int> roll(1, total);
        int remaining = roll(rng);
        for (std::size_t index = 0; index < pool.size(); ++index) {
            remaining -= weightOf(*pool[index]);
            if (remaining <= 0) {
                return index;
            }
        }
        return pool.size() - 1;
    };

    const std::size_t minimum =
        static_cast<std::size_t>(std::max(1, config.minLegalCandidates)); // withheld below this
    const bool bossGuaranteed = config.bossOffer != "none";

    FIgnitionSimResult result;
    result.runs = runs;
    for (int run = 0; run < runs; ++run) {
        std::set<std::string> owned;
        int pity = 0; // persists across rooms AND dungeons within the run (FRewards keeps it until an offer)
        bool established = false;
        int firstDungeon = 0;

        const auto drawCandidates = [&]() {
            std::vector<const game::gameplay::FRuneContent*> unowned;
            std::vector<const game::gameplay::FRuneContent*> ownedLegal;
            for (const game::gameplay::FRuneContent* rune : legalPool) {
                (owned.count(rune->id) > 0 ? ownedLegal : unowned).push_back(rune);
            }
            std::vector<std::string> candidates;
            std::vector<const game::gameplay::FRuneContent*> pool = unowned;
            const std::size_t requested = static_cast<std::size_t>(config.count);
            const std::size_t fromUnowned = std::min(requested, pool.size());
            for (std::size_t slot = 0; slot < fromUnowned; ++slot) {
                const std::size_t picked = pickWeighted(pool);
                candidates.push_back(pool[picked]->id);
                pool.erase(pool.begin() + static_cast<std::ptrdiff_t>(picked));
            }
            if (!ownedLegal.empty() && fromUnowned < requested) {
                candidates.push_back(ownedLegal[pickWeighted(ownedLegal)]->id);
            }
            return candidates;
        };
        // One offer attempt: a monster room rolls chance/pity, the boss room is forced (guaranteed).
        const auto attemptOffer = [&](int dungeonNumber, bool forced) {
            if (established) {
                return;
            }
            if (!forced) {
                if (pity >= config.pityThreshold) {
                    forced = true; // pity forces the offer without consuming a roll
                } else if (unit01(rng) >= config.chance) {
                    ++pity; // a miss extends the dry streak
                    return;
                }
            }
            const std::vector<std::string> candidates = drawCandidates();
            if (candidates.size() < minimum) {
                return; // a short pool is not a dry streak: pity is kept, the cursor advanced
            }
            // Greedy pick: completes a pair > engine piece > highest weight; ties on id.
            std::string pick;
            int pickScore = -1;
            int pickWeight = -1;
            for (const std::string& candidate : candidates) {
                bool piece = false;
                bool completes = false;
                for (const FEnginePair& pair : enginePairs) {
                    if (candidate != pair.first && candidate != pair.second) {
                        continue;
                    }
                    piece = true;
                    const std::string& other = candidate == pair.first ? pair.second : pair.first;
                    completes = completes || owned.count(other) > 0;
                }
                const game::gameplay::FRuneContent* row = content.runes.find(candidate);
                const int weight = row != nullptr ? weightOf(*row) : 0;
                const int score = completes ? 2 : (piece ? 1 : 0);
                if (score > pickScore ||
                    (score == pickScore && weight > pickWeight) ||
                    (score == pickScore && weight == pickWeight && candidate < pick)) {
                    pick = candidate;
                    pickScore = score;
                    pickWeight = weight;
                }
            }
            if (pick.empty()) {
                return;
            }
            owned.insert(pick);
            pity = 0;
            for (const FEnginePair& pair : enginePairs) {
                if (pair.establishable && owned.count(pair.first) > 0 && owned.count(pair.second) > 0) {
                    established = true;
                    firstDungeon = dungeonNumber;
                    ++result.firstEngineCount[pair.id];
                    break;
                }
            }
        };

        for (int dungeon = 1; dungeon <= dungeonCap && !established; ++dungeon) {
            // WP-28 A4: the structure is the shipped v2 sequence, not a constant. PositionOf wraps into
            // the themes' cycle; every battle ordinal is either a boss slot (a forced offer when the data
            // allows one, never a chance/pity roll) or a monster room (chance/pity).
            const game::FRunSequence::FPosition position = sequence.PositionOf(dungeon - 1);
            const int battles = sequence.BattleCountAt(position);
            for (int ordinal = 0; ordinal < battles && !established; ++ordinal) {
                if (sequence.IsBossBattle(position, ordinal)) {
                    if (bossGuaranteed) {
                        attemptOffer(dungeon, true);
                    }
                    continue;
                }
                attemptOffer(dungeon, false);
            }
        }
        if (established) {
            result.firstDungeon.push_back(firstDungeon);
        } else {
            ++result.never;
        }
    }
    return result;
}

/// IgnitionTimingSimulated: the report on "in which dungeon does the first engine become establishable".
/// 2000 runs x 10 dungeons; the numbers are printed, never gated; the same seed must reproduce them
/// exactly (the model's only structural invariant).
void IgnitionTimingSimulated() {
    const game::gameplay::FContentRegistry content = ShipContent();
    const FOfferConfig config = ReadOfferConfig();
    // WP-28 A4: the loop structure is the shipped v2 sequence (dungeon.json themes), never a constant.
    std::string sequenceError;
    const std::optional<game::FRunSequence> sequence =
        game::FRunSequence::Load(ReadJsonFile("assets/data/dungeon.json"), sequenceError);
    if (!sequence.has_value()) {
        throw std::runtime_error("the shipped dungeon.json v2 sequence was rejected: " + sequenceError);
    }

    std::vector<const game::gameplay::FSkillContent*> defaults;
    for (const std::string_view unitId : {"char.warrior", "char.archer", "char.mage", "char.healer"}) {
        for (const game::gameplay::FSkillContent* skill : SkillsOrDefaultFour(content, unitId)) {
            if (std::ranges::find(defaults, skill) == defaults.end()) {
                defaults.push_back(skill);
            }
        }
    }

    constexpr int kRuns = 2000;
    constexpr int kDungeonCap = 10;
    constexpr std::uint32_t kSeed = 0xC0FFEEu;
    const FIgnitionSimResult first = RunIgnitionSim(content, *sequence, defaults, config, kRuns, kDungeonCap, kSeed);
    const FIgnitionSimResult second = RunIgnitionSim(content, *sequence, defaults, config, kRuns, kDungeonCap, kSeed);

    std::vector<int> sorted = first.firstDungeon;
    std::ranges::sort(sorted);
    const auto quantile = [&](double q) {
        const std::size_t count = sorted.size();
        CHECK(count > 0); // with an establishable engine the sim must establish somewhere
        const std::size_t rank = static_cast<std::size_t>(std::ceil(q * static_cast<double>(count)));
        return sorted[std::min(count - 1, rank == 0 ? 0 : rank - 1)];
    };

    std::map<int, int> byDungeon;
    for (const int dungeon : first.firstDungeon) {
        ++byDungeon[dungeon];
    }
    std::cout << "[report] IgnitionTimingSimulated: " << first.runs << " runs x " << kDungeonCap
              << " dungeons; chance " << config.chance << " pity " << config.pityThreshold << " count "
              << config.count << " boss '" << config.bossOffer << "'\n";
    std::cout << "[report]   sequence (battles per dungeon, boss kind):";
    for (int dungeon = 1; dungeon <= kDungeonCap; ++dungeon) {
        const game::FRunSequence::FPosition position = sequence->PositionOf(dungeon - 1);
        const int battles = sequence->BattleCountAt(position);
        std::string bossKind;
        for (int ordinal = 0; ordinal < battles; ++ordinal) {
            if (!sequence->IsBossBattle(position, ordinal)) continue;
            const game::FRunRoomSlot* slot = sequence->BattleSlotAt(position, ordinal);
            bossKind = slot != nullptr && slot->boss == "final" ? "_final" : "_mid";
            break;
        }
        std::cout << " d" << dungeon << ':' << battles << bossKind;
    }
    std::cout << '\n';
    std::cout << "[report]   first-engine by dungeon:";
    for (const auto& [dungeon, count] : byDungeon) {
        std::cout << " d" << dungeon << ':' << count; 
    }
    std::cout << " | never " << first.never << "/" << first.runs << '\n';
    std::cout << "[report]   first-dungeon quantiles (established only): q1 " << quantile(0.25)
              << " median " << quantile(0.50) << " q3 " << quantile(0.75) << '\n';
    std::cout << "[report]   first engine counts:";
    for (const auto& [engine, count] : first.firstEngineCount) {
        std::cout << ' ' << engine << ':' << count;
    }
    std::cout << '\n';

    CHECK(first.runs == kRuns);
    CHECK(first.firstDungeon == second.firstDungeon); // seeded = deterministic
    CHECK(first.never == second.never);
    CHECK(first.firstEngineCount == second.firstEngineCount);
}

// ---------------------------------------------------------------------------------------------
// Rune behavior through the real resolver
// ---------------------------------------------------------------------------------------------

/// ChainRuneSpecValidated: the authored rune.chain spec is exactly the WP-16 contract
/// (on_hit -> chain, amount 2, distance 1, Projectile-gated), and the whole ship hook set passes the
/// registered per-kind validators at boot.
void ChainRuneSpecValidated() {
    const game::gameplay::FContentRegistry content = ShipContent();
    const game::gameplay::FRuneContent* chain = content.runes.find("rune.chain");
    CHECK(chain != nullptr);
    CHECK(chain->hooks.size() == 1);
    if (!chain->hooks.empty()) {
        const game::gameplay::FHook& hook = chain->hooks[0];
        CHECK(hook.event == game::gameplay::EHookEvent::OnHit);
        CHECK(std::string_view{hook.eventName} == game::gameplay::HookEventName(hook.event));
        CHECK(hook.effect.kind == "chain");
        CHECK(hook.effect.amount == 2);
        CHECK(hook.effect.distance == 1);
    }
    CHECK((chain->requiresAllTags == std::vector<std::string>{"Projectile"}));
    CHECK(chain->dropWeight == 6);

    const auto checkpoint = MakeCheckpointPath("chain-validate");
    {
        game_test::FTestRegistry loaded = game_test::LoadTestRegistry(ShipAssetRoots(), checkpoint.string());
        CHECK(loaded.content);
        CHECK(game::ValidateEffectContent(loaded.registry)); // every authored hook passes its validators
    }
    RemoveCheckpointFiles(checkpoint);
}

/// FrostpathMoveLocksOneTurn: a Cold skill carrying rune.frostpath move-locks the target for exactly the
/// definition's one turn (delta 0), and the tick then produces the move-AP loss once and expires.
void FrostpathMoveLocksOneTurn() {
    FContentFixture fixture{"frostpath"};
    fixture.SetSlots(fixture.actor, {Slot("skill.frost_lance", {"rune.frostpath"})});
    const auto token = fixture.StartTurn();

    const FCommandResult result = fixture.Tackle(token, fixture.foe, "skill.frost_lance");
    CHECK(result.reject == ECommandReject::None);
    auto& r = fixture.registry();
    const game::FStatusEffect* lock = FindStatus(r, fixture.foe, "status.locked_move");
    CHECK(lock != nullptr);
    CHECK(lock->turnsRemaining == 1); // definition 1 turn, hook deltas 0
    CHECK(lock->magnitude == 0);
    CHECK(HasStatus(r, fixture.foe, "status.freeze")); // the skill's own freeze still lands

    // One-turn semantics, isolated from the freeze in the same list.
    game::FActiveStatuses isolate;
    isolate.effects.push_back(*lock);
    const game::gameplay::FStatusContent* definition = fixture.Content().statuses.find("status.locked_move");
    CHECK(definition != nullptr);
    game::FBattleRng rng{0x5eedu};
    const game::FStatusTickResult tick = game::TickStatuses(isolate, {definition}, rng);
    CHECK(tick.moveApZero);
    CHECK(isolate.effects.empty()); // consumed by the same tick

    CHECK(CountLines(Log(r).lines, "rune:rune.frostpath on_hit -> apply_status_target") == 1);
}

/// GuardWeakensAttacker: rune.guard (slot-unbound OnDamageTaken) fires when the carrier is hit and targets
/// the ATTACKER with status.debuff; the victim is not healed, the attacker's HP is untouched.
void GuardWeakensAttacker() {
    FContentFixture fixture{"guard"};
    fixture.SetSlots(fixture.foe, {Slot("skill.strike", {"rune.guard"})});
    const auto token = fixture.StartTurn();

    auto& r = fixture.registry();
    const int actorHpBefore = r.get<game::FHealth>(fixture.actor).current;
    const int foeHpBefore = r.get<game::FHealth>(fixture.foe).current;
    const FCommandResult result = fixture.Tackle(token, fixture.foe, "skill.strike");
    CHECK(result.reject == ECommandReject::None);

    CHECK(r.get<game::FHealth>(fixture.foe).current == foeHpBefore - 12); // the strike only, no heal
    CHECK(r.get<game::FHealth>(fixture.actor).current == actorHpBefore);  // the hook never touches HP
    const game::FStatusEffect* debuff = FindStatus(r, fixture.actor, "status.debuff");
    CHECK(debuff != nullptr);
    // WP-28 A1: the sign fix landed — the authored -2 now survives EffectiveStatusMagnitude (it used to
    // clamp to 0 and do nothing), so a real magnitude assertion is back.
    CHECK(debuff->magnitude == -2);
    CHECK(debuff->turnsRemaining == 3);
    CHECK(FindStatus(r, fixture.foe, "status.debuff") == nullptr); // the victim is not the target
    CHECK(CountLines(Log(r).lines, "rune:rune.guard on_damage_taken -> apply_status_target") == 1);
}

/// ImpactPushesOnHit: a Melee skill carrying rune.impact pushes the struck enemy one cell away; a killing
/// hit moves nobody (the OnHit dispatch runs after the damage, and the dead are not displaced).
void ImpactPushesOnHit() {
    {
        FContentFixture fixture{"impact"};
        fixture.SetSlots(fixture.actor, {Slot("skill.strike", {"rune.impact"})});
        const auto token = fixture.StartTurn();
        const FCommandResult result = fixture.Tackle(token, fixture.foe, "skill.strike");
        CHECK(result.reject == ECommandReject::None);
        auto& r = fixture.registry();
        CHECK((r.get<game::FGridPosition>(fixture.foe) == game::FGridPosition{5, 3})); // pushed away from (5,5)
        CHECK(r.get<game::FHealth>(fixture.foe).current == 38);
        CHECK(CountLines(Log(r).lines, " is pushed 1.") == 1);
        CHECK(CountLines(Log(r).lines, "slams into an obstacle") == 0);
    }
    {
        FContentFixture fixture{"impact-kill"};
        fixture.SetSlots(fixture.actor, {Slot("skill.strike", {"rune.impact"})});
        const auto token = fixture.StartTurn();
        auto& r = fixture.registry();
        r.get<game::FHealth>(fixture.foe).current = 1;
        const FCommandResult result = fixture.Tackle(token, fixture.foe, "skill.strike");
        CHECK(result.reject == ECommandReject::None);
        CHECK(r.all_of<game::FDead>(fixture.foe));
        CHECK((r.get<game::FGridPosition>(fixture.foe) == game::FGridPosition{5, 4})); // stayed where it fell
        CHECK(CountLines(Log(r).lines, " is pushed ") == 0);
    }
}

/// CombustBouncesFromDeadCell: a lethal hit from a Fire-tagged slot carrying rune.combust bounces the
/// chain from the cell the victim died on — the same-side neighbour at distance 1 loses 2 HP, the one at
/// distance 2 does not, and a non-lethal hit bounces nothing (the hook is OnKill-only).
void CombustBouncesFromDeadCell() {
    FContentFixture fixture{"combust"};
    fixture.SetSlots(fixture.actor, {Slot("skill.firebolt", {"rune.combust"})});
    const auto token = fixture.StartTurn();
    auto& r = fixture.registry();
    r.get<game::FHealth>(fixture.foe).current = 1;

    const FCommandResult kill = fixture.Tackle(token, fixture.foe, "skill.firebolt");
    CHECK(kill.reject == ECommandReject::None);
    CHECK(r.all_of<game::FDead>(fixture.foe));
    CHECK((r.get<game::FGridPosition>(fixture.foe) == game::FGridPosition{5, 4})); // FGridPosition outlives FDead
    CHECK(r.get<game::FHealth>(fixture.foe2).current == 48); // distance 1: caught in the chain for 2
    CHECK(r.get<game::FHealth>(fixture.foe3).current == 50); // distance 2: out of reach (distance = 1)
    CHECK(CountLines(Log(r).lines, "is caught in the chain: -2 HP.") == 1);
    CHECK(CountLines(Log(r).lines, "rune:rune.combust on_kill -> chain") == 1);

    // The same slot on a surviving target: no OnKill, so nothing bounces.
    r.get<game::FActionPoints>(fixture.actor).skill = 2;
    const FCommandResult wound = fixture.Tackle(token, fixture.foe2, "skill.firebolt");
    CHECK(wound.reject == ECommandReject::None);
    CHECK(r.get<game::FHealth>(fixture.foe2).current == 37); // 48 - 11, no chain damage
    CHECK(r.get<game::FHealth>(fixture.foe3).current == 50);
    CHECK(CountLines(Log(r).lines, "is caught in the chain: -2 HP.") == 1); // still exactly the one from the kill
}

/// RuneHooksFireOnIntendedEvents: every new rune listens on the authored event, and no rune hook applies
/// a status that is an engine TRIGGER status — FireHooks applies data hooks before the ignition listener,
/// so such a hook would pre-satisfy an ignition on the same hit (the WP-25 seam, recorded in
/// engine_ignition.json).
void RuneHooksFireOnIntendedEvents() {
    const game::gameplay::FContentRegistry content = ShipContent();
    struct FExpectation {
        const char* runeId;
        game::gameplay::EHookEvent event;
    };
    const FExpectation expectations[] = {
        {"rune.chain", game::gameplay::EHookEvent::OnHit},
        {"rune.frostpath", game::gameplay::EHookEvent::OnHit},
        {"rune.combust", game::gameplay::EHookEvent::OnKill},
        {"rune.guard", game::gameplay::EHookEvent::OnDamageTaken},
        {"rune.impact", game::gameplay::EHookEvent::OnHit},
    };
    for (const FExpectation& expected : expectations) {
        const game::gameplay::FRuneContent* rune = content.runes.find(expected.runeId);
        CHECK(rune != nullptr);
        CHECK(rune->hooks.size() == 1);
        if (rune->hooks.empty()) {
            continue;
        }
        const game::gameplay::FHook& hook = rune->hooks[0];
        CHECK(hook.event == expected.event);
        CHECK(std::string_view{hook.eventName} == game::gameplay::HookEventName(expected.event));
        CHECK(!hook.oncePerTurn);
    }

    std::set<std::string> triggerStatuses;
    for (const game::gameplay::FEngineContent& engine : content.engines.rows) {
        triggerStatuses.insert(engine.triggerStatusId);
    }
    CHECK(triggerStatuses.size() == 3); // burn / freeze / paralyze
    int statusHooks = 0;
    for (const game::gameplay::FRuneContent& rune : content.runes.rows) {
        for (const game::gameplay::FHook& hook : rune.hooks) {
            const std::string_view kind = hook.effect.kind;
            if (kind != game::gameplay::kEffectKindApplyStatusTarget &&
                kind != game::gameplay::kEffectKindApplyStatusSelf) {
                continue;
            }
            ++statusHooks;
            CHECK(triggerStatuses.count(hook.effect.statusId) == 0);
        }
    }
    CHECK(statusHooks == 2); // frostpath (locked_move) and guard (debuff) — neither is an engine trigger
}

// ---------------------------------------------------------------------------------------------
// Pools / loadouts
// ---------------------------------------------------------------------------------------------

/// PoolCoversEverySkill: the union of the four character pools is exactly the 27 player skills, and the
/// enemy pools are the WP-52 sets.
void PoolCoversEverySkill() {
    const game::gameplay::FContentRegistry content = ShipContent();
    std::set<std::string> pool;
    for (const std::string_view unitId : {"char.warrior", "char.archer", "char.mage", "char.healer"}) {
        for (const std::string& skillId : PoolOf(content, unitId)) {
            pool.insert(skillId);
        }
    }
    CHECK(pool.size() == 27);
    for (const game::gameplay::FSkillContent& skill : content.skills.rows) {
        if (kEnemyOnlySkills.count(skill.id) == 1) {
            CHECK(pool.count(skill.id) == 0);
            continue;
        }
        CHECK(pool.count(skill.id) == 1);
    }

    CHECK((PoolOf(content, "enemy.guardian") == std::vector<std::string>{"skill.cleave", "skill.strike"}));
    CHECK((PoolOf(content, "enemy.imp") == std::vector<std::string>{"skill.strike"}));
    CHECK((PoolOf(content, "enemy.imp_archer") == std::vector<std::string>{"skill.arrow", "skill.pin"}));
    CHECK((PoolOf(content, "enemy.imp_shaman") ==
           std::vector<std::string>{"skill.bolt", "skill.imp_firebomb", "skill.hex"}));
}

/// PoolSizesMinimum: every character pool >= 5 and every class union >= 8 (warrior 8 / archer 8 /
/// mage-class 11).
void PoolSizesMinimum() {
    const game::gameplay::FContentRegistry content = ShipContent();
    CHECK(PoolOf(content, "char.warrior").size() == 8);
    CHECK(PoolOf(content, "char.archer").size() == 8);
    CHECK(PoolOf(content, "char.mage").size() == 9);
    CHECK(PoolOf(content, "char.healer").size() == 5);
    for (const std::string_view unitId : {"char.warrior", "char.archer", "char.mage", "char.healer"}) {
        CHECK(PoolOf(content, unitId).size() >= 5);
    }
    std::set<std::string> warriorClass;
    std::set<std::string> archerClass;
    std::set<std::string> mageClass;
    for (const game::gameplay::FUnitContent& unit : content.units.rows) {
        if (unit.className == "warrior") {
            warriorClass.insert(unit.skillIds.begin(), unit.skillIds.end());
        } else if (unit.className == "archer") {
            archerClass.insert(unit.skillIds.begin(), unit.skillIds.end());
        } else if (unit.className == "mage") {
            mageClass.insert(unit.skillIds.begin(), unit.skillIds.end());
        }
    }
    CHECK(warriorClass.size() == 8);
    CHECK(archerClass.size() == 8);
    CHECK(mageClass.size() == 11);
}

/// MageClassDisjointFourPlusFour: the mage-class union of 11 lets mage and healer each hold four pool
/// skills with no overlap (the WP-21 prep-screen 4-slot sanity).
void MageClassDisjointFourPlusFour() {
    const game::gameplay::FContentRegistry content = ShipContent();
    const std::vector<std::string> mage = PoolOf(content, "char.mage");
    const std::vector<std::string> healer = PoolOf(content, "char.healer");
    bool found = false;
    for (std::size_t a = 0; a < mage.size() && !found; ++a) {
        for (std::size_t b = a + 1; b < mage.size() && !found; ++b) {
            for (std::size_t c = b + 1; c < mage.size() && !found; ++c) {
                for (std::size_t d = c + 1; d < mage.size() && !found; ++d) {
                    const std::set<std::string> mageFour{mage[a], mage[b], mage[c], mage[d]};
                    for (std::size_t e = 0; e < healer.size(); ++e) {
                        for (std::size_t f = e + 1; f < healer.size(); ++f) {
                            for (std::size_t g = f + 1; g < healer.size(); ++g) {
                                for (std::size_t h = g + 1; h < healer.size(); ++h) {
                                    const std::set<std::string> healerFour{healer[e], healer[f], healer[g],
                                                                           healer[h]};
                                    bool disjoint = true;
                                    for (const std::string& skill : healerFour) {
                                        if (mageFour.count(skill) > 0) {
                                            disjoint = false;
                                        }
                                    }
                                    if (disjoint) {
                                        found = true;
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    CHECK(found);
}

/// DefaultLoadoutIsFirstFour: SetupCombatUnits on a placement without `skills` gives the first four of the
/// unit's pool, in pool order (the BuildLoadout cap).
void DefaultLoadoutIsFirstFour() {
    const auto checkpoint = MakeCheckpointPath("cap-unit");
    {
        game_test::FTestRegistry loaded = game_test::LoadTestRegistry(ShipAssetRoots(), checkpoint.string());
        CHECK(loaded.content);
        auto& registry = loaded.registry;
        const auto& content = registry.ctx().get<game::gameplay::FContentRegistry>();

        entt::entity entity = registry.create();
        game::FBattlefieldData battlefield;
        game::FUnitPlacement placement;
        placement.id = "probe.mage";
        placement.displayName = "Probe";
        placement.faction = game::EUnitFaction::Party;
        placement.cell = game::FGridPosition{5, 6};
        placement.entity = entity;
        placement.unitTemplateId = "char.mage"; // a pool of 9 — anything above 4 proves the cap
        battlefield.units.push_back(placement);
        CHECK(game::SetupCombatUnits(registry, battlefield, content));

        const std::vector<std::string> pool = PoolOf(content, "char.mage");
        const auto& slots = registry.get<game::FSkillLoadout>(entity).slots;
        CHECK(slots.size() == 4);
        for (std::size_t index = 0; index < slots.size() && index < pool.size(); ++index) {
            CHECK(slots[index].skillId == pool[index]);
            CHECK(slots[index].runeIds.empty());
        }
    }
    RemoveCheckpointFiles(checkpoint);
}

/// PlacementOverrideUnchanged: a placement that authors six skills keeps all six, in order — the cap only
/// covers the pool fallback (W5 injection and W9 captured-loadout regression).
void PlacementOverrideUnchanged() {
    const auto checkpoint = MakeCheckpointPath("cap-override");
    {
        game_test::FTestRegistry loaded = game_test::LoadTestRegistry(ShipAssetRoots(), checkpoint.string());
        CHECK(loaded.content);
        auto& registry = loaded.registry;
        const auto& content = registry.ctx().get<game::gameplay::FContentRegistry>();

        entt::entity entity = registry.create();
        game::FBattlefieldData battlefield;
        game::FUnitPlacement placement;
        placement.id = "probe.mage.six";
        placement.displayName = "Probe Six";
        placement.faction = game::EUnitFaction::Party;
        placement.cell = game::FGridPosition{5, 6};
        placement.entity = entity;
        placement.unitTemplateId = "char.mage";
        for (const std::string& skillId : {"skill.bolt", "skill.firebolt", "skill.frost_lance", "skill.fog",
                                           "skill.heal", "skill.buff"}) {
            placement.skills.push_back(Slot(skillId));
        }
        battlefield.units.push_back(placement);
        CHECK(game::SetupCombatUnits(registry, battlefield, content));

        const auto& slots = registry.get<game::FSkillLoadout>(entity).slots;
        CHECK(slots.size() == 6);
        const std::vector<std::string> expected{"skill.bolt", "skill.firebolt", "skill.frost_lance",
                                                "skill.fog", "skill.heal", "skill.buff"};
        for (std::size_t index = 0; index < slots.size(); ++index) {
            CHECK(slots[index].skillId == expected[index]);
        }
    }
    RemoveCheckpointFiles(checkpoint);
}

/// PullInWarriorPool: skill.hook (the pull skill) sits in the warrior AUTHORED POOL at index >= 4 — the
/// default loadout is untouched — and its authored hook is the WP-16 pull contract.
void PullInWarriorPool() {
    const game::gameplay::FContentRegistry content = ShipContent();
    const std::vector<std::string> pool = PoolOf(content, "char.warrior");
    const auto found = std::ranges::find(pool, "skill.hook");
    CHECK(found != pool.end());
    CHECK(static_cast<std::size_t>(found - pool.begin()) >= 4);
    CHECK((std::vector<std::string>(pool.begin(), pool.begin() + 4) ==
           std::vector<std::string>{"skill.strike", "skill.cleave", "skill.lunge", "skill.shove"}));

    const game::gameplay::FSkillContent* hook = content.skills.find("skill.hook");
    CHECK(hook != nullptr);
    CHECK(hook->apCost == 1);
    CHECK(hook->power >= 1); // OnHit fires in the damage branch only — power must not be 0
    CHECK(hook->range == 2);
    CHECK((hook->tags == std::vector<std::string>{"Attack", "Pull"}));
    CHECK(hook->hooks.size() == 1);
    if (!hook->hooks.empty()) {
        const game::gameplay::FHook& authored = hook->hooks[0];
        CHECK(authored.event == game::gameplay::EHookEvent::OnHit);
        CHECK(authored.effect.kind == "pull");
        CHECK(authored.effect.from == "caster");
        CHECK(authored.effect.distance == 1);
    }
}

/// PullMovesTargetAdjacent: through the real resolver, a range-2 enemy hit by skill.hook ends adjacent to
/// the warrior (the pull drags it one step toward the caster and stops there — an intended stop, not a
/// collision).
void PullMovesTargetAdjacent() {
    FContentFixture fixture{"pull"};
    fixture.SetSlots(fixture.actor, {Slot("skill.hook")});
    auto& r = fixture.registry();
    r.get<game::FGridPosition>(fixture.foe) = {5, 3}; // distance 2 — inside the hook's range 2
    const auto token = fixture.StartTurn();

    const FCommandResult result = fixture.Tackle(token, fixture.foe, "skill.hook");
    CHECK(result.reject == ECommandReject::None);
    CHECK((r.get<game::FGridPosition>(fixture.foe) == game::FGridPosition{5, 4})); // adjacent to the caster
    CHECK(r.get<game::FHealth>(fixture.foe).current == 41);                       // the hit itself: 10+1-2
    CHECK(CountLines(Log(r).lines, " is pulled 1.") == 1);
    CHECK(CountLines(Log(r).lines, "slams into an obstacle") == 0);
}

/// BraceSelfResolves: skill.brace (power 0, range 0, target self, applies_status buff) resolves on the
/// caster through the real resolver — the self-target path needs no range>=1: the resolved range clamp
/// (max(1, 0) = 1) admits the caster's own cell at distance 0, and the buff lands on the caster only.
void BraceSelfResolves() {
    FContentFixture fixture{"brace"};
    fixture.SetSlots(fixture.actor, {Slot("skill.brace")});
    const auto token = fixture.StartTurn();

    const FCommandResult result = fixture.Tackle(token, fixture.actor, "skill.brace");
    CHECK(result.reject == ECommandReject::None);
    auto& r = fixture.registry();
    const game::FStatusEffect* buff = FindStatus(r, fixture.actor, "status.buff");
    CHECK(buff != nullptr);
    CHECK(buff->magnitude == 2);      // definition values (hook deltas 0)
    CHECK(buff->turnsRemaining == 3);
    CHECK(FindStatus(r, fixture.foe, "status.buff") == nullptr);
    CHECK(r.get<game::FHealth>(fixture.actor).current == 50); // power 0: a buff is never a heal
    CHECK(r.get<game::FActionPoints>(fixture.actor).skill == 1); // ap_cost 1
}

// ---------------------------------------------------------------------------------------------
// AI contract: injected enemies need no AI code
// ---------------------------------------------------------------------------------------------

/// NewLoadoutsNeedNoAiWork: an enemy injected (through placement, not unit content) with two of the
/// WP-22 skills acts through the existing behavior tree with no AI code change (R2 §4.3 contract).
void NewLoadoutsNeedNoAiWork() {
    FContentFixture fixture{"ai-new-skills"};
    auto& r = fixture.registry();
    fixture.SetSlots(fixture.foe, {Slot("skill.pin"), Slot("skill.static")});
    r.get<game::FSpeed>(fixture.foe).value = 30; // the injected monster opens, so it acts before it dies
    r.emplace<game::FTurnParticipant>(fixture.foe);
    r.ctx().get<game::FCombatRuntime>().autoPlay = true;

    game::ecs::TurnSystem turns;
    turns.onRegister(r);
    game::ecs::TurnFlowSystem flow;
    game::ecs::ActionResolverSystem actions;
    game::ecs::CombatOutcomeSystem outcomes;
    auto& runtime = r.ctx().get<game::FTurnRuntime>();
    CHECK(runtime.controller.start(r, 20261108u).has_value());

    bool usedInjected = false;
    int badRejects = 0;
    std::uint64_t steps = 0;
    auto& outcome = r.ctx().get<game::FBattleOutcome>();
    while (!usedInjected && !outcome.resolved && steps++ < 6000) {
        turns.update(r, .1f);
        flow.update(r, .1f);
        actions.update(r, .1f);
        outcomes.update(r, .1f);
        for (const FCommandResult& result : r.ctx().get<game::FCommandResults>().recent) {
            if (result.command.source != game::ECommandSource::Ai) {
                continue;
            }
            const auto* skill = std::get_if<game::FActionRequest>(&result.command.action);
            if (skill == nullptr || skill->actor != fixture.foe) {
                continue;
            }
            if (skill->skillId != "skill.pin" && skill->skillId != "skill.static") {
                continue;
            }
            if (result.reject == ECommandReject::UnknownSkill ||
                result.reject == ECommandReject::SkillResolutionFailed) {
                ++badRejects;
            } else if (result.reject == ECommandReject::None) {
                usedInjected = true;
            }
        }
    }
    CHECK(usedInjected); // the tree played at least one injected skill, resolved through the real pipeline
    CHECK(badRejects == 0);
}

// ---------------------------------------------------------------------------------------------
// Ember run harnesses (real dungeon flow)
// ---------------------------------------------------------------------------------------------

/// A booted ember dungeon with the turn systems wired; plays battles through FEmberBattleState (the
/// tempo_skills_tests / run_flow_tests recipes).
struct FDungeonHarness {
    std::filesystem::path checkpoint;
    std::filesystem::path runSave;
    entt::registry registry;
    game::ecs::TurnSystem turns;

    explicit FDungeonHarness(std::uint32_t seed, std::string_view label) {
        checkpoint = MakeCheckpointPath(label);
        game::FAssetManager::Initialize(registry, ShipAssetRoots());
        CHECK(game::gameplay::FGameplayServices::Initialize(registry));
        CHECK(game::InitializeDungeon(registry, checkpoint.string()));
        if (const auto* save = registry.ctx().find<game::FDungeonSave>()) {
            runSave = save->path;
        }
        turns.onRegister(registry);
        game::StartDungeonRun(registry, seed, true);
    }

    ~FDungeonHarness() {
        RemoveCheckpointFiles(checkpoint);
        std::error_code error;
        if (!runSave.empty()) {
            std::filesystem::remove(runSave, error);
            std::filesystem::remove(runSave.string() + ".tmp", error);
        }
    }

    FDungeonHarness(const FDungeonHarness&) = delete;
    FDungeonHarness& operator=(const FDungeonHarness&) = delete;

    struct FBattleResult {
        bool resolved{false};
        bool victory{false};
        int rounds{0};
    };

    /// True for the capped runs: every battle must load at most four slots. The uncapped report baseline
    /// injects the whole pool on purpose, so it turns the contract check off.
    bool enforceCapContract{true};

    /// Sets up the current room's battle and auto-plays it to resolution (both sides).
    FBattleResult PlayBattle() {
        FBattleResult result;
        auto& r = registry;
        FEmberBattleState battle;
        battle.OnEnter(r);
        auto* combat = r.ctx().find<game::FCombatRuntime>();
        CHECK(combat != nullptr); // OnEnter fully set the battle up (FCombatRuntime is its last emission)
        combat->autoPlay = true;
        // The cap contract holds for every capped-run battle the harness sets up (structural, not a number).
        if (enforceCapContract) {
            for (const entt::entity entity : r.view<game::FPartyMember, game::FSkillLoadout>()) {
                CHECK(r.get<game::FSkillLoadout>(entity).slots.size() <= 4);
            }
        }
        game::ecs::TurnFlowSystem flow;
        game::ecs::ActionResolverSystem actions;
        game::ecs::CombatOutcomeSystem outcomes;
        std::uint64_t steps = 0;
        while (!r.ctx().get<game::FBattleOutcome>().resolved && steps++ < 6000) {
            turns.update(r, .1f);
            flow.update(r, .1f);
            actions.update(r, .1f);
            outcomes.update(r, .1f);
        }
        const auto& outcome = r.ctx().get<game::FBattleOutcome>();
        result.resolved = outcome.resolved;
        result.victory = outcome.victory;
        result.rounds = static_cast<int>(r.ctx().get<game::FTurnRuntime>().controller.snapshot().roundNumber);
        battle.OnExit(r);
        return result;
    }
};

/// DefaultBattleLoadoutNeverExceedsFour: start a fresh ember run on ship content, set up the first battle:
/// every party unit's loadout is the first four of its pool, and the captured run party matches.
void DefaultBattleLoadoutNeverExceedsFour() {
    FDungeonHarness harness{7u, "cap-run"};
    auto& r = harness.registry;
    const auto& content = r.ctx().get<game::gameplay::FContentRegistry>();
    const nlohmann::json& data = game::DungeonDefinition(r);
    const auto unitTemplateFor = [&](const std::string& placementId) -> std::string {
        for (const nlohmann::json& member : data.at("party")) {
            if (member.at("id").get<std::string>() == placementId) {
                return member.at("unit").get<std::string>();
            }
        }
        throw std::runtime_error("placement is not in the dungeon party block");
    };

    {
        FEmberBattleState battle;
        battle.OnEnter(r);
        CHECK(r.ctx().find<game::FCombatRuntime>() != nullptr);

        int checked = 0;
        for (const entt::entity entity : r.view<game::FPartyMember, game::FUnitData, game::FSkillLoadout>()) {
            const std::string placementId = r.get<game::FUnitData>(entity).definition.id;
            const std::string unitId = unitTemplateFor(placementId);
            const game::gameplay::FUnitContent* unit = content.units.find(unitId);
            CHECK(unit != nullptr);
            const auto& slots = r.get<game::FSkillLoadout>(entity).slots;
            CHECK(slots.size() == 4);
            for (std::size_t index = 0; index < slots.size() && index < unit->skillIds.size(); ++index) {
                CHECK(slots[index].skillId == unit->skillIds[index]);
            }
            ++checked;
        }
        CHECK(checked == 4);

        // The captured run party (ResolveRoomVictory captures through this exact function).
        game::CaptureRunParty(r);
        const auto& run = r.ctx().get<game::FDungeonRun>();
        CHECK(run.party.size() == 4);
        for (const game::FRunMember& member : run.party) {
            const std::string unitId = unitTemplateFor(member.id);
            const game::gameplay::FUnitContent* unit = content.units.find(unitId);
            CHECK(unit != nullptr);
            CHECK(member.skills.size() == 4);
            for (std::size_t index = 0; index < member.skills.size() && index < unit->skillIds.size(); ++index) {
                CHECK(member.skills[index].skillId == unit->skillIds[index]);
            }
        }

        battle.OnExit(r);
    }
}

/// EveryRuneIsOfferLegalForStartingParty: M-FUN gate S2 — the reward-offer gate (FRewards::RuneFitsSkills)
/// judges each rune ALONE against the BASE tags of the party's battle-loadout skills, so the capped
/// first-4 defaults must keep every one of the 24 runes offerable. Both PartySkills windows are covered:
/// pre-capture (empty run party -> the definition party's whole pools) and post-capture (the captured
/// first-4 battle loadout the room-1 reward roll sees).
void EveryRuneIsOfferLegalForStartingParty() {
    FDungeonHarness harness{11u, "offer-legal"};
    auto& registry = harness.registry;
    const auto& content = registry.ctx().get<game::gameplay::FContentRegistry>();

    const auto checkWindow = [&](std::string_view window, bool primary) {
        int legal = 0;
        std::vector<std::string> unreachable;
        for (const game::gameplay::FRuneContent& rune : content.runes.rows) {
            if (game::RuneFittingSkillNames(registry, rune.id).empty()) {
                unreachable.push_back(rune.id);
            } else {
                ++legal;
            }
        }
        if (primary) {
            std::cout << "[report] offer-legal runes: ";
        } else {
            std::cout << "[report] offer-legal runes (" << window << "): ";
        }
        std::cout << legal << '/' << content.runes.rows.size();
        for (const std::string& id : unreachable) {
            std::cout << ' ' << id;
        }
        std::cout << '\n';
        CHECK(static_cast<std::size_t>(legal) == content.runes.rows.size());
    };

    // Pre-capture window: a fresh run carries no slots, so the fallback reads the whole pools.
    for (const game::FRunMember& member : registry.ctx().get<game::FDungeonRun>().party) {
        CHECK(member.skills.empty());
    }
    checkWindow("pre-capture fallback", false);

    // Post-capture window: enter the first battle (the first-4 default loadout), capture like
    // ResolveRoomVictory does after room 1, and judge against exactly that state.
    {
        FEmberBattleState battle;
        battle.OnEnter(registry);
        CHECK(registry.ctx().find<game::FCombatRuntime>() != nullptr);
        game::CaptureRunParty(registry);
        battle.OnExit(registry);
    }
    for (const game::FRunMember& member : registry.ctx().get<game::FDungeonRun>().party) {
        CHECK(member.skills.size() == 4); // the capped default loadout is what offers are judged on
    }
    checkWindow("post-capture", true);
}

/// One full-dungeon auto-run for the report. `uncapped` injects each member's WHOLE pool into
/// FDungeonRun.party before the first battle — the exact path a captured loadout takes
/// (FEmberBattleState::OnEnter calls ApplyRunParty → ApplyBattleStart, which copies run.party[i].skills
/// over the placement loadout when non-empty, and ResolveRoomVictory captures the battle loadout back),
/// so the uncapped baseline carries across rooms like any real run's carried loadout. Wins/rounds are
/// printed and never gate; only a stalled battle (the harness itself failing) fails the test.
void RunCappedLoadoutReportSeed(std::uint32_t seed, bool uncapped) {
    FDungeonHarness harness{seed, uncapped ? "full-pool" : "capped"};
    if (uncapped) {
        harness.enforceCapContract = false;
        auto& registry = harness.registry;
        const auto& content = registry.ctx().get<game::gameplay::FContentRegistry>();
        const nlohmann::json& data = game::DungeonDefinition(registry);
        for (game::FRunMember& member : registry.ctx().get<game::FDungeonRun>().party) {
            for (const nlohmann::json& placement : data.at("party")) {
                if (placement.at("id").get<std::string>() != member.id) continue;
                const game::gameplay::FUnitContent* unit =
                    content.units.find(placement.at("unit").get<std::string>());
                CHECK(unit != nullptr);
                for (const std::string& skillId : unit->skillIds) {
                    member.skills.push_back(Slot(skillId));
                }
                break;
            }
            CHECK(member.skills.size() >= 5); // the whole authored pool, never a capped slice
        }
    }

    int battles = 0;
    int wins = 0;
    int rounds = 0;
    bool reachedCamp = false;
    bool defeated = false;
    bool stalled = false;
    for (int guard = 0; guard < 8 && !stalled; ++guard) {
        auto& run = harness.registry.ctx().get<game::FDungeonRun>();
        if (run.phase == game::EDungeonPhase::Camp) {
            reachedCamp = true;
            break;
        }
        if (run.phase == game::EDungeonPhase::Defeat) {
            defeated = true;
            break;
        }
        if (run.phase == game::EDungeonPhase::Reward) {
            (void)game::ResolveRuneOffer(harness.registry, 0);
            game::ContinueDungeonReward(harness.registry);
            continue;
        }
        if (run.phase == game::EDungeonPhase::Event) {
            game::ChooseDungeonEvent(harness.registry, false);
            continue;
        }
        const FDungeonHarness::FBattleResult battle = harness.PlayBattle();
        if (!battle.resolved) {
            stalled = true;
            break; // the harness could not finish the fight — a real failure below
        }
        ++battles;
        wins += battle.victory ? 1 : 0;
        rounds += battle.rounds;
        game::CompleteDungeonBattle(harness.registry, battle.victory);
    }
    std::cout << "[report] CappedLoadoutReport seed " << seed << ' ' << (uncapped ? "full pool" : "capped")
              << ": battles " << battles << " wins " << wins << " rounds " << rounds
              << (reachedCamp ? " | reached camp" : (defeated ? " | defeated" : " | NO END")) << '\n';
    CHECK(!stalled);              // numbers never gate; a stalled harness does
    CHECK(battles >= 1);
    CHECK(reachedCamp || defeated);
}

/// CappedLoadoutReport: all three seeds auto-play one FULL dungeon (3 rooms + event/camp steps) with the
/// capped default loadout (BuildLoadout first-4); the uncapped whole-pool baseline runs on seed 1 only for
/// the time budget (~86 s -> ~55 s against the 120 s ctest timeout) — the 3-seed full-pool numbers
/// (19/17/15 rounds) were recorded in the lane report. Printed numbers never gate.
void CappedLoadoutReport() {
    for (const std::uint32_t seed : {1u, 2u, 3u}) {
        RunCappedLoadoutReportSeed(seed, false); // capped default loadout
        if (seed == 1u) {
            RunCappedLoadoutReportSeed(seed, true); // uncapped baseline: the whole pool carried in
        }
    }
}

// WP-41 F16: the shipped events.json holds the six events of the spec table, and a ship-root boot passes.
void ShippedEventsMatchTheTable() {
    struct FEffectRow {
        const char* kind;
        int amount;
        int magnitude;
        const char* pool;
    };
    struct FChoiceRow {
        std::optional<std::pair<const char*, int>> check;
        std::vector<FEffectRow> effects;
    };
    struct FEventRow {
        const char* id;
        const char* tag;
        std::vector<FChoiceRow> choices;
    };
    const std::vector<FEventRow> table = {
        {"event.ember_shrine", "common",
         {{std::nullopt, {{"pay_materials", 3, 0, ""}, {"open_rune_offer", 3, 0, "boosted"}}}, {std::nullopt, {}}}},
        {"event.sealed_chest", "common",
         {{std::make_pair("attack", 10), {{"gain_materials_range", 3, 5, ""}}},
          {std::nullopt, {{"damage_party_pct", 20, 0, ""}, {"gain_materials_range", 2, 4, ""}}},
          {std::nullopt, {}}}},
        {"event.ash_gambler", "common",
         {{std::nullopt, {{"pay_materials", 2, 0, ""}, {"gain_materials_gamble", 7, 2, ""}}}, {std::nullopt, {}}}},
        {"event.rune_cache", "mystery",
         {{std::nullopt, {{"open_rune_offer", 4, 0, "boosted"}}},
          {std::nullopt, {{"gain_materials_range", 4, 6, ""}}}}},
        {"event.blood_altar", "mystery",
         {{std::nullopt, {{"damage_party_pct", 30, 0, ""}, {"open_rune_offer", 4, 0, "boss"}}},
          {std::nullopt, {{"gain_materials", 2, 0, ""}}}}},
        {"event.forgotten_armory", "mystery",
         {{std::make_pair("defense", 8), {{"open_rune_offer", 2, 0, "boosted"}, {"gain_materials", 2, 0, ""}}},
          {std::nullopt, {{"gain_materials_range", 2, 3, ""}}}}},
    };

    const nlohmann::json document = ReadJsonFile("assets/data/events.json");
    CHECK(document.at("schema_version") == 1);
    const auto& events = document.at("events");
    CHECK(events.size() == table.size());
    for (std::size_t i = 0; i < table.size() && i < events.size(); ++i) {
        const auto& row = table[i];
        const auto& event = events[i];
        CHECK(event.at("id") == row.id);
        CHECK(event.at("tags") == nlohmann::json::array({row.tag}));
        const auto& choices = event.at("choices");
        CHECK(choices.size() == row.choices.size());
        for (std::size_t j = 0; j < row.choices.size() && j < choices.size(); ++j) {
            const auto& choice = choices[j];
            const auto& expected = row.choices[j];
            CHECK(choice.contains("check") == expected.check.has_value());
            if (expected.check && choice.contains("check")) {
                CHECK(choice.at("check").at("stat") == expected.check->first);
                CHECK(choice.at("check").at("difficulty") == expected.check->second);
            }
            const auto effects = choice.value("effects", nlohmann::json::array());
            CHECK(effects.size() == expected.effects.size());
            for (std::size_t k = 0; k < expected.effects.size() && k < effects.size(); ++k) {
                const auto& effect = effects[k];
                CHECK(effect.at("kind") == expected.effects[k].kind);
                CHECK(effect.value("amount", 0) == expected.effects[k].amount);
                CHECK(effect.value("magnitude", 0) == expected.effects[k].magnitude);
                CHECK(effect.value("pool", std::string{}) == expected.effects[k].pool);
            }
        }
    }

    entt::registry registry;
    game::FAssetManager::Initialize(registry, ShipAssetRoots());
    CHECK(game::gameplay::FGameplayServices::Initialize(registry));
}

} // namespace

int main() {
    // Save safety: CMake gives every ctest feature test GAME01P_SAVE_DIR (WP-00b); this fallback only
    // covers running the executable directly, outside ctest — plain runs would otherwise target the
    // user's real save under %APPDATA%. Commit the override before any dungeon service can touch disk.
    if (const char* existing = std::getenv("GAME01P_SAVE_DIR"); existing == nullptr || *existing == '\0') {
        const std::filesystem::path overrideDir =
            std::filesystem::current_path() / "out" / "test-saves" / "content_a_tests";
        std::error_code error;
        std::filesystem::create_directories(overrideDir, error);
        const std::string value = overrideDir.string();
#ifdef _WIN32
        _putenv_s("GAME01P_SAVE_DIR", value.c_str());
#else
        setenv("GAME01P_SAVE_DIR", value.c_str(), 1);
#endif
    }
    return game_test::RunTests("content a", {
        {"SkillsLoadAndRegister", SkillsLoadAndRegister},
        {"RunesLoadAndRegister", RunesLoadAndRegister},
        {"AllRunesHaveLegalLoadout", AllRunesHaveLegalLoadout},
        {"RuneRequiresTagsAreKnown", RuneRequiresTagsAreKnown},
        {"UnknownRequireTagFailsBoot", UnknownRequireTagFailsBoot},
        {"ShippedRunesHaveRarityTable", ShippedRunesHaveRarityTable},
        {"UnknownRuneRarityFailsBoot", UnknownRuneRarityFailsBoot},
        {"AlternativePathExists", AlternativePathExists},
        {"ArchetypesUseExistingVocabulary", ArchetypesUseExistingVocabulary},
        {"NoStackTrigger", NoStackTrigger},
        {"IgnitionTimingSimulated", IgnitionTimingSimulated},
        {"ChainRuneSpecValidated", ChainRuneSpecValidated},
        {"FrostpathMoveLocksOneTurn", FrostpathMoveLocksOneTurn},
        {"GuardWeakensAttacker", GuardWeakensAttacker},
        {"ImpactPushesOnHit", ImpactPushesOnHit},
        {"CombustBouncesFromDeadCell", CombustBouncesFromDeadCell},
        {"RuneHooksFireOnIntendedEvents", RuneHooksFireOnIntendedEvents},
        {"PoolCoversEverySkill", PoolCoversEverySkill},
        {"PoolSizesMinimum", PoolSizesMinimum},
        {"MageClassDisjointFourPlusFour", MageClassDisjointFourPlusFour},
        {"DefaultLoadoutIsFirstFour", DefaultLoadoutIsFirstFour},
        {"PlacementOverrideUnchanged", PlacementOverrideUnchanged},
        {"PullInWarriorPool", PullInWarriorPool},
        {"PullMovesTargetAdjacent", PullMovesTargetAdjacent},
        {"BraceSelfResolves", BraceSelfResolves},
        {"DefaultBattleLoadoutNeverExceedsFour", DefaultBattleLoadoutNeverExceedsFour},
        {"EveryRuneIsOfferLegalForStartingParty", EveryRuneIsOfferLegalForStartingParty},
        {"NewLoadoutsNeedNoAiWork", NewLoadoutsNeedNoAiWork},
        {"CappedLoadoutReport", CappedLoadoutReport},
        {"ShippedEventsMatchTheTable", ShippedEventsMatchTheTable},
    });
}
