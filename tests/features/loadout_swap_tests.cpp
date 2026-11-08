// WP-10b: the camp skill swap — the camp-only phase guard, the single pool rule (authored pool minus the
// locked unlocks.json skill rows, MemberSkillPool), the rune return to the collected pool, same-skill and
// duplicate refusal, the next battle's loadout, save/continue, the free cost, the four active slots, the
// camp host glue (ApplyCampLoadoutEdit) and the pure display panel (DrawLoadoutPanel).
//
// Content: the FROZEN FIXTURE plus a temp overlay — roots {scratch, tests/fixtures/assets, assets}, with a
// units.json derived from the fixture written into the scratch root. A shipped rename, retune or pool trim
// therefore never moves this suite. The fixture's three-skill pools cannot fill the four-slot contract and
// author no gated skill, so SwapUnitsOverlay() widens each party unit from the fixture tables only. ONE
// case (ShipPoolsFillActiveSlots) reads the SHIPPED content on purpose: the shipped pools must keep filling
// the camp panel's active slots. Every writable file — checkpoint, run save, meta save — lives under the
// per-case scratch root. Rules cases may open the camp phase directly; the next-battle, save and camp-modal
// cases walk the real run flow. Expectations are derived from the loaded data (pools, slots, rune tags),
// never from balance constants.

#define GAME01P_TEST_SUPPORT_CHECK
#include "../TestSupport.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <SDL3/SDL.h>
#include <imgui.h>
#include <nlohmann/json.hpp>

#include "core/EngineState.h"
#include "core/ResourceManager.h"
#include "core/SDLDeleter.h"
#include "ecs/components/FFaction.h"
#include "ecs/components/FHealth.h"
#include "gameplay/battle/FBattleSetup.h"
#include "gameplay/combat/FCombatSheet.h"
#include "gameplay/data/FContentRegistry.h"
#include "gameplay/dungeon/FDungeonCamp.h"
#include "gameplay/dungeon/FDungeonRun.h"
#include "gameplay/dungeon/FDungeonSave.h"
#include "gameplay/dungeon/FLoadoutSwap.h"
#include "gameplay/dungeon/FRewards.h"
#include "gameplay/dungeon/FRunEventLog.h"
#include "gameplay/dungeon/FRunFlow.h"
#include "gameplay/dungeon/FRunStart.h"
#include "gameplay/factory/FEntityDefinitions.h"
#include "gameplay/factory/FEntityFactory.h"
#include "gameplay/factory/FScene.h"
#include "gameplay/grid/FGridTerrain.h"
#include "gameplay/grid/FGridTypes.h"
#include "gameplay/meta/FMetaProgress.h"
#include "presentation/FCampLoadoutHost.h"
#include "presentation/FDungeonArt.h"
#include "presentation/FDungeonOverlay.h"
#include "presentation/FDungeonViews.h"
#include "presentation/FLoadoutPanel.h"
#include "presentation/FModalViewRegistry.h"

namespace {

using nlohmann::json;

constexpr std::uint32_t kSeed = 1010;

// ---------------------------------------------------------------------------------------------
// Scratch roots and the booted registry
// ---------------------------------------------------------------------------------------------

std::string UniqueStem(const char* stem) {
    static std::uint32_t serial = 0;
    std::filesystem::create_directories("out");
    return std::string(stem) + "-" + std::to_string(++serial) + "-" +
           std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
}

std::string UniquePath(const char* stem) {
    return "out/" + UniqueStem(stem) + ".json";
}

std::string ReadTextFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    CHECK(file.good());
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

json ReadJsonFile(const std::filesystem::path& path) { return json::parse(ReadTextFile(path)); }

void WriteJsonFile(const std::filesystem::path& path, const json& document) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    CHECK(file.good());
    file << document.dump(2);
    CHECK(file.good());
}

std::string ReadBytes(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    CHECK(file.good());
    return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}

/// A per-test scratch directory under out/, removed when the case ends (pass or fail). Every file a test
/// writes — the checkpoint, the run save and the meta save — lives inside it.
struct FScratchRoot {
    std::filesystem::path root;

    explicit FScratchRoot(const char* stem) {
        root = std::filesystem::path("out") / UniqueStem(stem);
        std::error_code error;
        std::filesystem::create_directories(root / "data", error);
        CHECK(!error);
    }
    ~FScratchRoot() {
        std::error_code error;
        std::filesystem::remove_all(root, error);  // best effort: a locked file must not fail the test
    }
    FScratchRoot(const FScratchRoot&) = delete;
    FScratchRoot& operator=(const FScratchRoot&) = delete;
};

