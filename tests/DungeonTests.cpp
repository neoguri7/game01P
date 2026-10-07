#include <chrono>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <memory>
#include <imgui.h>

#include "core/AssetManager.h"
#include "core/EngineState.h"
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
#include "gameplay/dungeon/FDungeonRun.h"
#include "gameplay/factory/FEntityFactory.h"
#include "gameplay/grid/FGridQuery.h"
#include "gameplay/grid/FCameraMath.h"
#include "gameplay/grid/FGridTerrain.h"
#include "states/FEmberBattleState.h"
#include "presentation/FDungeonOverlay.h"
#include "presentation/FDungeonArt.h"
#include "core/InputState.h"

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

    explicit FFixture(std::uint32_t seed = 1234) {
        std::filesystem::create_directories("out");
        checkpoint = std::filesystem::path("out") / ("test-checkpoint-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".json");
        game::FAssetManager::Initialize(registry);
        CHECK(game::gameplay::FGameplayServices::Initialize(registry));
        CHECK(game::InitializeDungeon(registry, checkpoint.string()));
        turns.onRegister(registry);
        game::StartDungeonRun(registry, seed, true);
        battle.OnEnter(registry);
        CHECK(game::AliveCount(registry, true) == 4);
        CHECK(game::AliveCount(registry, false) == 3);
    }

    ~FFixture() {
        battle.OnExit(registry);
        std::error_code error;
        std::filesystem::remove(checkpoint, error);
        if (error) std::cerr << "Checkpoint cleanup failed: " << error.message() << '\n';
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
        write(data);
    }

    void write(const nlohmann::json& data) const {
        std::ofstream file(root / "data" / "dungeon.json");
        file << data.dump();
        file.flush();
        CHECK(file.good());
    }

    ~FTestContent() {
        for (const auto& path : {root / "ui.log", root / "data" / "dungeon.json", root / "data", root}) {
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

    terrain.cells[1 * 8 + 2] = game::ETile::Wall;
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

    terrain.cells[1 * 8 + 2] = game::ETile::Trap;
    CHECK(game::HasGridLineOfSight(registry, {1, 1}, {3, 1}));
    queue.pending.push_back(game::FMoveRequest{token.actor, {3, 1}, token});
    fixture.actions.update(registry, 0);
    CHECK(cell.x == 3 && cell.y == 1 && points.move == 0);
    CHECK(hp.current == startingHp - terrain.trapDamage);
    CHECK(points.skill == points.skillMax);
    queue.pending.push_back(game::FMoveRequest{token.actor, {3, 2}, token});
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

    terrain.cells[1 * 8 + 2] = game::ETile::Wall;
    CHECK(!game::HasGridLineOfSight(registry, {1, 1}, {2, 2}));
    const auto path = game::FindGridPath(registry, token.actor, {1, 1}, 10);
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
    terrain.cells[2 * 8 + 3] = game::ETile::Wall;
    queue.pending.push_back(game::FActionRequest{token.actor, enemy, "skill.arrow", token});
    fixture.actions.update(registry, 0);
    CHECK(hp.current == before && points.skill == points.skillMax);
    terrain.cells[2 * 8 + 3] = game::ETile::Floor;
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
    registry.ctx().get<game::FGridTerrain>().cells.fill(game::ETile::Floor);
    registry.get<game::FGridPosition>(token.actor) = {3, 3};
    const auto enemy = game::FindNearestAlive(registry, token.actor, false);
    CHECK(enemy != entt::null);
    registry.get<game::FGridPosition>(enemy) = {3, 2};
    registry.ctx().erase<game::FTurnRuntime>();
    CHECK(!registry.ctx().contains<game::FTurnRuntime>());
    const auto& content = registry.ctx().get<game::gameplay::FContentRegistry>();
    const auto skill = game::ChooseAutoAction(registry, token, false, content);
    CHECK(skill.has_value() && std::holds_alternative<game::FActionRequest>(*skill));
    CHECK(std::get<game::FActionRequest>(*skill).token == token);
    CHECK(std::get<game::FActionRequest>(*skill).actor == token.actor);
    auto& points = registry.get<game::FActionPoints>(token.actor);
    points.skill = 0;
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
    terrain.cells[1 * 8 + 3] = game::ETile::Wall;
    queue.pending.push_back(game::FActionRequest{token.actor, target, "skill.lunge", token});
    fixture.actions.update(registry, 0);
    CHECK(registry.get<game::FHealth>(target).current < targetHp);
    CHECK(registry.get<game::FHealth>(behind).current == behindHp);
    terrain.cells[1 * 8 + 3] = game::ETile::Floor;
    points.skill = points.skillMax;
    queue.pending.push_back(game::FActionRequest{token.actor, target, "skill.lunge", token});
    fixture.actions.update(registry, 0);
    CHECK(registry.get<game::FHealth>(behind).current < behindHp);

    registry.get<game::FGridPosition>(target) = {4, 1};
    registry.get<game::FGridPosition>(behind) = {5, 1};
    points.skill = points.skillMax;
    const auto effectsBefore = registry.get<game::FActiveStatuses>(target).effects.size();
    terrain.cells[1 * 8 + 2] = game::ETile::Wall;
    queue.pending.push_back(game::FActionRequest{token.actor, entt::null, "skill.fog", token, {3, 1}});
    fixture.actions.update(registry, 0);
    CHECK(points.skill == points.skillMax);
    CHECK(registry.get<game::FActiveStatuses>(target).effects.size() == effectsBefore);
    terrain.cells[1 * 8 + 2] = game::ETile::Floor;
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
    game::FCamera camera{};
    for (const auto entity : registry.view<game::FCamera>()) camera = registry.get<game::FCamera>(entity);
    input.mousePos = game::WorldToScreen(game::GridCellCenterToWorld(
        registry.ctx().get<game::FGridMetrics>(), 3, 1), camera, {1280, 800});
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
    std::ifstream source("assets/data/dungeon.json");
    auto data = nlohmann::json::parse(source);
    for (const auto& [key, value] : std::vector<std::pair<const char*, int>>{
             {"reward_health", 37}, {"reward_attack", 7}, {"reward_magic", 11},
             {"event_heal", 42}, {"event_materials", 6}, {"camp_cost", 5}, {"camp_heal", 61}}) {
        data["balance"][key] = value;
    }
    FTestContent content(data);
    registry.ctx().erase<game::FAssetManager>();
    registry.ctx().emplace<game::FAssetManager>(std::vector<std::string>{content.root.string(), "assets"});
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
    for (const char* label : {"VIGOR  /  +37 max HP and heal", "MIGHT  /  +7 physical attack",
             "FOCUS  /  +11 magic attack", "Rest at the shrine  /  heal 42 HP",
             "Open the cache  /  gain 6 materials", "Healed up to 61 HP.",
             "Reinforce (+7 attack/+11 magic, 5 materials) & descend"}) {
        CHECK(log.find(label) != std::string::npos);
    }
    run.phase = game::EDungeonPhase::Reward;
    game::ChooseDungeonReward(registry, 0);
    CHECK(run.party[0].bonusHealth == 37);
    game::ChooseDungeonEvent(registry, false);
    CHECK(run.materials == 6);
    run.phase = game::EDungeonPhase::Camp;
    game::LeaveDungeonCamp(registry, true);
    CHECK(run.materials == 1 && run.party[0].bonusAttack == 7 && run.party[0].bonusMagic == 11);
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
        CHECK(run.level == room + 2);
        CHECK(run.runes.size() == static_cast<std::size_t>(room + 1));
        const int bonus = run.party.front().bonusAttack;
        game::ChooseDungeonReward(registry, 1);
        CHECK(run.party.front().bonusAttack > bonus);
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
    const int materials = run.materials;
    game::LeaveDungeonCamp(registry, true);
    CHECK(run.depth == 2 && run.room == 0 && run.materials == materials - 3);
    fixture.nextRoom();
    CHECK(registry.get<game::FStats>(game::FindNearestAlive(registry,
        registry.view<game::FEnemy>().front(), true)).attack > 2);
    game::StartDungeonRun(registry, 999);
    const auto& restarted = registry.ctx().get<game::FDungeonRun>();
    CHECK(restarted.depth == 2 && restarted.room == 0);
    CHECK(restarted.materials == 0 && restarted.runes.empty() && restarted.experience == 0);
    CHECK(restarted.party.front().bonusAttack == 0);
    std::cout << "  Seed " << seed << ": " << firstRoom << " -> full dungeon + checkpoint + next depth\n";
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
    CHECK(registry.ctx().get<game::FDungeonCheckpoint>().cleared == checkpoint);
    auto document = game::BuildDungeonBattlefield(registry);
    document["units"][0]["grid_x"] = 0;
    document["units"][0]["grid_y"] = 0;
    entt::registry probe;
    game::FScene scene(probe, "invalid");
    scene.Load();
    CHECK(!game::FEntityFactory::CreateBattlefield(scene, document));
    document["terrain"] = nlohmann::json::array({"bad"});
    CHECK(!game::FEntityFactory::CreateBattlefield(scene, document));
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

void InvalidRuneDrops() {
    std::ifstream originalFile("assets/data/dungeon.json");
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
    missing["rooms"][0].erase("name");
    invalidDocuments.push_back(std::move(missing));
    for (const auto& data : invalidDocuments) {
        content.write(data);
        entt::registry registry;
        game::FAssetManager::Initialize(registry);
        CHECK(game::gameplay::FGameplayServices::Initialize(registry));
        registry.ctx().erase<game::FAssetManager>();
        registry.ctx().emplace<game::FAssetManager>(std::vector<std::string>{content.root.string()});
        CHECK(!game::InitializeDungeon(registry, (content.root / "checkpoint.json").string()));
    }
}

} // namespace

int main() {
    try {
        CollisionAndTurns();
        ActionValidation();
        AutoActionTokenBoundary();
        SharedCellAndPierceActions();
        CampRuneTransactions();
        BalanceDrivenChoiceUi();
        CompleteRun(1);
        CompleteRun(2);
        CompleteRun(3);
        DefeatAndFactory();
        GameplayKeyboardCapture();
        InvalidRuneDrops();
        std::cout << "Dungeon collision, AP, tokens, LOS, three layouts, AI, rewards, runes, checkpoints and defeat passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAILED: " << error.what() << '\n';
        return 1;
    }
}
