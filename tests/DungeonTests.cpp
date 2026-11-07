#include <chrono>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <memory>
#include <set>
#include <vector>
#include <imgui.h>

#include "core/AssetManager.h"
#include "core/EngineState.h"
#include "core/JsonLoader.h"
#include "core/ResourceManager.h"
#include "ecs/components/FActionPoints.h"
#include "ecs/components/FActiveStatuses.h"
#include "ecs/components/FFaction.h"
#include "ecs/components/FGridPosition.h"
#include "ecs/components/FHealth.h"
#include "ecs/components/FPosition.h"
#include "ecs/systems/ActionResolverSystem.h"
#include "ecs/systems/CombatOutcomeSystem.h"
#include "ecs/systems/EmberCombatHudSystem.h"
#include "ecs/systems/TurnFlowSystem.h"
#include "gameplay/GameplayServices.h"
#include "gameplay/combat/FCombatApply.h"
#include "gameplay/combat/FCombatQuery.h"
#include "gameplay/combat/FAutoAction.h"
#include "gameplay/combat/FSkillResolver.h"
#include "gameplay/dungeon/FDungeonRun.h"
#include "core/AnimationLibrary.h"
#include "gameplay/dungeon/FDungeonSave.h"
#include "gameplay/dungeon/FRoomGenerator.h"
#include "gameplay/factory/FEntityFactory.h"
#include "gameplay/grid/FGridQuery.h"
#include "gameplay/grid/FCameraMath.h"
#include "gameplay/grid/FGridTerrain.h"
#include "presentation/FBattleAnim.h"
#include "presentation/FIsoView.h"
#include "presentation/FUiFrame.h"
#include "presentation/FUiLayout.h"
#include "states/FEmberBattleState.h"
#include "states/FEmberMenuRequest.h"
#include "presentation/FDungeonOverlay.h"
#include "presentation/FDungeonArt.h"
#include "core/InputState.h"

#include "TestSupport.h"

namespace {

void Check(bool condition, const char* expression, int line) {
    if (!condition) throw std::runtime_error(std::string("Line ") + std::to_string(line) + ": " + expression);
}
#define CHECK(expression) Check((expression), #expression, __LINE__)

struct FFixture {
    entt::registry registry;
    game::ecs::TurnSystem turns;
    game::ecs::TurnFlowSystem flow;
    game::ecs::ActionResolverSystem actions;
    game::ecs::CombatOutcomeSystem outcomes;
    FEmberBattleState battle;
    std::filesystem::path checkpoint;
    std::filesystem::path runSave;