/// The fixture's party pools are widened from the FIXTURE tables only (no shipped gameplay file is read):
/// its three-skill warrior/archer/healer pools cannot fill the four active slots, a swap needs candidates
/// beyond the default loadout, and the locked-skill case needs an authored gated row. Per party unit: the
/// authored fixture skills in order, then fixture skills.json ids in catalog order that are not already in
/// the list and carry no unlock row, until the UNGATED count reaches the slot contract + 2, then the first
/// authored kind=skill unlock row LAST (today skill.shackle — skill.hook is not a fixture skill).
json SwapUnitsOverlay() {
    const json fixtureUnits = ReadJsonFile(FixtureDataPath("units.json"));
    const json fixtureSkills = ReadJsonFile(FixtureDataPath("skills.json"));
    const json fixtureDungeon = ReadJsonFile(FixtureDataPath("dungeon.json"));
    const json fixtureUnlocks = ReadJsonFile(FixtureDataPath("unlocks.json"));

    std::vector<std::string> skillCatalog;
    for (const json& row : fixtureSkills.at("skills")) skillCatalog.push_back(row.at("id").get<std::string>());
    std::vector<std::string> partyUnits;
    for (const json& row : fixtureDungeon.at("party")) partyUnits.push_back(row.at("unit").get<std::string>());

    std::vector<std::string> unlockIds;     // every unlock target: a row here means the id is gated
    std::string gatedSkill;                 // the first kind=skill row the skills table authors
    for (const json& row : fixtureUnlocks.at("unlocks")) {
        const std::string id = row.at("id").get<std::string>();
        unlockIds.push_back(id);
        if (gatedSkill.empty() && row.at("kind").get<std::string>() == "skill" &&
            std::ranges::find(skillCatalog, id) != skillCatalog.end()) {
            gatedSkill = id;
        }
    }
    CHECK(!gatedSkill.empty());

    const std::size_t requiredUngated = static_cast<std::size_t>(game::kBaseActiveSkillSlots) + 2;
    json units = fixtureUnits;
    for (json& unitRow : units.at("units")) {
        if (std::ranges::find(partyUnits, unitRow.at("id").get<std::string>()) == partyUnits.end()) continue;
        std::vector<std::string> skills;
        for (const json& skillId : unitRow.at("skills")) skills.push_back(skillId.get<std::string>());
        std::size_t ungated = 0;
        for (const std::string& skillId : skills) {
            if (std::ranges::find(unlockIds, skillId) == unlockIds.end()) ++ungated;
        }
        for (const std::string& candidate : skillCatalog) {
            if (ungated >= requiredUngated) break;
            if (std::ranges::find(skills, candidate) != skills.end()) continue;
            if (std::ranges::find(unlockIds, candidate) != unlockIds.end()) continue;
            skills.push_back(candidate);
            ++ungated;
        }
        CHECK(ungated >= requiredUngated);
        if (std::ranges::find(skills, gatedSkill) == skills.end()) skills.push_back(gatedSkill);
        unitRow["skills"] = skills;
    }
    return units;
}

/// Which content world a case boots.
enum class ESwapContent { Fixture, Shipped };

/// A booted registry with an isolated checkpoint/save/meta store. Fixture (the default) = the frozen fixture
/// plus the derived units overlay, so gameplay expectations never move with shipped balance data; Shipped =
/// the shipped content only, for the single pool-shape case.
struct FSwapBoot {
    FScratchRoot scratch;  // declared first: destroyed last, after the registry released its files
    entt::registry registry;

    explicit FSwapBoot(const char* stem, ESwapContent content = ESwapContent::Fixture) : scratch(stem) {
        std::vector<std::string> roots{scratch.root.string()};
        if (content == ESwapContent::Fixture) {
            // A per-file lookup: the scratch root shadows the fixture's units.json, every other gameplay
            // table still comes from the fixture and the ship root supplies presentation data only.
            WriteJsonFile(scratch.root / "data" / "units.json", SwapUnitsOverlay());
            roots.push_back(kTestFixtureRoot);
        }
        for (const std::string& root : ShipAssetRoots()) roots.push_back(root);
        game::FAssetManager::Initialize(registry, std::move(roots));
        CHECK(game::gameplay::FGameplayServices::Initialize(registry));
        CHECK(game::InitializeDungeon(registry, (scratch.root / "checkpoint.json").string()));
    }
    FSwapBoot(const FSwapBoot&) = delete;
    FSwapBoot& operator=(const FSwapBoot&) = delete;

    [[nodiscard]] bool InitMeta() {
        return game::InitializeMetaProgress(registry, (scratch.root / "meta_save.json").string());
    }
    [[nodiscard]] game::FDungeonRun& run() { return registry.ctx().get<game::FDungeonRun>(); }
};

// ---------------------------------------------------------------------------------------------
// Run state and the camp
// ---------------------------------------------------------------------------------------------

/// The preparation screen's default setup: the authored party with its default four-skill loadouts.
/// A bare StartDungeonRun installs the authored party with NO recorded skills (the combat fallback
/// supplies them at battle time), which would leave the camp swap nothing to swap.
void StartFresh(FSwapBoot& boot) {
    const game::FPreparationDraft draft =
        game::MakeDefaultDraft(boot.registry, game::FRunStartRequest{true, -1});
    CHECK(draft.setup.party.size() == game::kRunPartySize);
    CHECK(game::StartDungeonRunFromSetup(boot.registry, kSeed, true, -1, draft.setup));
    CHECK(boot.run().phase == game::EDungeonPhase::Battle);
}

/// Walks a fresh run to the camp through the real flow: win every battle, skip every offer, take the
/// event's loot. Shape-agnostic (it never hard-codes which room follows which).
void WalkToCamp(FSwapBoot& boot) {
    StartFresh(boot);
    for (int step = 0; step < 32; ++step) {
        game::FDungeonRun& run = boot.run();
        if (run.phase == game::EDungeonPhase::Camp) return;
        if (run.phase == game::EDungeonPhase::Battle) {
            CHECK(game::ResolveRoomVictory(boot.registry, true).has_value());
            continue;
        }
        if (run.phase == game::EDungeonPhase::Reward) {
            (void)game::ResolveRuneOffer(boot.registry, -1);  // the frozen offer table decides; none is fine
            CHECK(game::ApplyRunIntent(boot.registry, game::ERunIntent::ContinueReward).has_value());
            continue;
        }
        if (run.phase == game::EDungeonPhase::Event) {
            CHECK(game::ApplyRunIntent(boot.registry, game::ERunIntent::ChooseEventLoot).has_value());
            continue;
        }
        CHECK(false);  // a defeat inside a victory walk
        return;
    }
    CHECK(false);  // the walk never reached the camp
}

/// The rules cases' opening state: a started run parked at the camp. The save contract while the camp is
/// open is `cleared == depth` (FDungeonSave.cpp), so the in-memory phase change stays saveable.
void StartCamp(FSwapBoot& boot) {
    StartFresh(boot);
    game::FDungeonRun& run = boot.run();
    run.phase = game::EDungeonPhase::Camp;
    run.cleared = run.depth;
}

