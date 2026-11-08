// Tests and analysis harness for the behavior-tree AI (docs/design/enemy-ai.md).
//   ai_tests                         run every check
//   ai_tests --report [seeds]        battle matrix: new AI vs the pre-BT AI, same seeds, same rooms
//   ai_tests --battle <seed> <depth> <room> [party=new|legacy] [enemy=new|legacy]
//                                    play one battle and print its combat log
//   ai_tests --trace                 print each decision of a scripted scenario (debugging aid)
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

#include "core/AssetManager.h"
#include "ecs/components/FActionPoints.h"
#include "ecs/components/FDead.h"
#include "ecs/components/FFaction.h"
#include "ecs/components/FGridPosition.h"
#include "ecs/components/FHealth.h"
#include "ecs/components/FSkillLoadout.h"
#include "ecs/systems/ActionResolverSystem.h"
#include "ecs/systems/CombatOutcomeSystem.h"
#include "ecs/systems/TurnFlowSystem.h"
#include "gameplay/GameplayServices.h"
#include "gameplay/ai/FAiBrain.h"
#include "gameplay/ai/FAiQueries.h"
#include "gameplay/ai/FBtBuilder.h"
#include "gameplay/combat/FCombatApply.h"
#include "gameplay/combat/FStatusRules.h"
#include "gameplay/factory/FEntityDefinitions.h"
#include "gameplay/ai/FAiLibrary.h"
#include "gameplay/combat/FAutoAction.h"
#include "gameplay/combat/FCombatQuery.h"
#include "gameplay/combat/FCombatRules.h"
#include "gameplay/combat/FSkillResolver.h"
#include "gameplay/dungeon/FDungeonRun.h"
#include "gameplay/grid/FGridQuery.h"
#include "gameplay/grid/FGridTerrain.h"
#include "states/FEmberBattleState.h"

#include "TestSupport.h"

namespace {

void Check(bool condition, const char* expression, int line) {
    if (!condition) throw std::runtime_error(std::string("Line ") + std::to_string(line) + ": " + expression);
}
#define CHECK(expression) Check((expression), #expression, __LINE__)

// ---------------------------------------------------------------------------------------------------------
// The AI this project used before behavior trees (src/gameplay/combat/FAutoAction.cpp at git 8e157df),
// kept verbatim as the control group of the analysis. It is wrapped as a tree leaf so it plugs into the
// same turn flow, which is also the proof that a new node type needs no change outside its own registration.
// ---------------------------------------------------------------------------------------------------------
namespace legacy {

std::optional<game::FBattleAction> Choose(
    entt::registry& registry, game::FTurnToken token, bool targetPartySide,
    const game::gameplay::FContentRegistry& content) {
    using namespace game;
    const auto actor = token.actor;
    const auto* loadout = registry.try_get<FSkillLoadout>(actor);
    const auto* actorCell = registry.try_get<FGridPosition>(actor);
    const auto* points = registry.try_get<FActionPoints>(actor);
    if (!IsAlive(registry, actor) || !loadout || !actorCell || !points) {
        return std::nullopt;
    }
    for (const FSkillSlot& slot : loadout->slots) {
        const auto resolved = ResolveSkillSlot(slot, content);
        if (!resolved || points->skill < resolved->apCost) {
            continue;
        }
        const bool ownSide = resolved->target == "ally" || resolved->target == "self";
        for (const auto target : registry.view<FGridPosition, FHealth>()) {
            if (!IsAlive(registry, target) || registry.all_of<FPartyMember>(target) !=
                (ownSide ? !targetPartySide : targetPartySide)) continue;
            if (resolved->target == "self" && target != actor) continue;
            if (ownSide && resolved->power > 0 &&
                registry.get<FHealth>(target).current >= registry.get<FHealth>(target).max) continue;
            const auto targetCell = registry.get<FGridPosition>(target);
            if (IsInRange(*actorCell, targetCell, resolved->range) &&
                HasGridLineOfSight(registry, *actorCell, targetCell)) {
                return FActionRequest{actor, target, slot.skillId, token, targetCell};
            }
        }
    }
    if (points->move <= 0) return std::nullopt;
    const auto* terrain = registry.ctx().find<FGridTerrain>();
    const int moveRange = terrain ? terrain->moveRange : 1;
    const auto reachable = ComputeReachableCells(registry, actor, moveRange);
    int bestScore = std::numeric_limits<int>::max();
    std::optional<FGridPosition> best;
    for (const auto target : registry.view<FGridPosition, FHealth>()) {
        if (!IsAlive(registry, target) || registry.all_of<FPartyMember>(target) != targetPartySide) continue;
        const auto targetCell = registry.get<FGridPosition>(target);
        for (const auto neighbor : OrthogonalNeighbors(targetCell)) {
            const auto path = FindGridPath(registry, actor, neighbor, kGridWidth * kGridHeight);
            if (path.empty()) continue;
            const auto destination = path[std::min(path.size(), static_cast<std::size_t>(moveRange)) - 1];
            if (std::ranges::find_if(reachable, [&](auto cell) {
                    return cell.x == destination.x && cell.y == destination.y;
                }) == reachable.end()) continue;
            int score = static_cast<int>(path.size()) * 10;
            for (const auto cell : FindGridPath(registry, actor, destination, moveRange)) {
                if (terrain && terrain->at(cell.x, cell.y) == ETile::Trap) score += 15;
            }
            if (score < bestScore) { bestScore = score; best = destination; }
        }
    }
    if (best) return FMoveRequest{actor, *best, token};
    return std::nullopt;
}

class FLeaf final : public game::ai::FBtNode {
public:
    [[nodiscard]] std::string_view typeName() const noexcept override { return "Legacy"; }

protected:
    [[nodiscard]] game::ai::EBtStatus update(game::ai::FBtContext& ctx) const override {
        const auto action = Choose(ctx.registry, ctx.token, ctx.hostilePartySide, ctx.content);
        if (!action || !ctx.blackboard.issue(*action, {}, "legacy")) return game::ai::EBtStatus::Failure;
        return game::ai::EBtStatus::Success;
    }
};

} // namespace legacy

/// A real dungeon battle (same setup as the game) that tests then rearrange cell by cell.
struct FFixture {
    entt::registry registry;
    game::ecs::TurnSystem turns;
    game::ecs::TurnFlowSystem flow;
    game::ecs::ActionResolverSystem actions;
    game::ecs::CombatOutcomeSystem outcomes;
    FEmberBattleState battle;
    std::filesystem::path checkpoint;

