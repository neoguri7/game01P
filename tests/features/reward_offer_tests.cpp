// WP-17: the rune reward offer (3 cards + skip), pity, duplicate refunds, the `offers` save section and the
// forecast line. The offer is a decision the run flow enforces: pick/skip never move the phase, and
// ContinueReward refuses to advance while an offer is open.
//
// Every test boots the real composition root against the frozen fixture (tests/fixtures/assets), whose
// rewards.json is chance 1.0 / pity 0 so a test that wants a miss, a pity force or a different policy
// overrides the loaded tables in place instead of editing files.

#define GAME01P_TEST_SUPPORT_CHECK
#include "../TestSupport.h"

#include <array>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <iterator>
#include <memory>
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
#include "gameplay/data/FContentRegistry.h"
#include "gameplay/dungeon/FDungeonSave.h"
#include "gameplay/dungeon/FRewards.h"
#include "gameplay/dungeon/FRunFlow.h"
#include "presentation/FDungeonArt.h"
#include "presentation/FDungeonOverlay.h"
#include "presentation/FModalViewRegistry.h"

namespace {

using nlohmann::json;

constexpr std::uint32_t kSeed = 4242;

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

    game::FRewards& rewards() { return registry.ctx().get<game::FRewards>(); }
    game::FDungeonRun& run() { return registry.ctx().get<game::FDungeonRun>(); }
};

// --- a temporary asset root (broken/missing rewards.json, illegal rune content) ------------------

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

    /// Copies one fixture data file into the temp root.
    void Copy(const std::string& file) {
        std::error_code error;
        std::filesystem::copy_file(
            FixtureDataPath(file), root / "data" / file, std::filesystem::copy_options::overwrite_existing, error);
        CHECK(!error);
    }

    /// The tables gameplay needs to boot: content + AI profiles (+ the dungeon for a run).
    void CopyGameplayData(bool withDungeon = false) {
        for (const char* file : {"units.json", "skills.json", "runes.json", "statuses.json", "ai.json"}) Copy(file);
        if (withDungeon) {
            Copy("dungeon.json");
            Copy("passive-tree.json");
        }
    }

    void Write(const std::string& file, const json& document) {
        std::ofstream out(root / "data" / file, std::ios::trunc);
        CHECK(out.good());
        out << document.dump(2);
    }
};

/// Boots only the gameplay services (no dungeon run): false means the loader rejected something.
bool BootsGameplay(const std::vector<std::string>& roots) {
    entt::registry registry;
    game::FAssetManager::Initialize(registry, roots);
    return game::gameplay::FGameplayServices::Initialize(registry);
}

// --- shared steps -------------------------------------------------------------------------------

void StartFresh(FBoot& boot) {
    game::StartRun(boot.registry, kSeed, true);
}

/// A battle victory: the run is in the Reward phase and the room's offer decision has already run.
void WinRoom(FBoot& boot) {
    CHECK(game::ResolveRoomVictory(boot.registry, true).has_value());
    CHECK(boot.run().phase == game::EDungeonPhase::Reward);
}

/// Every rune the loaded party can legally socket right now, in content order.
std::vector<std::string> LegalRuneIds(FBoot& boot) {
    const auto& content = boot.registry.ctx().get<game::gameplay::FContentRegistry>();
    std::vector<std::string> legal;
    for (const auto& rune : content.runes.rows) {
        if (!game::RuneFittingSkillNames(boot.registry, rune.id).empty()) legal.push_back(rune.id);
    }
    return legal;
}

/// Marks every legal rune except the last `keepUnowned` ones as collected, shrinking the offer pool.
void OwnLegalRunes(FBoot& boot, std::size_t keepUnowned) {
    const auto legal = LegalRuneIds(boot);
    CHECK(legal.size() > keepUnowned);
    for (std::size_t index = 0; index + keepUnowned < legal.size(); ++index) {
        boot.run().runes.push_back(legal[index]);
    }
}

bool AllCandidatesLegal(FBoot& boot) {
    for (const auto& candidate : boot.rewards().offer.candidates) {
        if (game::RuneFittingSkillNames(boot.registry, candidate).empty()) return false;
    }
    return true;
}