/// The first pool skill beyond the member's active slots: in the authored pool, not one of the defaults,
/// and different from every default slot (the default loadout is the pool's first N entries).
std::string SwapCandidate(entt::registry& registry, const game::FDungeonRun& run, std::size_t memberIndex) {
    CHECK(memberIndex < run.party.size());
    const std::vector<std::string> pool = game::MemberSkillPool(registry, run.party[memberIndex].id);
    const int slots = game::MemberActiveSkillSlots(registry, run.party[memberIndex].id);
    CHECK(static_cast<int>(pool.size()) > slots);
    const std::string candidate = pool[static_cast<std::size_t>(slots)];
    for (const game::FSkillSlot& slot : run.party[memberIndex].skills) {
        CHECK(slot.skillId != candidate);
    }
    return candidate;
}

/// Fitted runes over the whole party plus the collected pool: the swap moves runes between the two.
std::size_t RuneTotal(const game::FDungeonRun& run) {
    std::size_t total = run.runes.size();
    for (const game::FRunMember& member : run.party) {
        for (const game::FSkillSlot& slot : member.skills) total += slot.runeIds.size();
    }
    return total;
}

/// Runes with no tag requirement and no exclusion: legal on every skill, so a case can fit them without
/// pinning any skill's tags. Catalog order; the frozen fixture carries three.
std::vector<std::string> UnconditionalRunes(entt::registry& registry) {
    const auto& content = registry.ctx().get<game::gameplay::FContentRegistry>();
    std::vector<std::string> runes;
    for (const game::gameplay::FRuneContent& rune : content.runes.rows) {
        if (!rune.requiresAllTags.empty() || !rune.requiresAnyTags.empty() || !rune.excludesTags.empty()) continue;
        runes.push_back(rune.id);
    }
    return runes;
}

const game::FLoadoutMemberView* FindMemberView(const std::vector<game::FLoadoutMemberView>& members,
                                               std::string_view memberId) {
    for (const game::FLoadoutMemberView& view : members) {
        if (std::string_view(view.memberId) == memberId) return &view;
    }
    return nullptr;
}

const game::FLoadoutPoolEntry* FindPoolEntry(const std::vector<game::FLoadoutPoolEntry>& pool,
                                             std::string_view skillId) {
    for (const game::FLoadoutPoolEntry& entry : pool) {
        if (std::string_view(entry.skillId) == skillId) return &entry;
    }
    return nullptr;
}

int CountCampSpent(const game::FRunEventLog& log) {
    return static_cast<int>(std::count_if(log.tail.begin(), log.tail.end(), [](const game::FRunEvent& event) {
        return event.kind == game::ERunEventKind::CampSpent;
    }));
}

// ---------------------------------------------------------------------------------------------
// The real battle field (the camp/reward-test helper: FEmberBattleState::OnEnter's combat section)
// ---------------------------------------------------------------------------------------------