    explicit FFixture(std::uint32_t seed = 1234, int depth = 1, int room = 0,
                      std::vector<std::string> roots = TestAssetRoots()) {
        std::filesystem::create_directories("out");
        checkpoint = std::filesystem::path("out") / ("ai-checkpoint-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".json");
        game::FAssetManager::Initialize(registry, std::move(roots));
        CHECK(game::gameplay::FGameplayServices::Initialize(registry));
        CHECK(game::InitializeDungeon(registry, checkpoint.string()));
        turns.onRegister(registry);
        game::StartDungeonRun(registry, seed, true);
        auto& run = registry.ctx().get<game::FDungeonRun>();
        run.depth = depth;
        run.room = room;
        battle.OnEnter(registry);
    }

    ~FFixture() {
        battle.OnExit(registry);
        std::error_code error;
        std::filesystem::remove(checkpoint, error);
    }

    void step() {
        turns.update(registry, .1f);
        flow.update(registry, .1f);
        actions.update(registry, .1f);
        outcomes.update(registry, .1f);
    }

    game::FTurnToken token() {
        turns.update(registry, 0);
        flow.update(registry, 0);
        const auto snapshot = registry.ctx().get<game::FTurnRuntime>().controller.snapshot();
        CHECK(snapshot.currentTurn.has_value());
        return *snapshot.currentTurn;
    }

    [[nodiscard]] const game::gameplay::FContentRegistry& content() {
        return registry.ctx().get<game::gameplay::FContentRegistry>();
    }

    /// Gives every unit of one side the pre-BT AI.
    void useLegacy(bool partySide) {
        static const std::optional<game::ai::FAiLibrary> library = [&]() {
            auto nodes = game::ai::FAiNodeRegistry::builtin();
            nodes.add("Legacy", 0, 0, [](game::ai::FNodeSpec&) -> game::ai::FNodeBuild {
                return std::make_unique<legacy::FLeaf>();
            });
            // Every profile id the units name must exist, or the library rejects the unit reference.
            nlohmann::json profiles = nlohmann::json::array({{{"id", "ai.default"}, {"tree", {{"type", "Legacy"}}}}});
            std::vector<std::string> seen{"ai.default"};
            for (const auto& unit : content().units.rows) {
                if (!unit.aiProfile.empty() && std::ranges::find(seen, unit.aiProfile) == seen.end()) {
                    seen.push_back(unit.aiProfile);
                    profiles.push_back({{"id", unit.aiProfile}, {"tree", {{"type", "Legacy"}}}});
                }
            }
            const nlohmann::json document = {{"schema_version", 1}, {"profiles", profiles}};
            std::string error;
            auto built = game::ai::FAiLibrary::fromDocument(document, content(), nodes, error);
            if (!built) throw std::runtime_error("legacy library: " + error);
            return built;
        }();
        for (const auto entity : registry.view<game::ai::FAiBrain>()) {
            if (registry.all_of<game::FPartyMember>(entity) == partySide) {
                registry.replace<game::ai::FAiBrain>(entity, game::ai::MakeBrain(library->fallback()));
            }
        }
    }
};

// ---------------------------------------------------------------------------------------------------------
// Battle analysis
// ---------------------------------------------------------------------------------------------------------

struct FSideStats {
    int turns{0};
    int idleTurns{0};       // the turn issued no action at all although enemies were alive
    int skillApLeft{0};     // skill AP thrown away at the end of turns while enemies were alive
    int skillApTotal{0};
};

struct FBattleResult {
    bool finished{false};
    bool victory{false};
    int rounds{0};
    int partyHpPercent{0};  // party HP left, percent of max
    int enemyHpPercent{0};  // enemy HP left, percent of max
    FSideStats party;
    FSideStats enemy;
    std::vector<std::string> log;
    std::vector<std::string> decisions; // "round N Name: why" for every action the AI issued
    std::string map;                    // final board: # wall, ^ trap, lowercase = enemy, uppercase = party
};

[[nodiscard]] std::string DumpMap(entt::registry& registry) {
    const auto* terrain = registry.ctx().find<game::FGridTerrain>();
    std::string rows;
    for (int y = 0; y < game::kGridHeight; ++y) {
        for (int x = 0; x < game::kGridWidth; ++x) {
            char glyph = terrain && terrain->at(x, y) == game::ETile::Wall ? '#'
                         : terrain && terrain->at(x, y) == game::ETile::Trap ? '^' : '.';
            if (const auto occupant = game::FindGridOccupant(registry, x, y); occupant != entt::null) {
                const std::string name = game::UnitDisplayName(registry, occupant);
                const bool party = registry.all_of<game::FPartyMember>(occupant);
                const char initial = name.find("Alden") != std::string::npos ? 'w'
                                     : name.find("Rowan") != std::string::npos ? 'a'
                                     : name.find("Mira") != std::string::npos ? 'm'
                                     : name.find("Lumen") != std::string::npos ? 'h'
                                     : name.find("guardian") != std::string::npos ? 'g'
                                     : name.find("shaman") != std::string::npos ? 's'
                                     : name.find("archer") != std::string::npos ? 'r' : 'i';
                glyph = party ? static_cast<char>(std::toupper(initial)) : initial;
                if (registry.all_of<game::FDead>(occupant)) glyph = 'x';
            }
            rows += glyph;
        }
        rows += '\n';
    }
    return rows;
}

[[nodiscard]] int HpPercent(entt::registry& registry, bool party) {
    int current = 0;
    int maximum = 0;
    for (const auto entity : registry.view<game::FHealth>()) {
        if (registry.all_of<game::FPartyMember>(entity) != party) continue;
        const auto& health = registry.get<game::FHealth>(entity);
        current += std::max(0, health.current);
        maximum += health.max;
    }
    return maximum > 0 ? current * 100 / maximum : 0;
}

using FTuningOverrides = std::vector<std::pair<std::string, int>>;

/// Overrides AI tuning on every enemy brain (the experiment knob of --sweep).
void ApplyEnemyTuning(entt::registry& registry, const FTuningOverrides& overrides) {
    for (const auto& [key, value] : overrides) {
        const auto field = std::ranges::find(game::ai::kAiTuningFields, std::string_view(key),
                                             &game::ai::FAiTuningField::key);
        if (field == std::end(game::ai::kAiTuningFields)) throw std::runtime_error("unknown tuning key " + key);
        for (const auto entity : registry.view<game::ai::FAiBrain, game::FEnemy>()) {
            registry.get<game::ai::FAiBrain>(entity).tuning.*(field->member) = value;
        }
    }
}

FBattleResult PlayBattle(std::uint32_t seed, int depth, int room, bool legacyParty, bool legacyEnemy,
                         int stopRound = 0, const FTuningOverrides& enemyTuning = {}) {
    FFixture fixture(seed, depth, room);
    auto& registry = fixture.registry;
    if (legacyParty) fixture.useLegacy(true);
    if (legacyEnemy) fixture.useLegacy(false);
    ApplyEnemyTuning(registry, enemyTuning);
    registry.ctx().get<game::FCombatRuntime>().autoPlay = true;

    FBattleResult result;
    std::uint64_t watchedSerial = 0;
    entt::entity watchedActor = entt::null;
    int watchedActions = 0;
    int steps = 0;
    while (!registry.ctx().get<game::FBattleOutcome>().resolved && steps++ < 6000) {
        fixture.step();
        const auto snapshot = registry.ctx().get<game::FTurnRuntime>().controller.snapshot();
        if (!snapshot.currentTurn) continue;
        if (stopRound > 0 && static_cast<int>(snapshot.roundNumber) > stopRound) break;
        const auto& combat = registry.ctx().get<game::FCombatRuntime>();
        if (snapshot.currentTurn->turnSerial != watchedSerial) {
            // The previous actor's turn just ended: book it before the new turn's counters take over.
            if (registry.valid(watchedActor) && game::IsAlive(registry, watchedActor) &&
                game::AliveCount(registry, true) > 0 && game::AliveCount(registry, false) > 0) {
                const bool party = registry.all_of<game::FPartyMember>(watchedActor);
                FSideStats& side = party ? result.party : result.enemy;
                ++side.turns;
                if (watchedActions == 0) {
                    ++side.idleTurns;
                    const auto& cell = registry.get<game::FGridPosition>(watchedActor);
                    result.decisions.push_back(
                        "round " + std::to_string(snapshot.roundNumber) + " " +
                        game::UnitDisplayName(registry, watchedActor) + ": IDLE at (" + std::to_string(cell.x) + "," +
                        std::to_string(cell.y) + ") nearest foe " +
                        std::to_string(game::GridDistance(cell, registry.get<game::FGridPosition>(
                            game::FindNearestAlive(registry, watchedActor, !party)))));
                }
                const auto& points = registry.get<game::FActionPoints>(watchedActor);
                side.skillApLeft += points.skill;
                side.skillApTotal += points.skillMax;
            }
            watchedSerial = snapshot.currentTurn->turnSerial;
            watchedActor = snapshot.currentTurn->actor;
            watchedActions = 0; // the flow system reset the per-turn counter for the new turn
        }
        if (combat.autoActionsThisTurn > watchedActions && registry.valid(watchedActor)) {
            if (const auto* brain = registry.try_get<game::ai::FAiBrain>(watchedActor)) {
                result.decisions.push_back("round " + std::to_string(snapshot.roundNumber) + " " +
                                           game::UnitDisplayName(registry, watchedActor) + ": " +
                                           brain->blackboard.reason);
            }
        }
        watchedActions = combat.autoActionsThisTurn;
        result.rounds = static_cast<int>(snapshot.roundNumber);
    }
    const auto& outcome = registry.ctx().get<game::FBattleOutcome>();
    result.finished = outcome.resolved && steps < 6000;
    result.victory = outcome.victory;
    result.partyHpPercent = HpPercent(registry, true);
    result.enemyHpPercent = HpPercent(registry, false);
    result.log = registry.ctx().get<game::FCombatLog>().lines;
    result.map = DumpMap(registry);
    return result;
}

struct FAggregate {
    int battles{0};
    int stalls{0};
    int stalemates{0}; // ended by the round limit, i.e. nobody could finish the fight
    int wins{0};
    long long rounds{0};
    long long partyHp{0};
    long long enemyHpOnLoss{0};
    int losses{0};
    FSideStats party;
    FSideStats enemy;

