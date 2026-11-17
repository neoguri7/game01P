// WP-52 monster variety: six new enemy kinds, a third skill for five kinds, larger encounters. Shipped
// content on purpose (ShipAssetRoots): these tests guard the roster that ships, not the frozen fixture.
// Each behaviour runs through the real resolver / planner / brain on a small rig.

#define GAME01P_TEST_SUPPORT_CHECK
#include "../TestSupport.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <SDL3/SDL.h>
#include <entt/entt.hpp>
#include <nlohmann/json.hpp>

#include "core/AnimationLibrary.h"
#include "core/AssetManager.h"
#include "core/ResourceManager.h"
#include "core/SDLDeleter.h"
#include "ecs/components/FActionPoints.h"
#include "ecs/components/FActiveStatuses.h"
#include "ecs/components/FDead.h"
#include "ecs/components/FFaction.h"
#include "ecs/components/FHealth.h"
#include "ecs/components/FSkillLoadout.h"
#include "ecs/components/FSpeed.h"
#include "ecs/components/FStats.h"
#include "ecs/components/FTurnParticipant.h"
#include "ecs/systems/ActionResolverSystem.h"
#include "ecs/systems/TurnSystem.h"
#include "gameplay/GameplayServices.h"
#include "gameplay/ai/FAiBlackboard.h"
#include "gameplay/ai/FAiBrain.h"
#include "gameplay/ai/FAiLeaves.h"
#include "gameplay/ai/FAiLibrary.h"
#include "gameplay/ai/FAiQueries.h"
#include "gameplay/ai/FAiTuning.h"
#include "gameplay/ai/FAiWorld.h"
#include "gameplay/combat/FCombatTypes.h"
#include "gameplay/combat/FIntent.h"
#include "gameplay/data/FContentRegistry.h"
#include "gameplay/dungeon/FDungeonRun.h"
#include "gameplay/dungeon/FEncounterContent.h"
#include "gameplay/grid/FBattlefieldData.h"
#include "gameplay/grid/FGridTerrain.h"
#include "gameplay/grid/FGridTypes.h"
#include "presentation/FBattleAnim.h"
#include "presentation/FCompendiumView.h"
#include "presentation/FDungeonArt.h"

namespace {

using nlohmann::json;

std::string UniquePath(const char* stem) {
    static std::uint32_t serial = 0;
    std::filesystem::create_directories("out");
    return "out/" + std::string(stem) + "-" + std::to_string(++serial) + "-" +
           std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".json";
}

json ReadJson(const char* path) {
    std::ifstream file(path);
    CHECK(file.good());
    return json::parse(file);
}

constexpr std::array<const char*, 6> kNewKinds{"imp_mender", "ember_hound", "powder_imp",
                                               "chain_thrall", "ash_wraith", "ash_drummer"};
constexpr std::array<const char*, 5> kActions{"idle", "walk", "attack", "hit", "death"};

// --- the rig -------------------------------------------------------------------------------------

/// Shipped content + grid + turn runtime on an all-floor board. Units come from shipped content rows.
struct FMonsterRig {
    entt::registry registry;

    FMonsterRig() {
        game::FAssetManager::Initialize(registry, ShipAssetRoots());
        CHECK(game::gameplay::FGameplayServices::Initialize(registry));
        auto& terrain = registry.ctx().emplace<game::FGridTerrain>();
        terrain.cells.fill(game::ETile::Floor);
        terrain.moveRange = ReadJson("assets/data/dungeon.json").at("balance").at("move_range").get<int>();
        registry.ctx().emplace<game::FGridMetrics>();
        registry.ctx().emplace<game::FDungeonRun>().phase = game::EDungeonPhase::Battle;
        registry.ctx().emplace<game::FCombatLog>();
        registry.ctx().emplace<game::FCombatRuntime>();
        registry.ctx().emplace<game::FTurnRuntime>();
        registry.ctx().emplace<game::FActionQueue>();
    }

    const game::gameplay::FContentRegistry& Content() {
        return registry.ctx().get<game::gameplay::FContentRegistry>();
    }

    const game::gameplay::FUnitContent& Row(const std::string& unitId) {
        const auto* row = Content().units.find(unitId);
        CHECK(row != nullptr);
        return *row;
    }