/// The shipped offer values from assets/data/rewards.json (OfferCountMatchesArithmetic runs on these, not on
/// the fixture's deterministic ones).
void ApplyShippedOfferTables(FBoot& boot) {
    const auto document = ReadJsonFile("assets/data/rewards.json");
    const auto& offer = document.at("rune_offer");
    auto& tables = boot.rewards().tables;
    tables.count = offer.at("count").get<int>();
    tables.chance = offer.at("chance").get<double>();
    tables.pityThreshold = offer.at("pity_threshold").get<int>();
    tables.minLegalCandidates = offer.at("min_legal_candidates").get<int>();
    tables.skipRefundMaterials = offer.at("skip_refund_materials").get<int>();
    tables.duplicateRefundMaterials = offer.at("duplicate_refund_materials").get<int>();
    tables.duplicatePolicy = offer.at("duplicate_policy").get<std::string>();
    tables.bossOffer = document.at("boss_offer").get<std::string>();
}

// --- UI capture (the forecast/duplicate card tests read the real views) -------------------------

/// Renders each registered modal view into an ImGui log and returns one captured text per id. The SDL/ImGui
/// stack is built exactly like DungeonTests' UI capture; the renderer lives only for this call.
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
        const std::string logPath = UniquePath("reward-ui") + ".log";
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

// --- determinism and the draw cursor ------------------------------------------------------------

void OfferIsDeterministic() {
    FBoot first(UniquePath("reward-det-a"));
    FBoot second(UniquePath("reward-det-b"));
    StartFresh(first);
    StartFresh(second);
    WinRoom(first);
    WinRoom(second);
    CHECK(first.rewards().offer.active);
    CHECK(!first.rewards().offer.candidates.empty());
    CHECK(first.rewards().offer.candidates == second.rewards().offer.candidates);
    CHECK(first.rewards().nextDrawIndex == second.rewards().nextDrawIndex);
    CHECK(first.rewards().pity == second.rewards().pity);
}

void CampPurchaseAdvancesDraw() {
    FBoot boot(UniquePath("reward-draw"));
    StartFresh(boot);
    const auto before = boot.rewards().nextDrawIndex;
    const auto first = game::RollRuneCandidates(boot.registry);
    CHECK(boot.rewards().nextDrawIndex == before + 1);
    const auto second = game::RollRuneCandidates(boot.registry);
    CHECK(boot.rewards().nextDrawIndex == before + 2);
    CHECK(!first.empty() && !second.empty());
    // A camp purchase sees a different draw of the same room: the cursor is part of the seed grid.
    CHECK(boot.rewards().nextDrawIndex == 2u);
}

void PeekDoesNotAdvance() {
    FBoot boot(UniquePath("reward-peek"));
    StartFresh(boot);
    const auto before = boot.rewards().nextDrawIndex;
    const auto peek = game::PeekRuneCandidates(boot.registry);
    CHECK(boot.rewards().nextDrawIndex == before);
    CHECK(game::PeekRuneCandidates(boot.registry) == peek);
    const auto rolled = game::RollRuneCandidates(boot.registry);
    CHECK(boot.rewards().nextDrawIndex == before + 1);
    CHECK(rolled == peek);  // the preview is exactly the next draw
}

// --- pity ---------------------------------------------------------------------------------------

void PityForcesOffer() {
    FBoot boot(UniquePath("reward-pity-one"));
    StartFresh(boot);
    auto& rewards = boot.rewards();
    rewards.tables.chance = 0.0;  // the roll can never hit: only pity can open an offer
    rewards.tables.pityThreshold = 1;

    game::RollRoomOffer(boot.registry, false);
    CHECK(!rewards.offer.active);
    CHECK(rewards.pity == 1);

    game::RollRoomOffer(boot.registry, false);  // forced by the streak
    CHECK(rewards.offer.active);
    CHECK(rewards.pity == 0);  // an offer resets the streak
}

void ChanceZeroStillPities() {
    FBoot boot(UniquePath("reward-pity-two"));
    StartFresh(boot);
    auto& rewards = boot.rewards();
    rewards.tables.chance = 0.0;
    rewards.tables.pityThreshold = 2;

    game::RollRoomOffer(boot.registry, false);
    game::RollRoomOffer(boot.registry, false);
    CHECK(!rewards.offer.active);
    CHECK(rewards.pity == 2);
    CHECK(game::RuneOfferForecastOf(boot.registry).guaranteed);

    game::RollRoomOffer(boot.registry, false);
    CHECK(rewards.offer.active);
    CHECK(rewards.pity == 0);
}