    void add(const FBattleResult& result) {
        ++battles;
        if (!result.finished) { ++stalls; return; }
        if (static_cast<std::uint64_t>(result.rounds) > game::kMaxBattleRounds) ++stalemates;
        rounds += result.rounds;
        partyHp += result.partyHpPercent;
        if (result.victory) {
            ++wins;
        } else {
            ++losses;
            enemyHpOnLoss += result.enemyHpPercent;
        }
        for (auto [total, part] : {std::pair{&party, &result.party}, std::pair{&enemy, &result.enemy}}) {
            total->turns += part->turns;
            total->idleTurns += part->idleTurns;
            total->skillApLeft += part->skillApLeft;
            total->skillApTotal += part->skillApTotal;
        }
    }
};

void PrintAggregate(const std::string& label, const FAggregate& a) {
    const auto pct = [](long long part, long long whole) { return whole > 0 ? 100.0 * part / whole : 0.0; };
    const int done = a.battles - a.stalls;
    std::cout << std::left << std::setw(26) << label << std::right << std::fixed << std::setprecision(1)
              << " win " << std::setw(5) << pct(a.wins, a.battles) << "%"
              << " | rounds " << std::setw(5) << (done ? static_cast<double>(a.rounds) / done : 0.0)
              << " | party HP left " << std::setw(5) << (done ? static_cast<double>(a.partyHp) / done : 0.0) << "%"
              << " | enemy HP left on loss " << std::setw(5)
              << (a.losses ? static_cast<double>(a.enemyHpOnLoss) / a.losses : 0.0) << "%"
              << " | stalemates " << a.stalemates << " stalls " << a.stalls << "\n"
              << std::setw(26) << "" << "   idle turns: party " << std::setw(4) << pct(a.party.idleTurns, a.party.turns)
              << "%  enemy " << std::setw(4) << pct(a.enemy.idleTurns, a.enemy.turns) << "%"
              << " | skill AP wasted: party " << std::setw(4) << pct(a.party.skillApLeft, a.party.skillApTotal)
              << "%  enemy " << std::setw(4) << pct(a.enemy.skillApLeft, a.enemy.skillApTotal) << "%\n";
}

void Report(int seedCount, int firstDepth, int lastDepth, const std::vector<int>& rooms, bool newPartyOnly) {
    struct FConfig { const char* label; bool legacyParty; bool legacyEnemy; };
    std::vector<FConfig> configs = {
        {"party legacy / enemy legacy", true, true},
        {"party legacy / enemy NEW", true, false},
        {"party NEW    / enemy legacy", false, true},
        {"party NEW    / enemy NEW", false, false},
    };
    if (newPartyOnly) {
        // The competent party is the useful opponent: the legacy party loses to its own heal loops.
        configs = {configs[2], configs[3]};
    }
    std::cout << "depths " << firstDepth << ".." << lastDepth << ", " << seedCount << " seeds per depth\n";
    for (const int room : rooms) {
        std::cout << (room == 2 ? "== boss room (room 3) ==" : room == 1 ? "== room 2 ==" : "== room 1 ==") << "\n";
        for (const FConfig& config : configs) {
            FAggregate total;
            struct FOutlier { int rounds; int seed; int depth; bool victory; };
            std::vector<FOutlier> longest;
            for (int depth = firstDepth; depth <= lastDepth; ++depth) {
                for (int seed = 1; seed <= seedCount; ++seed) {
                    const FBattleResult result = PlayBattle(static_cast<std::uint32_t>(seed), depth, room,
                                                            config.legacyParty, config.legacyEnemy);
                    total.add(result);
                    longest.push_back({result.rounds, seed, depth, result.victory});
                }
            }
            PrintAggregate(config.label, total);
            std::ranges::sort(longest, [](const FOutlier& a, const FOutlier& b) { return a.rounds > b.rounds; });
            std::cout << std::setw(26) << "" << "   longest:";
            for (std::size_t i = 0; i < std::min<std::size_t>(4, longest.size()); ++i) {
                std::cout << "  seed " << longest[i].seed << " d" << longest[i].depth << " -> " << longest[i].rounds
                          << (longest[i].victory ? "r" : "r LOSS");
            }
            std::cout << "\n";
        }
    }
}

/// One tuning key over several values, party = the competent new AI, enemies = the new AI with that value.
/// Lower party win rate / party HP left = a harder enemy, which is the direction this analysis wants.
void Sweep(const std::string& key, const std::vector<int>& values, int seedCount, int firstDepth, int lastDepth,
           const std::vector<int>& rooms) {
    std::cout << "sweep " << key << " | depths " << firstDepth << ".." << lastDepth << ", " << seedCount
              << " seeds, rooms";
    for (const int room : rooms) std::cout << ' ' << room + 1;
    std::cout << "\n";
    for (const int value : values) {
        FAggregate total;
        for (const int room : rooms) {
            for (int depth = firstDepth; depth <= lastDepth; ++depth) {
                for (int seed = 1; seed <= seedCount; ++seed) {
                    total.add(PlayBattle(static_cast<std::uint32_t>(seed), depth, room, false, false, 0,
                                         {{key, value}}));
                }
            }
        }
        PrintAggregate(key + "=" + std::to_string(value), total);
    }
}

void PrintBattle(std::uint32_t seed, int depth, int room, bool legacyParty, bool legacyEnemy, int stopRound,
                 const FTuningOverrides& enemyTuning = {}) {
    const FBattleResult result = PlayBattle(seed, depth, room, legacyParty, legacyEnemy, stopRound, enemyTuning);
    for (const std::string& line : result.decisions) std::cout << line << "\n";
    std::cout << "---- combat log ----\n";
    for (const std::string& line : result.log) std::cout << line << "\n";
    std::cout << result.map;
    std::cout << "== " << (result.victory ? "VICTORY" : "DEFEAT") << " in " << result.rounds
              << " rounds, party HP " << result.partyHpPercent << "%, enemy HP " << result.enemyHpPercent << "%\n";
}

// ---------------------------------------------------------------------------------------------------------
// Behavior-tree core: the contracts of docs/GameAIPro_Chapter06 §6.3 on a world with no battle in it.
// ---------------------------------------------------------------------------------------------------------

using game::ai::EBtStatus;
using game::ai::EParallelPolicy;
using game::ai::FBtBuilder;
using game::ai::FBtContext;
using game::ai::FBtLeaf;

/// A tree plus everything a tick needs, with no registry content behind it.
struct FRig {
    entt::registry registry;
    game::gameplay::FContentRegistry content;
    game::ai::FAiTuning tuning;
    game::ai::FAiWorld world;
    game::ai::FAiBlackboard blackboard;
    game::ai::FBehaviorTree tree;
    game::ai::FBtMemory memory;

    explicit FRig(game::ai::FBtNodePtr root) : tree(std::move(root)), memory(tree.makeMemory()) {}