    explicit FFixture(std::uint32_t seed = 1234) {
        std::filesystem::create_directories("out");
        checkpoint = std::filesystem::path("out") / ("test-checkpoint-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".json");
        game::FAssetManager::Initialize(registry, TestAssetRoots());
        CHECK(game::gameplay::FGameplayServices::Initialize(registry));
        CHECK(game::InitializeDungeon(registry, checkpoint.string()));
        runSave = registry.ctx().get<game::FDungeonSave>().path;
        turns.onRegister(registry);
        game::StartDungeonRun(registry, seed, true);
        battle.OnEnter(registry);
        CHECK(game::AliveCount(registry, true) == 4);
        const int enemies = game::AliveCount(registry, false);
        CHECK(enemies >= 3 && enemies <= 4);
    }

    ~FFixture() {
        battle.OnExit(registry);
        std::error_code error;
        std::filesystem::remove(checkpoint, error);
        if (error) std::cerr << "Checkpoint cleanup failed: " << error.message() << '\n';
        std::filesystem::remove(runSave, error);
        std::filesystem::remove(runSave.string() + ".tmp", error);
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

    void nextRoom() {
        CHECK(battle.Update(registry, 0) == EStateTransition::ToBattle);
        battle.OnExit(registry);
        battle.OnEnter(registry);
    }
};

struct FTestContent {
    std::filesystem::path root;

    explicit FTestContent(const nlohmann::json& data) {
        root = std::filesystem::path("out") / ("test-content-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(root / "data");
        // The passive tree is shared content; tests keep a copy so growth validation fails for the
        // document under test, not because the temp root lacks the tree.
        std::ifstream treeFile(FixtureDataPath("passive-tree.json"));
        CHECK(treeFile.good());
        std::ofstream treeOut(root / "data" / "passive-tree.json");
        treeOut << nlohmann::json::parse(treeFile).dump();
        treeOut.flush();
        CHECK(treeOut.good());
        write(data);
    }

    void write(const nlohmann::json& data) const {
        std::ofstream file(root / "data" / "dungeon.json");
        file << data.dump();
        file.flush();
        CHECK(file.good());
    }

    ~FTestContent() {
        for (const auto& path : {root / "ui.log", root / "data" / "dungeon.json",
                                 root / "data" / "passive-tree.json", root / "data", root}) {
            std::error_code error;
            std::filesystem::remove(path, error);
            if (error) std::cerr << "Test content cleanup failed: " << error.message() << '\n';
        }
    }
};

void CollisionAndTurns() {
    FFixture fixture;
    auto& registry = fixture.registry;
    const auto token = fixture.token();
    auto& terrain = registry.ctx().get<game::FGridTerrain>();
    terrain.cells.fill(game::ETile::Floor);
    auto& cell = registry.get<game::FGridPosition>(token.actor);
    cell = {1, 1};
    auto& points = registry.get<game::FActionPoints>(token.actor);
    auto& hp = registry.get<game::FHealth>(token.actor);
    const int startingHp = hp.current;
    auto& queue = registry.ctx().get<game::FActionQueue>();

    terrain.cells[1 * game::kGridWidth + 2] = game::ETile::Wall;
    CHECK(!game::HasGridLineOfSight(registry, {1, 1}, {3, 1}));
    queue.pending.push_back(game::FMoveRequest{token.actor, {2, 1}, token});
    fixture.actions.update(registry, 0);
    CHECK(cell.x == 1 && points.move == 1);
    queue.pending.push_back(game::FMoveRequest{token.actor, {-1, 1}, token});
    fixture.actions.update(registry, 0);
    CHECK(points.move == 1);
    auto stale = token;
    ++stale.turnSerial;
    queue.pending.push_back(game::FMoveRequest{token.actor, {1, 2}, stale});
    fixture.actions.update(registry, 0);
    CHECK(points.move == 1 && cell.y == 1);

    terrain.cells[1 * game::kGridWidth + 2] = game::ETile::Trap;
    CHECK(game::HasGridLineOfSight(registry, {1, 1}, {3, 1}));
    queue.pending.push_back(game::FMoveRequest{token.actor, {3, 1}, token});
    fixture.actions.update(registry, 0);
    CHECK(cell.x == 1 && points.move == 1);
    queue.pending.push_back(game::FMoveRequest{token.actor, {2, 1}, token});
    fixture.actions.update(registry, 0);
    CHECK(cell.x == 2 && cell.y == 1 && points.move == 0);
    CHECK(hp.current == startingHp - terrain.trapDamage);
    CHECK(points.skill == points.skillMax);
    queue.pending.push_back(game::FMoveRequest{token.actor, {2, 2}, token});
    fixture.actions.update(registry, 0);
    CHECK(cell.y == 1 && hp.current == startingHp - terrain.trapDamage);

    const auto enemy = game::FindNearestAlive(registry, token.actor, false);
    CHECK(enemy != entt::null);
    const auto enemyCell = registry.get<game::FGridPosition>(enemy);
    points.move = 1;
    queue.pending.push_back(game::FMoveRequest{token.actor, enemyCell, token});
    fixture.actions.update(registry, 0);
    CHECK(points.move == 1);
    CHECK(game::ApplyDamageToUnit(registry, enemy, 10000));
    CHECK(game::FindGridOccupant(registry, enemyCell.x, enemyCell.y) == entt::null);
    CHECK(!game::IsGridCellOccupied(registry, enemyCell.x, enemyCell.y));

    terrain.cells[1 * game::kGridWidth + 3] = game::ETile::Wall;
    CHECK(!game::HasGridLineOfSight(registry, {2, 1}, {3, 2}));
    const auto path = game::FindGridPath(registry, token.actor, {4, 1}, 10);
    CHECK(path.size() >= 4);
    for (const auto step : path) CHECK(terrain.at(step.x, step.y) != game::ETile::Wall);
    const auto world = game::GridCellTopLeftToWorld(registry.ctx().get<game::FGridMetrics>(), cell.x, cell.y);
    const auto position = registry.get<FPosition>(token.actor);
    CHECK(position.x == world.x && position.y == world.y);
}

void ActionValidation() {
    FFixture fixture;
    auto& registry = fixture.registry;
    const auto token = fixture.token();
    auto& terrain = registry.ctx().get<game::FGridTerrain>();
    terrain.cells.fill(game::ETile::Floor);
    registry.get<game::FGridPosition>(token.actor) = {3, 3};
    const auto enemy = game::FindNearestAlive(registry, token.actor, false);
    registry.get<game::FGridPosition>(enemy) = {3, 2};
    auto& points = registry.get<game::FActionPoints>(token.actor);
    auto& hp = registry.get<game::FHealth>(enemy);
    const int before = hp.current;
    auto& queue = registry.ctx().get<game::FActionQueue>();
    auto stale = token;
    ++stale.sessionId;
    queue.pending.push_back(game::FActionRequest{token.actor, enemy, "skill.arrow", stale});
    fixture.actions.update(registry, 0);
    CHECK(hp.current == before && points.skill == points.skillMax);
    terrain.cells[2 * game::kGridWidth + 3] = game::ETile::Wall;
    queue.pending.push_back(game::FActionRequest{token.actor, enemy, "skill.arrow", token});
    fixture.actions.update(registry, 0);
    CHECK(hp.current == before && points.skill == points.skillMax);
    terrain.cells[2 * game::kGridWidth + 3] = game::ETile::Floor;
    queue.pending.push_back(game::FActionRequest{token.actor, enemy, "skill.arrow", token});
    fixture.actions.update(registry, 0);
    CHECK(hp.current < before && points.skill == 0 && points.move == points.moveMax);
    const int damaged = hp.current;
    queue.pending.push_back(game::FActionRequest{token.actor, enemy, "skill.arrow", token});
    fixture.actions.update(registry, 0);
    CHECK(hp.current == damaged);
    fixture.flow.update(registry, 0);
    CHECK(points.skill == 0);
}

void AutoActionTokenBoundary() {
    FFixture fixture;
    auto& registry = fixture.registry;
    const auto token = fixture.token();
    // The two cells are named once: they are used for placement and for the reach precondition, so the check
    // cannot drift from the placement.
    const game::FGridPosition actorCell{3, 3};
    const game::FGridPosition enemyCell{3, 2};
    registry.ctx().get<game::FGridTerrain>().cells.fill(game::ETile::Floor);
    registry.get<game::FGridPosition>(token.actor) = actorCell;
    const auto enemy = game::FindNearestAlive(registry, token.actor, false);
    CHECK(enemy != entt::null);
    registry.get<game::FGridPosition>(enemy) = enemyCell;
    registry.ctx().erase<game::FTurnRuntime>();
    CHECK(!registry.ctx().contains<game::FTurnRuntime>());
    const auto& content = registry.ctx().get<game::gameplay::FContentRegistry>();
    // The boundary under test is that the AI answers without an FTurnRuntime and that the request carries the
    // token/actor. Freeze the move for this first call so repositioning is not an option: the actor must attack
    // the enemy, which is inside its skill reach. The reach is read from the actor's resolved loadout, so a
    // fixture change cannot silently make the scenario unattackable.
    auto& points = registry.get<game::FActionPoints>(token.actor);
    const int savedMoveAp = points.move;
    points.move = 0;
    int reach = 0;
    for (const auto& slot : registry.get<game::FSkillLoadout>(token.actor).slots) {
        if (const auto resolved = game::ResolveSkillSlot(slot, content)) reach = std::max(reach, resolved->range);
    }
    CHECK(game::GridDistance(registry.get<game::FGridPosition>(token.actor),
                             registry.get<game::FGridPosition>(enemy)) <= reach);
    const auto skill = game::ChooseAutoAction(registry, token, false, content);
    CHECK(skill.has_value() && std::holds_alternative<game::FActionRequest>(*skill));
    CHECK(std::get<game::FActionRequest>(*skill).token == token);
    CHECK(std::get<game::FActionRequest>(*skill).actor == token.actor);
    points.move = savedMoveAp;
    points.skill = 0;
    // Without skill AP the AI no longer shuffles next to an enemy it is already facing (a wasted move); it
    // moves when moving gets it somewhere. Put the actor and the enemies in opposite corners, out of every
    // skill's reach, to force that case.
    registry.get<game::FGridPosition>(token.actor) = {0, game::kGridHeight - 1};
    int corner = 0;
    for (const auto entity : registry.view<game::FEnemy, game::FGridPosition>()) {
        registry.get<game::FGridPosition>(entity) = {game::kGridWidth - 1 - corner % 2, corner / 2};
        ++corner;
    }
    const auto move = game::ChooseAutoAction(registry, token, false, content);
    CHECK(move.has_value() && std::holds_alternative<game::FMoveRequest>(*move));
    CHECK(std::get<game::FMoveRequest>(*move).token == token);
    CHECK(std::get<game::FMoveRequest>(*move).actor == token.actor);
    points.move = 0;
    CHECK(!game::ChooseAutoAction(registry, token, false, content));
    auto invalid = token;
    invalid.actor = entt::null;
    CHECK(!game::ChooseAutoAction(registry, invalid, false, content));
}

void SharedCellAndPierceActions() {
    FFixture fixture;
    auto& registry = fixture.registry;
    const auto token = fixture.token();
    auto& terrain = registry.ctx().get<game::FGridTerrain>();
    terrain.cells.fill(game::ETile::Floor);
    registry.get<game::FGridPosition>(token.actor) = {1, 1};
    std::vector<entt::entity> enemies;
    for (const auto entity : registry.view<game::FEnemy, game::FHealth>()) enemies.push_back(entity);
    CHECK(enemies.size() >= 2);
    for (std::size_t index = 0; index < enemies.size(); ++index) {
        registry.get<game::FGridPosition>(enemies[index]) = {6, 2 + static_cast<int>(index)};
    }
    const auto target = enemies[0];
    const auto behind = enemies[1];
    registry.get<game::FGridPosition>(target) = {2, 1};
    registry.get<game::FGridPosition>(behind) = {3, 1};
    auto& loadout = registry.get<game::FSkillLoadout>(token.actor);
    loadout.slots.push_back({"skill.lunge", {}});
    loadout.slots.push_back({"skill.fog", {}});
    auto& points = registry.get<game::FActionPoints>(token.actor);
    auto& queue = registry.ctx().get<game::FActionQueue>();
    const int targetHp = registry.get<game::FHealth>(target).current;
    const int behindHp = registry.get<game::FHealth>(behind).current;
    terrain.cells[1 * game::kGridWidth + 3] = game::ETile::Wall;
    queue.pending.push_back(game::FActionRequest{token.actor, target, "skill.lunge", token});
    fixture.actions.update(registry, 0);
    CHECK(registry.get<game::FHealth>(target).current < targetHp);
    CHECK(registry.get<game::FHealth>(behind).current == behindHp);
    terrain.cells[1 * game::kGridWidth + 3] = game::ETile::Floor;
    points.skill = points.skillMax;
    queue.pending.push_back(game::FActionRequest{token.actor, target, "skill.lunge", token});
    fixture.actions.update(registry, 0);
    CHECK(registry.get<game::FHealth>(behind).current < behindHp);

    registry.get<game::FGridPosition>(target) = {4, 1};
    registry.get<game::FGridPosition>(behind) = {5, 1};
    points.skill = points.skillMax;
    const auto effectsBefore = registry.get<game::FActiveStatuses>(target).effects.size();
    terrain.cells[1 * game::kGridWidth + 2] = game::ETile::Wall;
    queue.pending.push_back(game::FActionRequest{token.actor, entt::null, "skill.fog", token, {3, 1}});
    fixture.actions.update(registry, 0);
    CHECK(points.skill == points.skillMax);
    CHECK(registry.get<game::FActiveStatuses>(target).effects.size() == effectsBefore);
    terrain.cells[1 * game::kGridWidth + 2] = game::ETile::Floor;
    queue.pending.push_back(game::FActionRequest{token.actor, entt::null, "skill.fog", token, {3, 1}});
    fixture.actions.update(registry, 0);
    CHECK(points.skill < points.skillMax);
    CHECK(registry.get<game::FActiveStatuses>(target).effects.size() > effectsBefore);
    points.skill = points.skillMax;
    queue.pending.push_back(game::FActionRequest{token.actor, entt::null, "skill.fog", token, {-1, 1}});
    fixture.actions.update(registry, 0);
    CHECK(points.skill == points.skillMax);

    auto& window = registry.ctx().emplace<game::FEngineWindowState>();
    window.width = 1280;
    window.height = 800;
    auto& input = registry.ctx().emplace<game::FInputState>();
    CHECK(game::ui::InitializeUiFrame(registry));
    const auto isoView = game::ui::FindBattleIsoView(registry, registry.ctx().get<game::FGridMetrics>());
    CHECK(isoView.has_value());
    input.mousePos = isoView->cellCenter(3, 1);
    input.mouseLeftPressed = true;
    game::ecs::EmberCombatHudSystem hud;
    hud.boundTurn = token.turnSerial;
    hud.selectedSkill = static_cast<int>(loadout.slots.size()) - 1;
    hud.update(registry, 0);
    CHECK(queue.pending.size() == 1);
    const auto* cellAction = std::get_if<game::FActionRequest>(&queue.pending.front());
    CHECK(cellAction && cellAction->skillId == "skill.fog" && cellAction->target == entt::null);
    CHECK(cellAction->cell.x == 3 && cellAction->cell.y == 1 && cellAction->token == token);
    fixture.actions.update(registry, 0);
    CHECK(points.skill < points.skillMax);

    entt::entity ally = entt::null;
    for (const auto entity : registry.view<game::FPartyMember>()) {
        if (entity != token.actor) { ally = entity; break; }
    }
    CHECK(ally != entt::null);
    registry.get<game::FGridPosition>(ally) = {1, 2};
    CHECK(game::FindGridOccupant(registry, 1, 2) == ally);
    auto& allyHealth = registry.get<game::FHealth>(ally);
    allyHealth.current -= 10;
    const int allyHp = allyHealth.current;
    loadout.slots.push_back({"skill.buff", {}});
    points.skill = points.skillMax;
    queue.pending.push_back(game::FActionRequest{token.actor, ally, "skill.buff", token});
    fixture.actions.update(registry, 0);
    CHECK(allyHealth.current == allyHp);
    CHECK(!registry.get<game::FActiveStatuses>(ally).effects.empty());
}

void CampRuneTransactions() {
    FFixture fixture;
    auto& registry = fixture.registry;
    game::CaptureRunParty(registry);
    auto& run = registry.ctx().get<game::FDungeonRun>();
    run.phase = game::EDungeonPhase::Camp;
    auto& slot = run.party[0].skills[0];
    CHECK(slot.skillId == "skill.strike");
    slot.runeIds.clear();
    run.runes = {"rune.fire", "rune.ember", "rune.fire", "rune.multishot",
                 "rune.power", "rune.swift", "rune.unknown"};
    const auto state = [&] {
        nlohmann::json result{{"runes", run.runes}, {"phase", static_cast<int>(run.phase)},
                              {"party", nlohmann::json::array()}};
        for (const auto& member : run.party) {
            nlohmann::json skills = nlohmann::json::array();
            for (const auto& skill : member.skills) {
                skills.push_back({{"id", skill.skillId}, {"runes", skill.runeIds}});
            }
            result["party"].push_back(std::move(skills));
        }
        return result;
    };
    const auto allRunes = [&] {
        auto result = run.runes;
        for (const auto& member : run.party) {
            for (const auto& skill : member.skills) {
                result.insert(result.end(), skill.runeIds.begin(), skill.runeIds.end());
            }
        }
        std::ranges::sort(result);
        return result;
    };
    const auto original = allRunes();
    const auto reject = [&](auto operation) {
        const auto before = state();
        CHECK(operation().has_value());
        CHECK(state() == before);
        CHECK(allRunes() == original);
    };
    const auto runeIndex = [&](const std::string& id) {
        const auto found = std::ranges::find(run.runes, id);
        CHECK(found != run.runes.end());
        return static_cast<std::size_t>(found - run.runes.begin());
    };
    const auto beforePreview = state();
    CHECK(!game::ValidateDungeonRuneEquip(registry, 0, 0, 0));
    CHECK(state() == beforePreview);
    reject([&] { return game::EquipDungeonRune(registry, run.party.size(), 0, 0); });
    reject([&] { return game::EquipDungeonRune(registry, 0, run.party[0].skills.size(), 0); });
    reject([&] { return game::EquipDungeonRune(registry, 0, 0, run.runes.size()); });
    CHECK(!game::EquipDungeonRune(registry, 0, 0, runeIndex("rune.fire")));
    CHECK(slot.runeIds == std::vector<std::string>{"rune.fire"});
    CHECK(allRunes() == original);
    reject([&] { return game::EquipDungeonRune(registry, 0, 0, runeIndex("rune.fire")); });
    reject([&] { return game::EquipDungeonRune(registry, 0, 0, runeIndex("rune.multishot")); });
    reject([&] { return game::EquipDungeonRune(registry, 0, 0, runeIndex("rune.unknown")); });
    run.phase = game::EDungeonPhase::Battle;
    reject([&] { return game::EquipDungeonRune(registry, 0, 0, runeIndex("rune.power")); });
    reject([&] { return game::RemoveDungeonRune(registry, 0, 0, 0); });
    run.phase = game::EDungeonPhase::Camp;
    CHECK(!game::EquipDungeonRune(registry, 0, 0, runeIndex("rune.ember")));
    reject([&] { return game::RemoveDungeonRune(registry, 0, 0, 0); });
    CHECK(!game::EquipDungeonRune(registry, 0, 0, runeIndex("rune.power")));
    CHECK(slot.runeIds.size() == 3);
    reject([&] { return game::EquipDungeonRune(registry, 0, 0, runeIndex("rune.swift")); });
    reject([&] { return game::RemoveDungeonRune(registry, run.party.size(), 0, 0); });
    reject([&] { return game::RemoveDungeonRune(registry, 0, run.party[0].skills.size(), 0); });
    reject([&] { return game::RemoveDungeonRune(registry, 0, 0, slot.runeIds.size()); });
    CHECK(!game::RemoveDungeonRune(registry, 0, 0, 2));
    CHECK(!game::RemoveDungeonRune(registry, 0, 0, 1));
    CHECK(!game::RemoveDungeonRune(registry, 0, 0, 0));
    CHECK(slot.runeIds.empty() && allRunes() == original);
    entt::registry missing;
    CHECK(game::ValidateDungeonRuneEquip(missing, 0, 0, 0).has_value());
    CHECK(game::EquipDungeonRune(missing, 0, 0, 0).has_value());
    CHECK(game::RemoveDungeonRune(missing, 0, 0, 0).has_value());
}

void BalanceDrivenChoiceUi() {
    std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> surface(
        SDL_CreateSurface(1280, 800, SDL_PIXELFORMAT_RGBA8888), &SDL_DestroySurface);
    CHECK(surface != nullptr);
    FRendererPtr renderer{SDL_CreateSoftwareRenderer(surface.get())};
    CHECK(renderer != nullptr);
    FFixture fixture;
    auto& registry = fixture.registry;
    std::ifstream source(FixtureDataPath("dungeon.json"));
    auto data = nlohmann::json::parse(source);
    for (const auto& [key, value] : std::vector<std::pair<const char*, int>>{
             {"event_heal", 42}, {"event_materials", 6}, {"camp_heal", 61}}) {
        data["balance"][key] = value;
    }
    data["stat_upgrades"][0]["cost"] = 5;  // 재강화 라벨이 데이터를 따라가는지 확인
    FTestContent content(data);
    registry.ctx().erase<game::FAssetManager>();
    registry.ctx().emplace<game::FAssetManager>(std::vector<std::string>{content.root.string(), kTestFixtureRoot, kShipAssetRoot});
    CHECK(game::InitializeDungeon(registry, fixture.checkpoint.string()));
    registry.ctx().emplace<game::FEngineWindowState>().width = 1280;
    registry.ctx().get<game::FEngineWindowState>().height = 800;
    registry.ctx().emplace<game::FResourceManager>().init(renderer.get());
    CHECK(game::InitializeDungeonArt(registry));
    std::unique_ptr<ImGuiContext, decltype(&ImGui::DestroyContext)> context(
        ImGui::CreateContext(), &ImGui::DestroyContext);
    auto& io = ImGui::GetIO();
    io.DisplaySize = {1280, 800};
    io.DeltaTime = 1.f / 60.f;
    io.Fonts->Build();
    game::InitializeDungeonUi(registry);
    const game::Time time;
    const game::SystemManager systems;
    auto& run = registry.ctx().get<game::FDungeonRun>();
    std::string log;
    for (const auto phase : {game::EDungeonPhase::Reward, game::EDungeonPhase::Event, game::EDungeonPhase::Camp}) {
        run.phase = phase;
        for (int frame = 0; frame < 3; ++frame) {
            ImGui::NewFrame();
            if (frame == 2) ImGui::LogToFile(0, (content.root / "ui.log").string().c_str());
            game::RenderDungeonOverlay(registry, time, systems);
            if (frame == 2) ImGui::LogFinish();
            ImGui::Render();
        }
        std::ifstream capture(content.root / "ui.log");
        CHECK(capture.good());
        log.append(std::istreambuf_iterator<char>(capture), std::istreambuf_iterator<char>());
    }
    for (const char* label : {"ROOM SECURED", "Level 1   EXP 0 / 25   Node points 0",
             "Rest at the shrine  /  heal 42 HP", "Open the cache  /  gain 6 materials",
             "Healed up to 61 HP. Allocate nodes, reinforce stats and fit runes for free before descending.",
             "Attack +1  (5 materials, 0/6)"}) {
        CHECK(log.find(label) != std::string::npos);
    }
    run.phase = game::EDungeonPhase::Reward;
    CHECK(game::UpgradeDungeonStat(registry, 0, 0).has_value());  // 강화는 캠프 전용
    game::ContinueDungeonReward(registry);
    CHECK(run.phase == game::EDungeonPhase::Event);
    game::ChooseDungeonEvent(registry, false);
    CHECK(run.materials == 6);
    run.phase = game::EDungeonPhase::Camp;
    CHECK(!game::UpgradeDungeonStat(registry, 0, 0).has_value());
    CHECK(run.materials == 1 && run.party[0].bonusAttack == 1);
    game::LeaveDungeonCamp(registry);
    CHECK(run.depth == 2 && run.room == 0);
}

void CompleteRun(std::uint32_t seed) {
    FFixture fixture(seed);
    auto& registry = fixture.registry;
    std::string firstRoom = registry.ctx().get<game::FDungeonRun>().roomName;
    for (int room = 0; room < 3; ++room) {
        registry.ctx().get<game::FCombatRuntime>().autoPlay = true;
        int steps = 0;
        while (!registry.ctx().get<game::FBattleOutcome>().resolved && steps++ < 5000) fixture.step();
        CHECK(steps < 5000);
        CHECK(registry.ctx().get<game::FBattleOutcome>().victory);
        CHECK(fixture.battle.Update(registry, 0) == EStateTransition::None);
        auto& run = registry.ctx().get<game::FDungeonRun>();
        CHECK(run.phase == game::EDungeonPhase::Reward);
        CHECK(run.runes.size() == static_cast<std::size_t>(room + 1));
        // 정본 §6: 캐릭터별 EXP가 임계를 넘으면 레벨업 + 노드 포인트 1.
        for (const auto& member : run.party) {
            CHECK(member.level == (room == 2 ? 3 : 2));
            CHECK(member.experience == (room == 0 ? 0 : room == 1 ? 25 : 20));
        }
        if (room == 0) {
            CHECK(run.party.front().passivePoints == 1);
            CHECK(game::AllocateDungeonNode(registry, 0, "node.might"));
            CHECK(run.party.front().passivePoints == 0 && run.party.front().allocatedNodes.size() == 1);
            CHECK(!game::AllocateDungeonNode(registry, 0, "node.vitality"));  // 포인트 소진
        } else if (room == 2) {
            CHECK(run.party.front().passivePoints == 1);
        } else {
            CHECK(run.party.front().passivePoints == 0);
        }
        game::ContinueDungeonReward(registry);
        if (room == 0) {
            CHECK(run.phase == game::EDungeonPhase::Event);
            game::ChooseDungeonEvent(registry, false);
            CHECK(run.materials == 4);
        }
        if (room < 2) fixture.nextRoom();
    }
    auto& run = registry.ctx().get<game::FDungeonRun>();
    CHECK(run.phase == game::EDungeonPhase::Camp && run.cleared == 1);
    CHECK(std::filesystem::exists(fixture.checkpoint));
    std::ifstream file(fixture.checkpoint);
    const auto saved = nlohmann::json::parse(file);
    CHECK(saved.at("cleared") == 1);
    CHECK(run.party.front().skills.size() ==
        registry.ctx().get<game::gameplay::FContentRegistry>().units.find("char.warrior")->skillIds.size());
    auto candidate = run.party.front().skills.front();
    candidate.runeIds.push_back("rune.fire");
    CHECK(!game::ValidateSkillSlot(candidate, registry.ctx().get<game::gameplay::FContentRegistry>()));
    candidate.runeIds.push_back("rune.fire");
    CHECK(game::ValidateSkillSlot(candidate, registry.ctx().get<game::gameplay::FContentRegistry>()).has_value());
    // 캠프: 물질로 스탯을 강화하고 다음 던전으로 내려간다.
    const auto& growth = registry.ctx().get<game::FDungeonGrowth>();
    const auto upgrade = std::ranges::find(growth.statUpgrades, "upgrade.attack", &game::FStatUpgrade::id);
    CHECK(upgrade != growth.statUpgrades.end());
    const int materials = run.materials;
    CHECK(!game::UpgradeDungeonStat(registry, 0, static_cast<std::size_t>(upgrade - growth.statUpgrades.begin())).has_value());
    CHECK(run.materials == materials - upgrade->cost && run.party.front().bonusAttack == 1);
    game::LeaveDungeonCamp(registry);
    CHECK(run.depth == 2 && run.room == 0);
    fixture.nextRoom();
    const auto* warriorUnit = registry.ctx().get<game::gameplay::FContentRegistry>().units.find("char.warrior");
    CHECK(warriorUnit != nullptr);
    entt::entity warriorEntity = entt::null;
    for (const auto entity : registry.view<game::FPartyMember, game::FUnitData>()) {
        if (registry.get<game::FUnitData>(entity).definition.id == "party.warrior") warriorEntity = entity;
    }
    CHECK(warriorEntity != entt::null);
    // 캐릭터별 성장이 다음 전투 셋업에 합산된다: 기본 + 물질 강화(+1) + node.might(+1).
    CHECK(registry.get<game::FStats>(warriorEntity).attack == warriorUnit->attack + 2);
    game::StartDungeonRun(registry, 999);
    const auto& restarted = registry.ctx().get<game::FDungeonRun>();
    CHECK(restarted.depth == 2 && restarted.room == 0);
    CHECK(restarted.materials == 0 && restarted.runes.empty());
    CHECK(restarted.party.front().level == 1 && restarted.party.front().passivePoints == 0
          && restarted.party.front().allocatedNodes.empty() && restarted.party.front().bonusAttack == 0);
    std::cout << "  Seed " << seed << ": " << firstRoom << " -> full dungeon + checkpoint + next depth\n";
}

void PerCharacterRunGrowth() {
    FFixture fixture;
    auto& registry = fixture.registry;
    auto& run = registry.ctx().get<game::FDungeonRun>();
    const auto& growth = registry.ctx().get<game::FDungeonGrowth>();
    CHECK(!growth.passiveTree.nodes.empty() && !growth.statUpgrades.empty());
    CHECK(growth.ExperienceForNextLevel(1) == 25 && growth.ExperienceForNextLevel(2) == 30);

    // 전투 중에는 노드 할당이 잠긴다.
    CHECK(run.phase == game::EDungeonPhase::Battle);
    CHECK(!game::CanAllocateDungeonNode(registry, 0, "node.vitality"));
    CHECK(!game::AllocateDungeonNode(registry, 0, "node.vitality"));

    // 방 클리어: 캐릭터별 EXP → 레벨 → 포인트(정본 §6).
    game::CompleteDungeonBattle(registry, true);
    CHECK(run.phase == game::EDungeonPhase::Reward);
    for (const auto& member : run.party) {
        CHECK(member.level == 2 && member.experience == 0 && member.passivePoints == 1);
    }

    // 인접 규칙: 루트 자체는 할당 불가, 부모가 없으면 잠김, 포인트가 있어야 할당.
    CHECK(!game::CanAllocateDungeonNode(registry, 0, "node.origin"));
    CHECK(!game::CanAllocateDungeonNode(registry, 0, "node.swiftstep"));
    CHECK(game::AllocateDungeonNode(registry, 0, "node.might"));
    CHECK(!game::CanAllocateDungeonNode(registry, 0, "node.guard"));  // 포인트 소진
    CHECK(!game::CanAllocateDungeonNode(registry, 1, "node.endurance"));  // 부모 insight 미할당
    CHECK(game::AllocateDungeonNode(registry, 1, "node.insight"));
    CHECK(run.party[1].allocatedNodes == std::vector<std::string>{"node.insight"});

    // 다음 전투 셋업에서 노드 보너스가 스탯에 합산된다(파이프라인 2단계).
    const auto warriorMember = std::ranges::find(run.party, "party.warrior", &game::FRunMember::id);
    CHECK(warriorMember != run.party.end());
    warriorMember->health = -1;  // 다음 전투는 최대 HP로 시작
    entt::entity warrior = entt::null;
    entt::entity archer = entt::null;
    for (const auto entity : registry.view<game::FPartyMember, game::FUnitData>()) {
        const auto& id = registry.get<game::FUnitData>(entity).definition.id;
        if (id == "party.warrior") warrior = entity;
        if (id == "party.archer") archer = entity;
    }
    CHECK(warrior != entt::null && archer != entt::null);
    const int warriorAttack = registry.get<game::FStats>(warrior).attack;
    const int archerMagic = registry.get<game::FStats>(archer).magicAttack;
    game::ApplyRunParty(registry);
    CHECK(registry.get<game::FStats>(warrior).attack == warriorAttack + 1);
    CHECK(registry.get<game::FStats>(archer).magicAttack == archerMagic + 1);
    CHECK(registry.get<game::FHealth>(warrior).current == registry.get<game::FHealth>(warrior).max);

    // 물질 강화: 캠프에서만, 비용·상한을 지킨다(정본 §5).
    const auto upgradeIndex = [&](const std::string& id) {
        const auto found = std::ranges::find(growth.statUpgrades, id, &game::FStatUpgrade::id);
        CHECK(found != growth.statUpgrades.end());
        return static_cast<std::size_t>(found - growth.statUpgrades.begin());
    };
    const std::size_t attackUpgrade = upgradeIndex("upgrade.attack");
    const std::size_t speedUpgrade = upgradeIndex("upgrade.speed");
    run.materials = 5;
    CHECK(game::ValidateDungeonStatUpgrade(registry, 0, attackUpgrade).has_value());  // 캠프 전용
    run.phase = game::EDungeonPhase::Camp;
    CHECK(!game::UpgradeDungeonStat(registry, 0, attackUpgrade).has_value());
    CHECK(run.party[0].bonusAttack == 1 && run.materials == 3);
    run.materials = 1;
    CHECK(game::UpgradeDungeonStat(registry, 0, attackUpgrade).has_value());  // 비용 부족
    CHECK(run.party[0].bonusAttack == 1 && run.materials == 1);
    run.materials = 100;
    run.party[0].bonusAttack = growth.statUpgrades[attackUpgrade].cap;
    CHECK(game::UpgradeDungeonStat(registry, 0, attackUpgrade).has_value());  // 상한
    CHECK(run.party[0].bonusAttack == growth.statUpgrades[attackUpgrade].cap && run.materials == 100);
    CHECK(!game::UpgradeDungeonStat(registry, 0, speedUpgrade).has_value());
    CHECK(run.party[0].bonusSpeed == 1 && run.materials == 96);

    // 다음 런은 0부터: 성장은 런 스코프다.
    game::StartDungeonRun(registry, 77);
    const auto& fresh = registry.ctx().get<game::FDungeonRun>();
    CHECK(fresh.party[0].level == 1 && fresh.party[0].passivePoints == 0
          && fresh.party[0].allocatedNodes.empty());
    CHECK(fresh.party[0].bonusAttack == 0 && fresh.party[0].bonusSpeed == 0 && fresh.materials == 0);
}

void DefeatAndFactory() {
    FFixture fixture;
    auto& registry = fixture.registry;
    for (const auto entity : registry.view<game::FPartyMember, game::FHealth>()) {
        CHECK(game::ApplyDamageToUnit(registry, entity, 10000));
    }
    fixture.outcomes.update(registry, 0);
    CHECK(registry.ctx().get<game::FBattleOutcome>().resolved);
    CHECK(!registry.ctx().get<game::FBattleOutcome>().victory);
    fixture.battle.Update(registry, 0);
    CHECK(registry.ctx().get<game::FDungeonRun>().phase == game::EDungeonPhase::Defeat);
    const auto checkpoint = registry.ctx().get<game::FDungeonCheckpoint>().cleared;
    game::StartDungeonRun(registry, 7);
    CHECK(registry.ctx().get<game::FDungeonRun>().party.front().bonusHealth == 0);
    CHECK(registry.ctx().get<game::FDungeonRun>().party.front().level == 1);
    CHECK(registry.ctx().get<game::FDungeonCheckpoint>().cleared == checkpoint);
    auto document = game::BuildDungeonBattlefield(registry);
    entt::registry probe;
    game::FScene scene(probe, "invalid");
    scene.Load();
    // Generated rooms keep their edge open, so the wall under the unit has to be placed by hand.
    auto topRow = document["terrain"][0].get<std::string>();
    topRow[0] = '#';
    document["terrain"][0] = topRow;
    document["units"][0]["grid_x"] = 0;
    document["units"][0]["grid_y"] = 0;
    CHECK(!game::FEntityFactory::CreateBattlefield(scene, document));
    document["units"][0]["grid_x"] = game::kGridWidth;
    CHECK(!game::FEntityFactory::CreateBattlefield(scene, document));
    document["terrain"] = nlohmann::json::array({"bad"});
    CHECK(!game::FEntityFactory::CreateBattlefield(scene, document));
}

nlohmann::json ReadJson(const std::filesystem::path& path) {
    std::ifstream file(path);
    CHECK(file.good());
    return nlohmann::json::parse(file);
}

bool SameMember(const game::FRunMember& a, const game::FRunMember& b) {
    const auto sameSlot = [](const game::FSkillSlot& x, const game::FSkillSlot& y) {
        return x.skillId == y.skillId && x.runeIds == y.runeIds;
    };
    return a.id == b.id && a.displayName == b.displayName && a.health == b.health &&
           a.bonusHealth == b.bonusHealth && a.bonusAttack == b.bonusAttack && a.bonusMagic == b.bonusMagic &&
           a.bonusDefense == b.bonusDefense && a.bonusMagicDefense == b.bonusMagicDefense &&
           a.bonusSpeed == b.bonusSpeed && a.experience == b.experience && a.level == b.level &&
           a.passivePoints == b.passivePoints && a.allocatedNodes == b.allocatedNodes &&
           std::ranges::equal(a.skills, b.skills, sameSlot);
}

void SaveAndContinue() {
    FFixture fixture;
    auto& registry = fixture.registry;
    const auto saved = [&]() -> const game::FDungeonSave& { return registry.ctx().get<game::FDungeonSave>(); };

    // A run is on disk from its first moment, and every step afterwards rewrites it.
    CHECK(saved().exists && saved().depth == 1 && saved().room == 0 && saved().phase == game::EDungeonPhase::Battle);
    CHECK(ReadJson(saved().path)["run"]["phase"] == "battle");
    game::CompleteDungeonBattle(registry, true);
    CHECK(saved().phase == game::EDungeonPhase::Reward && ReadJson(saved().path)["run"]["phase"] == "reward");

    const auto& growth = registry.ctx().get<game::FDungeonGrowth>();
    const auto node = std::ranges::find_if(growth.passiveTree.nodes, [&](const game::FPassiveNode& candidate) {
        return candidate.parentId == growth.passiveTree.rootNodeId;
    });
    CHECK(node != growth.passiveTree.nodes.end());
    CHECK(game::AllocateDungeonNode(registry, 0, node->id));
    CHECK(ReadJson(saved().path)["run"]["party"][0]["allocated_nodes"].size() == 1);
    game::ContinueDungeonReward(registry);
    CHECK(saved().phase == game::EDungeonPhase::Event);
    game::ChooseDungeonEvent(registry, false);
    CHECK(saved().phase == game::EDungeonPhase::Battle && saved().room == 1);

    // Continue brings back exactly what was saved, with the one-shot request flags cleared.
    const game::FDungeonRun before = registry.ctx().get<game::FDungeonRun>();
    CHECK(before.party[0].allocatedNodes.size() == 1 && before.materials > 0 && !before.runes.empty());
    registry.ctx().erase<game::FDungeonRun>();
    CHECK(game::ContinueDungeonRun(registry));
    const auto& after = registry.ctx().get<game::FDungeonRun>();
    CHECK(after.seed == before.seed && after.depth == before.depth && after.room == before.room);
    CHECK(after.cleared == before.cleared && after.materials == before.materials && after.phase == before.phase);
    CHECK(after.runes == before.runes && after.roomName == before.roomName && after.message == before.message);
    CHECK(after.party.size() == before.party.size());
    for (std::size_t index = 0; index < before.party.size(); ++index) {
        CHECK(SameMember(after.party[index], before.party[index]));
    }
    CHECK(!after.advanceRequested && !after.abandonRequested && !after.leaveRequested);

    // A fresh boot finds the save and tells the menu what is in it.
    {
        entt::registry reboot;
        game::FAssetManager::Initialize(reboot, TestAssetRoots());
        CHECK(game::gameplay::FGameplayServices::Initialize(reboot));
        CHECK(game::InitializeDungeon(reboot, fixture.checkpoint.string()));
        const auto& found = reboot.ctx().get<game::FDungeonSave>();
        CHECK(found.exists && found.room == 1 && found.phase == game::EDungeonPhase::Battle && !found.savedAt.empty());
    }

    // Anything that does not agree with the shipped content is refused whole, and the live run is left alone.
    const auto seed = registry.ctx().get<game::FDungeonRun>().seed;
    const auto valid = game::DungeonRunToJson(registry.ctx().get<game::FDungeonRun>());
    const auto rejects = [&](const char* label, const auto& edit) {
        auto document = valid;
        edit(document);
        std::string reason;
        CHECK(game::WriteSaveFile(saved().path, document, reason));
        Check(!game::ContinueDungeonRun(registry), label, __LINE__);
        Check(!saved().exists && !saved().error.empty(), label, __LINE__);
        CHECK(registry.ctx().get<game::FDungeonRun>().seed == seed);
    };
    rejects("newer schema version", [](auto& d) { d["schema_version"] = 99; });
    rejects("lost run phase", [](auto& d) { d["run"]["phase"] = "defeat"; });
    rejects("unknown phase", [](auto& d) { d["run"]["phase"] = "somewhere"; });
    rejects("depth out of range", [](auto& d) { d["run"]["depth"] = 0; });
    rejects("cleared count off the depth", [](auto& d) { d["run"]["cleared"] = 7; });
    rejects("unknown collected rune", [](auto& d) { d["run"]["runes"].push_back("rune.nonexistent"); });
    rejects("unknown party member", [](auto& d) { d["run"]["party"][0]["id"] = "party.nobody"; });
    rejects("repeated party member", [](auto& d) { d["run"]["party"][1]["id"] = d["run"]["party"][0]["id"]; });
    rejects("missing party member", [](auto& d) { d["run"]["party"].erase(3); });
    rejects("unknown passive node", [](auto& d) {
        d["run"]["party"][0]["allocated_nodes"] = nlohmann::json::array({"node.nonexistent"});
    });
    rejects("unknown skill", [](auto& d) {
        d["run"]["party"][0]["skills"] = nlohmann::json::array(
            {{{"skill", "skill.nonexistent"}, {"runes", nlohmann::json::array()}}});
    });
    rejects("wrong field type", [](auto& d) { d["run"]["party"][0]["level"] = "ten"; });
    {
        std::ofstream garbage(saved().path, std::ios::trunc);
        garbage << "{ this is not json";
    }
    CHECK(!game::ContinueDungeonRun(registry) && !saved().exists);
    CHECK(game::SaveDungeonRun(registry) && saved().exists && game::ContinueDungeonRun(registry));

    // Leaving keeps the save; abandoning or losing deletes it.
    {
        FFixture leave;
        leave.registry.ctx().get<game::FDungeonRun>().leaveRequested = true;
        CHECK(leave.battle.Update(leave.registry, 0) == EStateTransition::ToHub);
        CHECK(!leave.registry.ctx().contains<game::FDungeonRun>());
        CHECK(leave.registry.ctx().get<game::FDungeonSave>().exists && std::filesystem::exists(leave.runSave));
        CHECK(game::ContinueDungeonRun(leave.registry));
        const auto& resumed = leave.registry.ctx().get<game::FDungeonRun>();
        CHECK(resumed.phase == game::EDungeonPhase::Battle && resumed.room == 0 && !resumed.leaveRequested);
    }
    {
        FFixture abandon;
        abandon.registry.ctx().get<game::FDungeonRun>().abandonRequested = true;
        CHECK(abandon.battle.Update(abandon.registry, 0) == EStateTransition::ToHub);
        CHECK(!abandon.registry.ctx().contains<game::FDungeonRun>());
        CHECK(!abandon.registry.ctx().get<game::FDungeonSave>().exists && !std::filesystem::exists(abandon.runSave));
    }
    {
        FFixture lost;
        game::CompleteDungeonBattle(lost.registry, false);
        CHECK(!lost.registry.ctx().get<game::FDungeonSave>().exists && !std::filesystem::exists(lost.runSave));
        CHECK(!game::ContinueDungeonRun(lost.registry));
    }

    // Continuing at a reward step shows the cleared room instead of starting a fight.
    {
        FFixture between;
        auto& r = between.registry;
        game::CompleteDungeonBattle(r, true);
        r.ctx().erase<game::FDungeonRun>();
        CHECK(game::ContinueDungeonRun(r));
        CHECK(r.ctx().get<game::FDungeonRun>().phase == game::EDungeonPhase::Reward);
        between.battle.OnExit(r);
        between.battle.OnEnter(r);
        CHECK(game::AliveCount(r, false) == 0 && game::AliveCount(r, true) == 4);
        CHECK(r.ctx().get<game::FBattleOutcome>().resolved && r.ctx().get<game::FBattleOutcome>().victory);
        CHECK(between.battle.Update(r, 0) == EStateTransition::None);
        CHECK(r.ctx().get<game::FDungeonRun>().phase == game::EDungeonPhase::Reward);
        game::ContinueDungeonReward(r);
        CHECK(r.ctx().get<game::FDungeonRun>().phase == game::EDungeonPhase::Event);
    }

    // The menu button: Continue enters the battle state, and an unusable save keeps the player on the menu.
    {
        FFixture menu;
        auto& r = menu.registry;
        r.ctx().erase<game::FDungeonRun>();
        r.ctx().get<game::FMenuRequest>().value = game::EMenuRequest::Continue;
        const auto next = ResolveEmberMenuRequest(r);
        CHECK(next.has_value() && *next == EStateTransition::ToBattle);
        CHECK(r.ctx().contains<game::FDungeonRun>() && r.ctx().get<game::FMenuRequest>().value == game::EMenuRequest::None);
        r.ctx().erase<game::FDungeonRun>();
        std::filesystem::remove(menu.runSave);
        r.ctx().get<game::FMenuRequest>().value = game::EMenuRequest::Continue;
        CHECK(!ResolveEmberMenuRequest(r).has_value());
        CHECK(!r.ctx().contains<game::FDungeonRun>() && !r.ctx().get<game::FDungeonSave>().error.empty());
    }
}

void BattleAnimations() {
    std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> surface(
        SDL_CreateSurface(1280, 800, SDL_PIXELFORMAT_RGBA8888), &SDL_DestroySurface);
    CHECK(surface != nullptr);
    FRendererPtr renderer{SDL_CreateSoftwareRenderer(surface.get())};
    CHECK(renderer != nullptr);
    FFixture fixture;
    auto& registry = fixture.registry;
    registry.ctx().emplace<game::FResourceManager>().init(renderer.get());
    registry.ctx().emplace<SDL_Renderer*>(renderer.get());
    CHECK(game::FAnimationLibrary::Initialize(registry));
    CHECK(game::InitializeBattleAnim(registry));
    fixture.battle.OnExit(registry);
    fixture.battle.OnEnter(registry);  // the battle starts again, now with animation data to attach

    // Every unit of the room carries its animation state, standing still on its cell.
    const auto& data = registry.ctx().get<game::FBattleAnimData>();
    const auto* library = game::FAnimationLibrary::Find(registry);
    std::size_t animated = 0;
    for (const auto entity : registry.view<game::FUnitAnim, game::FGridPosition>()) {
        const auto& anim = registry.get<game::FUnitAnim>(entity);
        const auto& cell = registry.get<game::FGridPosition>(entity);
        CHECK(anim.state == game::EUnitAnim::Idle && anim.visual.x == cell.x && anim.visual.y == cell.y);
        ++animated;
    }
    CHECK(animated == static_cast<std::size_t>(game::AliveCount(registry, true) + game::AliveCount(registry, false)));
    CHECK(data.skills.size() == 12 && data.sets.size() == 8);
    const auto clipId = [&](const char* id) { return library->findClip(id).value_or(game::FAnimClipId{}); };

    // Bad data is refused by name instead of drawing half an animation.
    std::ifstream file("assets/data/battle_anim.json");
    const auto shipped = nlohmann::json::parse(file);
    const auto& content = registry.ctx().get<game::gameplay::FContentRegistry>();
    CHECK(game::ParseBattleAnim(shipped, *library, content).has_value());
    const auto refused = [&](const char* label, const auto& edit) {
        auto document = shipped;
        edit(document);
        Check(!game::ParseBattleAnim(document, *library, content).has_value(), label, __LINE__);
    };
    refused("unknown skill", [](auto& d) { d["skills"]["skill.nonexistent"] = d["skills"]["skill.strike"]; });
    refused("unknown unit", [](auto& d) { d["units"]["enemy.nobody"] = d["units"]["enemy.imp"]; });
    refused("unknown clip prefix", [](auto& d) { d["units"]["char.warrior"]["clips"] = "unit.nobody"; });
    refused("unknown effect clip", [](auto& d) { d["skills"]["skill.strike"]["clip"] = "fx.nonexistent"; });
    refused("impact frame outside the clip", [](auto& d) { d["units"]["char.warrior"]["impact_frame"] = 99; });
    refused("unknown effect kind", [](auto& d) { d["skills"]["skill.strike"]["kind"] = "teleport"; });
    refused("landing picture on an impact", [](auto& d) { d["skills"]["skill.strike"]["impact"] = "fx.hit"; });
    refused("missing default effect", [](auto& d) { d.erase("default_effect"); });
    refused("wrong schema version", [](auto& d) { d["schema_version"] = 2; });

    const auto party = registry.view<game::FPartyMember, game::FUnitAnim>().front();
    const auto foes = registry.view<game::FEnemy, game::FUnitAnim>();
    const auto foe = foes.front();
    auto& hero = registry.get<game::FUnitAnim>(party);
    auto& victim = registry.get<game::FUnitAnim>(foe);
    const auto step = [&](float seconds) {
        for (float elapsed = 0.f; elapsed < seconds; elapsed += 1.f / 60.f) game::UpdateBattleAnimations(registry, 1.f / 60.f);
    };
    const auto effectCount = [&] { return registry.ctx().get<game::FBattleEffects>().active.size(); };
    const auto skillEvent = [&](const char* skill, bool hostile, bool died) {
        game::FBattleEvent event;
        event.actor = party;
        event.skillId = skill;
        event.aim = registry.get<game::FGridPosition>(foe);
        const int health = registry.get<game::FHealth>(foe).current;
        event.targets.push_back({foe, health, std::max(0, health - 5), hostile, died});
        registry.ctx().get<game::FBattleEvents>().pending.push_back(event);
        return health;
    };

    // One attack motion for every skill: the caster attacks at once, and the blow lands on the clip's impact frame.
    const int health = skillEvent("skill.strike", true, false);
    game::UpdateBattleAnimations(registry, 0.f);
    CHECK(hero.state == game::EUnitAnim::Attack && victim.shownHealth == health);
    step(.1f);
    CHECK(victim.state == game::EUnitAnim::Idle && effectCount() == 0);   // not yet: the swing is still going
    step(.35f);
    CHECK(victim.state == game::EUnitAnim::Hit && victim.shownHealth == -1);
    CHECK(effectCount() == 1 && registry.ctx().get<game::FBattleEffects>().active.front().clip == clipId("fx.slash"));
    step(1.5f);
    CHECK(hero.state == game::EUnitAnim::Idle && victim.state == game::EUnitAnim::Idle && effectCount() == 0);

    // A knock-out stays on its feet until the blow lands, then falls and stays down.
    const int last = registry.get<game::FHealth>(foe).current;
    (void)game::ApplyDamageToUnit(registry, foe, last);
    skillEvent("skill.strike", true, true);
    game::UpdateBattleAnimations(registry, 0.f);
    CHECK(victim.pendingDeaths == 1 && victim.state != game::EUnitAnim::Death);
    step(.15f);
    CHECK(victim.state != game::EUnitAnim::Death);
    step(.4f);
    CHECK(victim.state == game::EUnitAnim::Death && victim.pendingDeaths == 0);
    step(2.f);
    CHECK(victim.player.finished);

    // A knock-out no event announced (a spike trap, a burn) still ends in the death pose.
    auto second = entt::entity{entt::null};
    for (const auto entity : foes) {
        if (entity != foe) { second = entity; break; }
    }
    CHECK(second != entt::null);
    (void)game::ApplyDamageToUnit(registry, second, registry.get<game::FHealth>(second).current);
    step(.05f);
    CHECK(registry.get<game::FUnitAnim>(second).state == game::EUnitAnim::Death);

    // Walking: the picture crosses the cell instead of jumping, and goes back to idle on arrival.
    const auto from = registry.get<game::FGridPosition>(party);
    const game::FGridPosition to{from.x + 1, from.y};
    registry.get<game::FGridPosition>(party) = to;
    game::FBattleEvent walk;
    walk.kind = game::FBattleEvent::EKind::Move;
    walk.actor = party;
    walk.path = {from, to};
    registry.ctx().get<game::FBattleEvents>().pending.push_back(walk);
    step(.11f);
    CHECK(hero.state == game::EUnitAnim::Move && hero.visual.x > from.x && hero.visual.x < to.x);
    step(.3f);
    CHECK(hero.state == game::EUnitAnim::Idle && hero.visual.x == to.x && hero.visual.y == to.y);

    // A projectile flies, then its landing picture plays where it arrived.
    skillEvent("skill.firebolt", true, false);
    game::UpdateBattleAnimations(registry, 0.f);
    step(.32f);
    CHECK(effectCount() == 1 && registry.ctx().get<game::FBattleEffects>().active.front().duration > 0.f);
    CHECK(registry.ctx().get<game::FBattleEffects>().active.front().clip == clipId("fx.orb_fire"));
    bool landed = false;
    for (float elapsed = 0.f; elapsed < 1.5f && !landed; elapsed += 1.f / 60.f) {
        game::UpdateBattleAnimations(registry, 1.f / 60.f);
        for (const auto& effect : registry.ctx().get<game::FBattleEffects>().active) {
            landed = landed || effect.clip == clipId("fx.explosion_fire");
        }
    }
    CHECK(landed);
    step(2.f);
    CHECK(effectCount() == 0);

    // A zone covers every cell it names, one after another.
    game::FBattleEvent fog;
    fog.actor = party;
    fog.skillId = "skill.fog";
    fog.aim = registry.get<game::FGridPosition>(foe);
    fog.cells = {fog.aim, {fog.aim.x + 1, fog.aim.y}, {fog.aim.x, fog.aim.y + 1}};
    registry.ctx().get<game::FBattleEvents>().pending.push_back(fog);
    game::UpdateBattleAnimations(registry, 0.f);
    step(.55f);
    CHECK(effectCount() == 3);
    step(2.f);
    CHECK(effectCount() == 0);

    // Pictures only draw through the renderer, and a unit already down can be put straight into its last frame.
    CHECK(game::DrawAnimFrame(registry, clipId("unit.warrior.idle"), 0, {100.f, 100.f}, 1.f));
    CHECK(!game::DrawAnimFrame(registry, game::FAnimClipId{}, 0, {0.f, 0.f}, 1.f));
    registry.get<game::FUnitAnim>(second).state = game::EUnitAnim::Idle;
    game::SettleUnitAnimations(registry);
    CHECK(registry.get<game::FUnitAnim>(second).state == game::EUnitAnim::Death && registry.get<game::FUnitAnim>(second).player.finished);

    // Without the animation system running, events are dropped instead of piling up.
    registry.ctx().erase<game::FBattleTimeline>();
    skillEvent("skill.strike", true, false);
    game::UpdateBattleAnimations(registry, 1.f / 60.f);
    CHECK(registry.ctx().get<game::FBattleEvents>().pending.empty());
}

void GameplayKeyboardCapture() {
    std::unique_ptr<ImGuiContext, decltype(&ImGui::DestroyContext)> context(
        ImGui::CreateContext(), &ImGui::DestroyContext);
    entt::registry registry;
    auto& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.DisplaySize = {1280, 800};
    io.DeltaTime = 1.f / 60.f;
    io.Fonts->Build();
    game::InitializeDungeonUi(registry);
    for (int frame = 0; frame < 4; ++frame) {
        ImGui::NewFrame();
        ImGui::SetNextWindowFocus();
        ImGui::Begin("Focused tactics");
        ImGui::Button("End turn");
        ImGui::End();
        ImGui::Render();
    }
    CHECK(!io.WantCaptureKeyboard);
    game::FInputState input;
    input.beginFrame(io.WantCaptureKeyboard, io.WantCaptureMouse);
    SDL_Event key{};
    key.type = SDL_EVENT_KEY_DOWN;
    key.key.scancode = SDL_SCANCODE_W;
    input.processEvent(key);
    CHECK(input.isActionPressed(game::EInputAction::MoveUp));
    for (int frame = 0; frame < 3; ++frame) {
        ImGui::NewFrame();
        ImGui::OpenPopup("Blocking decision");
        if (ImGui::BeginPopupModal("Blocking decision")) {
            ImGui::Button("Confirm");
            ImGui::EndPopup();
        }
        ImGui::Render();
    }
    CHECK(io.WantCaptureKeyboard);
}

void GeneratedRooms() {
    const game::gameplay::FRoomGenConfig config{
        {"A", "B", "C"},
        {{"enemy.imp", "Imp", 3}, {"enemy.imp_archer", "Archer", 1}},
        "enemy.guardian", "Guardian", {3, 4}, {2, 5}, {1, 3}};
    constexpr int size = game::kGridWidth;
    const std::vector<std::pair<int, int>> party{{3, 9}, {4, 9}, {6, 9}, {7, 9}};
    std::set<std::string> layouts;
    for (std::uint32_t seed = 1; seed <= 200; ++seed) {
        const int room = static_cast<int>(seed % 3);
        const game::gameplay::FRoomRequest request{seed, 1 + static_cast<int>(seed % 5), room, room == 2, party};
        const auto generated = game::gameplay::GenerateRoom(config, request);
        const auto again = game::gameplay::GenerateRoom(config, request);
        CHECK(generated.terrain == again.terrain && generated.enemies.size() == again.enemies.size());
        CHECK(generated.enemies.size() >= 3 && generated.enemies.size() <= 4);
        CHECK(request.boss == (generated.enemies.front().unit == "enemy.guardian"));
        std::set<std::pair<int, int>> taken(party.begin(), party.end());
        for (const auto& enemy : generated.enemies) {
            CHECK(enemy.y >= 1 && enemy.y <= 3 && enemy.x >= 1 && enemy.x <= size - 2);
            CHECK(generated.terrain[enemy.y][enemy.x] == '.');
            CHECK(taken.insert({enemy.x, enemy.y}).second);
        }
        for (const auto& [x, y] : party) CHECK(generated.terrain[y][x] == '.');
        int walls = 0;
        for (int y = 0; y < size; ++y) {
            CHECK(generated.terrain[y].size() == size);
            for (int x = 0; x < size; ++x) {
                // Open map: no wall ring along the grid edge, and nothing else is scattered there either.
                const bool edge = x == 0 || y == 0 || x == size - 1 || y == size - 1;
                CHECK(!edge || generated.terrain[y][x] == '.');
                if (generated.terrain[y][x] == '#') ++walls;
            }
        }
        CHECK(walls <= 5);
        // Every unit must reach every other unit around the walls.
        std::set<std::pair<int, int>> seen{*taken.begin()};
        std::vector<std::pair<int, int>> open{*taken.begin()};
        while (!open.empty()) {
            const auto [x, y] = open.back();
            open.pop_back();
            for (const auto [dx, dy] : std::array<std::pair<int, int>, 4>{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}}) {
                const int nx = x + dx;
                const int ny = y + dy;
                if (nx < 0 || ny < 0 || nx >= size || ny >= size) continue;
                if (generated.terrain[ny][nx] != '#' && seen.insert({nx, ny}).second) open.push_back({nx, ny});
            }
        }
        for (const auto& cell : taken) CHECK(seen.contains(cell));
        std::string key;
        for (const auto& row : generated.terrain) key += row;
        for (const auto& enemy : generated.enemies) key += enemy.unit + std::to_string(enemy.x * size + enemy.y);
        layouts.insert(key);
    }
    CHECK(layouts.size() > 150);
}

void InvalidRuneDrops() {
    std::ifstream originalFile(FixtureDataPath("dungeon.json"));
    const auto original = nlohmann::json::parse(originalFile);
    FTestContent content(original);
    const nlohmann::json invalidDrops[]{nlohmann::json::array(), nullptr,
        nlohmann::json::array({42}), nlohmann::json::array({"rune.unknown"})};
    std::vector<nlohmann::json> invalidDocuments;
    for (const auto& drops : invalidDrops) {
        auto data = original;
        data["rune_drops"] = drops;
        invalidDocuments.push_back(std::move(data));
    }
    auto missing = original;
    missing.erase("rune_drops");
    invalidDocuments.push_back(std::move(missing));
    missing = original;
    missing["generation"]["room_names"] = nlohmann::json::array();
    invalidDocuments.push_back(std::move(missing));
    missing = original;
    missing["generation"]["enemies"][0]["unit"] = "enemy.unknown";
    invalidDocuments.push_back(std::move(missing));
    missing = original;
    missing["generation"].erase("boss_unit");
    invalidDocuments.push_back(std::move(missing));
    missing = original;
    missing["stat_upgrades"] = nlohmann::json::array();
    invalidDocuments.push_back(std::move(missing));
    missing = original;
    missing["stat_upgrades"][0]["stat"] = "unknown";
    invalidDocuments.push_back(std::move(missing));
    missing = original;
    missing["passive_tree"] = "data/unknown-tree";
    invalidDocuments.push_back(std::move(missing));
    for (const auto& data : invalidDocuments) {
        content.write(data);
        entt::registry registry;
        game::FAssetManager::Initialize(registry, TestAssetRoots());
        CHECK(game::gameplay::FGameplayServices::Initialize(registry));
        registry.ctx().erase<game::FAssetManager>();
        registry.ctx().emplace<game::FAssetManager>(std::vector<std::string>{content.root.string()});
        CHECK(!game::InitializeDungeon(registry, (content.root / "checkpoint.json").string()));
    }
}

game::ui::FUiLayoutSpec LoadUiLayoutSpec() {
    const auto document = game::FJsonLoader::Load("assets/data/ui_layout.json");
    CHECK(document.has_value());
    const game::FAssetDigest digest("data/ui_layout", *document);
    auto spec = game::ui::ParseUiLayoutSpec(digest);
    CHECK(spec.has_value());
    return *spec;
}

void IsometricProjection() {
    const game::FGridMetrics metrics{};
    const auto spec = LoadUiLayoutSpec();
    for (const auto [width, height] : {std::pair{1280.f, 800.f}, std::pair{1920.f, 1080.f}, std::pair{960.f, 640.f}}) {
        const auto layout = game::ui::ComputeUiLayout(spec, width, height);
        const auto view = game::MakeIsoView(
            metrics, {layout.viewport.x, layout.viewport.y, layout.viewport.w, layout.viewport.h});
        CHECK(view.scale >= 1.f);
        for (int y = 0; y < metrics.height; ++y) {
            for (int x = 0; x < metrics.width; ++x) {
                int cellX = -1, cellY = -1;
                CHECK(game::IsoScreenToCell(view, metrics, view.cellCenter(x, y), cellX, cellY));
                CHECK(cellX == x && cellY == y);
                // A point just inside each diamond corner still belongs to the same cell.
                const auto nearTop = view.cellCenter(x, y) - glm::vec2{0.f, 14.f * view.scale};
                CHECK(game::IsoScreenToCell(view, metrics, nearTop, cellX, cellY) && cellX == x && cellY == y);
            }
        }
        int outX = 0, outY = 0;
        CHECK(!game::IsoScreenToCell(view, metrics, view.corner(-0.5f, 2.f), outX, outY));
        CHECK(!game::IsoScreenToCell(view, metrics, view.corner(2.f, static_cast<float>(metrics.height) + 0.5f), outX, outY));
        // The arena stays inside the frame's viewport so the bars never cover it.
        const auto& viewport = layout.viewport;
        const auto gridWidth = static_cast<float>(metrics.width);
        const auto gridHeight = static_cast<float>(metrics.height);
        CHECK(view.origin.y >= viewport.y - 1.f);
        CHECK(view.corner(gridWidth, gridHeight).y <= viewport.bottom() + 1.f);
        CHECK(view.corner(0.f, gridHeight).x >= viewport.x - 1.f);
        CHECK(view.corner(gridWidth, 0.f).x <= viewport.right() + 1.f);
    }
}

void UiFrameLayout() {
    const auto spec = LoadUiLayoutSpec();
    // The nominal window reproduces the authored design exactly.
    const auto nominal = game::ui::ComputeUiLayout(spec, spec.designWidth, spec.designHeight);
    CHECK(nominal.scale == 1.f);
    CHECK(nominal.zones.skills.x == spec.zones.skills.rect.x && nominal.zones.skills.y == spec.zones.skills.rect.y);
    CHECK(nominal.viewport.w == spec.designWidth - spec.bands.left - spec.bands.right);
    CHECK(nominal.viewport.h == spec.designHeight - spec.bands.top - spec.bands.bottom);

    // Every supported window shape keeps the zones inside their band and clear of each other.
    for (const auto [width, height] : {std::pair{1280.f, 800.f}, std::pair{1920.f, 1080.f}, std::pair{960.f, 640.f},
                                       std::pair{1140.f, 800.f}, std::pair{1000.f, 800.f}, std::pair{800.f, 600.f},
                                       std::pair{2560.f, 1080.f}, std::pair{1280.f, 1024.f}}) {
        const auto layout = game::ui::ComputeUiLayout(spec, width, height);
        CHECK(layout.viewport.w > 0.f && layout.viewport.h > 0.f);
        CHECK(layout.screen.contains(layout.viewport));
        for (const auto* band : {&layout.topBar, &layout.dock, &layout.leftRail, &layout.rightRail}) {
            CHECK(!layout.viewport.intersects(*band));
        }
        const auto& zones = layout.zones;
        const game::ui::FRect* top[]{&zones.title, &zones.turnOrder, &zones.status};
        const game::ui::FRect* dock[]{&zones.party, &zones.unit, &zones.skills, &zones.endTurn, &zones.journal, &zones.hints};
        for (const auto* zone : top) CHECK(layout.topBar.contains(*zone));
        for (const auto* zone : dock) CHECK(layout.dock.contains(*zone));
        CHECK(layout.viewport.contains(zones.targets));
        for (std::size_t first = 0; first < std::size(top); ++first) {
            for (std::size_t second = first + 1; second < std::size(top); ++second) CHECK(!top[first]->intersects(*top[second]));
        }
        for (std::size_t first = 0; first < std::size(dock); ++first) {
            for (std::size_t second = first + 1; second < std::size(dock); ++second) CHECK(!dock[first]->intersects(*dock[second]));
        }
    }

    // A broken layout document is reported, not half-applied.
    auto document = *game::FJsonLoader::Load("assets/data/ui_layout.json");
    auto missing = document;
    missing["zones"].erase("journal");
    CHECK(!game::ui::ParseUiLayoutSpec(game::FAssetDigest("data/ui_layout", missing)).has_value());
    auto badAnchor = document;
    badAnchor["zones"]["hints"]["anchor"] = "middle";
    CHECK(!game::ui::ParseUiLayoutSpec(game::FAssetDigest("data/ui_layout", badAnchor)).has_value());
    auto noViewport = document;
    noViewport["bands"]["top"] = 900;
    CHECK(!game::ui::ParseUiLayoutSpec(game::FAssetDigest("data/ui_layout", noViewport)).has_value());
}

} // namespace

int main() {
    try {
        IsometricProjection();
        UiFrameLayout();
        CollisionAndTurns();
        ActionValidation();
        AutoActionTokenBoundary();
        SharedCellAndPierceActions();
        CampRuneTransactions();
        BalanceDrivenChoiceUi();
        CompleteRun(1);
        CompleteRun(2);
        CompleteRun(3);
        PerCharacterRunGrowth();
        DefeatAndFactory();
        SaveAndContinue();
        BattleAnimations();
        GameplayKeyboardCapture();
        InvalidRuneDrops();
        GeneratedRooms();
        std::cout << "Dungeon collision, AP, tokens, LOS, generated layouts, AI, rewards, runes, "
                     "per-character run growth, checkpoints, defeat and save/continue passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAILED: " << error.what() << '\n';
        return 1;
    }
}