void OfferCountMatchesArithmetic() {
    FBoot boot(UniquePath("reward-count"));
    ApplyShippedOfferTables(boot);

    constexpr int kSeeds = 60;
    constexpr int kRooms = 3;
    int totalOffers = 0;
    int smallest = kRooms;
    int largest = 0;
    for (int seedIndex = 0; seedIndex < kSeeds; ++seedIndex) {
        game::StartRun(boot.registry, static_cast<std::uint32_t>(1000 + seedIndex), true);
        int offers = 0;
        for (int room = 0; room < kRooms; ++room) {
            boot.run().room = room;
            game::RollRoomOffer(boot.registry, room == kRooms - 1);
            if (boot.rewards().offer.active) {
                ++offers;
                CHECK(!boot.rewards().offer.candidates.empty());
                CHECK(game::ResolveRuneOffer(boot.registry, -1));  // clear the room for the next decision
            }
        }
        totalOffers += offers;
        smallest = std::min(smallest, offers);
        largest = std::max(largest, offers);
    }
    // Derived from the data, not fixed counts: two monster rooms roll `chance`, the boss room is guaranteed.
    const double chance = boot.rewards().tables.chance;
    const double expected = 2.0 * chance + 1.0;
    const double mean = static_cast<double>(totalOffers) / kSeeds;
    std::cout << "  offers over " << kSeeds << " seeds: mean " << mean << " (expected " << expected << ") min "
              << smallest << " max " << largest << '\n';
    CHECK(smallest >= 1);  // the guaranteed boss offer
    CHECK(largest <= kRooms);
    CHECK(mean > expected - 0.4 && mean < expected + 0.4);
}

// --- refunds and duplicates ---------------------------------------------------------------------

void SkipRefundsMaterials() {
    FBoot boot(UniquePath("reward-skip"));
    StartFresh(boot);
    WinRoom(boot);
    auto& rewards = boot.rewards();
    CHECK(rewards.offer.active);
    const int materials = boot.run().materials;
    const auto runes = boot.run().runes;

    CHECK(game::ResolveRuneOffer(boot.registry, -1));
    CHECK(boot.run().materials == materials + rewards.tables.skipRefundMaterials);
    CHECK(boot.run().runes == runes);
    CHECK(!rewards.offer.active);
    CHECK(boot.run().phase == game::EDungeonPhase::Reward);  // the skip never advances the room
}

void DuplicateRefundsByDefault() {
    FBoot boot(UniquePath("reward-duplicate"));
    StartFresh(boot);
    WinRoom(boot);
    auto& rewards = boot.rewards();
    CHECK(rewards.tables.duplicatePolicy == game::kRewardDuplicatePolicyRefund);
    CHECK(!rewards.offer.candidates.empty());
    const std::string duplicate = rewards.offer.candidates.front();
    boot.run().runes.push_back(duplicate);
    const int materials = boot.run().materials;
    const auto runes = boot.run().runes;

    CHECK(game::ResolveRuneOffer(boot.registry, 0));
    CHECK(boot.run().materials == materials + rewards.tables.duplicateRefundMaterials);
    CHECK(boot.run().runes == runes);  // the refund policy never adds a copy
}

void DuplicateAllowAddsCopy() {
    FBoot boot(UniquePath("reward-duplicate-allow"));
    StartFresh(boot);
    WinRoom(boot);
    auto& rewards = boot.rewards();
    rewards.tables.duplicatePolicy = game::kRewardDuplicatePolicyAllow;
    CHECK(!rewards.offer.candidates.empty());
    const std::string duplicate = rewards.offer.candidates.front();
    boot.run().runes.push_back(duplicate);
    const int materials = boot.run().materials;
    const std::size_t owned = boot.run().runes.size();

    CHECK(game::ResolveRuneOffer(boot.registry, 0));
    CHECK(boot.run().materials == materials);
    CHECK(boot.run().runes.size() == owned + 1);
    CHECK(boot.run().runes.back() == duplicate);
}