    EBtStatus tick() {
        FBtContext ctx{registry, content, tuning, world, blackboard, memory};
        return tree.tick(ctx);
    }
};

[[nodiscard]] FBtLeaf::FUpdate Returning(int& calls, EBtStatus result) {
    return [&calls, result](FBtContext&) {
        ++calls;
        return result;
    };
}

[[nodiscard]] game::ai::FBtNodePtr Watching(const char* name, int& ticks, int& aborts, EBtStatus result) {
    return std::make_unique<FBtLeaf>(
        name,
        [&ticks, result](FBtContext&) {
            ++ticks;
            return result;
        },
        [&aborts](FBtContext&, EBtStatus status) {
            if (status == EBtStatus::Aborted) ++aborts;
        });
}

[[nodiscard]] bool Throws(const std::function<void()>& body) {
    try {
        body();
    } catch (const std::logic_error&) {
        return true;
    }
    return false;
}

void SequenceContract() {
    int a = 0, b = 0, c = 0, runsLeft = 2;
    FRig rig(FBtBuilder().sequence()
                 .action("A", Returning(a, EBtStatus::Success))
                 .action("B", [&](FBtContext&) {
                     ++b;
                     return --runsLeft >= 0 ? EBtStatus::Running : EBtStatus::Success;
                 })
                 .action("C", Returning(c, EBtStatus::Success))
                 .end().build());
    CHECK(rig.tick() == EBtStatus::Running);
    CHECK(a == 1 && b == 1 && c == 0);
    CHECK(rig.tick() == EBtStatus::Running);
    CHECK(a == 1 && b == 2 && c == 0); // resumes at the running child; A is not run again
    CHECK(rig.tick() == EBtStatus::Success);
    CHECK(a == 1 && b == 3 && c == 1); // C starts in the same update B finished in (§6.3.7)

    int d = 0, e = 0;
    FRig failing(FBtBuilder().sequence()
                     .action("D", Returning(d, EBtStatus::Failure))
                     .action("E", Returning(e, EBtStatus::Success))
                     .end().build());
    CHECK(failing.tick() == EBtStatus::Failure);
    CHECK(d == 1 && e == 0);
}

void SelectorContract() {
    int a = 0, b = 0, c = 0;
    FRig rig(FBtBuilder().selector()
                 .action("A", Returning(a, EBtStatus::Failure))
                 .action("B", Returning(b, EBtStatus::Success))
                 .action("C", Returning(c, EBtStatus::Success))
                 .end().build());
    CHECK(rig.tick() == EBtStatus::Success);
    CHECK(a == 1 && b == 1 && c == 0);

    // Passive (§6.3.9): a child that keeps running keeps the selector, whatever a higher child would now say.
    bool highReady = false;
    int high = 0, low = 0;
    FRig passive(FBtBuilder().selector()
                     .action("high", [&](FBtContext&) {
                         ++high;
                         return highReady ? EBtStatus::Success : EBtStatus::Failure;
                     })
                     .action("low", Returning(low, EBtStatus::Running))
                     .end().build());
    CHECK(passive.tick() == EBtStatus::Running);
    highReady = true;
    CHECK(passive.tick() == EBtStatus::Running);
    CHECK(high == 1 && low == 2);
}

void ActiveSelectorPreempts() {
    bool highReady = false;
    int highTicks = 0, lowTicks = 0, lowAborts = 0;
    FRig rig(FBtBuilder().activeSelector()
                 .action("high", [&](FBtContext&) {
                     ++highTicks;
                     return highReady ? EBtStatus::Success : EBtStatus::Failure;
                 })
                 .node(Watching("low", lowTicks, lowAborts, EBtStatus::Running))
                 .end().build());
    CHECK(rig.tick() == EBtStatus::Running);
    CHECK(rig.tick() == EBtStatus::Running);
    CHECK(highTicks == 2 && lowTicks == 2 && lowAborts == 0); // §6.3.12: the higher child is re-checked each tick
    highReady = true;
    CHECK(rig.tick() == EBtStatus::Success);
    CHECK(lowAborts == 1 && lowTicks == 2); // the lower-priority behavior was cut off, not left running
    highReady = false;
    CHECK(rig.tick() == EBtStatus::Running);
    CHECK(lowTicks == 3); // and it starts afresh
}

void ParallelPolicies() {
    int runnerTicks = 0, runnerAborts = 0, done = 0;
    FRig one(FBtBuilder().parallel(EParallelPolicy::RequireOne, EParallelPolicy::RequireOne)
                 .node(Watching("runner", runnerTicks, runnerAborts, EBtStatus::Running))
                 .action("done", Returning(done, EBtStatus::Success))
                 .end().build());
    CHECK(one.tick() == EBtStatus::Success);
    CHECK(runnerAborts == 1); // §6.3.10: finishing early aborts the siblings that still run

    int slowLeft = 2, fast = 0;
    FRig all(FBtBuilder().parallel(EParallelPolicy::RequireAll, EParallelPolicy::RequireOne)
                 .action("fast", Returning(fast, EBtStatus::Success))
                 .action("slow", [&](FBtContext&) {
                     return --slowLeft >= 0 ? EBtStatus::Running : EBtStatus::Success;
                 })
                 .end().build());
    CHECK(all.tick() == EBtStatus::Running);
    CHECK(all.tick() == EBtStatus::Running);
    CHECK(all.tick() == EBtStatus::Success);
    CHECK(fast == 1); // a finished child is not ticked again

    int ok = 0, bad = 0;
    FRig failing(FBtBuilder().parallel(EParallelPolicy::RequireAll, EParallelPolicy::RequireOne)
                     .action("ok", Returning(ok, EBtStatus::Success))
                     .action("bad", Returning(bad, EBtStatus::Failure))
                     .end().build());
    CHECK(failing.tick() == EBtStatus::Failure);

    // §6.3.11 monitor: a watched condition supervises a long action and cancels it when it stops holding.
    bool assumptionHolds = true;
    int work = 0, workAborts = 0;
    FRig monitor(FBtBuilder().parallel(EParallelPolicy::RequireOne, EParallelPolicy::RequireOne)
                     .watch().condition("assumption", [&](FBtContext&) {
                         return assumptionHolds ? EBtStatus::Success : EBtStatus::Failure;
                     })
                     .node(Watching("work", work, workAborts, EBtStatus::Running))
                     .end().build());
    CHECK(monitor.tick() == EBtStatus::Running);
    CHECK(monitor.tick() == EBtStatus::Running);
    assumptionHolds = false;
    CHECK(monitor.tick() == EBtStatus::Failure);
    CHECK(workAborts == 1);
}

void DecoratorContracts() {
    int calls = 0;
    FRig repeat(FBtBuilder().repeat(3).action("hit", Returning(calls, EBtStatus::Success)).build());
    CHECK(repeat.tick() == EBtStatus::Success);
    CHECK(calls == 3); // §6.3.5: all repeats happen inside one update

    int steps = 0;
    FRig sustained(FBtBuilder().repeat(2).action("step", [&](FBtContext&) {
                       ++steps;
                       return steps % 2 == 1 ? EBtStatus::Running : EBtStatus::Success;
                   }).build());
    CHECK(sustained.tick() == EBtStatus::Running);
    CHECK(sustained.tick() == EBtStatus::Running); // first repeat done, second one started and is running
    CHECK(sustained.tick() == EBtStatus::Success);

    int broken = 0;
    FRig failing(FBtBuilder().repeat(5).action("break", Returning(broken, EBtStatus::Failure)).build());
    CHECK(failing.tick() == EBtStatus::Failure && broken == 1);

    int inner = 0;
    FRig inverted(FBtBuilder().inverter().action("no", Returning(inner, EBtStatus::Failure)).build());
    CHECK(inverted.tick() == EBtStatus::Success);
    FRig optional(FBtBuilder().succeeder().action("no", Returning(inner, EBtStatus::Failure)).build());
    CHECK(optional.tick() == EBtStatus::Success);

    int swings = 0;
    FRig cooldown(FBtBuilder().cooldown("swing", 2).action("swing", Returning(swings, EBtStatus::Success)).build());
    cooldown.blackboard.turnsTaken = 1;
    CHECK(cooldown.tick() == EBtStatus::Success);
    CHECK(cooldown.tick() == EBtStatus::Failure); // same turn
    cooldown.blackboard.turnsTaken = 2;
    CHECK(cooldown.tick() == EBtStatus::Failure); // one turn later is still too early for turns = 2
    cooldown.blackboard.turnsTaken = 3;
    CHECK(cooldown.tick() == EBtStatus::Success);
    CHECK(swings == 2);

    int cries = 0;
    FRig once(FBtBuilder().once("cry").action("cry", Returning(cries, EBtStatus::Success)).build());
    CHECK(once.tick() == EBtStatus::Success);
    CHECK(once.tick() == EBtStatus::Failure);
    CHECK(cries == 1);

    // A cooldown only starts when the child actually did something.
    int failedTries = 0;
    FRig idle(FBtBuilder().cooldown("idle", 5).action("fail", Returning(failedTries, EBtStatus::Failure)).build());
    idle.blackboard.turnsTaken = 1;
    CHECK(idle.tick() == EBtStatus::Failure);
    CHECK(idle.tick() == EBtStatus::Failure);
    CHECK(failedTries == 2);
}

void BuilderRejectsMisuse() {
    CHECK(Throws([] { FBtBuilder().end(); }));
    CHECK(Throws([] { (void)FBtBuilder().repeat(2).build(); }));
    CHECK(Throws([] { (void)FBtBuilder().build(); }));
    CHECK(Throws([] {
        FBtBuilder builder;
        builder.action("a", [](FBtContext&) { return EBtStatus::Success; });
        builder.action("b", [](FBtContext&) { return EBtStatus::Success; });
    }));
    FRig nested(FBtBuilder().activeSelector()
                    .sequence().condition("never", [](FBtContext&) { return EBtStatus::Failure; }).end()
                    .succeeder().inverter().action("x", [](FBtContext&) { return EBtStatus::Success; })
                    .end().build());
    CHECK(nested.tree.nodeCount() == 6);
    CHECK(nested.tick() == EBtStatus::Success);
}

void SharedTreeKeepsActorsApart() {
    int a = 0, b = 0;
    game::ai::FBehaviorTree tree(FBtBuilder().sequence()
                                     .action("A", Returning(a, EBtStatus::Success))
                                     .action("B", [&](FBtContext& ctx) {
                                         ++b;
                                         return ctx.blackboard.turnsTaken == 1 ? EBtStatus::Running : EBtStatus::Success;
                                     })
                                     .end().build());
    entt::registry registry;
    game::gameplay::FContentRegistry content;
    game::ai::FAiTuning tuning;
    game::ai::FAiWorld world;
    game::ai::FAiBlackboard first, second;
    first.turnsTaken = 1;
    second.turnsTaken = 2;
    game::ai::FBtMemory memoryOne = tree.makeMemory(), memoryTwo = tree.makeMemory();
    FBtContext one{registry, content, tuning, world, first, memoryOne};
    FBtContext two{registry, content, tuning, world, second, memoryTwo};
    CHECK(tree.tick(one) == EBtStatus::Running); // actor one waits inside B
    CHECK(tree.tick(two) == EBtStatus::Success); // actor two runs through the very same nodes
    CHECK(tree.tick(one) == EBtStatus::Running); // actor one resumes in B without redoing A (§6.4.2)
    CHECK(a == 2 && b == 3);
}

// ---------------------------------------------------------------------------------------------------------
// Authored trees: ai.json is validated like every other content table (R19/R22).
// ---------------------------------------------------------------------------------------------------------

[[nodiscard]] nlohmann::json DocumentWith(nlohmann::json tree) {
    // Units name ai.boss in the shipped data, so a document must define it too to pass the unit check.
    const nlohmann::json boss = {{"id", "ai.boss"}, {"tree", {{"type", "Wait"}}}};
    return {{"schema_version", 1},
            {"profiles", nlohmann::json::array({{{"id", "ai.default"}, {"tree", std::move(tree)}}, boss})}};
}

/// Empty string = loaded; otherwise the rejection reason.
[[nodiscard]] std::string LoadError(const nlohmann::json& document, const game::gameplay::FContentRegistry& content,
                                    const game::ai::FAiNodeRegistry& nodes = game::ai::FAiNodeRegistry::builtin()) {
    std::string error;
    return game::ai::FAiLibrary::fromDocument(document, content, nodes, error) ? std::string{} : error;
}

[[nodiscard]] bool Mentions(const std::string& text, const char* needle) { return text.find(needle) != std::string::npos; }

void ShippedDataLoads() {
    // This test validates the SHIPPED content itself (not the frozen fixture), so it reads the ship roots only.
    FFixture fixture{1234, 1, 0, ShipAssetRoots()};
    const auto& library = fixture.registry.ctx().get<game::ai::FAiLibrary>();
    CHECK(library.find("ai.default") != nullptr && &library.fallback() == library.find("ai.default"));
    CHECK(library.find("ai.boss") != nullptr);
    CHECK(library.find("ai.nobody") == nullptr);
    const auto* guardian = fixture.content().units.find("enemy.guardian");
    CHECK(guardian != nullptr && guardian->aiProfile == "ai.boss");
    CHECK(Mentions(library.fallback().tree->describe(), "Engage"));
    CHECK(Mentions(library.find("ai.boss")->tree->describe(), "Once(enrage)"));

    // The boss room really gives the boss its profile; everything else runs the default. WP-08: with the
    // v2 sequence the first boss is dungeon 3's mid boss (battle ordinal 2) — dungeon 1 has no boss slot.
    FFixture bossRoom(1, 3, 2);
    int bosses = 0;
    for (const auto entity : bossRoom.registry.view<game::FEnemy, game::ai::FAiBrain>()) {
        if (bossRoom.registry.get<game::ai::FAiBrain>(entity).profileId == "ai.boss") ++bosses;
    }
    CHECK(bosses == 1);
}

void BrokenDataIsRejected() {
    FFixture fixture;
    const auto& content = fixture.content();
    const auto wait = nlohmann::json{{"type", "Wait"}};
    CHECK(LoadError(DocumentWith(wait), content).empty());

    const auto rejects = [&](nlohmann::json tree, const char* reason) {
        const std::string error = LoadError(DocumentWith(std::move(tree)), content);
        if (!Mentions(error, reason)) {
            throw std::runtime_error("expected '" + std::string(reason) + "' but got '" + error + "'");
        }
    };
    rejects({{"type", "Dance"}}, "unknown node type");
    rejects({{"type", "Wait"}, {"speed", 3}}, "unknown parameter");
    rejects({{"type", "SelfHpBelow"}}, "missing required");
    rejects({{"type", "SelfHpBelow"}, {"pct", 0}}, "must be in");
    rejects({{"type", "SelfHpBelow"}, {"pct", "half"}}, "whole number");
    rejects({{"type", "Inverter"}}, "expects 1..1 children");
    rejects({{"type", "Wait"}, {"children", nlohmann::json::array({wait})}}, "expects 0..0 children");
    rejects({{"type", "UseSkill"}, {"role", "bard"}}, "unknown role");
    rejects({{"type", "UseSkill"}, {"skills", nlohmann::json::array({"skill.nope"})}}, "unknown skill");
    rejects({{"type", "SelfHasStatus"}, {"status", "status.nope"}}, "unknown status");
    rejects({{"type", "Ref"}, {"id", "fragment.nope"}}, "unknown fragment");
    rejects({{"type", "Parallel"}, {"success", "some"}, {"failure", "one"},
             {"children", nlohmann::json::array({wait})}}, "must be");
    rejects({{"type", "TurnIs"}, {"every", 2}, {"offset", 2}}, "smaller than 'every'");

    nlohmann::json deep = wait;
    for (int i = 0; i < 30; ++i) deep = {{"type", "Succeeder"}, {"children", nlohmann::json::array({deep})}};
    rejects(deep, "nested deeper");

    // Document level.
    auto document = DocumentWith(wait);
    document["schema_version"] = 2;
    CHECK(Mentions(LoadError(document, content), "schema_version"));
    document = DocumentWith(wait);
    document["profiles"][0]["id"] = "ai.other";
    CHECK(Mentions(LoadError(document, content), "ai.default"));
    document = DocumentWith(wait);
    document["profiles"].push_back(document["profiles"][0]);
    CHECK(Mentions(LoadError(document, content), "defined twice"));
    document = DocumentWith(wait);
    document["tuning"] = {{"kill_horizon_turns", 99}};
    CHECK(Mentions(LoadError(document, content), "tuning"));
    document["tuning"] = {{"bravery", 3}};
    CHECK(Mentions(LoadError(document, content), "unknown tuning key"));
    document = DocumentWith(wait);
    document["unexpected"] = true;
    CHECK(Mentions(LoadError(document, content), "unknown key"));
    document = DocumentWith({{"type", "Ref"}, {"id", "fragment.loop"}});
    document["fragments"] = nlohmann::json::array({{{"id", "fragment.loop"}, {"node", {{"type", "Ref"}, {"id", "fragment.loop"}}}}});
    CHECK(Mentions(LoadError(document, content), "includes itself"));

    // A unit may only name a profile that exists.
    auto brokenContent = content;
    brokenContent.units.rows.front().aiProfile = "ai.nobody";
    CHECK(Mentions(LoadError(DocumentWith(wait), brokenContent), "unknown profile"));
}

void FragmentsAndTuningCompose() {
    FFixture fixture;
    nlohmann::json document = DocumentWith({{"type", "Ref"}, {"id", "fragment.twice"}});
    document["fragments"] = nlohmann::json::array(
        {{{"id", "fragment.once"}, {"node", {{"type", "Cooldown"}, {"key", "big"}, {"turns", 2},
                                             {"children", nlohmann::json::array({{{"type", "UseSkill"}, {"role", "damage"}}})}}}},
         {{"id", "fragment.twice"}, {"node", {{"type", "Sequence"}, {"children", nlohmann::json::array(
             {{{"type", "Ref"}, {"id", "fragment.once"}}, {{"type", "Ref"}, {"id", "fragment.once"}}})}}}}});
    document["profiles"][0]["tuning"] = {{"danger_percent", 7}};
    std::string error;
    const auto library = game::ai::FAiLibrary::fromDocument(document, fixture.content(),
                                                            game::ai::FAiNodeRegistry::builtin(), error);
    CHECK(library.has_value());
    const auto& profile = library->fallback();
    CHECK(profile.tuning.dangerPercent == 7 && profile.tuning.progressPoints == game::ai::FAiTuning{}.progressPoints);
    CHECK(profile.tree->nodeCount() == 5); // Sequence + two private copies of Cooldown/UseSkill... shared by id, not by node
    CHECK(Mentions(profile.tree->describe(), "Cooldown(big, 2)"));
}

void NewNodeTypesNeedNoLoaderChange() {
    FFixture fixture;
    auto nodes = game::ai::FAiNodeRegistry::builtin();
    int custom = 0;
    nodes.add("Custom", 0, 0, [&custom](game::ai::FNodeSpec& spec) -> game::ai::FNodeBuild {
        const int bonus = spec.params.integer("bonus", 1, 9);
        if (const auto problem = spec.params.finish()) return std::unexpected(*problem);
        return std::make_unique<FBtLeaf>("Custom", [&custom, bonus](FBtContext&) {
            custom += bonus;
            return EBtStatus::Success;
        });
    });
    std::string error;
    auto library = game::ai::FAiLibrary::fromDocument(
        DocumentWith({{"type", "Custom"}, {"bonus", 4}}), fixture.content(), nodes, error);
    CHECK(library.has_value());
    CHECK(Mentions(LoadError(DocumentWith({{"type", "Custom"}}), fixture.content(), nodes), "missing required"));
    CHECK(Mentions(LoadError(DocumentWith({{"type", "Custom"}, {"bonus", 4}}), fixture.content()), "unknown node type"));
    FRig rig(FBtBuilder().node(std::make_unique<FBtLeaf>("probe", [&](FBtContext&) { return EBtStatus::Success; })).build());
    CHECK(rig.tick() == EBtStatus::Success);
}

// ---------------------------------------------------------------------------------------------------------
// Game rules: scripted situations on the real battlefield, one decision at a time.
// ---------------------------------------------------------------------------------------------------------

/// An empty floor: every unit of the dungeon fixture is dead until a test places it.
struct FArena {
    FFixture fixture;
    entt::registry& registry;
    std::vector<entt::entity> everyone;
    std::uint64_t serial{500};

