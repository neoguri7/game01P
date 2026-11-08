// WP-27: reward-time rune equip — the {Camp, Reward} loadout window, the fit-on-the-spot block in the
// reward modal, the `loadout` run-log line and the display-name/hook card text. The offer economy itself
// (WP-17) is untouched; these tests drive the real composition root against the frozen fixture.
//
// Every save and run-log file this suite writes stays under `out/` (the per-test checkpoint stems), never
// %APPDATA%\Game01P\EmberDungeon.

#define GAME01P_TEST_SUPPORT_CHECK
#include "../TestSupport.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <nlohmann/json.hpp>

#include "core/EngineState.h"
#include "core/ResourceManager.h"
#include "core/SDLDeleter.h"
#include "ecs/components/FFaction.h"
#include "ecs/components/FHealth.h"
#include "ecs/components/FSkillLoadout.h"
#include "ecs/components/FStats.h"
#include "gameplay/battle/FBattleSetup.h"
#include "gameplay/combat/FCombatSheet.h"
#include "gameplay/data/FContentRegistry.h"
#include "gameplay/dungeon/FDungeonRun.h"
#include "gameplay/dungeon/FDungeonSave.h"
#include "gameplay/dungeon/FRewards.h"
#include "gameplay/dungeon/FRunEventLog.h"
#include "gameplay/dungeon/FRunFlow.h"
#include "gameplay/dungeon/FRunLog.h"
#include "gameplay/factory/FEntityDefinitions.h"
#include "gameplay/factory/FEntityFactory.h"
#include "gameplay/factory/FScene.h"
#include "presentation/FDungeonArt.h"
#include "presentation/FDungeonOverlay.h"
#include "presentation/FDungeonViews.h"
#include "presentation/FModalViewRegistry.h"

namespace {

using nlohmann::json;

constexpr std::uint32_t kSeed = 4242;

/// The one window sentence (WP-27 §2). Verbatim: the rules own this text, the tests never paraphrase it.
constexpr const char* kWindowMessage = "Runes can be changed between rooms, not in battle.";

/// The test rune: no tag requirements, so it is a legal socket on every party skill.
constexpr const char* kRune = "rune.power";

/// A rune the zero-legal fit block must refuse once the whole party runs a Support/Duration skill:
/// "rune.leech" requires_any ["Attack"] while `skill.buff` carries the tags Support, Duration.
constexpr const char* kUnfittableRune = "rune.leech";

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
    // WP-27: the fielded room's scene owns its entities (FScene::~FScene destroys them) — it must outlive
    // the fielding call for CaptureRunParty/ApplyBattleStart to see the party.
    std::unique_ptr<game::FScene> fieldScene;

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
    game::FRunLog& runLog() { return registry.ctx().get<game::FRunLog>(); }
};

// --- a temporary asset root (the hook-rune cards) ----------------------------------------------

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
        for (const char* file : {"units.json", "skills.json", "runes.json", "statuses.json", "ai.json"}) Copy(file);
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

// --- shared steps ------------------------------------------------------------------------------

void StartFresh(FBoot& boot) {
    game::StartRun(boot.registry, kSeed, true);
}

/// Fields the current room the way the battle state does — document -> factory -> combat units -> run
/// party — so CaptureRunParty reads a real loadout. The scene is kept on the boot: FScene destroys its
/// entities on scope exit, and the later steps still need the fielded party.
void FieldRoom(FBoot& boot) {
    auto& registry = boot.registry;
    const auto document = game::BuildDungeonBattlefield(registry);
    auto scene = std::make_unique<game::FScene>(registry, "RewardEquipTest");
    scene->Load();
    const auto battlefield = game::FEntityFactory::CreateBattlefield(*scene, document);
    CHECK(battlefield.has_value());
    const auto& content = registry.ctx().get<game::gameplay::FContentRegistry>();
    CHECK(game::SetupCombatUnits(registry, *battlefield, content));
    game::ApplyRunParty(registry);
    boot.fieldScene = std::move(scene);
}

/// The run party's skill slots come from the fielded units; a win captures them (the reward step's first act).
void CaptureLoadout(FBoot& boot) {
    FieldRoom(boot);
    game::CaptureRunParty(boot.registry);
}

void WinRoom(FBoot& boot) {
    CHECK(game::ResolveRoomVictory(boot.registry, true).has_value());
    CHECK(boot.run().phase == game::EDungeonPhase::Reward);
}

/// A reward-phase run with the party loadout already captured.
void RewardWithLoadout(FBoot& boot) {
    StartFresh(boot);
    CaptureLoadout(boot);
    WinRoom(boot);
}

/// Pushes `runeId` into the collected pool and returns its index.
std::size_t CollectRune(FBoot& boot, const std::string& runeId) {
    boot.run().runes.push_back(runeId);
    const auto found = std::ranges::find(boot.run().runes, runeId);
    CHECK(found != boot.run().runes.end());
    return static_cast<std::size_t>(found - boot.run().runes.begin());
}

/// The deterministic first (member, slot) the rules accept for a collected rune.
struct FEquipSite {
    std::size_t member{0};
    std::size_t slot{0};
};