    const game::gameplay::FSkillContent& Skill(const std::string& id) {
        const auto* row = Content().skills.find(id);
        CHECK(row != nullptr);
        return *row;
    }

    entt::entity Spawn(const std::string& unitId, game::FGridPosition cell) {
        const auto& row = Row(unitId);
        const bool party = unitId.starts_with("char.");
        const auto entity = registry.create();
        if (party) {
            registry.emplace<game::FPartyMember>(entity);
        } else {
            registry.emplace<game::FEnemy>(entity);
        }
        game::FUnitDefinition definition;
        definition.id = unitId;
        definition.displayName = row.displayName;
        definition.faction = party ? game::EUnitFaction::Party : game::EUnitFaction::Enemy;
        registry.emplace<game::FUnitData>(entity, game::FUnitData{definition});
        registry.emplace<game::FStats>(entity,
                                       game::FStats{row.attack, row.magicAttack, row.defense, row.magicDefense});
        const int hp = static_cast<int>(row.maxHealth);
        registry.emplace<game::FHealth>(entity, game::FHealth{hp, hp});
        registry.emplace<game::FGridPosition>(entity, cell);
        registry.emplace<game::FActionPoints>(entity,
                                              game::FActionPoints{row.moveAp, row.moveAp, row.skillAp, row.skillAp});
        registry.emplace<game::FSpeed>(entity, game::FSpeed{static_cast<int>(row.speed)});
        registry.emplace<game::FTurnParticipant>(entity);
        registry.emplace<game::FActiveStatuses>(entity);
        auto& loadout = registry.emplace<game::FSkillLoadout>(entity);
        for (std::size_t i = 0; i < row.skillIds.size() && i < 4; ++i) {
            loadout.slots.push_back(game::FSkillSlot{row.skillIds[i], {}});
        }
        // The unit's mind, as FBattleSetup attaches it: its authored profile or the default one.
        if (const auto* library = registry.ctx().find<game::ai::FAiLibrary>()) {
            const auto* profile = row.aiProfile.empty() ? &library->fallback() : library->find(row.aiProfile);
            CHECK(profile != nullptr);
            registry.emplace_or_replace<game::ai::FAiBrain>(entity, game::ai::MakeBrain(*profile));
        }
        return entity;
    }

    int Hp(entt::entity unit) { return registry.get<game::FHealth>(unit).current; }
    int MaxHp(entt::entity unit) { return registry.get<game::FHealth>(unit).max; }
    void SetHp(entt::entity unit, int hp) { registry.get<game::FHealth>(unit).current = hp; }
    const game::FStats& Stats(entt::entity unit) { return registry.get<game::FStats>(unit); }

    bool Holds(entt::entity unit, std::string_view statusId) {
        const auto& active = registry.get<game::FActiveStatuses>(unit);
        return std::ranges::any_of(active.effects, [statusId](const game::FStatusEffect& effect) {
            return effect.statusId == statusId;
        });
    }

    /// The turn passes until `actor` holds it; the token is returned.
    game::FTurnToken StartTurnFor(entt::entity actor) {
        auto& runtime = registry.ctx().get<game::FTurnRuntime>();
        if (runtime.controller.snapshot().state != game::ETurnState::Running) {
            CHECK(runtime.controller.start(registry, 1234u).has_value());
        }
        for (int step = 0; step < 32; ++step) {
            const auto snapshot = runtime.controller.snapshot();
            CHECK(snapshot.currentTurn.has_value());
            if (snapshot.currentTurn->actor == actor) return *snapshot.currentTurn;
            CHECK(runtime.controller.endTurn(registry, *snapshot.currentTurn).has_value());
        }
        throw std::runtime_error("the requested actor never became the active unit");
    }

    /// One skill through the real action-queue path. A cell-target skill passes `entt::null` and a cell.
    void Cast(entt::entity actor, entt::entity target, game::FGridPosition cell, const std::string& skillId) {
        game::FActionRequest request;
        request.actor = actor;
        request.target = target;
        request.skillId = skillId;
        request.cell = cell;
        request.token = StartTurnFor(actor);
        auto& points = registry.get<game::FActionPoints>(actor);
        points.skill = points.skillMax;
        registry.ctx().get<game::FActionQueue>().pending.push_back(request);
        game::ecs::ActionResolverSystem resolver;
        resolver.update(registry, 0.f);
    }