void DuplicateCardShowsRefund() {
    FBoot boot(UniquePath("reward-duplicate-card"));
    StartFresh(boot);
    WinRoom(boot);
    auto& rewards = boot.rewards();
    CHECK(rewards.offer.active);
    CHECK(!rewards.offer.candidates.empty());
    boot.run().runes.push_back(rewards.offer.candidates.front());

    const auto log = RenderModalLogs(boot.registry, {std::string(game::kRunModalReward)});
    CHECK(log.size() == 1u);
    CHECK(log[0].find("Already owned") != std::string::npos);
    CHECK(log[0].find("+" + std::to_string(rewards.tables.duplicateRefundMaterials) + " materials") !=
          std::string::npos);
    CHECK(log[0].find("Fits:") != std::string::npos);
    CHECK(log[0].find("Skip (+" + std::to_string(rewards.tables.skipRefundMaterials) + " materials)") !=
          std::string::npos);
}

// --- the save section ---------------------------------------------------------------------------

void SaveRestoresOffer() {
    FBoot boot(UniquePath("reward-save"));
    StartFresh(boot);
    WinRoom(boot);
    const auto candidates = boot.rewards().offer.candidates;
    const auto drawIndex = boot.rewards().nextDrawIndex;
    CHECK(!candidates.empty() && drawIndex > 0);

    boot.registry.ctx().erase<game::FDungeonRun>();
    CHECK(game::ContinueDungeonRun(boot.registry));
    const auto& rewards = boot.registry.ctx().get<game::FRewards>();
    CHECK(rewards.offer.active);
    CHECK(rewards.offer.candidates == candidates);
    CHECK(rewards.nextDrawIndex == drawIndex);
    CHECK(boot.run().phase == game::EDungeonPhase::Reward);
    CHECK(boot.run().room == 0);

    // A dry streak is part of the same section: a miss survives the save round-trip too.
    CHECK(game::ResolveRuneOffer(boot.registry, -1));
    auto& live = boot.registry.ctx().get<game::FRewards>();
    live.tables.chance = 0.0;
    live.tables.pityThreshold = 3;
    game::RollRoomOffer(boot.registry, false);
    CHECK(!live.offer.active && live.pity == 1);
    CHECK(game::SaveDungeonRun(boot.registry));
    boot.registry.ctx().erase<game::FDungeonRun>();
    CHECK(game::ContinueDungeonRun(boot.registry));
    const auto& restored = boot.registry.ctx().get<game::FRewards>();
    CHECK(!restored.offer.active);
    CHECK(restored.pity == 1);
}

void CorruptOfferDoesNotBrickSave() {
    FBoot boot(UniquePath("reward-corrupt"));
    StartFresh(boot);
    WinRoom(boot);
    CHECK(boot.rewards().offer.active);

    auto document = ReadJsonFile(boot.runSave);
    document["run"]["ext"]["offers"]["offer"]["candidates"] = json::array({"rune.does_not_exist"});
    WriteJsonFile(boot.runSave, document);

    boot.registry.ctx().erase<game::FDungeonRun>();
    CHECK(game::ContinueDungeonRun(boot.registry));  // a bad rune id drops the offer, never the run
    CHECK(!boot.registry.ctx().get<game::FRewards>().offer.active);
    CHECK(boot.run().phase == game::EDungeonPhase::Reward);
    // The run plays on: with no offer open the reward step advances again.
    CHECK(game::ApplyRunIntent(boot.registry, game::ERunIntent::ContinueReward).has_value());
    CHECK(boot.run().phase == game::EDungeonPhase::Event);
}

void NewRunResetsOfferState() {
    FBoot boot(UniquePath("reward-new-run"));
    StartFresh(boot);
    WinRoom(boot);
    auto& rewards = boot.rewards();
    CHECK(rewards.offer.active);
    rewards.pity = 2;

    game::StartRun(boot.registry, kSeed, true);  // the same seed must still start clean
    CHECK(!rewards.offer.active);
    CHECK(rewards.offer.candidates.empty());
    CHECK(rewards.pity == 0);
    CHECK(rewards.nextDrawIndex == 0u);
    CHECK(rewards.runSeed == kSeed);
}

// --- the legality gate --------------------------------------------------------------------------