bool FindEquipSite(FBoot& boot, std::size_t runeIndex, FEquipSite& site) {
    for (std::size_t member = 0; member < boot.run().party.size(); ++member) {
        for (std::size_t slot = 0; slot < boot.run().party[member].skills.size(); ++slot) {
            if (!game::ValidateDungeonRuneEquip(boot.registry, member, slot, runeIndex)) {
                site = {member, slot};
                return true;
            }
        }
    }
    return false;
}

/// True when any party skill slot carries the rune.
bool AnySlotCarries(FBoot& boot, const std::string& runeId) {
    for (const auto& member : boot.run().party) {
        for (const auto& slot : member.skills) {
            if (std::ranges::find(slot.runeIds, runeId) != slot.runeIds.end()) return true;
        }
    }
    return false;
}

/// The number of party skill slots carrying the rune (the equipped-copy count).
int CarriedCopies(FBoot& boot, const std::string& runeId) {
    int carried = 0;
    for (const auto& member : boot.run().party) {
        for (const auto& slot : member.skills) {
            if (std::ranges::find(slot.runeIds, runeId) != slot.runeIds.end()) ++carried;
        }
    }
    return carried;
}

entt::entity FindPartyEntity(entt::registry& registry, const std::string& id) {
    for (const auto entity : registry.view<game::FPartyMember, game::FUnitData>()) {
        if (registry.get<game::FUnitData>(entity).definition.id == id) return entity;
    }
    return entt::null;
}

/// The run-log JSONL lines whose `t` equals `type`, in file order.
std::vector<json> RunLogLines(const game::FRunLog& log, const std::string& type) {
    CHECK(!log.path.empty());
    std::ifstream file(log.path);
    CHECK(file.good());
    std::vector<json> lines;
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty()) continue;
        const json parsed = json::parse(line);
        if (parsed.is_object() && parsed.value("t", std::string{}) == type) lines.push_back(parsed);
    }
    return lines;
}

// --- UI capture ---------------------------------------------------------------------------------

/// Renders one modal view into an ImGui log (3 frames; the last frame logs) and returns the captured text.
/// `beforeRender` runs after the UI state exists and before the frames — it seeds the view state a real
/// click would have set. `autoOpenDepth` is ImGui's log depth: -1 (the default of 2) expands tree nodes
/// while logging, so the camp screen's nested loadout block is captured.
std::string RenderModalLog(entt::registry& registry, const std::string& modalId,
                           const std::function<void()>& beforeRender = {}, int autoOpenDepth = 0) {
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
    if (registry.ctx().find<game::FDungeonUiState>() == nullptr) game::InitializeDungeonUi(registry);
    if (beforeRender) beforeRender();

    const std::string logPath = UniquePath("reward-equip-ui") + ".log";
    for (int frame = 0; frame < 3; ++frame) {
        ImGui::NewFrame();
        if (frame == 2) ImGui::LogToFile(autoOpenDepth, logPath.c_str());
        const auto* view = game::FindModalView(registry, modalId);
        CHECK(view != nullptr);
        view->render(registry);
        if (frame == 2) ImGui::LogFinish();
        ImGui::Render();
    }
    std::string captured = ReadBytes(logPath);
    std::error_code error;
    std::filesystem::remove(logPath, error);
    return captured;
}

/// The FitsLineWraps probe: one ImGui context renders the reward modal twice — with a long Fits line and a
/// short one — and measures the modal's auto-fitted height plus the Fits item's own rect (rendered through
/// the same TextWrapped call at the same width in a probe window; the view's item rect is not reachable from
/// outside). The text log stores the raw string even when a line is clipped, so log substrings prove nothing
/// about wrapping; geometry does: the two cards differ only in the Fits item, and the modal delta must
/// reproduce the item delta. A TextDisabled revert would clip the line (modal delta ~0) and fail the match.
struct FWrapProbe {
    float longHeight{0.f};
    float shortHeight{0.f};
    float longFitsItemHeight{0.f};
    float shortFitsItemHeight{0.f};
    float lineHeight{0.f};
    std::string longLog;
    std::string shortLog;
};