std::unique_ptr<game::FScene> FieldParty(FSwapBoot& boot) {
    auto& registry = boot.registry;
    const auto document = game::BuildDungeonBattlefield(registry);
    auto scene = std::make_unique<game::FScene>(registry, "LoadoutSwapTest");
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

// ---------------------------------------------------------------------------------------------
// The headless modal capture (the camp_tests RenderModalLogs pattern)
// ---------------------------------------------------------------------------------------------

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
        const std::string logPath = UniquePath("loadout-camp-ui") + ".log";
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
// The phase guard
// ---------------------------------------------------------------------------------------------

void SwapAtCampOnly() {
    FSwapBoot boot("swap-camp-only");
    entt::registry& registry = boot.registry;

    // No run at all: the first guard.
    const auto noRun = game::ValidateDungeonSkillSwap(registry, 0, 0, "skill.strike");
    CHECK(noRun.has_value());
    CHECK(*noRun == "Run state is unavailable.");

    StartFresh(boot);
    game::FDungeonRun& run = boot.run();
    const std::string original = run.party[0].skills[0].skillId;
    const std::string candidate = SwapCandidate(registry, run, 0);

    for (const game::EDungeonPhase phase : {game::EDungeonPhase::Battle, game::EDungeonPhase::Reward,
                                            game::EDungeonPhase::Event, game::EDungeonPhase::Defeat}) {
        run.phase = phase;
        const auto problem = game::ValidateDungeonSkillSwap(registry, 0, 0, candidate);
        CHECK(problem.has_value());
        CHECK(*problem == "Skills can be changed at camp only.");
        const auto applied = game::SwapDungeonSkill(registry, 0, 0, candidate);
        CHECK(applied.has_value());
        CHECK(*applied == "Skills can be changed at camp only.");
        // The run is untouched: the skill stays and no rune moves.
        CHECK(run.party[0].skills[0].skillId == original);
        CHECK(run.party[0].skills[0].runeIds.empty());
        CHECK(run.runes.empty());
    }

    // The positive control: the same request at the camp passes the phase guard.
    run.phase = game::EDungeonPhase::Camp;
    run.cleared = run.depth;
    CHECK(!game::ValidateDungeonSkillSwap(registry, 0, 0, candidate).has_value());
}

// ---------------------------------------------------------------------------------------------
// The pool rule
// ---------------------------------------------------------------------------------------------

void SwapPoolLimited() {
    FSwapBoot boot("swap-pool");
    CHECK(boot.InitMeta());
    StartCamp(boot);
    entt::registry& registry = boot.registry;
    game::FDungeonRun& run = boot.run();

    const game::FRunMember& member = run.party[0];
    const std::string original = member.skills[0].skillId;
    const std::vector<std::string> pool = game::MemberSkillPool(registry, member.id);
    const game::gameplay::FUnitContent* unit = game::ResolveRunMemberUnit(registry, member.id);
    CHECK(unit != nullptr);

    // A real skill the member never authored (another class's row), so this is not the unlock gate.
    const auto& content = registry.ctx().get<game::gameplay::FContentRegistry>();
    std::string foreign;
    for (const game::gameplay::FSkillContent& row : content.skills.rows) {
        if (std::ranges::find(unit->skillIds, row.id) == unit->skillIds.end()) {
            foreign = row.id;
            break;
        }
    }
    CHECK(!foreign.empty());
    CHECK(std::ranges::find(pool, foreign) == pool.end());

    const auto problem = game::ValidateDungeonSkillSwap(registry, 0, 0, foreign);
    CHECK(problem.has_value());
    CHECK(*problem == "That skill is not in this member's pool.");
    const auto applied = game::SwapDungeonSkill(registry, 0, 0, foreign);
    CHECK(applied.has_value());
    CHECK(*applied == "That skill is not in this member's pool.");

    // An id that is no content row at all is refused by the same rule.
    const auto ghost = game::ValidateDungeonSkillSwap(registry, 0, 0, "skill.ghost");
    CHECK(ghost.has_value());
    CHECK(*ghost == "That skill is not in this member's pool.");

    // Nothing changed.
    CHECK(run.party[0].skills[0].skillId == original);
    CHECK(run.party[0].skills[0].runeIds.empty());
}

void CampSwapCannotPickLockedSkill() {
    FSwapBoot boot("swap-locked-skill");
    entt::registry& registry = boot.registry;
    CHECK(boot.InitMeta());  // a real meta store: nothing bought yet
    StartCamp(boot);
    game::FDungeonRun& run = boot.run();

    // Derive the locked skill from the fixture catalog crossed with the authored pool: a kind=skill unlock
    // row that this party member's unit authors (today skill.shackle for party.warrior — never hard-coded).
    std::size_t memberIndex = run.party.size();
    std::string locked;
    for (std::size_t i = 0; i < run.party.size() && locked.empty(); ++i) {
        const game::gameplay::FUnitContent* unit = game::ResolveRunMemberUnit(registry, run.party[i].id);
        if (unit == nullptr) continue;
        for (const game::FMetaUnlockRow& row : game::MetaUnlockCatalog(registry)) {
            if (row.kind != "skill") continue;
            if (std::ranges::find(unit->skillIds, row.id) == unit->skillIds.end()) continue;
            if (game::HasMetaUnlock(registry, row.id)) continue;
            memberIndex = i;
            locked = row.id;
            break;
        }
    }
    CHECK(memberIndex < run.party.size());
    CHECK(!locked.empty());
    CHECK(!game::HasMetaUnlock(registry, locked));

    // Before the purchase the single pool rule hides it and the swap is refused.
    std::vector<std::string> pool = game::MemberSkillPool(registry, run.party[memberIndex].id);
    CHECK(std::ranges::find(pool, locked) == pool.end());
    const std::string original = run.party[memberIndex].skills[0].skillId;
    const auto problem = game::ValidateDungeonSkillSwap(registry, memberIndex, 0, locked);
    CHECK(problem.has_value());
    CHECK(*problem == "That skill is not in this member's pool.");
    const auto applied = game::SwapDungeonSkill(registry, memberIndex, 0, locked);
    CHECK(applied.has_value());
    CHECK(*applied == "That skill is not in this member's pool.");
    CHECK(run.party[memberIndex].skills[0].skillId == original);

    // After GrantMetaUnlock the same authored skill enters the pool and the swap is accepted.
    game::GrantMetaUnlock(registry, locked);
    CHECK(game::HasMetaUnlock(registry, locked));
    pool = game::MemberSkillPool(registry, run.party[memberIndex].id);
    CHECK(std::ranges::find(pool, locked) != pool.end());
    CHECK(!game::ValidateDungeonSkillSwap(registry, memberIndex, 0, locked).has_value());
    CHECK(!game::SwapDungeonSkill(registry, memberIndex, 0, locked).has_value());
    CHECK(run.party[memberIndex].skills[0].skillId == locked);
}

// ---------------------------------------------------------------------------------------------
// Rune return, same-skill and duplicate refusal
// ---------------------------------------------------------------------------------------------

void SwapReturnsRunesToPool() {
    FSwapBoot boot("swap-runes");
    StartCamp(boot);
    entt::registry& registry = boot.registry;
    game::FDungeonRun& run = boot.run();

    // Fit three universally legal runes into slot 0 through the real camp API.
    const std::vector<std::string> runes = UnconditionalRunes(registry);
    CHECK(runes.size() >= 3);
    run.runes = {runes[0], runes[1], runes[2]};
    for (int fitted = 0; fitted < 3; ++fitted) {
        const auto error = game::EquipDungeonRune(registry, 0, 0, 0);
        CHECK(!error.has_value());
    }
    CHECK(run.runes.empty());
    CHECK(run.party[0].skills[0].runeIds.size() == 3);
    const std::vector<std::string> fitted = run.party[0].skills[0].runeIds;

    // The pre-swap notice reads exactly the fitted count.
    const std::size_t predicted = game::SkillSwapReturnedRunes(registry, 0, 0);
    CHECK(predicted == fitted.size());
    CHECK(game::SkillSwapReturnedRunes(registry, run.party.size(), 0) == 0);   // unknown member
    CHECK(game::SkillSwapReturnedRunes(registry, 0, run.party[0].skills.size()) == 0);  // unknown slot

    const std::string original = run.party[0].skills[0].skillId;
    const std::string candidate = SwapCandidate(registry, run, 0);
    const std::size_t totalBefore = RuneTotal(run);
    CHECK(!game::SwapDungeonSkill(registry, 0, 0, candidate).has_value());

    // The slot holds the new skill with zero runes; every fitted rune is back in run.runes, in slot order.
    CHECK(run.party[0].skills[0].skillId == candidate);
    CHECK(run.party[0].skills[0].skillId != original);
    CHECK(run.party[0].skills[0].runeIds.empty());
    CHECK(run.runes == fitted);
    CHECK(RuneTotal(run) == totalBefore);
    CHECK(game::SkillSwapReturnedRunes(registry, 0, 0) == 0);
}

void SwapSameSkillRejected() {
    FSwapBoot boot("swap-same-skill");
    StartCamp(boot);
    entt::registry& registry = boot.registry;
    game::FDungeonRun& run = boot.run();

    // Give the slot a fitted rune so the refusal is visibly not a no-op that unsticks it.
    const std::vector<std::string> runes = UnconditionalRunes(registry);
    CHECK(!runes.empty());
    run.runes = {runes[0]};
    CHECK(!game::EquipDungeonRune(registry, 0, 1, 0).has_value());
    CHECK(run.party[0].skills[1].runeIds.size() == 1);
    const std::string current = run.party[0].skills[1].skillId;
    const std::size_t totalBefore = RuneTotal(run);

    const auto problem = game::ValidateDungeonSkillSwap(registry, 0, 1, current);
    CHECK(problem.has_value());
    CHECK(*problem == "That skill is already in this slot.");
    const auto applied = game::SwapDungeonSkill(registry, 0, 1, current);
    CHECK(applied.has_value());
    CHECK(*applied == "That skill is already in this slot.");
    CHECK(run.party[0].skills[1].skillId == current);
    CHECK(run.party[0].skills[1].runeIds.size() == 1);
    CHECK(RuneTotal(run) == totalBefore);
}

void SwapDuplicateRejected() {
    FSwapBoot boot("swap-duplicate");
    StartCamp(boot);
    entt::registry& registry = boot.registry;
    game::FDungeonRun& run = boot.run();

    const std::string duplicate = run.party[0].skills[1].skillId;
    const std::string original = run.party[0].skills[0].skillId;
    CHECK(duplicate != original);
    const std::vector<std::string> pool = game::MemberSkillPool(registry, run.party[0].id);
    CHECK(std::ranges::find(pool, duplicate) != pool.end());

    const auto problem = game::ValidateDungeonSkillSwap(registry, 0, 0, duplicate);
    CHECK(problem.has_value());
    CHECK(*problem == "That skill is already equipped in another slot.");
    const auto applied = game::SwapDungeonSkill(registry, 0, 0, duplicate);
    CHECK(applied.has_value());
    CHECK(*applied == "That skill is already equipped in another slot.");
    CHECK(run.party[0].skills[0].skillId == original);
    CHECK(run.party[0].skills[1].skillId == duplicate);
}

// ---------------------------------------------------------------------------------------------
// The next battle, the save round trip and the cost
// ---------------------------------------------------------------------------------------------

void SwapAppliesNextBattle() {
    FSwapBoot boot("swap-next-battle");
    WalkToCamp(boot);
    entt::registry& registry = boot.registry;
    game::FDungeonRun& run = boot.run();
    CHECK(run.phase == game::EDungeonPhase::Camp);

    const std::string candidate = SwapCandidate(registry, run, 0);
    CHECK(!game::SwapDungeonSkill(registry, 0, 0, candidate).has_value());
    CHECK(run.party[0].skills[0].skillId == candidate);

    // Leave the camp through the run flow: the next battle installs the swapped loadout.
    CHECK(game::ApplyRunIntent(registry, game::ERunIntent::LeaveCamp).has_value());
    CHECK(run.phase == game::EDungeonPhase::Battle);
    CHECK(run.depth == 2);

    auto scene = FieldParty(boot);
    game::ApplyRunParty(registry);
    const entt::entity member = FindEntity(registry, run.party[0].id);
    CHECK(member != entt::null);
    const auto& loadout = registry.get<game::FSkillLoadout>(member);
    CHECK(loadout.slots.size() == run.party[0].skills.size());
    CHECK(!loadout.slots.empty());
    CHECK(loadout.slots[0].skillId == candidate);
    (void)scene;
}

void SwapSaveRoundTrip() {
    FSwapBoot boot("swap-save");
    WalkToCamp(boot);
    entt::registry& registry = boot.registry;
    game::FDungeonRun& run = boot.run();
    CHECK(run.phase == game::EDungeonPhase::Camp);

    // One fitted rune returns to the collected pool with the swap, and both must survive the save.
    const std::vector<std::string> runes = UnconditionalRunes(registry);
    CHECK(!runes.empty());
    const std::string returnedRune = runes[0];
    run.runes = {returnedRune};
    CHECK(!game::EquipDungeonRune(registry, 0, 0, 0).has_value());
    CHECK(run.party[0].skills[0].runeIds.size() == 1);
    const std::string candidate = SwapCandidate(registry, run, 0);
    CHECK(!game::SwapDungeonSkill(registry, 0, 0, candidate).has_value());

    // The file itself carries the new skill and the returned rune (the normal skills/runes serialization).
    const std::string savePath = registry.ctx().get<game::FDungeonSave>().path;
    CHECK(!savePath.empty());
    CHECK(std::filesystem::exists(savePath));
    const json document = ReadJsonFile(savePath);
    bool memberFound = false;
    for (const json& entry : document.at("run").at("party")) {
        if (entry.at("id").get<std::string>() != run.party[0].id) continue;
        memberFound = true;
        CHECK(entry.at("skills").at(0).at("skill").get<std::string>() == candidate);
        CHECK(entry.at("skills").at(0).at("runes").empty());
    }
    CHECK(memberFound);
    CHECK(document.at("run").at("runes").size() == 1);
    CHECK(document.at("run").at("runes").at(0).get<std::string>() == returnedRune);

    // Continue re-validates every slot through SkillsFromJson: the swapped slot loads, the rune is collected.
    registry.ctx().erase<game::FDungeonRun>();
    CHECK(game::ContinueDungeonRun(registry));
    const game::FDungeonRun& resumed = boot.run();
    CHECK(resumed.phase == game::EDungeonPhase::Camp);
    CHECK(resumed.party[0].skills[0].skillId == candidate);
    CHECK(resumed.party[0].skills[0].runeIds.empty());
    CHECK(std::ranges::find(resumed.runes, returnedRune) != resumed.runes.end());
}

void SwapIsFree() {
    FSwapBoot boot("swap-free");
    CHECK(boot.InitMeta());
    StartCamp(boot);
    entt::registry& registry = boot.registry;
    game::FDungeonRun& run = boot.run();
    game::FCampProgress& progress = registry.ctx().get<game::FCampProgress>();
    progress.upgradesTotal = 2;
    progress.runeDrawsTotal = 1;
    progress.campDepth = static_cast<std::uint32_t>(run.depth);
    progress.runeDrawsThisCamp = 1;

    const int materials = run.materials;
    const game::FCampProgress before = progress;
    const game::FRunEventLog& log = registry.ctx().get<game::FRunEventLog>();
    const std::size_t logSize = log.tail.size();
    const int campSpentBefore = CountCampSpent(log);
    CHECK(campSpentBefore == 0);

    const std::string candidate = SwapCandidate(registry, run, 0);
    CHECK(!game::SwapDungeonSkill(registry, 0, 0, candidate).has_value());
    CHECK(run.party[0].skills[0].skillId == candidate);

    // D-17: the swap costs nothing and writes no camp-spend line.
    CHECK(run.materials == materials);
    CHECK(progress.upgradesTotal == before.upgradesTotal);
    CHECK(progress.runeDrawsTotal == before.runeDrawsTotal);
    CHECK(progress.campDepth == before.campDepth);
    CHECK(progress.runeDrawsThisCamp == before.runeDrawsThisCamp);
    CHECK(log.tail.size() == logSize);
    CHECK(CountCampSpent(log) == campSpentBefore);
}

// ---------------------------------------------------------------------------------------------
// The active slot contract
// ---------------------------------------------------------------------------------------------

void FourSlotsPreserved() {
    FSwapBoot boot("swap-four-slots");
    CHECK(boot.InitMeta());  // MemberActiveSkillSlots reads the progression file when meta is up
    StartCamp(boot);
    entt::registry& registry = boot.registry;
    game::FDungeonRun& run = boot.run();

    std::vector<std::size_t> before;
    for (const game::FRunMember& member : run.party) {
        const int slots = game::MemberActiveSkillSlots(registry, member.id);
        CHECK(slots == game::kBaseActiveSkillSlots);  // a fresh profile: four active slots
        CHECK(member.skills.size() == static_cast<std::size_t>(slots));
        before.push_back(member.skills.size());
    }
    CHECK(before.size() == game::kRunPartySize);

    for (std::size_t i = 0; i < run.party.size(); ++i) {
        const std::string candidate = SwapCandidate(registry, run, i);
        CHECK(!game::SwapDungeonSkill(registry, i, 0, candidate).has_value());
    }

    for (std::size_t i = 0; i < run.party.size(); ++i) {
        CHECK(run.party[i].skills.size() == before[i]);
        CHECK(run.party[i].skills.size() ==
              static_cast<std::size_t>(game::MemberActiveSkillSlots(registry, run.party[i].id)));
    }
}

// ---------------------------------------------------------------------------------------------
// The camp host glue
// ---------------------------------------------------------------------------------------------

void CampPanelDrivesSwap() {
    FSwapBoot boot("swap-panel");
    entt::registry& registry = boot.registry;
    const auto& content = registry.ctx().get<game::gameplay::FContentRegistry>();

    // Empty model without a run.
    const game::FCampLoadoutSelection noRunSelection{-1, -1};
    CHECK(game::BuildCampLoadoutModel(registry, noRunSelection).members.empty());

    StartCamp(boot);
    game::FCampLoadoutSelection& selection = game::CampLoadoutSelection(registry);
    CHECK(selection.member == -1 && selection.slot == -1);
    game::FDungeonRun& run = boot.run();

    // The model reflects the run: names, rune counts, pool order, and no problem before a selection.
    game::FLoadoutPanelModel model = game::BuildCampLoadoutModel(registry, selection);
    CHECK(model.members.size() == run.party.size());
    CHECK(model.selectedMember == -1 && model.selectedSlot == -1);
    for (std::size_t i = 0; i < run.party.size(); ++i) {
        const game::FLoadoutMemberView& view = model.members[i];
        CHECK(view.memberId == run.party[i].id);
        CHECK(view.name == game::MemberName(run.party[i]));
        CHECK(view.slots.size() == run.party[i].skills.size());
        for (std::size_t slot = 0; slot < view.slots.size(); ++slot) {
            const game::FSkillSlot& runSlot = run.party[i].skills[slot];
            CHECK(view.slots[slot].skillId == runSlot.skillId);
            CHECK(view.slots[slot].displayName ==
                  game::ContentDisplayName(content.skills.find(runSlot.skillId), runSlot.skillId));
            CHECK(view.slots[slot].runeCount == static_cast<int>(runSlot.runeIds.size()));
        }
        const std::vector<std::string> pool = game::MemberSkillPool(registry, run.party[i].id);
        CHECK(view.pool.size() == pool.size());
        for (std::size_t index = 0; index < pool.size(); ++index) {
            CHECK(view.pool[index].skillId == pool[index]);
            CHECK(view.pool[index].displayName ==
                  game::ContentDisplayName(content.skills.find(pool[index]), pool[index]));
            CHECK(view.pool[index].swapProblem.empty());  // no member/slot selected yet
        }
    }

    // An out-of-range stored selection is reported as -1 (the stored selection itself is not changed).
    const game::FCampLoadoutSelection stale{9, 9};
    const game::FLoadoutPanelModel staleModel = game::BuildCampLoadoutModel(registry, stale);
    CHECK(staleModel.selectedMember == -1 && staleModel.selectedSlot == -1);
    CHECK(stale.member == 9 && stale.slot == 9);

    // Selecting member 0 slot 0 fills swapProblem for that member only.
    selection.member = 0;
    selection.slot = 0;
    model = game::BuildCampLoadoutModel(registry, selection);
    CHECK(model.selectedMember == 0 && model.selectedSlot == 0);
    for (std::size_t i = 0; i < model.members.size(); ++i) {
        for (const game::FLoadoutPoolEntry& entry : model.members[i].pool) {
            const std::optional<std::string> expected =
                i == 0 ? game::ValidateDungeonSkillSwap(registry, 0, 0, entry.skillId)
                       : std::optional<std::string>{};
            CHECK(entry.swapProblem == expected.value_or(""));
        }
    }
    // equippedElsewhere marks a skill sitting in the member's other slots; a pool-only skill is not marked.
    const std::string slotZero = run.party[0].skills[0].skillId;
    const std::string slotOne = run.party[0].skills[1].skillId;
    const std::string poolOnly = SwapCandidate(registry, run, 0);
    const game::FLoadoutPoolEntry* elsewhere = FindPoolEntry(model.members[0].pool, slotOne);
    const game::FLoadoutPoolEntry* notEquipped = FindPoolEntry(model.members[0].pool, poolOnly);
    CHECK(elsewhere != nullptr && elsewhere->equippedElsewhere);
    CHECK(notEquipped != nullptr && !notEquipped->equippedElsewhere);
    CHECK(FindPoolEntry(model.members[0].pool, slotZero) != nullptr);

    // An empty newSkillId only moves the selection; the run is untouched.
    const std::string untouchedSkill = run.party[0].skills[0].skillId;
    const int materials = run.materials;
    CHECK(!game::ApplyCampLoadoutEdit(registry, selection, game::FLoadoutEdit{1, 2, ""}).has_value());
    CHECK(selection.member == 1 && selection.slot == 2);
    CHECK(run.party[0].skills[0].skillId == untouchedSkill);
    CHECK(run.materials == materials);

    // A skill request applies through FLoadoutSwap: the run changes and the host sets run.message.
    const std::string candidate = SwapCandidate(registry, run, 1);
    run.message.clear();
    CHECK(!game::ApplyCampLoadoutEdit(registry, selection, game::FLoadoutEdit{1, 2, candidate}).has_value());
    CHECK(run.party[1].skills[2].skillId == candidate);
    CHECK(!run.message.empty());
    CHECK(selection.member == 1 && selection.slot == 2);  // a successful swap keeps the selection

    // A refused request reports the reason and prefixes the run message. The overlay authors class-foreign
    // ids into every pool, so derive a skill outside THIS member's pool instead of borrowing another's.
    const std::vector<std::string> pool = game::MemberSkillPool(registry, run.party[1].id);
    std::string rejected;
    for (const game::gameplay::FSkillContent& row : content.skills.rows) {
        if (std::ranges::find(pool, row.id) == pool.end()) {
            rejected = row.id;
            break;
        }
    }
    CHECK(!rejected.empty());
    const auto refusal = game::ApplyCampLoadoutEdit(registry, selection, game::FLoadoutEdit{1, 2, rejected});
    CHECK(refusal.has_value());
    CHECK(*refusal == "That skill is not in this member's pool.");
    CHECK(run.message == "Cannot swap skill: " + *refusal);
    CHECK(run.party[1].skills[2].skillId == candidate);

    // The camp view hosts the panel: it draws the model and routes edits through the host glue.
    const std::string campView = ReadTextFile("src/presentation/FDungeonCampView.cpp");
    CHECK(campView.find("DrawLoadoutPanel(") != std::string::npos);
    CHECK(campView.find("ApplyCampLoadoutEdit(") != std::string::npos);
    CHECK(campView.find("SwapDungeonSkill(") == std::string::npos);
}

void PanelIsDisplayOnly() {
    // The panel is a pure display: no swap rule, no registry, no content lookup in its translation unit.
    const std::string panel = ReadTextFile("src/presentation/FLoadoutPanel.cpp");
    CHECK(!panel.empty());
    CHECK(panel.find("SwapDungeonSkill") == std::string::npos);
    CHECK(panel.find("ValidateDungeonSkillSwap") == std::string::npos);
    CHECK(panel.find("registry") == std::string::npos);
    CHECK(panel.find("entt") == std::string::npos);
    CHECK(panel.find("FContentRegistry") == std::string::npos);
}

// ---------------------------------------------------------------------------------------------
// The pool = preparation choice set (one rule, no duplicate derivation)
// ---------------------------------------------------------------------------------------------

void MemberSkillPoolMatchesPreparation() {
    FSwapBoot boot("swap-pool-preparation");
    CHECK(boot.InitMeta());  // the frozen fixture's unlock gates, as the real boot sees them
    StartCamp(boot);
    entt::registry& registry = boot.registry;
    game::FDungeonRun& run = boot.run();
    const auto& content = registry.ctx().get<game::gameplay::FContentRegistry>();

    game::FPreparationDraft draft = game::MakeDefaultDraft(registry, game::FRunStartRequest{true, -1});
    CHECK(draft.setup.party.size() == game::kRunPartySize);
    const game::FLoadoutPanelModel model =
        game::BuildCampLoadoutModel(registry, game::FCampLoadoutSelection{-1, -1});
    CHECK(model.members.size() == draft.setup.party.size());

    for (std::size_t i = 0; i < draft.setup.party.size(); ++i) {
        const std::string& memberId = draft.setup.party[i];
        const game::FLoadoutMemberView* view = FindMemberView(model.members, memberId);
        CHECK(view != nullptr);
        const std::vector<std::string> pool = game::MemberSkillPool(registry, memberId);

        // The panel's pool is the one rules pool, in the same order.
        CHECK(view->pool.size() == pool.size());
        for (std::size_t index = 0; index < pool.size(); ++index) {
            CHECK(view->pool[index].skillId == pool[index]);
            // ...and the preparation editor accepts exactly that set on a default draft.
            CHECK(game::SetDraftSkill(registry, draft, i, 0, pool[index]));
        }

        // A skill outside the pool is refused by both views of the same rule.
        std::string outside;
        for (const game::gameplay::FSkillContent& row : content.skills.rows) {
            if (std::ranges::find(pool, row.id) == pool.end()) {
                outside = row.id;
                break;
            }
        }
        CHECK(!outside.empty());
        CHECK(!game::SetDraftSkill(registry, draft, i, 0, outside));
        CHECK(std::ranges::find(pool, outside) == pool.end());
    }
}

// ---------------------------------------------------------------------------------------------
// The shipped pool shape — the ONLY case that reads the shipped content. The camp panel's pool is a
// shipped-game promise: every default party member must fill its active slots (the shape the roster boot
// validation enforces), so a content edit that trims a pool fails here instead of silently starving the
// camp panel. Every other case in this suite is pinned to the frozen fixture.
// ---------------------------------------------------------------------------------------------

void ShipPoolsFillActiveSlots() {
    FSwapBoot boot("swap-ship-pools", ESwapContent::Shipped);
    CHECK(boot.InitMeta());
    const game::FPreparationDraft draft =
        game::MakeDefaultDraft(boot.registry, game::FRunStartRequest{true, -1});
    CHECK(draft.setup.party.size() == game::kRunPartySize);
    for (const std::string& memberId : draft.setup.party) {
        const int slots = game::MemberActiveSkillSlots(boot.registry, memberId);
        const std::vector<std::string> pool = game::MemberSkillPool(boot.registry, memberId);
        if (static_cast<int>(pool.size()) < slots) {
            std::cout << "[report] shipped pool " << memberId << " holds " << pool.size() << " of " << slots
                      << " active slots\n";
        }
        CHECK(static_cast<int>(pool.size()) >= slots);
    }
}

// ---------------------------------------------------------------------------------------------
// Rune sockets stay skill-bound across several swaps
// ---------------------------------------------------------------------------------------------

void RuneSocketsStaySkillBound() {
    FSwapBoot boot("swap-rune-invariant");
    StartCamp(boot);
    entt::registry& registry = boot.registry;
    game::FDungeonRun& run = boot.run();

    // Fit one rune into slot 0 of three members.
    const std::vector<std::string> runes = UnconditionalRunes(registry);
    CHECK(runes.size() >= 3);
    run.runes = {runes[0], runes[1], runes[2]};
    for (std::size_t member = 0; member < 3; ++member) {
        CHECK(!game::EquipDungeonRune(registry, member, 0, 0).has_value());
        CHECK(run.party[member].skills[0].runeIds.size() == 1);
    }
    CHECK(run.runes.empty());
    const std::size_t before = RuneTotal(run);
    CHECK(before == 3);

    // Several swaps across the party: every returned rune must land in the pool, none may vanish.
    for (std::size_t member = 0; member < run.party.size(); ++member) {
        const std::string candidate = SwapCandidate(registry, run, member);
        CHECK(!game::SwapDungeonSkill(registry, member, 0, candidate).has_value());
        CHECK(RuneTotal(run) == before);
    }

    std::size_t fitted = 0;
    for (const game::FRunMember& member : run.party) {
        for (const game::FSkillSlot& slot : member.skills) fitted += slot.runeIds.size();
    }
    CHECK(run.runes.size() + fitted == before);
}

// ---------------------------------------------------------------------------------------------
// The camp modal renders the panel
// ---------------------------------------------------------------------------------------------

void CampModalShowsLoadoutPanel() {
    FSwapBoot boot("swap-camp-modal");
    WalkToCamp(boot);
    entt::registry& registry = boot.registry;
    game::FDungeonRun& run = boot.run();
    CHECK(run.phase == game::EDungeonPhase::Camp);

    const auto& content = registry.ctx().get<game::gameplay::FContentRegistry>();
    const std::vector<std::string> logs = RenderModalLogs(registry, {std::string(game::kRunModalCamp)});
    CHECK(logs.size() == 1u);
    const std::string& log = logs[0];

    // Diagnostics before the assertions (a failing CHECK throws the rest of the case away).
    if (log.find("LOADOUT") == std::string::npos) {
        std::cout << "[report] camp-ui.log bytes=" << log.size() << " content=[" << log << "]\n";
    }
    CHECK(log.find("LOADOUT") != std::string::npos);
    for (const game::FRunMember& member : run.party) {
        for (const game::FSkillSlot& slot : member.skills) {
            const auto* skill = content.skills.find(slot.skillId);
            CHECK(skill != nullptr);
            const std::string name = game::ContentDisplayName(skill, slot.skillId);
            if (log.find(name) == std::string::npos) {
                std::cout << "[report] camp-ui.log misses the slot name '" << name << "'\n";
            }
            CHECK(log.find(name) != std::string::npos);
        }
    }
}

} // namespace