    void CastAt(entt::entity actor, entt::entity target, const std::string& skillId) {
        Cast(actor, target, registry.get<game::FGridPosition>(target), skillId);
    }

    void CastOnCell(entt::entity actor, game::FGridPosition cell, const std::string& skillId) {
        Cast(actor, entt::null, cell, skillId);
    }

    /// combat-math 1.1: max(1, offense + power - defense), computed from the content rows.
    int Expected(const std::string& skillId, entt::entity caster, entt::entity target) {
        const auto& skill = Skill(skillId);
        const bool magical = skill.damageType == "magical";
        const int offense = magical ? Stats(caster).magicAttack : Stats(caster).attack;
        const int defence = magical ? Stats(target).magicDefense : Stats(target).defense;
        return std::max(1, offense + skill.power - defence);
    }

    std::optional<game::FIntentPlan> Plan(entt::entity enemy) { return game::PlanIntent(registry, enemy, Content()); }

    /// One brain decision; the same `turn` keeps the brain inside one turn.
    std::optional<game::FBattleAction> Decide(entt::entity actor, std::uint64_t turn) {
        game::FTurnToken token;
        token.sessionId = 1;
        token.roundNumber = turn;
        token.turnSerial = turn;
        token.actor = actor;
        return game::ai::RunBrain(registry, token, true, Content());  // an enemy's hostile side is the party
    }