FWrapProbe MeasureFitsWrap(entt::registry& registry, const std::string& longRune, const std::string& shortRune) {
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
    if (registry.ctx().find<game::FDungeonUiState>() == nullptr) game::InitializeDungeonUi(registry);

    auto& rewards = registry.ctx().get<game::FRewards>();
    const auto render = [&](const std::string& runeId, const std::string& logSuffix) {
        rewards.offer.active = true;
        rewards.offer.candidates = {runeId};
        const std::string logPath = logSuffix.empty() ? std::string{} : UniquePath("reward-fits") + logSuffix;
        for (int frame = 0; frame < 3; ++frame) {
            ImGui::NewFrame();
            if (!logPath.empty() && frame == 2) ImGui::LogToFile(0, logPath.c_str());
            const auto* view = game::FindModalView(registry, game::kRunModalReward);
            CHECK(view != nullptr);
            view->render(registry);
            if (!logPath.empty() && frame == 2) ImGui::LogFinish();
            ImGui::Render();
        }
        const ImGuiWindow* modal = ImGui::FindWindowByName("Expedition choice");
        CHECK(modal != nullptr);
        if (!logPath.empty()) {
            const std::string captured = ReadBytes(logPath);
            std::error_code error;
            std::filesystem::remove(logPath, error);
            return std::pair{modal->Size.y, captured};
        }
        return std::pair{modal->Size.y, std::string{}};
    };

    FWrapProbe probe;
    std::tie(probe.longHeight, probe.longLog) = render(longRune, ".log");
    std::tie(probe.shortHeight, probe.shortLog) = render(shortRune, ".short.log");
    probe.lineHeight = ImGui::GetTextLineHeight();

    // The Fits item's own rect cannot be read from inside the view, so the test renders the identical string
    // through the identical call — ImGui::TextWrapped at the modal's fixed 580px width — in a probe window
    // and reads the item rect. The two offer cards differ only in the Fits item, so the real modal's
    // auto-fit height delta must equal this item's height delta; a TextDisabled revert in the view would
    // leave the modal delta near zero while the probe still measures the wrapped height.
    const auto fitsLine = [&](const std::string& runeId) {
        const auto names = game::RuneFittingSkillNames(registry, runeId);
        std::string line = "Fits: ";
        if (names.empty()) return line + "no current party skill";
        for (std::size_t index = 0; index < names.size(); ++index) {
            if (index > 0) line += ", ";
            line += names[index];
        }
        return line;
    };
    ImGui::NewFrame();
    ImGui::SetNextWindowPos({0, 0});
    ImGui::SetNextWindowSize({580, 0}, ImGuiCond_Always);
    ImGui::Begin("fit-wrap-probe", nullptr,
                 ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar);
    ImGui::TextWrapped("%s", fitsLine(longRune).c_str());
    probe.longFitsItemHeight = ImGui::GetItemRectSize().y;
    ImGui::TextWrapped("%s", fitsLine(shortRune).c_str());
    probe.shortFitsItemHeight = ImGui::GetItemRectSize().y;
    ImGui::End();
    ImGui::Render();
    return probe;
}

// ------------------------------------------------------------------------------------------------
// The equip window (WP-27 §2)
// ------------------------------------------------------------------------------------------------

void EquipAllowedInRewardPhase() {
    FBoot boot(UniquePath("equip-reward"));
    RewardWithLoadout(boot);
    const std::size_t index = CollectRune(boot, kRune);
    FEquipSite site;
    CHECK(FindEquipSite(boot, index, site));

    CHECK(!game::ValidateDungeonRuneEquip(boot.registry, site.member, site.slot, index));
    CHECK(!game::EquipDungeonRune(boot.registry, site.member, site.slot, index));
    CHECK(boot.run().party[site.member].skills[site.slot].runeIds == std::vector<std::string>{kRune});
    CHECK(std::ranges::find(boot.run().runes, std::string(kRune)) == boot.run().runes.end());
    CHECK(boot.run().phase == game::EDungeonPhase::Reward);
}

void EquipRejectedInBattleDefeatAndEvent() {
    FBoot boot(UniquePath("equip-window"));
    RewardWithLoadout(boot);
    CollectRune(boot, kRune);
    const std::size_t runes = boot.run().runes.size();

    for (const auto phase :
         {game::EDungeonPhase::Battle, game::EDungeonPhase::Defeat, game::EDungeonPhase::Event}) {
        boot.run().phase = phase;
        const auto validate = game::ValidateDungeonRuneEquip(boot.registry, 0, 0, 0);
        CHECK(validate.has_value());
        CHECK(*validate == kWindowMessage);
        const auto equip = game::EquipDungeonRune(boot.registry, 0, 0, 0);
        CHECK(equip.has_value());
        CHECK(*equip == kWindowMessage);
        const auto remove = game::RemoveDungeonRune(boot.registry, 0, 0, 0);
        CHECK(remove.has_value());
        CHECK(*remove == kWindowMessage);
    }
    CHECK(boot.run().runes.size() == runes);
    CHECK(!AnySlotCarries(boot, kRune));
    CHECK(RunLogLines(boot.runLog(), "loadout").empty());  // a rejected edit never logs (success path only)
}

void EquipStillAllowedAtCamp() {
    FBoot boot(UniquePath("equip-camp"));
    StartFresh(boot);
    CaptureLoadout(boot);
    boot.run().phase = game::EDungeonPhase::Camp;
    const std::size_t index = CollectRune(boot, kRune);
    FEquipSite site;
    CHECK(FindEquipSite(boot, index, site));
    CHECK(!game::EquipDungeonRune(boot.registry, site.member, site.slot, index));
    CHECK(boot.run().party[site.member].skills[site.slot].runeIds == std::vector<std::string>{kRune});
}

// ------------------------------------------------------------------------------------------------
// Pick -> fit -> the next battle (WP-27 §1)
// ------------------------------------------------------------------------------------------------