void MinLegalGate() {
    FBoot boot(UniquePath("reward-min-legal"));
    StartFresh(boot);
    OwnLegalRunes(boot, 1);  // pool = one unowned rune + at most one owned duplicate = 2
    auto& rewards = boot.rewards();
    rewards.tables.count = 3;
    rewards.tables.minLegalCandidates = 3;

    game::RollRoomOffer(boot.registry, false);
    CHECK(!rewards.offer.active);
    CHECK(rewards.pity == 0);  // a short pool is not a dry streak

    rewards.tables.minLegalCandidates = 2;
    game::RollRoomOffer(boot.registry, false);
    CHECK(rewards.offer.active);
}

void OfferSizeIsMinCountLegalPool() {
    FBoot boot(UniquePath("reward-offer-size"));
    StartFresh(boot);
    OwnLegalRunes(boot, 1);
    auto& rewards = boot.rewards();
    rewards.tables.count = 3;
    rewards.tables.minLegalCandidates = 2;

    game::RollRoomOffer(boot.registry, false);
    CHECK(rewards.offer.active);
    CHECK(rewards.offer.candidates.size() == 2u);  // min(count 3, pool 2)
}

void OwnedDuplicateAtMostOne() {
    FBoot boot(UniquePath("reward-duplicate-limit"));
    StartFresh(boot);
    OwnLegalRunes(boot, 0);  // every legal rune is already collected
    auto& rewards = boot.rewards();
    rewards.tables.minLegalCandidates = 1;  // observe the pool composition itself, not the default gate

    game::RollRoomOffer(boot.registry, false);
    CHECK(rewards.offer.active);
    CHECK(rewards.offer.candidates.size() <= 1u);
    for (const auto& candidate : rewards.offer.candidates) CHECK(game::IsRuneOwned(boot.registry, candidate));
}

void UnownedFirstWhenPoolIsDeep() {
    FBoot boot(UniquePath("reward-unowned-first"));
    StartFresh(boot);
    OwnLegalRunes(boot, 3);  // still deep enough: the offer count is 3, so every card could be new
    auto& run = boot.run();
    auto& rewards = boot.rewards();
    for (std::uint32_t seedIndex = 0; seedIndex < 20; ++seedIndex) {
        // A different run seed resets the streak lazily, so every draw in the grid is a fresh offer.
        run.seed = 9000u + seedIndex;
        game::RollRoomOffer(boot.registry, false);
        CHECK(rewards.offer.active);
        CHECK(rewards.offer.candidates.size() == 3u);
        for (const auto& candidate : rewards.offer.candidates) CHECK(!game::IsRuneOwned(boot.registry, candidate));
        CHECK(game::ResolveRuneOffer(boot.registry, -1));  // skip: the collection stays at 3 owned runes
    }
}

void MixedPoolFillsOneOwnedCard() {
    FBoot boot(UniquePath("reward-mixed-pool"));
    StartFresh(boot);
    const std::size_t legal = LegalRuneIds(boot).size();
    CHECK(legal >= 5u);
    OwnLegalRunes(boot, 2);  // all but two legal runes are collected: the unowned pool is one card short
    auto& rewards = boot.rewards();

    game::RollRoomOffer(boot.registry, false);
    CHECK(rewards.offer.active);
    CHECK(rewards.offer.candidates.size() == 3u);
    // The unowned cards fill the first slots; the single owned duplicate is always appended last.
    CHECK(!game::IsRuneOwned(boot.registry, rewards.offer.candidates[0]));
    CHECK(!game::IsRuneOwned(boot.registry, rewards.offer.candidates[1]));
    CHECK(game::IsRuneOwned(boot.registry, rewards.offer.candidates[2]));
}

void OffersMostlyLegal() {
    FBoot boot(UniquePath("reward-legal-grid"));
    ApplyShippedOfferTables(boot);
    int offers = 0;
    for (int seedIndex = 0; seedIndex < 20; ++seedIndex) {
        game::StartRun(boot.registry, static_cast<std::uint32_t>(500 + seedIndex), true);
        WinRoom(boot);
        if (!boot.rewards().offer.active) continue;
        ++offers;
        CHECK(AllCandidatesLegal(boot));
        CHECK(!boot.rewards().offer.candidates.empty());
    }
    CHECK(offers > 0);
}