    FArena() : fixture(7, 1, 0), registry(fixture.registry) {
        registry.ctx().get<game::FGridTerrain>().cells.fill(game::ETile::Floor);
        for (const auto entity : registry.view<game::FHealth>()) {
            everyone.push_back(entity);
            (void)game::ApplyDamageToUnit(registry, entity, 100000);
        }
    }

    [[nodiscard]] entt::entity party(std::string_view id) {
        for (const auto entity : everyone) {
            if (registry.get<game::FUnitData>(entity).definition.id == id) return entity;
        }
        throw std::runtime_error("no party unit " + std::string(id));
    }

    [[nodiscard]] entt::entity enemy(std::size_t index) {
        std::size_t seen = 0;
        for (const auto entity : everyone) {
            if (registry.all_of<game::FEnemy>(entity) && seen++ == index) return entity;
        }
        throw std::runtime_error("not enough enemies");
    }

    entt::entity place(entt::entity unit, game::FGridPosition cell, int hpPercent = 100) {
        registry.remove<game::FDead>(unit);
        auto& health = registry.get<game::FHealth>(unit);
        health.current = std::max(1, health.max * hpPercent / 100);
        registry.get<game::FGridPosition>(unit) = cell;
        refill(unit);
        return unit;
    }

    void refill(entt::entity unit) {
        auto& points = registry.get<game::FActionPoints>(unit);
        points.move = points.moveMax;
        points.skill = points.skillMax;
    }

    void skills(entt::entity unit, std::vector<std::string> ids) {
        auto& loadout = registry.get<game::FSkillLoadout>(unit);
        loadout.slots.clear();
        for (auto& id : ids) loadout.slots.push_back({std::move(id), {}});
    }

    void useProfile(entt::entity unit, std::string_view id) {
        const auto& library = registry.ctx().get<game::ai::FAiLibrary>();
        registry.emplace_or_replace<game::ai::FAiBrain>(unit, game::ai::MakeBrain(*library.find(id)));
    }

    [[nodiscard]] std::optional<game::FBattleAction> decide(entt::entity unit, bool newTurn = true) {
        if (newTurn) ++serial;
        const game::FTurnToken token{1, 1, serial, unit};
        return game::ChooseAutoAction(registry, token, !registry.all_of<game::FPartyMember>(unit), fixture.content());
    }

    /// A whole turn: refill, then ask for actions until the AI has none. Moves are carried out by hand (position
    /// and move AP) and skills spend their AP, the way the resolver would; no damage is dealt, so the picture
    /// the AI sees only changes through its own movement.
    [[nodiscard]] std::vector<game::FBattleAction> playTurn(entt::entity unit) {
        ++serial;
        refill(unit);
        std::vector<game::FBattleAction> actions;
        for (int step = 0; step < 6; ++step) {
            const game::FTurnToken token{1, 1, serial, unit};
            const auto action = game::ChooseAutoAction(registry, token, !registry.all_of<game::FPartyMember>(unit),
                                                       fixture.content());
            if (!action) break;
            auto& points = registry.get<game::FActionPoints>(unit);
            if (const auto* move = std::get_if<game::FMoveRequest>(&*action)) {
                registry.get<game::FGridPosition>(unit) = move->destination;
                --points.move;
            } else {
                const auto& request = std::get<game::FActionRequest>(*action);
                for (const auto& slot : registry.get<game::FSkillLoadout>(unit).slots) {
                    if (slot.skillId == request.skillId) {
                        points.skill -= game::ResolveSkillSlot(slot, fixture.content())->apCost;
                    }
                }
            }
            actions.push_back(*action);
        }
        return actions;
    }