void FitAtPickAppliesNextBattle() {
    FBoot boot(UniquePath("equip-next-battle"));
    RewardWithLoadout(boot);
    CHECK(boot.rewards().offer.active);
    const std::string picked = boot.rewards().offer.candidates.front();
    CHECK(game::ResolveRuneOffer(boot.registry, 0));
    const auto found = std::ranges::find(boot.run().runes, picked);
    CHECK(found != boot.run().runes.end());
    const std::size_t index = static_cast<std::size_t>(found - boot.run().runes.begin());
    FEquipSite site;
    CHECK(FindEquipSite(boot, index, site));
    CHECK(!game::EquipDungeonRune(boot.registry, site.member, site.slot, index));

    // Fitting is a choice, not a condition: Continue moves on immediately, and the fixture's first dungeon
    // has an event gap between room 0 and the next battle.
    CHECK(game::ApplyRunIntent(boot.registry, game::ERunIntent::ContinueReward).has_value());
    CHECK(boot.run().phase == game::EDungeonPhase::Event);
    CHECK(game::ApplyRunIntent(boot.registry, game::ERunIntent::ChooseEventRest).has_value());
    CHECK(boot.run().phase == game::EDungeonPhase::Battle);

    // The next battle fields a fresh room exactly like FEmberBattleState::OnEnter: the cleared room's scene
    // is gone, the new room document builds new units, and the run loadout lands on them through
    // ApplyBattleStart. Reading the unit's FSkillLoadout is battle-side state — not just run bookkeeping.
    boot.fieldScene.reset();
    game::FScene nextScene(boot.registry, "RewardEquipNextBattle");
    nextScene.Load();
    const auto nextDocument = game::BuildDungeonBattlefield(boot.registry);
    const auto nextBattlefield = game::FEntityFactory::CreateBattlefield(nextScene, nextDocument);
    CHECK(nextBattlefield.has_value());
    const auto& nextContent = boot.registry.ctx().get<game::gameplay::FContentRegistry>();
    CHECK(game::SetupCombatUnits(boot.registry, *nextBattlefield, nextContent));
    game::ApplyRunParty(boot.registry);

    const entt::entity entity = FindPartyEntity(boot.registry, boot.run().party[site.member].id);
    CHECK(entity != entt::null);
    const auto& loadout = boot.registry.get<game::FSkillLoadout>(entity);
    CHECK(site.slot < loadout.slots.size());
    // The fielded slot is the run slot, and it carries the fitted rune.
    CHECK(loadout.slots[site.slot].skillId == boot.run().party[site.member].skills[site.slot].skillId);
    CHECK(std::ranges::find(loadout.slots[site.slot].runeIds, picked) != loadout.slots[site.slot].runeIds.end());
}

void FitAtPickSavesAndRestores() {
    FBoot boot(UniquePath("equip-save"));
    RewardWithLoadout(boot);
    const std::size_t index = CollectRune(boot, kRune);
    FEquipSite site;
    CHECK(FindEquipSite(boot, index, site));
    CHECK(!game::EquipDungeonRune(boot.registry, site.member, site.slot, index));
    // No explicit save here: the reload below must prove the equip path itself wrote the run (WP-27 §1:
    // equip/remove pass through the existing SaveDungeonRun), not a save the test added.

    boot.registry.ctx().erase<game::FDungeonRun>();
    CHECK(game::ContinueDungeonRun(boot.registry));
    CHECK(boot.run().phase == game::EDungeonPhase::Reward);
    CHECK(boot.run().party[site.member].skills[site.slot].runeIds == std::vector<std::string>{kRune});
    CHECK(std::ranges::find(boot.run().runes, std::string(kRune)) == boot.run().runes.end());
}

void FittedRuneMovesFromPoolToSlot() {
    FBoot boot(UniquePath("equip-moves"));
    RewardWithLoadout(boot);
    const std::size_t index = CollectRune(boot, kRune);
    const std::size_t poolBefore = boot.run().runes.size();
    FEquipSite site;
    CHECK(FindEquipSite(boot, index, site));
    CHECK(!game::EquipDungeonRune(boot.registry, site.member, site.slot, index));

    CHECK(boot.run().runes.size() == poolBefore - 1);                                   // pool -1
    CHECK(boot.run().party[site.member].skills[site.slot].runeIds.size() == 1u);        // slot +1
    // A fitted rune still counts as owned: it is never offered again as an unowned card.
    CHECK(game::IsRuneOwned(boot.registry, kRune));
}

void FitLaterKeepsRuneInPool() {
    FBoot boot(UniquePath("equip-later"));
    RewardWithLoadout(boot);
    CHECK(boot.rewards().offer.active);
    const std::string picked = boot.rewards().offer.candidates.front();
    CHECK(game::ResolveRuneOffer(boot.registry, 0));  // picked, not fitted
    CHECK(std::ranges::find(boot.run().runes, picked) != boot.run().runes.end());
    CHECK(!AnySlotCarries(boot, picked));
    // "Later at camp" never blocks the run.
    CHECK(game::ApplyRunIntent(boot.registry, game::ERunIntent::ContinueReward).has_value());
}

void EquipByIndexWithDuplicateCopies() {
    FBoot boot(UniquePath("equip-duplicate"));
    RewardWithLoadout(boot);
    boot.run().runes = {kRune, kRune};
    FEquipSite first;
    CHECK(FindEquipSite(boot, 0, first));
    CHECK(!game::EquipDungeonRune(boot.registry, first.member, first.slot, 0));
    CHECK(boot.run().runes == std::vector<std::string>{kRune});  // the first copy moved, one stays
    FEquipSite second;
    CHECK(FindEquipSite(boot, 0, second));
    CHECK(!game::EquipDungeonRune(boot.registry, second.member, second.slot, 0));
    CHECK(boot.run().runes.empty());
    CHECK(CarriedCopies(boot, kRune) == 2);
}