    /// The AI's best option under the default filter (value per AP, AP required).
    std::optional<game::ai::FSkillOption> Best(entt::entity actor) {
        const game::ai::FAiWorld world = game::ai::BuildAiWorld(registry, actor, Content());
        game::ai::FAiBlackboard board;
        game::ai::FBtMemory memory;
        const game::ai::FAiTuning tuning;
        const game::ai::FSkillFilter filter;
        const game::ai::FBtContext ctx{registry, Content(), tuning, world, board, memory, actor,
                                       game::FTurnToken{1, 1, 1, actor}, true};
        return game::ai::FindBestOption(ctx, registry.get<game::FGridPosition>(actor), filter, true);
    }
};

int Distance(game::FGridPosition a, game::FGridPosition b) { return std::abs(a.x - b.x) + std::abs(a.y - b.y); }

/// The mender's brain, up to three decisions inside one turn; each move is applied (position + move AP).
/// Returns the first skill request, or nullopt when the tree only moved / ended.
std::optional<game::FActionRequest> FirstSkillRequest(FMonsterRig& rig, entt::entity unit, std::uint64_t turn) {
    rig.StartTurnFor(unit);
    for (int i = 0; i < 3; ++i) {
        const auto action = rig.Decide(unit, turn);
        if (!action) return std::nullopt;
        if (const auto* request = std::get_if<game::FActionRequest>(&*action)) return *request;
        const auto& move = std::get<game::FMoveRequest>(*action);
        rig.registry.get<game::FGridPosition>(unit) = move.destination;
        auto& points = rig.registry.get<game::FActionPoints>(unit);
        points.move = std::max(0, points.move - 1);
    }
    return std::nullopt;
}

/// The mender rig of F4/F5: mender, two wounded imps, a far warrior.
struct FMenderRig {
    FMonsterRig rig;
    entt::entity mender{}, a{}, b{}, warrior{};
    FMenderRig() {
        mender = rig.Spawn("enemy.imp_mender", {2, 2});
        a = rig.Spawn("enemy.imp", {3, 2});
        b = rig.Spawn("enemy.imp", {2, 3});
        warrior = rig.Spawn("char.warrior", {7, 7});
        rig.SetHp(a, 20);
        rig.SetHp(b, 44);
    }
};

// --- F1 ------------------------------------------------------------------------------------------

void ShippedRosterBoots() {
    auto loaded = game_test::LoadTestRegistry(ShipAssetRoots(), UniquePath("wp52-ship"));
    CHECK(loaded.content);
    CHECK(loaded.dungeon);
    const auto& content = loaded.registry.ctx().get<game::gameplay::FContentRegistry>();
    CHECK(content.units.rows.size() == 24);
    CHECK(content.skills.rows.size() == 37);
    const auto* encounters = game::FindEncounterContent(loaded.registry);
    CHECK(encounters != nullptr);
    CHECK(encounters->encounters.size() == 10);
}

// --- F4 / F5: the mender -------------------------------------------------------------------------

void MenderHealsTheMostWoundedAlly() {
    FMenderRig m;
    auto& rig = m.rig;
    const auto best = rig.Best(m.mender);
    CHECK(best.has_value());
    CHECK(best->slot >= 0);
    CHECK(rig.registry.get<game::FSkillLoadout>(m.mender).slots.at(static_cast<std::size_t>(best->slot)).skillId ==
          "skill.mend");
    CHECK(best->target == m.a);

    const auto request = FirstSkillRequest(rig, m.mender, 1);
    CHECK(request.has_value());
    CHECK(request->skillId == "skill.mend");
    CHECK(request->target == m.a);

    const int heal = rig.Stats(m.mender).magicAttack + rig.Skill("skill.mend").power;
    rig.CastAt(m.mender, m.a, "skill.mend");
    CHECK(rig.Hp(m.a) == std::min(rig.MaxHp(m.a), 20 + heal));
    CHECK(rig.Hp(m.a) == 28);
}

void MenderCommitsNoAttackIntent() {
    FMenderRig m;
    auto& rig = m.rig;
    auto plan = rig.Plan(m.mender);
    CHECK(!plan || (plan->skillId.empty() && !plan->attack));

    rig.SetHp(m.a, rig.MaxHp(m.a));
    rig.SetHp(m.b, rig.MaxHp(m.b));
    plan = rig.Plan(m.mender);
    CHECK(!plan || (plan->skillId.empty() && !plan->attack));
    CHECK(!FirstSkillRequest(rig, m.mender, 2).has_value());
}

// --- F6 ------------------------------------------------------------------------------------------

void HoundOutspeedsAndReachesTheBackLine() {
    FMonsterRig rig;
    const double hound = rig.Row("enemy.ember_hound").speed;
    for (const char* hero : {"char.warrior", "char.archer", "char.mage", "char.healer"}) {
        CHECK(hound > rig.Row(hero).speed);
    }
    const auto hunter = rig.Spawn("enemy.ember_hound", {0, 3});
    const auto healer = rig.Spawn("char.healer", {5, 3});
    const auto plan = rig.Plan(hunter);
    CHECK(plan.has_value());
    CHECK(plan->skillId == "skill.pounce");
    CHECK(plan->target == healer);
    CHECK(plan->mode == game::EIntentMode::Unit);
}

// --- F7 / F8: Detonate ---------------------------------------------------------------------------

void DetonateHitsTheZoneAndKillsTheCaster() {
    FMonsterRig rig;
    const auto imp = rig.Spawn("enemy.powder_imp", {3, 3});
    const auto warrior = rig.Spawn("char.warrior", {3, 4});
    const auto archer = rig.Spawn("char.archer", {2, 4});
    const auto mage = rig.Spawn("char.mage", {4, 4});
    const auto healer = rig.Spawn("char.healer", {6, 6});
    const std::array victims{warrior, archer, mage};
    std::array<int, 3> expected{};
    for (std::size_t i = 0; i < victims.size(); ++i) {
        expected[i] = rig.MaxHp(victims[i]) - rig.Expected("skill.detonate", imp, victims[i]);
    }
    rig.CastOnCell(imp, {3, 4}, "skill.detonate");
    for (std::size_t i = 0; i < victims.size(); ++i) CHECK(rig.Hp(victims[i]) == expected[i]);
    CHECK(rig.Hp(healer) == rig.MaxHp(healer));
    CHECK(rig.registry.all_of<game::FDead>(imp));
    CHECK(!rig.registry.all_of<game::FTurnParticipant>(imp));
}

void DetonateOnAnEmptyZoneSparesTheCaster() {
    FMonsterRig rig;
    const auto imp = rig.Spawn("enemy.powder_imp", {3, 3});
    const auto warrior = rig.Spawn("char.warrior", {6, 6});
    const auto archer = rig.Spawn("char.archer", {7, 6});
    rig.CastOnCell(imp, {3, 4}, "skill.detonate");
    CHECK(!rig.registry.all_of<game::FDead>(imp));
    CHECK(rig.Hp(imp) == rig.MaxHp(imp));
    CHECK(rig.Hp(imp) == 22);
    CHECK(rig.Hp(warrior) == rig.MaxHp(warrior));
    CHECK(rig.Hp(archer) == rig.MaxHp(archer));
}

// --- F9 ------------------------------------------------------------------------------------------

void BomberAndDrummerCommitTileIntents() {
    {
        FMonsterRig rig;
        const auto imp = rig.Spawn("enemy.powder_imp", {3, 3});
        rig.Spawn("char.warrior", {3, 5});
        rig.Spawn("char.archer", {4, 4});
        const auto plan = rig.Plan(imp);
        CHECK(plan.has_value());
        CHECK(plan->skillId == "skill.detonate");
        CHECK(plan->mode == game::EIntentMode::Tile);
    }
    {
        FMonsterRig rig;
        const auto drummer = rig.Spawn("enemy.ash_drummer", {1, 1});
        rig.Spawn("char.warrior", {3, 3});
        rig.Spawn("char.archer", {3, 4});
        const auto plan = rig.Plan(drummer);
        CHECK(plan.has_value());
        CHECK(plan->skillId == "skill.war_drum");
        CHECK(plan->mode == game::EIntentMode::Tile);
    }
}

// --- F20: no unannounced blast -------------------------------------------------------------------

void BomberDetonatesOnlyThroughATileIntent() {
    FMonsterRig rig;
    const auto imp = rig.Spawn("enemy.powder_imp", {4, 4});
    rig.Spawn("char.warrior", {5, 5});
    rig.StartTurnFor(imp);
    for (int i = 0; i < 3; ++i) {
        const auto action = rig.Decide(imp, 1);
        if (!action) break;
        if (const auto* request = std::get_if<game::FActionRequest>(&*action)) {
            CHECK(request->skillId != "skill.detonate");
            break;
        }
        const auto& move = std::get<game::FMoveRequest>(*action);
        rig.registry.get<game::FGridPosition>(imp) = move.destination;
        auto& points = rig.registry.get<game::FActionPoints>(imp);
        points.move = std::max(0, points.move - 1);
    }
    const auto plan = rig.Plan(imp);
    CHECK(plan.has_value());
    CHECK(plan->skillId == "skill.detonate");
    CHECK(plan->mode == game::EIntentMode::Tile);
}

// --- F10 -----------------------------------------------------------------------------------------

void ThrallHooksAndShackles() {
    FMonsterRig rig;
    const auto thrall = rig.Spawn("enemy.chain_thrall", {2, 2});
    const auto warrior = rig.Spawn("char.warrior", {4, 2});
    rig.CastAt(thrall, warrior, "skill.hook");
    CHECK(Distance(rig.registry.get<game::FGridPosition>(thrall), rig.registry.get<game::FGridPosition>(warrior)) == 1);
    rig.CastAt(thrall, warrior, "skill.shackle");
    CHECK(rig.Holds(warrior, "status.locked_move"));
}

// --- F11 -----------------------------------------------------------------------------------------

void WraithBoltPinsTheTarget() {
    FMonsterRig rig;
    const auto wraith = rig.Spawn("enemy.ash_wraith", {1, 1});
    const auto mage = rig.Spawn("char.mage", {1, 5});
    const int expectedHp = rig.MaxHp(mage) - rig.Expected("skill.ash_bolt", wraith, mage);
    rig.CastAt(wraith, mage, "skill.ash_bolt");
    CHECK(rig.Hp(mage) == expectedHp);
    CHECK(rig.Holds(mage, "status.locked_move"));
}

// --- F12 -----------------------------------------------------------------------------------------

void DrumDebuffsHeroesInTheZoneOnly() {
    FMonsterRig rig;
    const auto drummer = rig.Spawn("enemy.ash_drummer", {1, 1});
    const auto warrior = rig.Spawn("char.warrior", {3, 3});
    const auto archer = rig.Spawn("char.archer", {3, 4});
    const auto imp = rig.Spawn("enemy.imp", {2, 3});
    const auto mage = rig.Spawn("char.mage", {7, 7});
    rig.CastOnCell(drummer, {3, 3}, "skill.war_drum");
    CHECK(rig.Holds(warrior, "status.debuff"));
    CHECK(rig.Holds(archer, "status.debuff"));
    CHECK(rig.Hp(warrior) == rig.MaxHp(warrior));
    CHECK(rig.Hp(archer) == rig.MaxHp(archer));
    CHECK(!rig.Holds(imp, "status.debuff"));
    CHECK(!rig.Holds(mage, "status.debuff"));
}

// --- F13 -----------------------------------------------------------------------------------------

void ExistingKindsUseTheirThirdSkill() {
    const std::vector<std::pair<std::string, std::vector<std::string>>> expectedLists{
        {"enemy.imp_archer", {"skill.arrow", "skill.pin"}},
        {"enemy.imp_shaman", {"skill.bolt", "skill.imp_firebomb", "skill.hex"}},
        {"enemy.brute", {"skill.strike", "skill.shove", "skill.ground_slam"}},
        {"enemy.bone_guard", {"skill.strike", "skill.brace", "skill.bash"}},
        {"enemy.slinger", {"skill.arrow", "skill.barrage", "skill.venom_spit"}},
    };
    {
        FMonsterRig rig;
        for (const auto& [unit, skills] : expectedLists) CHECK(rig.Row(unit).skillIds == skills);
    }
    // A fresh rig per cast, the target at min(range, 2) cells in a row.
    const auto fresh = [](const std::string& unit, const std::string& skill, auto&& check) {
        FMonsterRig rig;
        const auto caster = rig.Spawn(unit, {1, 1});
        const int reach = std::min(rig.Skill(skill).range, 2);
        const auto target = rig.Spawn("char.warrior", {1 + reach, 1});
        check(rig, caster, target);
    };
    fresh("enemy.imp_archer", "skill.pin", [](FMonsterRig& rig, entt::entity caster, entt::entity target) {
        rig.CastAt(caster, target, "skill.pin");
        CHECK(rig.Hp(target) < rig.MaxHp(target));
        CHECK(rig.Holds(target, "status.locked_move"));
    });
    fresh("enemy.imp_shaman", "skill.hex", [](FMonsterRig& rig, entt::entity caster, entt::entity target) {
        rig.CastAt(caster, target, "skill.hex");
        CHECK(rig.Hp(target) < rig.MaxHp(target));
        CHECK(rig.Holds(target, "status.burn"));
    });
    fresh("enemy.bone_guard", "skill.bash", [](FMonsterRig& rig, entt::entity caster, entt::entity target) {
        rig.CastAt(caster, target, "skill.bash");
        CHECK(rig.Hp(target) < rig.MaxHp(target));
        CHECK(rig.Holds(target, "status.debuff"));
    });
    fresh("enemy.slinger", "skill.venom_spit", [](FMonsterRig& rig, entt::entity caster, entt::entity target) {
        rig.CastAt(caster, target, "skill.venom_spit");
        CHECK(rig.Hp(target) < rig.MaxHp(target));
        CHECK(rig.Holds(target, "status.debuff"));
    });
    {
        FMonsterRig rig;
        const auto brute = rig.Spawn("enemy.brute", {1, 1});
        const auto first = rig.Spawn("char.warrior", {2, 1});
        const auto second = rig.Spawn("char.archer", {2, 2});
        const int hitFirst = rig.MaxHp(first) - rig.Expected("skill.ground_slam", brute, first);
        const int hitSecond = rig.MaxHp(second) - rig.Expected("skill.ground_slam", brute, second);
        rig.CastAt(brute, first, "skill.ground_slam");
        CHECK(rig.Hp(first) == hitFirst);
        CHECK(rig.Hp(second) == hitSecond);
    }
}

// --- F14 -----------------------------------------------------------------------------------------

void EncounterCompositionFollowsTheThemes() {
    auto loaded = game_test::LoadTestRegistry(ShipAssetRoots(), UniquePath("wp52-enc"));
    CHECK(loaded.content && loaded.dungeon);
    const auto* table = game::FindEncounterContent(loaded.registry);
    CHECK(table != nullptr);
    std::map<std::string, std::set<std::string>> unitsByTheme;
    std::map<std::string, int> firstDepth;
    for (const auto& encounter : table->encounters) {
        const auto hasTag = [&](const char* tag) { return std::ranges::find(encounter.tags, tag) != encounter.tags.end(); };
        const bool crypt = hasTag("theme.ember_crypt");
        const bool ashen = hasTag("theme.ashen_reach");
        CHECK(crypt != ashen);
        if (crypt) CHECK(4 <= encounter.count.min && encounter.count.max <= 5);
        if (ashen) CHECK(encounter.count.min == 4 && encounter.count.max == 4);
        const std::string theme = crypt ? "crypt" : "ashen";
        std::set<std::string> units;
        for (const auto& slot : encounter.slots)
            for (const auto& choice : slot.units) units.insert(choice.unit);
        for (const auto& choice : encounter.fill) units.insert(choice.unit);
        for (const auto& unit : units) {
            unitsByTheme[theme].insert(unit);
            const auto found = firstDepth.find(unit);
            if (found == firstDepth.end() || encounter.introducedAtDepth < found->second) {
                firstDepth[unit] = encounter.introducedAtDepth;
            }
        }
    }
    const auto& crypt = unitsByTheme["crypt"];
    const auto& ashen = unitsByTheme["ashen"];
    CHECK(crypt.contains("enemy.ember_hound") && !ashen.contains("enemy.ember_hound"));
    CHECK(crypt.contains("enemy.powder_imp") && !ashen.contains("enemy.powder_imp"));
    for (const char* unit : {"enemy.chain_thrall", "enemy.ash_wraith", "enemy.ash_drummer"}) {
        CHECK(ashen.contains(unit) && !crypt.contains(unit));
    }
    CHECK(crypt.contains("enemy.imp_mender") && ashen.contains("enemy.imp_mender"));
    CHECK(firstDepth.at("enemy.ember_hound") == 1);
    CHECK(firstDepth.at("enemy.imp_mender") == 2);
    CHECK(firstDepth.at("enemy.powder_imp") == 2);
    CHECK(firstDepth.at("enemy.chain_thrall") == 5);
    CHECK(firstDepth.at("enemy.ash_wraith") == 5);
    CHECK(firstDepth.at("enemy.ash_drummer") == 6);
}

// --- F15 -----------------------------------------------------------------------------------------

void NewKindsClassifyInTheCompendium() {
    FMonsterRig rig;
    const auto role = [&](const char* unit) {
        return game::DeriveUnitRole(rig.Content(), rig.Row(std::string("enemy.") + unit));
    };
    CHECK(role("imp_mender") == "Support");
    CHECK(role("ember_hound") == "Ranged");
    CHECK(role("chain_thrall") == "Ranged");
    CHECK(role("ash_wraith") == "Ranged");
    CHECK(role("powder_imp") == "Zone");
    CHECK(role("ash_drummer") == "Zone");
}

// --- F16 / F17: art ------------------------------------------------------------------------------

struct FRenderedWorld {
    entt::registry registry;
    std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> surface{
        SDL_CreateSurface(1280, 800, SDL_PIXELFORMAT_RGBA8888), &SDL_DestroySurface};
    FRendererPtr renderer;