    void trap(int x, int y) { registry.ctx().get<game::FGridTerrain>().cells[static_cast<std::size_t>(y * game::kGridWidth + x)] = game::ETile::Trap; }
    void wall(int x, int y) { registry.ctx().get<game::FGridTerrain>().cells[static_cast<std::size_t>(y * game::kGridWidth + x)] = game::ETile::Wall; }
};

[[nodiscard]] const game::FActionRequest* AsSkill(const std::optional<game::FBattleAction>& action) {
    return action ? std::get_if<game::FActionRequest>(&*action) : nullptr;
}
[[nodiscard]] const game::FMoveRequest* AsMove(const std::optional<game::FBattleAction>& action) {
    return action ? std::get_if<game::FMoveRequest>(&*action) : nullptr;
}

/// The reach the AI's DangerAt treats as threatening: the enemy's move range plus its longest damage-skill
/// range. Built from the same registry/content the AI reads, so the test cannot drift from the rule
/// (FAiQueries.cpp:248-252, FAiWorld.cpp:135).
[[nodiscard]] int ThreatReach(FArena& arena, entt::entity enemy) {
    const auto world = game::ai::BuildAiWorld(arena.registry, enemy, arena.fixture.content());
    const auto* view = world.find(enemy);
    return view == nullptr ? 0 : (view->hasMoveAp ? world.moveRange : 0) + view->damageRange;
}

/// The longest range among the unit's damage skills, as the AI resolves its loadout.
[[nodiscard]] int DamageReach(FArena& arena, entt::entity unit) {
    const auto world = game::ai::BuildAiWorld(arena.registry, unit, arena.fixture.content());
    const auto* view = world.find(unit);
    return view == nullptr ? 0 : view->damageRange;
}

/// The first use of `skillId` in a played turn, or null.
[[nodiscard]] const game::FActionRequest* FindSkill(const std::vector<game::FBattleAction>& turn, std::string_view skillId) {
    for (const auto& action : turn) {
        if (const auto* request = std::get_if<game::FActionRequest>(&action); request && request->skillId == skillId) {
            return request;
        }
    }
    return nullptr;
}

[[nodiscard]] bool UsesAnySkill(const std::vector<game::FBattleAction>& turn) {
    return std::ranges::any_of(turn, [](const auto& action) { return std::holds_alternative<game::FActionRequest>(action); });
}

void RangedUnitStepsOutOfReach() {
    FArena arena;
    const auto archer = arena.place(arena.party("party.archer"), {4, 4});
    const auto imp = arena.place(arena.enemy(0), {4, 2});
    arena.skills(imp, {"skill.strike"});
    const auto threat = ThreatReach(arena, imp);
    const auto archerRange = DamageReach(arena, archer);
    CHECK(threat < archerRange); // the scenario needs a safe cell inside the bow's range
    const auto action = arena.decide(archer);
    const auto* move = AsMove(action);
    CHECK(move != nullptr);
    // The imp's reach (its move range plus its blade's range) must not cover the firing cell, and the bow must
    // still reach it.
    CHECK(game::GridDistance(move->destination, {4, 2}) > threat);
    CHECK(game::GridDistance(move->destination, {4, 2}) <= archerRange);
}

void MeleeUnitClosesIn() {
    FArena arena;
    const auto warrior = arena.place(arena.party("party.warrior"), {1, 6});
    const auto imp = arena.place(arena.enemy(0), {5, 2});
    arena.skills(warrior, {"skill.strike"});
    const auto action = arena.decide(warrior);
    const auto* move = AsMove(action);
    CHECK(move != nullptr);
    CHECK(game::GridDistance(move->destination, {5, 2}) < game::GridDistance({1, 6}, {5, 2}));
    CHECK(imp != entt::null);
}

void StrikeThenStandStill() {
    FArena arena;
    const auto warrior = arena.place(arena.party("party.warrior"), {3, 3});
    const auto imp = arena.place(arena.enemy(0), {3, 2});
    arena.skills(warrior, {"skill.strike"});
    const auto action = arena.decide(warrior);
    const auto* skill = AsSkill(action);
    CHECK(skill != nullptr && skill->skillId == "skill.strike" && skill->target == imp);
    // Out of skill AP and already facing the enemy: no pointless shuffle.
    arena.registry.get<game::FActionPoints>(warrior).skill = 0;
    CHECK(!arena.decide(warrior).has_value());
}

void KillBeatsDamage() {
    FArena arena;
    const auto warrior = arena.place(arena.party("party.warrior"), {3, 3});
    const auto healthy = arena.place(arena.enemy(0), {4, 3});
    const auto weak = arena.place(arena.enemy(1), {2, 3}, 5);
    arena.registry.get<game::FStats>(weak).attack = 12; // a dangerous unit: removing it is worth more than a hit
    arena.skills(weak, {"skill.strike"});
    arena.skills(warrior, {"skill.strike"});
    const auto action = arena.decide(warrior);
    const auto* skill = AsSkill(action);
    if (!(skill != nullptr && skill->target == weak && skill->target != healthy)) {
        const auto& brain = arena.registry.get<game::ai::FAiBrain>(warrior);
        throw std::runtime_error("kill not preferred: " + brain.blackboard.reason + " | weak hp " +
                                 std::to_string(arena.registry.get<game::FHealth>(weak).current));
    }
}

void WoundedTargetIsFocused() {
    FArena arena;
    const auto warrior = arena.place(arena.party("party.warrior"), {3, 3});
    const auto unhurt = arena.place(arena.enemy(0), {4, 3});
    const auto hurt = arena.place(arena.enemy(1), {2, 3}, 60);
    for (const auto enemy : {unhurt, hurt}) arena.registry.get<game::FHealth>(enemy).max = 400; // nothing dies
    arena.registry.get<game::FHealth>(unhurt).current = 400;
    arena.registry.get<game::FHealth>(hurt).current = 240;
    arena.skills(warrior, {"skill.strike"});
    const auto action = arena.decide(warrior);
    CHECK(AsSkill(action) != nullptr && AsSkill(action)->target == hurt);
}

void AreaSkillCatchesTheCluster() {
    FArena arena;
    const auto warrior = arena.place(arena.party("party.warrior"), {3, 3});
    const auto front = arena.place(arena.enemy(0), {2, 3});
    arena.place(arena.enemy(1), {2, 4}); // one step from the target: inside the cleave's spread
    arena.skills(warrior, {"skill.strike", "skill.cleave"});
    const auto action = arena.decide(warrior);
    const auto* skill = AsSkill(action);
    CHECK(skill != nullptr && skill->skillId == "skill.cleave" && skill->target == front);
    // A lone enemy gets the stronger single-target blow instead.
    arena.place(arena.enemy(1), {7, 7});
    arena.registry.get<game::FHealth>(arena.enemy(1)).current = 0; // dead again below
    (void)game::ApplyDamageToUnit(arena.registry, arena.enemy(1), 1000);
    const auto alone = arena.decide(warrior);
    CHECK(AsSkill(alone) != nullptr && AsSkill(alone)->skillId == "skill.strike");
}

void HealIsARescueNotATopUp() {
    FArena arena;
    const auto mage = arena.place(arena.party("party.mage"), {3, 6});
    const auto warrior = arena.place(arena.party("party.warrior"), {3, 4}, 5); // 7 HP left
    const auto attacker = arena.place(arena.enemy(0), {3, 2});
    arena.registry.get<game::FStats>(attacker).attack = 12;
    arena.skills(attacker, {"skill.strike"});
    arena.skills(mage, {"skill.bolt", "skill.heal"});
    const auto rescue = arena.playTurn(mage);
    const auto* heal = FindSkill(rescue, "skill.heal");
    CHECK(heal != nullptr && heal->target == warrior);
    CHECK(FindSkill(rescue, "skill.bolt") == nullptr); // one skill's worth of AP, spent on the ally who would die

    // The same ally, healthy enough to survive the next hit, and an enemy that can be finished: attack instead.
    arena.place(mage, {3, 6});
    arena.place(warrior, {3, 4}, 60);
    arena.place(attacker, {3, 5}, 3);
    const auto strike = arena.playTurn(mage);
    CHECK(FindSkill(strike, "skill.bolt") != nullptr && FindSkill(strike, "skill.bolt")->target == attacker);
    CHECK(FindSkill(strike, "skill.heal") == nullptr);
}

void NoHealOnTheUnhurtAndNoBuffTwice() {
    FArena arena;
    const auto mage = arena.place(arena.party("party.mage"), {3, 6});
    const auto warrior = arena.place(arena.party("party.warrior"), {2, 6});
    arena.place(arena.enemy(0), {7, 0});
    arena.skills(mage, {"skill.heal", "skill.buff"});
    const auto first = arena.decide(mage);
    const auto* buff = AsSkill(first);
    CHECK(buff != nullptr && buff->skillId == "skill.buff" && buff->target == warrior); // heal: nobody is hurt

    const auto& statuses = arena.fixture.content().statuses;
    auto& active = arena.registry.get<game::FActiveStatuses>(warrior);
    game::ApplyStatus(active, *statuses.find("status.buff"));
    const auto second = arena.decide(mage);
    // The only unit that can use it already carries the buff; a mage with just heal and buff has nothing to do.
    CHECK(!second.has_value() || AsMove(second) != nullptr);
}

void SpikesAreCrossedOnlyWhenWorthIt() {
    const game::FGridPosition trapCell{3, 2};
    FArena arena;
    const auto warrior = arena.place(arena.party("party.warrior"), {3, 3});
    const auto enemy = arena.place(arena.enemy(0), {5, 1});
    const auto enemyCell = arena.registry.get<game::FGridPosition>(enemy);
    arena.skills(warrior, {"skill.strike"});
    arena.trap(trapCell.x, trapCell.y); // equally close to the enemy, but spiked
    const auto around = arena.decide(warrior);
    const auto* step = AsMove(around);
    CHECK(step != nullptr);
    CHECK(!(step->destination == trapCell));
    // A safe step equally close to the enemy is the answer: never pay spikes for a position a free cell reaches.
    CHECK(game::GridDistance(step->destination, enemyCell) <= game::GridDistance(trapCell, enemyCell));

    // A corridor whose only way forward is the spike: worth crossing at full health ...
    // The three walls (2,3), (4,3), (3,4) leave the trap cell as the only exit, so any destination that ends at or
    // beyond the trap's distance to the enemy has crossed the trap.
    FArena corridor;
    const auto fighter = corridor.place(corridor.party("party.warrior"), {3, 3});
    const auto corridorEnemy = corridor.place(corridor.enemy(0), {3, 0});
    const auto corridorEnemyCell = corridor.registry.get<game::FGridPosition>(corridorEnemy);
    corridor.skills(fighter, {"skill.strike"});
    corridor.wall(2, 3);
    corridor.wall(4, 3);
    corridor.wall(3, 4);
    corridor.trap(trapCell.x, trapCell.y);
    const auto crossing = corridor.decide(fighter);
    CHECK(AsMove(crossing) != nullptr);
    // The spike is crossed: the destination is on the trap cell or beyond it, closer to the enemy.
    CHECK(game::GridDistance(AsMove(crossing)->destination, corridorEnemyCell) <=
          game::GridDistance(trapCell, corridorEnemyCell));

    // ... and never when the spike would kill.
    const int trapDamage = corridor.registry.ctx().get<game::FGridTerrain>().trapDamage;
    corridor.registry.get<game::FHealth>(fighter).current = trapDamage;
    CHECK(!corridor.decide(fighter).has_value());
}

void StatusesAreValuedByWhatTheyDo() {
    FArena arena;
    const auto mage = arena.place(arena.party("party.mage"), {3, 6});
    const auto warrior = arena.place(arena.party("party.warrior"), {2, 6});
    const auto imp = arena.place(arena.enemy(0), {3, 2});
    arena.skills(imp, {"skill.strike"});
    game::ai::FAiBlackboard blackboard;
    game::ai::FBtMemory memory;
    const game::ai::FAiTuning tuning;
    const auto world = game::ai::BuildAiWorld(arena.registry, mage, arena.fixture.content());
    FBtContext ctx{arena.registry, arena.fixture.content(), tuning, world, blackboard, memory, mage, {}, false};
    const auto value = [&](const char* status, entt::entity target, bool ally) {
        return game::ai::ValueOfStatus(ctx, {status, 0, 0}, *world.find(target), ally);
    };
    CHECK(value("status.debuff", imp, false) == 0); // the rules clamp a negative magnitude to nothing
    CHECK(value("status.burn", imp, false) > 0);
    CHECK(value("status.freeze", imp, false) > 0);
    CHECK(value("status.buff", warrior, true) > 0);
    CHECK(value("status.buff", imp, false) < 0);    // empowering an enemy is a mistake
    CHECK(value("status.burn", warrior, true) < 0); // so is burning a friend

    game::ApplyStatus(arena.registry.get<game::FActiveStatuses>(imp), *arena.fixture.content().statuses.find("status.burn"));
    const auto burning = game::ai::BuildAiWorld(arena.registry, mage, arena.fixture.content());
    FBtContext again{arena.registry, arena.fixture.content(), tuning, burning, blackboard, memory, mage, {}, false};
    CHECK(game::ai::ValueOfStatus(again, {"status.burn", 0, 0}, *burning.find(imp), false) == 0); // nothing to add
}

void RefusedActionsAreNotRepeated() {
    FArena arena;
    const auto warrior = arena.place(arena.party("party.warrior"), {3, 3});
    arena.place(arena.enemy(0), {3, 2});
    arena.skills(warrior, {"skill.strike"});
    const auto first = arena.decide(warrior);
    CHECK(AsSkill(first) != nullptr);
    // Nobody resolved the action (AP unchanged): the same turn must not ask for it again.
    const auto second = arena.decide(warrior, false);
    CHECK(!second.has_value());
    // A new turn forgets the refusal.
    CHECK(AsSkill(arena.decide(warrior)) != nullptr);
}

void NewLoadoutsNeedNoAiWork() {
    FArena arena;
    // A monster given two skills the shipped monsters never had. No profile, no new node: roles do the work.
    const auto warrior = arena.place(arena.party("party.warrior"), {1, 6});
    const auto sniper = arena.place(arena.enemy(0), {6, 1});
    const auto friend_ = arena.place(arena.enemy(1), {7, 1});
    arena.skills(sniper, {"skill.snipe", "skill.heal"});
    const auto closing = arena.playTurn(sniper);
    CHECK(!closing.empty() && std::holds_alternative<game::FMoveRequest>(closing.front())); // walks into firing range

    arena.place(warrior, {2, 2});
    arena.place(sniper, {6, 1});
    const auto shot = arena.playTurn(sniper);
    CHECK(FindSkill(shot, "skill.snipe") != nullptr && FindSkill(shot, "skill.snipe")->target == warrior);

    // With nobody to shoot, the same unit patches up a wounded friend instead.
    arena.place(warrior, {0, 7});
    arena.place(sniper, {6, 1});
    arena.place(friend_, {7, 1}, 30);
    const auto mend = arena.playTurn(sniper);
    CHECK(FindSkill(mend, "skill.heal") != nullptr && FindSkill(mend, "skill.heal")->target == friend_);
}

void BossEnragesOnceBelowHalfHealth() {
    FArena arena;
    const auto hero = arena.place(arena.party("party.warrior"), {3, 3});
    const auto boss = arena.place(arena.enemy(0), {3, 2});
    arena.registry.get<game::FStats>(boss).attack = 20; // hits far harder than a buff is worth
    arena.skills(boss, {"skill.cleave", "skill.strike", "skill.buff"});
    arena.useProfile(boss, "ai.boss");
    arena.registry.get<game::FHealth>(hero).max = arena.registry.get<game::FHealth>(hero).current = 900;

    const auto healthy = arena.playTurn(boss);
    CHECK(UsesAnySkill(healthy) && FindSkill(healthy, "skill.buff") == nullptr);

    arena.place(boss, {3, 2}, 40);
    const auto enraged = arena.playTurn(boss);
    const auto* cry = FindSkill(enraged, "skill.buff");
    CHECK(cry != nullptr && cry->target == boss);

    const auto later = arena.playTurn(boss);
    CHECK(UsesAnySkill(later) && FindSkill(later, "skill.buff") == nullptr); // once per battle

    // A boss without a buff skill simply fights on.
    arena.skills(boss, {"skill.cleave", "skill.strike"});
    arena.useProfile(boss, "ai.boss");
    const auto plain = arena.playTurn(boss);
    CHECK(UsesAnySkill(plain) && FindSkill(plain, "skill.buff") == nullptr);
}

void SignatureMoveFollowsItsCooldown() {
    FArena arena;
    const auto fighter = arena.place(arena.party("party.warrior"), {3, 3});
    const auto victim = arena.place(arena.enemy(0), {3, 2});
    arena.registry.get<game::FHealth>(victim).max = arena.registry.get<game::FHealth>(victim).current = 900;
    arena.skills(fighter, {"skill.strike", "skill.cleave"});

    nlohmann::json document = DocumentWith({
        {"type", "Selector"},
        {"children", nlohmann::json::array({
            {{"type", "Cooldown"}, {"key", "signature"}, {"turns", 3},
             {"children", nlohmann::json::array({{{"type", "UseSkill"}, {"skills", nlohmann::json::array({"skill.cleave"})}}})}},
            {{"type", "UseSkill"}, {"skills", nlohmann::json::array({"skill.strike"})}}})}});
    std::string error;
    const auto library = game::ai::FAiLibrary::fromDocument(document, arena.fixture.content(),
                                                            game::ai::FAiNodeRegistry::builtin(), error);
    CHECK(library.has_value());
    arena.registry.emplace_or_replace<game::ai::FAiBrain>(fighter, game::ai::MakeBrain(library->fallback()));

    std::vector<std::string> used;
    for (int turn = 0; turn < 6; ++turn) {
        arena.refill(fighter);
        const auto action = arena.decide(fighter);
        CHECK(AsSkill(action) != nullptr);
        used.push_back(AsSkill(action)->skillId);
    }
    const std::vector<std::string> expected{"skill.cleave", "skill.strike", "skill.strike",
                                            "skill.cleave", "skill.strike", "skill.strike"};
    CHECK(used == expected);
}

void DecisionsAreDeterministic() {
    const auto describe = [] {
        FArena arena;
        const auto archer = arena.place(arena.party("party.archer"), {4, 4});
        arena.place(arena.enemy(0), {4, 2});
        arena.place(arena.enemy(1), {5, 1});
        arena.place(arena.enemy(2), {2, 1});
        std::string log;
        for (int turn = 0; turn < 4; ++turn) {
            arena.refill(archer);
            const auto action = arena.decide(archer);
            if (const auto* move = AsMove(action)) {
                log += "move " + std::to_string(move->destination.x) + "," + std::to_string(move->destination.y);
                arena.registry.get<game::FGridPosition>(archer) = move->destination;
            } else if (const auto* skill = AsSkill(action)) {
                log += skill->skillId + "@" + std::to_string(skill->cell.x) + "," + std::to_string(skill->cell.y);
            } else {
                log += "none";
            }
            log += ";";
        }
        return log;
    };
    CHECK(describe() == describe());
}

// ---------------------------------------------------------------------------------------------------------
// Whole battles
// ---------------------------------------------------------------------------------------------------------

void AutoBattlesFinishAndAreWon() {
    for (int room = 0; room < 3; ++room) {
        for (std::uint32_t seed = 1; seed <= 4; ++seed) {
            const FBattleResult result = PlayBattle(seed, 1, room, false, false);
            CHECK(result.finished && result.victory);
            CHECK(static_cast<std::uint64_t>(result.rounds) <= game::kMaxBattleRounds);
        }
    }
}

/// The analysis in miniature (opt-in: it is slow in a Debug build). The new enemy AI must not be a regression
/// from the pre-BT AI against a competent party on deep floors, where fights are close.
void EnemyAiIsNotWeakerThanLegacy() {
    FAggregate fresh;
    FAggregate old;
    for (const int room : {0, 2}) {
        for (int depth = 9; depth <= 11; ++depth) {
            for (std::uint32_t seed = 1; seed <= 16; ++seed) {
                fresh.add(PlayBattle(seed, depth, room, false, false));
                old.add(PlayBattle(seed, depth, room, false, true));
            }
        }
    }
    std::cout << "  party wins " << fresh.wins << "/" << fresh.battles << " vs new enemies, " << old.wins << "/"
              << old.battles << " vs legacy enemies\n";
    CHECK(fresh.wins <= old.wins);
    CHECK(fresh.stalls == 0);
}

void RetreatSeeksSafety() {
    FArena arena;
    const auto archer = arena.place(arena.party("party.archer"), {4, 4}, 30);
    const auto imp = arena.place(arena.enemy(0), {4, 2});
    const auto impCell = arena.registry.get<game::FGridPosition>(imp);
    arena.skills(imp, {"skill.strike"});
    const auto threat = ThreatReach(arena, imp);
    std::string error;
    const auto library = game::ai::FAiLibrary::fromDocument(DocumentWith({{"type", "Retreat"}}), arena.fixture.content(),
                                                            game::ai::FAiNodeRegistry::builtin(), error);
    CHECK(library.has_value());
    arena.registry.emplace_or_replace<game::ai::FAiBrain>(archer, game::ai::MakeBrain(library->fallback()));
    // The imp's reach covers the archer's current cell: one step back puts the archer outside it.
    const auto action = arena.decide(archer);
    CHECK(AsMove(action) != nullptr);
    CHECK(game::GridDistance(AsMove(action)->destination, impCell) > threat);
    // Already safe: a cell one beyond the reach gives nothing to gain, so no move.
    const game::FGridPosition safeCell{impCell.x, impCell.y + threat + 1};
    CHECK(safeCell.y < game::kGridHeight);
    CHECK(game::GridDistance(safeCell, impCell) > threat);
    arena.place(archer, safeCell, 30);
    CHECK(!arena.decide(archer).has_value());
}

void ConditionsReadTheBattle() {
    FArena arena;
    const auto warrior = arena.place(arena.party("party.warrior"), {3, 3});
    arena.place(arena.enemy(0), {3, 2});
    arena.skills(warrior, {"skill.strike"});
    const auto holds = [&](nlohmann::json condition, int turn = 1) {
        std::string error;
        const auto library = game::ai::FAiLibrary::fromDocument(DocumentWith(std::move(condition)), arena.fixture.content(),
                                                                game::ai::FAiNodeRegistry::builtin(), error);
        if (!library) throw std::runtime_error(error);
        const auto world = game::ai::BuildAiWorld(arena.registry, warrior, arena.fixture.content());
        game::ai::FAiBlackboard blackboard;
        blackboard.turnsTaken = turn;
        game::ai::FBtMemory memory = library->fallback().tree->makeMemory();
        const game::ai::FAiTuning tuning;
        FBtContext ctx{arena.registry, arena.fixture.content(), tuning, world, blackboard, memory, warrior, {}, false};
        return library->fallback().tree->tick(ctx) == EBtStatus::Success;
    };
    using nlohmann::json;

    CHECK(!holds({{"type", "SelfHpBelow"}, {"pct", 50}}));
    CHECK(holds({{"type", "SelfHpAtLeast"}, {"pct", 50}}));
    arena.place(warrior, {3, 3}, 40);
    CHECK(holds({{"type", "SelfHpBelow"}, {"pct", 50}}));
    CHECK(!holds({{"type", "SelfHpAtLeast"}, {"pct", 50}}));

    CHECK(holds({{"type", "AllyHpBelow"}, {"pct", 50}}));                          // the unit itself counts ...
    CHECK(!holds({{"type", "AllyHpBelow"}, {"pct", 50}, {"include_self", false}})); // ... unless told not to
    CHECK(!holds({{"type", "AlliesAlive"}, {"min", 1}}));                           // alone
    const auto mage = arena.place(arena.party("party.mage"), {3, 6}, 30);
    CHECK(holds({{"type", "AlliesAlive"}, {"min", 1}}));
    CHECK(holds({{"type", "AllyHpBelow"}, {"pct", 50}, {"include_self", false}}));
    CHECK(mage != entt::null);

    CHECK(holds({{"type", "EnemiesAlive"}, {"max", 1}}));
    arena.place(arena.enemy(1), {6, 6});
    CHECK(!holds({{"type", "EnemiesAlive"}, {"max", 1}}) && holds({{"type", "EnemiesAlive"}, {"min", 2}}));

    CHECK(holds({{"type", "EnemyWithin"}, {"distance", 1}}));
    arena.place(arena.enemy(0), {3, 0});
    arena.place(arena.enemy(1), {7, 7});
    CHECK(!holds({{"type", "EnemyWithin"}, {"distance", 2}}) && holds({{"type", "EnemyWithin"}, {"distance", 3}}));

    CHECK(holds({{"type", "CanUseSkill"}, {"role", "damage"}}) == false); // the enemy is out of the blade's reach
    arena.place(arena.enemy(0), {3, 2});
    CHECK(holds({{"type", "CanUseSkill"}, {"role", "damage"}}));
    CHECK(!holds({{"type", "CanUseSkill"}, {"role", "heal"}}));

    CHECK(!holds({{"type", "SelfHasStatus"}, {"status", "status.buff"}}));
    game::ApplyStatus(arena.registry.get<game::FActiveStatuses>(warrior), *arena.fixture.content().statuses.find("status.buff"));
    CHECK(holds({{"type", "SelfHasStatus"}, {"status", "status.buff"}}));

    // Turn counting is the actor's own: every 2 turns starting with the first.
    CHECK(holds({{"type", "TurnIs"}, {"every", 2}}, 1) && !holds({{"type", "TurnIs"}, {"every", 2}}, 2) &&
          holds({{"type", "TurnIs"}, {"every", 2}}, 3));
    CHECK(holds({{"type", "TurnIs"}, {"every", 2}, {"offset", 1}}, 2));
    CHECK(!holds({{"type", "TurnAtLeast"}, {"turn", 3}}, 2) && holds({{"type", "TurnAtLeast"}, {"turn", 3}}, 3));
}

struct FNamedTest {
    const char* name;
    void (*run)();
};

int RunAll(bool includeSlow) {
    std::vector<FNamedTest> tests = {
        {"BT: sequence contract", SequenceContract},
        {"BT: selector contract", SelectorContract},
        {"BT: active selector preempts", ActiveSelectorPreempts},
        {"BT: parallel and monitor", ParallelPolicies},
        {"BT: decorators", DecoratorContracts},
        {"BT: builder", BuilderRejectsMisuse},
        {"BT: shared tree, separate memory", SharedTreeKeepsActorsApart},
        {"data: shipped ai.json", ShippedDataLoads},
        {"data: broken trees are rejected", BrokenDataIsRejected},
        {"data: fragments and tuning compose", FragmentsAndTuningCompose},
        {"data: new node types need no loader change", NewNodeTypesNeedNoLoaderChange},
        {"rules: ranged unit steps out of reach", RangedUnitStepsOutOfReach},
        {"rules: melee unit closes in", MeleeUnitClosesIn},
        {"rules: strike, then stand still", StrikeThenStandStill},
        {"rules: a kill beats damage", KillBeatsDamage},
        {"rules: wounded targets are focused", WoundedTargetIsFocused},
        {"rules: area skill catches the cluster", AreaSkillCatchesTheCluster},
        {"rules: heal is a rescue, not a top-up", HealIsARescueNotATopUp},
        {"rules: no heal on the unhurt, no buff twice", NoHealOnTheUnhurtAndNoBuffTwice},
        {"rules: spikes only when worth it", SpikesAreCrossedOnlyWhenWorthIt},
        {"rules: statuses valued by effect", StatusesAreValuedByWhatTheyDo},
        {"rules: refused actions are not repeated", RefusedActionsAreNotRepeated},
        {"rules: new loadouts need no AI work", NewLoadoutsNeedNoAiWork},
        {"rules: retreat only when it buys safety", RetreatSeeksSafety},
        {"rules: conditions read the battle", ConditionsReadTheBattle},
        {"rules: boss enrages once", BossEnragesOnceBelowHalfHealth},
        {"rules: signature move follows cooldown", SignatureMoveFollowsItsCooldown},
        {"rules: decisions are deterministic", DecisionsAreDeterministic},
        {"battle: auto battles finish and are won", AutoBattlesFinishAndAreWon},
    };
    if (includeSlow) {
        tests.push_back({"battle: enemy AI is not weaker than legacy", EnemyAiIsNotWeakerThanLegacy});
    }
    int failures = 0;
    for (const FNamedTest& test : tests) {
        try {
            test.run();
            std::cout << "[ ok ] " << test.name << "\n";
        } catch (const std::exception& error) {
            ++failures;
            std::cout << "[FAIL] " << test.name << ": " << error.what() << "\n";
        }
    }
    std::cout << (failures == 0 ? "AI behavior tests passed.\n" : "AI behavior tests FAILED.\n");
    return failures == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    try {
        const std::string mode = argc > 1 ? argv[1] : "";
        if (mode == "--report") {
            // --report [seeds] [firstDepth] [lastDepth] [rooms e.g. 02] [new]   ("new" = only the competent party)
            const int seeds = argc > 2 ? std::atoi(argv[2]) : 10;
            const int first = argc > 3 ? std::atoi(argv[3]) : 1;
            const int last = argc > 4 ? std::atoi(argv[4]) : 3;
            std::vector<int> rooms;
            for (const char digit : std::string(argc > 5 ? argv[5] : "012")) rooms.push_back(digit - '0');
            Report(seeds, first, last, rooms, argc > 6 && std::string(argv[6]) == "new");
            return 0;
        }
        if (mode == "--sweep") {
            // --sweep <tuning key> <v1,v2,...> [seeds] [firstDepth] [lastDepth] [rooms e.g. 02]
            if (argc < 4) throw std::runtime_error("usage: --sweep <key> <v1,v2,...> [seeds] [first] [last] [rooms]");
            std::vector<int> values;
            for (std::size_t start = 0; start < std::string(argv[3]).size();) {
                const std::size_t comma = std::string(argv[3]).find(',', start);
                values.push_back(std::atoi(std::string(argv[3]).substr(start, comma - start).c_str()));
                start = comma == std::string::npos ? std::string(argv[3]).size() : comma + 1;
            }
            std::vector<int> rooms;
            for (const char digit : std::string(argc > 7 ? argv[7] : "02")) rooms.push_back(digit - '0');
            Sweep(argv[2], values, argc > 4 ? std::atoi(argv[4]) : 6, argc > 5 ? std::atoi(argv[5]) : 8,
                  argc > 6 ? std::atoi(argv[6]) : 14, rooms);
            return 0;
        }
        if (mode == "--battle") {
            if (argc < 5) throw std::runtime_error("usage: --battle <seed> <depth> <room> [party=legacy] [enemy=legacy]");
            bool legacyParty = false;
            bool legacyEnemy = false;
            int stopRound = 0;
            for (int i = 5; i < argc; ++i) {
                const std::string option = argv[i];
                legacyParty = legacyParty || option == "party=legacy";
                legacyEnemy = legacyEnemy || option == "enemy=legacy";
                if (option.rfind("stop=", 0) == 0) stopRound = std::atoi(option.c_str() + 5);
            }
            PrintBattle(static_cast<std::uint32_t>(std::atoi(argv[2])), std::atoi(argv[3]), std::atoi(argv[4]),
                        legacyParty, legacyEnemy, stopRound);
            return 0;
        }
        return RunAll(mode == "--slow");
    } catch (const std::exception& error) {
        std::cerr << "FAILED: " << error.what() << '\n';
        return 1;
    }
}