void RewardEquipIsFree() {
    FBoot boot(UniquePath("equip-free"));
    RewardWithLoadout(boot);
    const int materials = boot.run().materials;
    const std::size_t index = CollectRune(boot, kRune);
    FEquipSite site;
    CHECK(FindEquipSite(boot, index, site));
    CHECK(!game::EquipDungeonRune(boot.registry, site.member, site.slot, index));
    CHECK(boot.run().materials == materials);

    // The camp path is the same free edit (D-17).
    boot.run().phase = game::EDungeonPhase::Camp;
    const std::size_t second = CollectRune(boot, kRune);
    FEquipSite campSite;
    CHECK(FindEquipSite(boot, second, campSite));
    CHECK(!game::EquipDungeonRune(boot.registry, campSite.member, campSite.slot, second));
    CHECK(boot.run().materials == materials);
    // Zero CampSpent events: the event tail never carries one and the run log never gets its `camp` line.
    const auto* events = boot.registry.ctx().find<game::FRunEventLog>();
    CHECK(events != nullptr);
    for (const auto& event : events->tail) CHECK(event.kind != game::ERunEventKind::CampSpent);
    CHECK(RunLogLines(boot.runLog(), "camp").empty());
}

// ------------------------------------------------------------------------------------------------
// The `loadout` run-log line (WP-27 §3)
// ------------------------------------------------------------------------------------------------

void LoadoutLineWritten() {
    FBoot boot(UniquePath("equip-log"));
    RewardWithLoadout(boot);
    const std::size_t index = CollectRune(boot, kRune);
    FEquipSite rewardSite;
    CHECK(FindEquipSite(boot, index, rewardSite));
    CHECK(!game::EquipDungeonRune(boot.registry, rewardSite.member, rewardSite.slot, index));

    // The camp equip passes through the same call and speaks the same line with phase "camp".
    boot.run().phase = game::EDungeonPhase::Camp;
    const std::size_t second = CollectRune(boot, kRune);
    FEquipSite campSite;
    CHECK(FindEquipSite(boot, second, campSite));
    CHECK(!game::EquipDungeonRune(boot.registry, campSite.member, campSite.slot, second));

    // Removal logs too (the rule allows it in Reward; the UI exposes it at camp).
    CHECK(!game::RemoveDungeonRune(boot.registry, campSite.member, campSite.slot, 0));

    const auto lines = RunLogLines(boot.runLog(), "loadout");
    CHECK(lines.size() == 3u);  // exactly one line per successful edit
    const auto& equip = lines[0];
    CHECK(equip.value("t", std::string{}) == "loadout");
    CHECK(equip.at("depth") == boot.run().depth);
    CHECK(equip.at("room") == boot.run().room);
    CHECK(equip.at("phase") == "reward");  // a phase id string, never an int cast
    CHECK(equip.at("member") == static_cast<int>(rewardSite.member));
    CHECK(equip.at("slot") == static_cast<int>(rewardSite.slot));
    CHECK(equip.at("rune") == kRune);
    CHECK(equip.at("action") == "equip");

    const auto& campEquip = lines[1];
    CHECK(campEquip.value("t", std::string{}) == "loadout");
    CHECK(campEquip.at("depth") == boot.run().depth);
    CHECK(campEquip.at("room") == boot.run().room);
    CHECK(campEquip.at("phase") == "camp");
    CHECK(campEquip.at("member") == static_cast<int>(campSite.member));
    CHECK(campEquip.at("slot") == static_cast<int>(campSite.slot));
    CHECK(campEquip.at("rune") == kRune);
    CHECK(campEquip.at("action") == "equip");

    const auto& remove = lines[2];
    CHECK(remove.value("t", std::string{}) == "loadout");
    CHECK(remove.at("phase") == "camp");
    CHECK(remove.at("member") == static_cast<int>(campSite.member));
    CHECK(remove.at("slot") == static_cast<int>(campSite.slot));
    CHECK(remove.at("rune") == kRune);
    CHECK(remove.at("action") == "remove");
}

void SchemaAcceptsLoadout() {
    FBoot boot(UniquePath("equip-schema"));
    RewardWithLoadout(boot);
    const std::size_t index = CollectRune(boot, kRune);
    FEquipSite site;
    CHECK(FindEquipSite(boot, index, site));
    CHECK(!game::EquipDungeonRune(boot.registry, site.member, site.slot, index));

    const json* loadout = nullptr;
    for (const auto& event : boot.runLog().events) {
        if (event.is_object() && event.value("t", std::string{}) == "loadout") {
            loadout = &event;
            break;
        }
    }
    CHECK(loadout != nullptr);
    CHECK(game::FRunEventSchemaValid(*loadout));
    // The row really is loadout's required-key set: dropping a key flips the verdict.
    json broken = *loadout;
    broken.erase("phase");
    CHECK(!game::FRunEventSchemaValid(broken));
}

// ------------------------------------------------------------------------------------------------
// The card text (WP-27 §4)
// ------------------------------------------------------------------------------------------------