void EmptyLegalPoolDeactivatesOffer() {
    FTempAssets assets("reward-empty-pool");
    assets.CopyGameplayData(true);
    auto runes = ReadJsonFile(FixtureDataPath("runes.json"));
    runes["runes"] = json::array({json{{"id", "rune.impossible"}, {"display_name", "Impossible"},
        {"requires_all_tags", json::array({"NoSkillHasThisTag"})}}});
    assets.Write("runes.json", runes);
    auto dungeon = ReadJsonFile(FixtureDataPath("dungeon.json"));
    dungeon["rune_drops"] = json::array({"rune.impossible"});
    assets.Write("dungeon.json", dungeon);
    assets.Copy("rewards.json");

    FBoot boot(UniquePath("reward-empty-pool"), {assets.root.string()});
    StartFresh(boot);
    auto& rewards = boot.rewards();
    rewards.tables.chance = 1.0;
    game::RollRoomOffer(boot.registry, false);
    CHECK(!rewards.offer.active);
    CHECK(rewards.pity == 0);
    CHECK(game::RuneFittingSkillNames(boot.registry, "rune.impossible").empty());
}

// --- data discipline ----------------------------------------------------------------------------

void RewardsJsonDoesNotDuplicateBalance() {
    const auto document = ReadJsonFile("assets/data/rewards.json");
    CHECK(document.contains("rune_offer"));
    for (const char* banned : {"room_materials", "room_heal", "room_experience", "event_heal", "event_materials",
                               "camp_heal", "experience", "materials"}) {
        CHECK(!document.contains(banned));
        CHECK(!document.at("rune_offer").contains(banned));
    }
    // The shipped file boots and carries the intended defaults (D-2/D-15).
    FBoot boot(UniquePath("reward-shipped"), ShipAssetRoots());
    const auto& tables = boot.rewards().tables;
    CHECK(tables.count == 3);
    CHECK(tables.chance > 0.69 && tables.chance < 0.71);
    CHECK(tables.pityThreshold == 2);
    CHECK(tables.minLegalCandidates == 2);
    CHECK(tables.duplicatePolicy == game::kRewardDuplicatePolicyRefund);
    CHECK(tables.bossOffer == game::kRewardBossOfferGuaranteed);
}

void RewardsJsonMissingFailsBoot() {
    FTempAssets assets("reward-missing");
    assets.CopyGameplayData();  // everything except rewards.json
    CHECK(!BootsGameplay({assets.root.string()}));  // the only root: no shipped file to fall back to
}

void RewardsValidationRejects() {
    FTempAssets assets("reward-invalid");
    assets.CopyGameplayData();
    const auto base = ReadJsonFile(FixtureDataPath("rewards.json"));

    const auto edit = [&base](const auto& change) {
        auto document = base;
        change(document);
        return document;
    };
    const std::vector<std::pair<const char*, json>> cases = {
        {"chance above one", edit([](json& d) { d["rune_offer"]["chance"] = 1.5; })},
        {"negative chance", edit([](json& d) { d["rune_offer"]["chance"] = -0.1; })},
        {"zero count", edit([](json& d) { d["rune_offer"]["count"] = 0; })},
        {"min legal above count", edit([](json& d) { d["rune_offer"]["min_legal_candidates"] = 4; })},
        {"negative pity", edit([](json& d) { d["rune_offer"]["pity_threshold"] = -1; })},
        {"negative refund", edit([](json& d) { d["rune_offer"]["skip_refund_materials"] = -1; })},
        {"unknown duplicate policy", edit([](json& d) { d["rune_offer"]["duplicate_policy"] = "copy"; })},
        {"unknown boss offer", edit([](json& d) { d["boss_offer"] = "sometimes"; })},
        {"missing rune offer", edit([](json& d) { d.erase("rune_offer"); })},
    };
    for (const auto& [label, document] : cases) {
        assets.Write("rewards.json", document);
        const bool booted = BootsGameplay({assets.root.string()});
        if (booted) std::cout << "    accepted invalid rewards.json: " << label << '\n';
        CHECK(!booted);
    }
    assets.Write("rewards.json", base);
    CHECK(BootsGameplay({assets.root.string()}));
}