int main() {
    std::cout.setf(std::ios::unitbuf);  // a crash must still show how far the suite got
    // CTest sets GAME01P_SAVE_DIR per feature test; a direct run does not. Fall back to out/test-saves so
    // a run can never touch %APPDATA%\Game01P\EmberDungeon.
    const char* saveDir = nullptr;
#ifdef _MSC_VER
#pragma warning(suppress: 4996)  // std::getenv: read-only, no buffer to overflow
#endif
    saveDir = std::getenv("GAME01P_SAVE_DIR");
    if (saveDir == nullptr || *saveDir == '\0') {
        const std::string path =
            (std::filesystem::current_path() / "out" / "test-saves" / "loadout_swap_tests").string();
        std::error_code error;
        std::filesystem::create_directories(path, error);
        _putenv_s("GAME01P_SAVE_DIR", path.c_str());
    }

    return game_test::RunTests("loadout_swap", {
        {"SwapAtCampOnly", SwapAtCampOnly},
        {"SwapPoolLimited", SwapPoolLimited},
        {"CampSwapCannotPickLockedSkill", CampSwapCannotPickLockedSkill},
        {"SwapReturnsRunesToPool", SwapReturnsRunesToPool},
        {"SwapSameSkillRejected", SwapSameSkillRejected},
        {"SwapDuplicateRejected", SwapDuplicateRejected},
        {"SwapAppliesNextBattle", SwapAppliesNextBattle},
        {"SwapSaveRoundTrip", SwapSaveRoundTrip},
        {"SwapIsFree", SwapIsFree},
        {"FourSlotsPreserved", FourSlotsPreserved},
        {"CampPanelDrivesSwap", CampPanelDrivesSwap},
        {"PanelIsDisplayOnly", PanelIsDisplayOnly},
        {"MemberSkillPoolMatchesPreparation", MemberSkillPoolMatchesPreparation},
        {"ShipPoolsFillActiveSlots", ShipPoolsFillActiveSlots},
        {"RuneSocketsStaySkillBound", RuneSocketsStaySkillBound},
        {"CampModalShowsLoadoutPanel", CampModalShowsLoadoutPanel},
    });
}