void CardExplainsHooks() {
    FTempAssets assets("reward-hooks");
    assets.CopyGameplayData(true);
    assets.Copy("rewards.json");
    // The fixture runes plus the five data-authored hook runes (WP-27 §4 reads rune.hooks).
    auto runes = ReadJsonFile(FixtureDataPath("runes.json"));
    const json hookRunes = json::array({
        {{"id", "rune.chain"}, {"display_name", "Chain"}, {"requires_all_tags", json::array({"Projectile"})},
         {"hooks", json::array({{{"event", "on_hit"}, {"effect", {{"kind", "chain"}, {"amount", 2}, {"distance", 1}}}}})}},
        {{"id", "rune.frostpath"}, {"display_name", "Frost Path"}, {"requires_all_tags", json::array({"Cold"})},
         {"hooks", json::array({{{"event", "on_hit"}, {"effect", {{"kind", "apply_status_target"}, {"status_id", "status.locked_move"}}}}})}},
        {{"id", "rune.combust"}, {"display_name", "Combust"}, {"requires_all_tags", json::array({"Fire"})},
         {"hooks", json::array({{{"event", "on_kill"}, {"effect", {{"kind", "chain"}, {"amount", 2}, {"distance", 1}}}}})}},
        {{"id", "rune.guard"}, {"display_name", "Counter Guard"},
         {"hooks", json::array({{{"event", "on_damage_taken"}, {"effect", {{"kind", "apply_status_target"}, {"status_id", "status.debuff"}}}}})}},
        {{"id", "rune.impact"}, {"display_name", "Impact"}, {"requires_all_tags", json::array({"Melee"})},
         {"hooks", json::array({{{"event", "on_hit"}, {"effect", {{"kind", "push"}, {"from", "caster"}, {"distance", 1}}}}})}},
    });
    for (const auto& row : hookRunes) runes["runes"].push_back(row);
    assets.Write("runes.json", runes);

    FBoot boot(UniquePath("reward-hooks"), {assets.root.string(), kShipAssetRoot});
    StartFresh(boot);
    WinRoom(boot);  // the fixture chance 1.0 opens an offer; the candidates below are the card set under test
    auto& rewards = boot.rewards();
    rewards.offer.active = true;
    rewards.offer.candidates = {"rune.chain", "rune.frostpath", "rune.combust", "rune.guard", "rune.impact"};

    const std::string log = RenderModalLog(boot.registry, std::string(game::kRunModalReward));
    const auto& content = boot.registry.ctx().get<game::gameplay::FContentRegistry>();
    const auto statusName = [&](const std::string& id) {
        const auto* row = content.statuses.find(id);
        return row != nullptr && !row->displayName.empty() ? row->displayName : id;
    };
    CHECK(log.find("On hit: chain x2 (dist 1)") != std::string::npos);
    CHECK(log.find("On kill: chain x2 (dist 1)") != std::string::npos);
    CHECK(log.find("On hit: apply " + statusName("status.locked_move")) != std::string::npos);
    CHECK(log.find("When hit: apply " + statusName("status.debuff")) != std::string::npos);
    CHECK(log.find("On hit: push 1") != std::string::npos);
    CHECK(log.find("No numeric change") == std::string::npos);  // a hooked rune never reads as "no change"
    // The status speaks its display name: the raw id would only appear if the card fell back to it.
    CHECK(log.find("status.locked_move") == std::string::npos);
    CHECK(log.find("status.debuff") == std::string::npos);
}

void FitsLineWraps() {
    FBoot boot(UniquePath("reward-fits-wrap"));
    StartFresh(boot);
    WinRoom(boot);  // no fielded party: the fits list falls back to the unit content (12 skills)
    // The growth panel would dominate the window height; the offer section is what this test measures.
    boot.registry.ctx().erase<game::FDungeonGrowth>();

    const auto probe = MeasureFitsWrap(boot.registry, "rune.power", "rune.multishot");
    const auto fitting = game::RuneFittingSkillNames(boot.registry, "rune.power");
    CHECK(fitting.size() >= 8u);  // the long line: every party skill accepts it
    std::string expected = "Fits: ";
    for (std::size_t index = 0; index < fitting.size(); ++index) {
        if (index > 0) expected += ", ";
        expected += fitting[index];
    }
    CHECK(probe.longLog.find(expected) != std::string::npos);  // the full line reaches the frame: nothing is cut
    CHECK(probe.shortLog.find("Shape: spread") != std::string::npos);  // the short card's effect line rendered

    std::cout << "  Fits: modal delta " << probe.longHeight - probe.shortHeight << ", Fits item "
              << probe.longFitsItemHeight << " vs " << probe.shortFitsItemHeight << ", one text line "
              << probe.lineHeight << '\n';
    const float windowDelta = probe.longHeight - probe.shortHeight;
    const float fitsDelta = probe.longFitsItemHeight - probe.shortFitsItemHeight;
    // The long Fits string really folds into more than one extra line of height ...
    CHECK(fitsDelta > probe.lineHeight);
    // ... and the view did not clip it: the modal grew by exactly that item's delta. With TextDisabled the
    // clipped item would add ~no window height (windowDelta ~0) and this equality fails.
    CHECK(windowDelta > fitsDelta - 1.f && windowDelta < fitsDelta + 1.f);
}