// --- boss rooms ---------------------------------------------------------------------------------

void BossOfferIsGuaranteedOnce() {
    FBoot boot(UniquePath("reward-boss"));
    StartFresh(boot);
    auto& rewards = boot.rewards();
    rewards.tables.chance = 0.0;  // proves the boss room does not depend on the monster-room roll

    game::RollRoomOffer(boot.registry, true);
    CHECK(rewards.offer.active);
    const auto candidates = rewards.offer.candidates;
    const auto drawIndex = rewards.nextDrawIndex;
    game::RollRoomOffer(boot.registry, true);  // a repeated call is not a second offer
    CHECK(rewards.offer.candidates == candidates);
    CHECK(rewards.nextDrawIndex == drawIndex);
    CHECK(rewards.pity == 0);
}

void BossExtraModeDrawsTwice() {
    FBoot boot(UniquePath("reward-boss-extra"));
    StartFresh(boot);
    auto& rewards = boot.rewards();
    rewards.tables.chance = 0.0;
    rewards.tables.bossOffer = game::kRewardBossOfferExtra;

    game::RollRoomOffer(boot.registry, true);
    CHECK(rewards.offer.active);
    CHECK(rewards.bossExtraPending);
    CHECK(rewards.nextDrawIndex == 1u);

    CHECK(game::ResolveRuneOffer(boot.registry, -1));
    CHECK(rewards.offer.active);  // the second, still-rejectable offer
    CHECK(!rewards.bossExtraPending);
    CHECK(rewards.nextDrawIndex == 2u);

    CHECK(game::ResolveRuneOffer(boot.registry, -1));
    CHECK(!rewards.offer.active);
}

// --- the decision/advance split -----------------------------------------------------------------

void PickDoesNotAdvancePhase() {
    FBoot boot(UniquePath("reward-pick-phase"));
    StartFresh(boot);
    WinRoom(boot);
    const int room = boot.run().room;
    const auto phase = boot.run().phase;
    const int materials = boot.run().materials;
    const auto runes = boot.run().runes;
    CHECK(boot.rewards().offer.active);

    CHECK(game::ResolveRuneOffer(boot.registry, 0));
    CHECK(boot.run().phase == phase);
    CHECK(boot.run().room == room);
    CHECK(boot.run().runes.size() == runes.size() + 1);  // the pick grants ...
    CHECK(boot.run().materials == materials);            // ... and pays nothing else
}

void ContinueBlockedWhileOfferActive() {
    FBoot boot(UniquePath("reward-blocked"));
    StartFresh(boot);
    WinRoom(boot);
    const std::string saveBefore = ReadBytes(boot.runSave);

    const auto result = game::ApplyRunIntent(boot.registry, game::ERunIntent::ContinueReward);
    CHECK(!result.has_value());
    CHECK(!result.error().empty());
    CHECK(boot.run().phase == game::EDungeonPhase::Reward);
    CHECK(ReadBytes(boot.runSave) == saveBefore);  // refused = nothing changed, not even the save
}

void SkipUnblocksContinue() {
    FBoot boot(UniquePath("reward-unblock"));
    StartFresh(boot);
    WinRoom(boot);
    CHECK(boot.rewards().offer.active);
    CHECK(game::ResolveRuneOffer(boot.registry, -1));

    const auto result = game::ApplyRunIntent(boot.registry, game::ERunIntent::ContinueReward);
    CHECK(result.has_value());
    CHECK(result->exit == game::ERunExit::ShowModal);
    CHECK(result->modalId == std::string(game::kRunModalEvent));
    CHECK(boot.run().phase == game::EDungeonPhase::Event);
}

// --- the forecast -------------------------------------------------------------------------------

void OfferForecastShown() {
    FBoot boot(UniquePath("reward-forecast"));
    StartFresh(boot);
    WinRoom(boot);
    CHECK(boot.rewards().offer.active);

    const auto logs = RenderModalLogs(
        boot.registry, {std::string(game::kRunModalReward), std::string(game::kRunModalCamp)});
    CHECK(logs.size() == 2u);
    CHECK(logs[0].find("Next battle room rune offer: chance 100%") != std::string::npos);
    CHECK(logs[0].find("CHOOSE A RUNE") != std::string::npos);
    CHECK(logs[1].find("Next battle room rune offer: chance 100%") != std::string::npos);  // the camp screen too
}