    FRenderedWorld() {
        CHECK(surface != nullptr);
        renderer.reset(SDL_CreateSoftwareRenderer(surface.get()));
        CHECK(renderer != nullptr);
        game::FAssetManager::Initialize(registry, ShipAssetRoots());
        auto content = game::gameplay::FContentRegistry::load(registry.ctx().get<game::FAssetManager>());
        CHECK(content.has_value());
        registry.ctx().emplace<game::gameplay::FContentRegistry>(std::move(*content));
        registry.ctx().emplace<game::FResourceManager>().init(renderer.get());
        registry.ctx().emplace<SDL_Renderer*>(renderer.get());
    }
};

void NewKindsHaveTheirOwnArt() {
    FRenderedWorld world;
    auto& registry = world.registry;
    std::vector<game::FUnitPlacement> placements;
    for (const char* kind : kNewKinds) {
        game::FUnitPlacement placement;
        placement.id = std::string("enemy.") + kind;
        placement.unitTemplateId = placement.id;
        placement.entity = registry.create();
        placements.push_back(std::move(placement));
    }
    game::AssignDungeonSprites(registry, placements);
    const std::array<game::EDungeonSprite, 6> expected{
        game::EDungeonSprite::ImpMender, game::EDungeonSprite::EmberHound, game::EDungeonSprite::PowderImp,
        game::EDungeonSprite::ChainThrall, game::EDungeonSprite::AshWraith, game::EDungeonSprite::AshDrummer};
    for (std::size_t i = 0; i < placements.size(); ++i) {
        CHECK(registry.get<game::FUnitArt>(placements[i].entity).sprite == expected[i]);
    }

    CHECK(game::FAnimationLibrary::Initialize(registry));
    const auto* library = game::FAnimationLibrary::Find(registry);
    CHECK(library != nullptr);
    for (const char* kind : kNewKinds) {
        for (const char* action : kActions) {
            const auto id = library->findClip(std::string("unit.") + kind + "." + action);
            CHECK(id.has_value());
            const auto* clip = library->get(*id);
            CHECK(clip != nullptr && !clip->frames.empty());
            const std::string name = action;
            CHECK(clip->loop == (name == "idle" || name == "walk"));
        }
    }
    const auto parsed = game::ParseBattleAnim(ReadJson("assets/data/battle_anim.json"), *library,
                                              registry.ctx().get<game::gameplay::FContentRegistry>());
    CHECK(parsed.has_value());
    for (const char* kind : kNewKinds) CHECK(parsed->setByUnit.contains(std::string("enemy.") + kind));
}

void NewSkillsHaveIconsAndEffects() {
    const std::array<std::pair<const char*, game::EDungeonSprite>, 9> icons{{
        {"skill.mend", game::EDungeonSprite::SkillMend},
        {"skill.bite", game::EDungeonSprite::SkillBite},
        {"skill.pounce", game::EDungeonSprite::SkillPounce},
        {"skill.detonate", game::EDungeonSprite::SkillDetonate},
        {"skill.war_drum", game::EDungeonSprite::SkillWarDrum},
        {"skill.ash_bolt", game::EDungeonSprite::SkillAshBolt},
        {"skill.hex", game::EDungeonSprite::SkillHex},
        {"skill.venom_spit", game::EDungeonSprite::SkillVenomSpit},
        {"skill.ground_slam", game::EDungeonSprite::SkillGroundSlam},
    }};
    FRenderedWorld world;
    CHECK(game::FAnimationLibrary::Initialize(world.registry));
    const auto* library = game::FAnimationLibrary::Find(world.registry);
    CHECK(library != nullptr);
    const auto parsed = game::ParseBattleAnim(ReadJson("assets/data/battle_anim.json"), *library,
                                              world.registry.ctx().get<game::gameplay::FContentRegistry>());
    CHECK(parsed.has_value());
    for (const auto& [id, sprite] : icons) {
        const auto icon = game::SkillIconSprite(id);
        CHECK(icon.has_value());
        CHECK(*icon == sprite);
        CHECK(parsed->skills.contains(id));
    }
}

}  // namespace