void CampShowsDisplayNames() {
    FBoot boot(UniquePath("camp-names"));
    StartFresh(boot);
    auto& run = boot.run();
    run.phase = game::EDungeonPhase::Camp;
    run.party[0].skills = {game::FSkillSlot{"skill.strike", {}}};
    const std::size_t index = CollectRune(boot, kRune);
    CHECK(!game::EquipDungeonRune(boot.registry, 0, 0, index));

    // The ImGui log expands the member and "Skills & runes" tree nodes while logging (auto-open depth 2).
    const std::string log = RenderModalLog(boot.registry, std::string(game::kRunModalCamp), {}, -1);
    const auto& content = boot.registry.ctx().get<game::gameplay::FContentRegistry>();
    const auto* skill = content.skills.find("skill.strike");
    const auto* rune = content.runes.find(kRune);
    CHECK(skill != nullptr && rune != nullptr);
    CHECK(log.find(skill->displayName) != std::string::npos);                 // "Heavy Strike", not "skill.strike"
    CHECK(log.find(rune->displayName + "  [remove]") != std::string::npos);   // "Brutality  [remove]"
    CHECK(log.find("skill.strike") == std::string::npos);  // the raw id is tooltip-only now
    CHECK(log.find(kRune) == std::string::npos);
}

// ------------------------------------------------------------------------------------------------
// The fit block (WP-27 §1, rendered)
// ------------------------------------------------------------------------------------------------

void PickedRuneShowsFitBlock() {
    FBoot boot(UniquePath("equip-fit-block"));
    RewardWithLoadout(boot);
    CHECK(boot.rewards().offer.active);
    const std::string picked = boot.rewards().offer.candidates.front();
    CHECK(game::ResolveRuneOffer(boot.registry, 0));  // the pick lands in the collected pool
    CHECK(!boot.rewards().offer.active);

    const auto& content = boot.registry.ctx().get<game::gameplay::FContentRegistry>();
    const std::string runeName = game::ContentDisplayName(content.runes.find(picked), picked);

    // The pick id is view-owned state (a real click sets it in DrawRuneOfferCard): seed it exactly as that
    // click would, then read the rendered modal.
    const std::string log = RenderModalLog(boot.registry, std::string(game::kRunModalReward), [&] {
        boot.registry.ctx().get<game::FDungeonUiState>().pickedRuneId = picked;
    });
    CHECK(log.find("FIT RUNE") != std::string::npos);
    CHECK(log.find("Equip on:") != std::string::npos);
    // The offer is resolved but the copy is unfitted: the modal says what was picked instead of claiming
    // there was no offer at all.
    CHECK(log.find("Picked: " + runeName + " — fit it now or later at camp.") != std::string::npos);
    CHECK(log.find("No rune offer this room.") == std::string::npos);
    // Continue sits right after the offer section, above the growth panel. "Level " is not a safe marker
    // (the room message may carry "Level up: ..."); "Node points" only exists in the growth panel's line.
    const std::size_t continueAt = log.find("Continue");
    const std::size_t growthAt = log.find("Node points");
    CHECK(continueAt != std::string::npos);
    CHECK(growthAt != std::string::npos);
    CHECK(continueAt < growthAt);

    // The block's first legal pair, in the same deterministic walk the rows use (member asc -> slot asc).
    const auto found = std::ranges::find(boot.run().runes, picked);
    CHECK(found != boot.run().runes.end());
    const std::size_t pickedIndex = static_cast<std::size_t>(found - boot.run().runes.begin());
    FEquipSite site;
    CHECK(FindEquipSite(boot, pickedIndex, site));
    const auto& member = boot.run().party[site.member];
    const std::string skillId = member.skills[site.slot].skillId;
    const std::string skillName = game::ContentDisplayName(content.skills.find(skillId), skillId);
    CHECK(log.find(game::MemberName(member)) != std::string::npos);  // the row's member label
    // The button's log form is ImGui's "[ label ]" (ButtonEx brackets the visible label; "##m<i>s<j>" is
    // hidden): the first legal pair's skill is the first button the block draws.
    CHECK(log.find("[ " + skillName + " ]") != std::string::npos);
    // The hover tooltip ("Equip on <member> · <skill>") is hover-only: a non-hover frame never logs it.
    CHECK(log.find("Equip on ") == std::string::npos);

    // Equip through the same call the block's first button makes, on the same first legal pair.
    const std::string originalMessage = boot.run().message;
    CHECK(!originalMessage.empty());
    CHECK(!game::EquipDungeonRune(boot.registry, site.member, site.slot, pickedIndex));
    // The equip path never clobbers the step's own message (the button appends its sentence to it).
    CHECK(boot.run().message.find(originalMessage) != std::string::npos);

    // The copy is fitted now and the pick id is still the view's: no block, and the picked line says so.
    const std::string afterLog = RenderModalLog(boot.registry, std::string(game::kRunModalReward));
    CHECK(afterLog.find("FIT RUNE") == std::string::npos);
    CHECK(afterLog.find("Equip on:") == std::string::npos);
    CHECK(afterLog.find("Picked: " + runeName + " — already fitted.") != std::string::npos);
    CHECK(afterLog.find(originalMessage) != std::string::npos);  // the room text still renders
}