void ForecastGuaranteesAfterPity() {
    FBoot boot(UniquePath("reward-forecast-pity"));
    StartFresh(boot);
    auto& rewards = boot.rewards();
    rewards.tables.pityThreshold = 2;
    CHECK(!game::RuneOfferForecastOf(boot.registry).guaranteed);
    rewards.pity = 2;
    const auto forecast = game::RuneOfferForecastOf(boot.registry);
    CHECK(forecast.guaranteed);
    CHECK(forecast.pity == 2);

    const auto log = RenderModalLogs(boot.registry, {std::string(game::kRunModalReward)});
    CHECK(log.size() == 1u);
    CHECK(log[0].find("Next battle room rune offer: guaranteed (pity)") != std::string::npos);
}

void FixtureRewardsAreDeterministic() {
    FBoot boot(UniquePath("reward-fixture"));
    CHECK(boot.rewards().tables.chance == 1.0);   // every monster room offers
    CHECK(boot.rewards().tables.pityThreshold == 0);  // pity off: the fixture never forces one
    StartFresh(boot);
    game::RollRoomOffer(boot.registry, false);
    CHECK(boot.rewards().offer.active);
    CHECK(boot.rewards().pity == 0);
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
            (std::filesystem::current_path() / "out" / "test-saves" / "reward_offer_tests").string();
        std::error_code error;
        std::filesystem::create_directories(path, error);
        _putenv_s("GAME01P_SAVE_DIR", path.c_str());
    }

    return game_test::RunTests("reward offer", {
        {"OfferIsDeterministic", OfferIsDeterministic},
        {"CampPurchaseAdvancesDraw", CampPurchaseAdvancesDraw},
        {"PeekDoesNotAdvance", PeekDoesNotAdvance},
        {"PityForcesOffer", PityForcesOffer},
        {"ChanceZeroStillPities", ChanceZeroStillPities},
        {"OfferCountMatchesArithmetic", OfferCountMatchesArithmetic},
        {"SkipRefundsMaterials", SkipRefundsMaterials},
        {"DuplicateRefundsByDefault", DuplicateRefundsByDefault},
        {"DuplicateAllowAddsCopy", DuplicateAllowAddsCopy},
        {"DuplicateCardShowsRefund", DuplicateCardShowsRefund},
        {"SaveRestoresOffer", SaveRestoresOffer},
        {"CorruptOfferDoesNotBrickSave", CorruptOfferDoesNotBrickSave},
        {"NewRunResetsOfferState", NewRunResetsOfferState},
        {"MinLegalGate", MinLegalGate},
        {"OfferSizeIsMinCountLegalPool", OfferSizeIsMinCountLegalPool},
        {"OwnedDuplicateAtMostOne", OwnedDuplicateAtMostOne},
        {"UnownedFirstWhenPoolIsDeep", UnownedFirstWhenPoolIsDeep},
        {"MixedPoolFillsOneOwnedCard", MixedPoolFillsOneOwnedCard},
        {"OffersMostlyLegal", OffersMostlyLegal},
        {"EmptyLegalPoolDeactivatesOffer", EmptyLegalPoolDeactivatesOffer},
        {"RewardsJsonDoesNotDuplicateBalance", RewardsJsonDoesNotDuplicateBalance},
        {"RewardsJsonMissingFailsBoot", RewardsJsonMissingFailsBoot},
        {"RewardsValidationRejects", RewardsValidationRejects},
        {"BossOfferIsGuaranteedOnce", BossOfferIsGuaranteedOnce},
        {"BossExtraModeDrawsTwice", BossExtraModeDrawsTwice},
        {"PickDoesNotAdvancePhase", PickDoesNotAdvancePhase},
        {"ContinueBlockedWhileOfferActive", ContinueBlockedWhileOfferActive},
        {"SkipUnblocksContinue", SkipUnblocksContinue},
        {"OfferForecastShown", OfferForecastShown},
        {"ForecastGuaranteesAfterPity", ForecastGuaranteesAfterPity},
        {"FixtureRewardsAreDeterministic", FixtureRewardsAreDeterministic},
    });
}