int main() {
    return game_test::RunTests("wp52_monsters", {
        {"ShippedRosterBoots", ShippedRosterBoots},
        {"MenderHealsTheMostWoundedAlly", MenderHealsTheMostWoundedAlly},
        {"MenderCommitsNoAttackIntent", MenderCommitsNoAttackIntent},
        {"HoundOutspeedsAndReachesTheBackLine", HoundOutspeedsAndReachesTheBackLine},
        {"DetonateHitsTheZoneAndKillsTheCaster", DetonateHitsTheZoneAndKillsTheCaster},
        {"DetonateOnAnEmptyZoneSparesTheCaster", DetonateOnAnEmptyZoneSparesTheCaster},
        {"BomberAndDrummerCommitTileIntents", BomberAndDrummerCommitTileIntents},
        {"BomberDetonatesOnlyThroughATileIntent", BomberDetonatesOnlyThroughATileIntent},
        {"ThrallHooksAndShackles", ThrallHooksAndShackles},
        {"WraithBoltPinsTheTarget", WraithBoltPinsTheTarget},
        {"DrumDebuffsHeroesInTheZoneOnly", DrumDebuffsHeroesInTheZoneOnly},
        {"ExistingKindsUseTheirThirdSkill", ExistingKindsUseTheirThirdSkill},
        {"EncounterCompositionFollowsTheThemes", EncounterCompositionFollowsTheThemes},
        {"NewKindsClassifyInTheCompendium", NewKindsClassifyInTheCompendium},
        {"NewKindsHaveTheirOwnArt", NewKindsHaveTheirOwnArt},
        {"NewSkillsHaveIconsAndEffects", NewSkillsHaveIconsAndEffects},
    });
}