void RefundPickWithEquippedCopyShowsNoBlock() {
    FBoot boot(UniquePath("equip-refund-block"));
    RewardWithLoadout(boot);
    // Fit the rune first, then take a duplicate pick of the same rune: the refund policy adds no copy.
    const std::size_t index = CollectRune(boot, kRune);
    FEquipSite site;
    CHECK(FindEquipSite(boot, index, site));
    CHECK(!game::EquipDungeonRune(boot.registry, site.member, site.slot, index));
    CHECK(std::ranges::find(boot.run().runes, std::string(kRune)) == boot.run().runes.end());
    boot.rewards().offer.active = true;
    boot.rewards().offer.candidates = {kRune};
    const int materials = boot.run().materials;
    CHECK(game::ResolveRuneOffer(boot.registry, 0));  // already owned + fitted: refund, no new copy
    CHECK(boot.run().materials == materials + boot.rewards().tables.duplicateRefundMaterials);
    CHECK(std::ranges::find(boot.run().runes, std::string(kRune)) == boot.run().runes.end());

    const std::string log = RenderModalLog(boot.registry, std::string(game::kRunModalReward), [&] {
        boot.registry.ctx().get<game::FDungeonUiState>().pickedRuneId = kRune;  // what the pick click records
    });
    const auto& content = boot.registry.ctx().get<game::gameplay::FContentRegistry>();
    const std::string runeName = game::ContentDisplayName(content.runes.find(kRune), kRune);
    CHECK(log.find("FIT RUNE") == std::string::npos);  // no unfitted copy: no block, Continue only
    CHECK(log.find("Equip on:") == std::string::npos);
    // The refund pick still says what was picked — and that there is nothing left to fit.
    CHECK(log.find("Picked: " + runeName + " — already fitted.") != std::string::npos);
    CHECK(log.find("No rune offer this room.") == std::string::npos);  // the picked line replaces it
}

void FitBlockZeroLegalFallsBack() {
    FBoot boot(UniquePath("equip-zero-legal"));
    RewardWithLoadout(boot);
    // No party skill accepts the picked rune: every slot becomes a Support/Duration skill while the rune
    // demands an Attack skill, so the block's deterministic walk finds no legal pair at all.
    for (auto& member : boot.run().party) member.skills = {game::FSkillSlot{"skill.buff", {}}};
    boot.rewards().offer.active = false;  // the pick already resolved the offer
    const std::size_t index = CollectRune(boot, kUnfittableRune);
    FEquipSite site;
    CHECK(!FindEquipSite(boot, index, site));  // the precondition: every pair is refused
    const auto reason = game::ValidateDungeonRuneEquip(boot.registry, 0, 0, index);
    CHECK(reason.has_value());
    CHECK(!reason->empty());

    const std::string log = RenderModalLog(boot.registry, std::string(game::kRunModalReward), [&] {
        boot.registry.ctx().get<game::FDungeonUiState>().pickedRuneId = kUnfittableRune;
    });
    CHECK(log.find("FIT RUNE") != std::string::npos);
    CHECK(log.find(*reason) != std::string::npos);  // the first refusal in the walk, verbatim
    CHECK(log.find("Equip later at camp.") != std::string::npos);
    CHECK(log.find("Equip on:") == std::string::npos);  // zero legal pairs: no rows exist
}

} // namespace

int main() {
    // Feature tests get no per-test save directory from CMake: isolate every save under the build tree so a
    // run can never touch %APPDATA%\Game01P\EmberDungeon.
    const char* saveDir = nullptr;
#ifdef _MSC_VER
#pragma warning(suppress : 4996)  // std::getenv: read-only, no buffer to overflow
#endif
    saveDir = std::getenv("GAME01P_SAVE_DIR");
    if (saveDir == nullptr || *saveDir == '\0') {
        const std::string path =
            (std::filesystem::current_path() / "out" / "test-saves" / "reward_equip_tests").string();
        std::error_code error;
        std::filesystem::create_directories(path, error);
        _putenv_s("GAME01P_SAVE_DIR", path.c_str());
    }

    return game_test::RunTests("reward equip", {
        {"EquipAllowedInRewardPhase", EquipAllowedInRewardPhase},
        {"EquipRejectedInBattleDefeatAndEvent", EquipRejectedInBattleDefeatAndEvent},
        {"EquipStillAllowedAtCamp", EquipStillAllowedAtCamp},
        {"FitAtPickAppliesNextBattle", FitAtPickAppliesNextBattle},
        {"FitAtPickSavesAndRestores", FitAtPickSavesAndRestores},
        {"FittedRuneMovesFromPoolToSlot", FittedRuneMovesFromPoolToSlot},
        {"FitLaterKeepsRuneInPool", FitLaterKeepsRuneInPool},
        {"EquipByIndexWithDuplicateCopies", EquipByIndexWithDuplicateCopies},
        {"RewardEquipIsFree", RewardEquipIsFree},
        {"LoadoutLineWritten", LoadoutLineWritten},
        {"SchemaAcceptsLoadout", SchemaAcceptsLoadout},
        {"CardExplainsHooks", CardExplainsHooks},
        {"FitsLineWraps", FitsLineWraps},
        {"CampShowsDisplayNames", CampShowsDisplayNames},
        {"PickedRuneShowsFitBlock", PickedRuneShowsFitBlock},
        {"RefundPickWithEquippedCopyShowsNoBlock", RefundPickWithEquippedCopyShowsNoBlock},
        {"FitBlockZeroLegalFallsBack", FitBlockZeroLegalFallsBack},
    });
}
