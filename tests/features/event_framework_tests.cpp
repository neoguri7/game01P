// WP-09: the room type table, the event framework and the event save section. The suite pins the data
// contracts (schema, gates, numbers, pools), the pick rule (integer hash, run-scope non-repeat) and the
// two-stage flow (choose = resolve only, continue = advance) end to end through the run save.
//
// Everything the tests expect is derived from the loaded fixture data (dungeon balance/recovery, units,
// the fixture events.json) or from the documented rule under test. The remaining literals are the WP's
// own contracts: the seed id/pool, the fixture pool tags ("common"/"test"), the fixture event ids, and
// the D-3 payout read straight from the SHIP assets/data/dungeon.json.
//
// Sections: schema/loader (EventsLoad over a temp events.json), numbers/effects, stats, dispatch/pools,
// pick, non-repeat, and the two-stage flow + log + save section.

#define GAME01P_TEST_SUPPORT_CHECK
#include "../TestSupport.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <SDL3/SDL.h>
#include <imgui.h>
#include <nlohmann/json.hpp>

#include "core/AssetManager.h"
#include "core/EngineState.h"
#include "core/ResourceManager.h"
#include "core/SDLDeleter.h"
#include "gameplay/GameplayServices.h"
#include "gameplay/combat/FEffectHooks.h"
#include "gameplay/data/FEffectContent.h"
#include "gameplay/dungeon/FDungeonRun.h"
#include "gameplay/dungeon/FDungeonSave.h"
#include "gameplay/dungeon/FEventContent.h"
#include "gameplay/dungeon/FEventEffects.h"
#include "gameplay/dungeon/FEventSection.h"
#include "gameplay/dungeon/FRewards.h"
#include "gameplay/dungeon/FRoomTypeRegistry.h"
#include "gameplay/dungeon/FRunEventLog.h"
#include "gameplay/dungeon/FRunFlow.h"
#include "gameplay/dungeon/FRunSequence.h"
#include "gameplay/dungeon/FSaveSections.h"
#include "presentation/FDungeonArt.h"
#include "presentation/FDungeonOverlay.h"
#include "presentation/FDungeonViews.h"
#include "presentation/FModalViewRegistry.h"
#include "states/FBaseState.h"
#include "states/FEmberBattleState.h"
#include "states/FEmberModalHostState.h"

namespace {

using nlohmann::json;

constexpr std::uint32_t kSeed = 4242;

/// The fixture event ids the fixture events.json authors (the M1 fixture contract; the tests read the
/// file for everything else, but the gate/effect probes address these by id).
constexpr std::string_view kFixtureGatedEvent = "event.test_sealed_cache";
constexpr std::string_view kFixtureSpringEvent = "event.test_spring";

// --- scratch files (camp_tests pattern) ---------------------------------------------------------

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

json FixtureDoc(const char* file) { return ReadJsonFile(FixtureDataPath(file)); }

// --- a booted registry with its own checkpoint and save slot (camp_tests pattern) ---------------

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

// --- a temporary asset root layered over the fixture (data override pattern) --------------------

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

/// LoadEventDefs through a manager that sees `temp` first: true = the structural loader accepted the doc.
bool EventsLoad(const FTempAssets& assets) {
    entt::registry registry;
    game::FAssetManager::Initialize(registry, assets.Roots());
    const auto defs = game::LoadEventDefs(registry.ctx().get<game::FAssetManager>());
    return defs.has_value();
}

/// The full gameplay boot (content + AI + effects + WP-09 cross validation); false = a loader refused.
bool ContentBoots(const std::vector<std::string>& roots) {
    entt::registry registry;
    game::FAssetManager::Initialize(registry, roots);
    return game::gameplay::FGameplayServices::Initialize(registry);
}

/// The fixture dungeon with both walked event slots pointed at the "test" pool (the non-repeat probe).
json TestPoolDungeon() {
    auto document = FixtureDoc("dungeon.json");
    document["themes"][0]["dungeons"][0]["rooms"][1]["event_pool"] = "test";
    document["themes"][0]["dungeons"][1]["rooms"][1]["event_pool"] = "test";
    return document;
}

// --- run flow helpers ---------------------------------------------------------------------------

void StartFresh(FBoot& boot) { game::StartRun(boot.registry, kSeed, true); }

/// Simulate a process restart: drop the live run and every run-scope module object the ctx would lose on
/// reload (FEventInstance / FEventRunState) — after this only the save file can bring the state back.
void ForgetRunInMemory(FBoot& boot) {
    boot.registry.ctx().erase<game::FDungeonRun>();
    boot.registry.ctx().erase<game::FEventInstance>();
    boot.registry.ctx().erase<game::FEventRunState>();
}

game::FRunMember& Member(game::FDungeonRun& run, std::string_view id) {
    const auto found = std::ranges::find(run.party, id, &game::FRunMember::id);
    CHECK(found != run.party.end());
    return *found;
}

void WinRoom(FBoot& boot) {
    CHECK(game::ResolveRoomVictory(boot.registry, true).has_value());
    CHECK(boot.run().phase == game::EDungeonPhase::Reward);
}

/// The reward step opens a rune offer on every monster room in the frozen fixture (chance 1.0); the offer
/// is a decision, so the flow refuses to continue until it is skipped.
void SkipOfferIfActive(FBoot& boot) {
    auto* rewards = boot.registry.ctx().find<game::FRewards>();
    CHECK(rewards != nullptr);
    if (rewards->offer.active) CHECK(game::ResolveRuneOffer(boot.registry, -1));
}

void ContinueRewardStep(FBoot& boot) {
    CHECK(game::ApplyRunIntent(boot.registry, game::ERunIntent::ContinueReward).has_value());
}

/// Battle victory + offer decision + reward continue: reaches the event room of the gap after the battle.
std::optional<game::FEventInstance> EnterEvent(FBoot& boot) {
    WinRoom(boot);
    SkipOfferIfActive(boot);
    ContinueRewardStep(boot);
    CHECK(boot.run().phase == game::EDungeonPhase::Event);
    return game::CurrentEventInstance(boot.registry);
}

/// Choose (resolve only) + continue (advance): the two-stage path through the event room.
void ResolveEvent(FBoot& boot, int choice) {
    const auto resolved = game::ApplyRunIntent(boot.registry, game::ERunIntent::ChooseEventOption, choice);
    CHECK(resolved.has_value());
    CHECK(resolved->exit == game::ERunExit::None);  // resolving keeps the modal on the result stage
    CHECK(boot.run().phase == game::EDungeonPhase::Event);
    CHECK(game::ApplyRunIntent(boot.registry, game::ERunIntent::ContinueEvent).has_value());
    CHECK(boot.run().phase == game::EDungeonPhase::Battle);
    CHECK(!boot.registry.ctx().contains<game::FEventInstance>());
}

int EventChosenCount(const entt::registry& registry) {
    const auto* log = registry.ctx().find<game::FRunEventLog>();
    if (log == nullptr) return 0;
    return static_cast<int>(std::count_if(log->tail.begin(), log->tail.end(), [](const game::FRunEvent& event) {
        return event.kind == game::ERunEventKind::EventChosen;
    }));
}

/// The WP-09 loss preview's own name for a member must be the member display name (id fallback) — the
/// same string the view prints under the button.
std::string PreviewMemberName(FBoot& boot, std::size_t index) {
    CHECK(index < boot.run().party.size());
    return game::MemberName(boot.run().party[index]);
}

// --- headless render of the event modal (camp_tests RenderModalLogs pattern) --------------------

std::string RenderEventModal(FBoot& boot) {
    std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> surface(
        SDL_CreateSurface(1280, 800, SDL_PIXELFORMAT_RGBA8888), &SDL_DestroySurface);
    CHECK(surface != nullptr);
    FRendererPtr renderer{SDL_CreateSoftwareRenderer(surface.get())};
    CHECK(renderer != nullptr);

    auto* window = boot.registry.ctx().find<game::FEngineWindowState>();
    if (window == nullptr) window = &boot.registry.ctx().emplace<game::FEngineWindowState>();
    window->width = 1280;
    window->height = 800;
    auto* resources = boot.registry.ctx().find<game::FResourceManager>();
    if (resources == nullptr) resources = &boot.registry.ctx().emplace<game::FResourceManager>();
    resources->init(renderer.get());
    CHECK(game::InitializeDungeonArt(boot.registry));

    std::unique_ptr<ImGuiContext, decltype(&ImGui::DestroyContext)> context(
        ImGui::CreateContext(), &ImGui::DestroyContext);
    auto& io = ImGui::GetIO();
    io.DisplaySize = {1280, 800};
    io.DeltaTime = 1.f / 60.f;
    io.Fonts->Build();
    game::InitializeDungeonUi(boot.registry);

    const std::string logPath = UniquePath("event-ui") + ".log";
    for (int frame = 0; frame < 3; ++frame) {
        ImGui::NewFrame();
        if (frame == 2) ImGui::LogToFile(0, logPath.c_str());
        const auto* view = game::FindModalView(boot.registry, game::kRunModalEvent);
        CHECK(view != nullptr);
        view->render(boot.registry);
        if (frame == 2) ImGui::LogFinish();
        ImGui::Render();
    }
    const std::string text = ReadBytes(logPath);
    std::error_code error;
    std::filesystem::remove(logPath, error);
    return text;
}

// ---------------------------------------------------------------------------------------------
// Schema / loader
// ---------------------------------------------------------------------------------------------

void EventSchemaValidated() {
    FBoot boot(UniquePath("events-schema"));
    const auto defs = game::LoadEventDefs(boot.registry.ctx().get<game::FAssetManager>());
    CHECK(defs.has_value());
    CHECK(defs->size() == 3u);  // the fixture authors the gate probe and the two ungated events
    bool sawGate = false;
    for (const auto& def : *defs) {
        CHECK(!def.id.empty());
        CHECK(!def.title.empty());
        CHECK(!def.body.empty());
        CHECK(!def.tags.empty());
        CHECK(def.choices.size() >= game::kEventMinChoices);
        CHECK(def.choices.size() <= game::kEventMaxChoices);
        for (const auto& choice : def.choices) {
            CHECK(!choice.text.empty());
            CHECK(!choice.resultText.empty());
            for (const auto& effect : choice.effects) CHECK(!effect.kind.empty());
            if (choice.check.has_value()) {
                sawGate = true;
                CHECK(std::ranges::find(game::kEventCheckStats, choice.check->stat) !=
                      std::end(game::kEventCheckStats));
                CHECK(choice.check->difficulty >= game::kEventCheckDifficultyMin);
                CHECK(choice.check->difficulty <= game::kEventCheckDifficultyMax);
            }
        }
    }
    CHECK(sawGate);

    // LoadEventContent installs the same list; the catalog then carries the seed first, then the file order.
    CHECK(game::LoadEventContent(boot.registry, boot.registry.ctx().get<game::FAssetManager>()));
    CHECK(game::FindEventDef(boot.registry, defs->front().id).has_value());
    const auto catalog = game::EventCatalog(boot.registry);
    CHECK(catalog.size() == defs->size() + 1u);
    CHECK(catalog.front().id == game::kLegacySeedEventId);
}

void UnknownKeyRejected() {
    FTempAssets assets("events-unknown-key");
    const json base = FixtureDoc("events.json");
    CHECK(base["events"].is_array() && base["events"].size() == 3u);
    const auto edit = [&base](auto&& change) {
        auto document = base;
        change(document);
        return document;
    };
    const json unknownEffectKey = edit([](json& d) {
        d["events"][1]["choices"][0]["effects"][0]["who"] = "party";
    });
    const std::vector<std::pair<const char*, json>> cases = {
        {"unknown key in the event", edit([](json& d) { d["events"][0]["surprise"] = 1; })},
        {"unknown key in check", edit([](json& d) { d["events"][0]["choices"][0]["check"]["bonus"] = 1; })},
        {"unknown key in an effect", unknownEffectKey},
    };
    for (const auto& [label, document] : cases) {
        assets.Write("events.json", document);
        const bool loaded = EventsLoad(assets);
        if (loaded) std::cout << "    accepted: " << label << '\n';
        CHECK(!loaded);
    }
    assets.Write("events.json", base);
    CHECK(EventsLoad(assets));
}

void ResultTextIsSingleText() {
    FTempAssets assets("events-result-text");
    const json base = FixtureDoc("events.json");
    const auto edit = [&base](auto&& change) {
        auto document = base;
        change(document);
        return document;
    };
    const std::vector<json> cases = {
        edit([](json& d) { d["events"][0]["choices"][0]["result_text"] = json{{"text", "x"}}; }),
        edit([](json& d) { d["events"][0]["choices"][0]["result_text"] = json::array({"x"}); }),
    };
    for (const auto& document : cases) {
        assets.Write("events.json", document);
        CHECK(!EventsLoad(assets));  // result_text is a single Text — there is no success/failure union
    }
    assets.Write("events.json", base);
    CHECK(EventsLoad(assets));
}

void BadCheckStatFailsBoot() {
    FTempAssets assets("events-bad-stat");
    const json base = FixtureDoc("events.json");
    auto document = base;
    document["events"][0]["choices"][0]["check"]["stat"] = "luck";  // outside the six upgrade stats
    assets.Write("events.json", document);
    CHECK(!EventsLoad(assets));
    CHECK(!ContentBoots(assets.Roots()));
    assets.Write("events.json", base);
    CHECK(EventsLoad(assets));
    CHECK(ContentBoots(assets.Roots()));
}

void CheckGateDisablesWhenUnmet() {
    FBoot boot(UniquePath("events-gate"));
    StartFresh(boot);
    CHECK(EnterEvent(boot).has_value());
    // Drive the gate with the fixture gated event instead of whatever the "common" pool picked.
    boot.registry.ctx().insert_or_assign<game::FEventInstance>(
        game::FEventInstance{std::string(kFixtureGatedEvent), -1, false});
    const auto def = game::FindEventDef(boot.registry, kFixtureGatedEvent);
    CHECK(def.has_value());
    CHECK(!def->choices.empty() && def->choices[0].check.has_value());
    const game::FEventCheck& check = *def->choices[0].check;
    auto& run = boot.run();

    const game::FEventStatReading best = game::EventPartyStat(boot.registry, check.stat);
    CHECK(best.resolved);
    CHECK(best.value < check.difficulty);  // the fixture party is authored one point short
    const std::string closedReason = game::EventStatLabel(check.stat) + " " + std::to_string(best.value) +
                                     " < " + std::to_string(check.difficulty);
    const auto closed = game::EvaluateEventGate(boot.registry, def->choices[0]);
    CHECK(!closed.open);
    CHECK(closed.reason == closedReason);  // "Attack 8 < 9"

    // A closed gate refuses the choice with the reason and changes nothing: no effect, no seen, no log.
    const int materials = run.materials;
    const int chosen = EventChosenCount(boot.registry);
    std::vector<int> points;
    for (const auto& member : run.party) points.push_back(member.passivePoints);
    const auto refused = game::ApplyRunIntent(boot.registry, game::ERunIntent::ChooseEventOption, 0);
    CHECK(!refused.has_value());
    CHECK(refused.error().find(closedReason) != std::string::npos);
    CHECK(run.materials == materials);
    CHECK(!boot.registry.ctx().get<game::FEventInstance>().resolved);
    CHECK(game::EventSeen(boot.registry).empty());
    CHECK(EventChosenCount(boot.registry) == chosen);
    for (std::size_t index = 0; index < run.party.size(); ++index) {
        CHECK(run.party[index].passivePoints == points[index]);
    }

    // One bought point opens the gate and the reason flips to the satisfied form with the leader's name.
    Member(run, "party.warrior").bonusAttack += 1;
    const game::FEventStatReading openBest = game::EventPartyStat(boot.registry, check.stat);
    CHECK(openBest.value >= check.difficulty);
    const std::string openReason = game::EventStatLabel(check.stat) + " " + std::to_string(openBest.value) +
                                   " >= " + std::to_string(check.difficulty) + " (" + openBest.memberName + ")";
    const auto open = game::EvaluateEventGate(boot.registry, def->choices[0]);
    CHECK(open.open);
    CHECK(open.reason == openReason);  // "Attack 9 >= 9 (Alden / Warrior)"

    int nodeGain = 1;
    for (const auto& effect : def->choices[0].effects) {
        if (effect.kind == game::kEventEffectGainNodePoints) nodeGain = effect.amount > 0 ? effect.amount : 1;
    }
    CHECK(game::ApplyRunIntent(boot.registry, game::ERunIntent::ChooseEventOption, 0).has_value());
    CHECK(boot.registry.ctx().get<game::FEventInstance>().resolved);
    CHECK(game::EventSeen(boot.registry) == std::vector<std::string>{std::string(kFixtureGatedEvent)});
    for (std::size_t index = 0; index < run.party.size(); ++index) {
        CHECK(run.party[index].passivePoints == points[index] + nodeGain);
    }
}

void CheckHasNoFailureBranch() {
    FTempAssets assets("events-no-failure-branch");
    const json base = FixtureDoc("events.json");
    const auto edit = [&base](auto&& change) {
        auto document = base;
        change(document);
        return document;
    };
    const std::vector<json> cases = {
        edit([](json& d) { d["events"][0]["choices"][0]["on_failure"] = json{{"effects", json::array()}}; }),
        edit([](json& d) { d["events"][0]["choices"][0]["failure_text"] = "You fail."; }),
    };
    for (const auto& document : cases) {
        assets.Write("events.json", document);
        CHECK(!EventsLoad(assets));  // a gate has no failure branch: the keys do not exist
    }
    assets.Write("events.json", base);
    CHECK(EventsLoad(assets));
}

void GatedOptionsLeaveAChoice() {
    FTempAssets assets("events-all-gated");
    const json base = FixtureDoc("events.json");
    auto document = base;
    // Mirror the sealed cache's gate onto its ungated second choice: 2 of 2 gated = the player has no
    // real decision left when qualified (the cross validation refuses it).
    document["events"][0]["choices"][1]["check"] = document["events"][0]["choices"][0]["check"];
    assets.Write("events.json", document);
    CHECK(EventsLoad(assets));  // structurally legal: the rule is cross validation, not the schema
    CHECK(!ContentBoots(assets.Roots()));
    assets.Write("events.json", base);
    CHECK(ContentBoots(assets.Roots()));
}

// ---------------------------------------------------------------------------------------------
// Numbers and effects
// ---------------------------------------------------------------------------------------------

void EventHealClampsToMax() {
    FBoot boot(UniquePath("events-heal-clamp"));
    StartFresh(boot);
    const auto def = game::FindEventDef(boot.registry, game::kLegacySeedEventId);
    CHECK(def.has_value() && !def->choices.empty());
    const game::FEventChoice& rest = def->choices[0];
    CHECK(!rest.effects.empty());
    CHECK(rest.effects[0].kind == game::kEventEffectRestorePartyHp);
    const int heal = rest.effects[0].amount;
    CHECK(heal > 0);

    auto& run = boot.run();
    const int max0 = game::DungeonMemberMaxHealth(boot.registry, run.party[0]);
    const int max1 = game::DungeonMemberMaxHealth(boot.registry, run.party[1]);
    const int max3 = game::DungeonMemberMaxHealth(boot.registry, run.party[3]);
    CHECK(max0 > 0 && max1 > 0 && max3 > 0);
    run.party[0].health = max0;         // full: nothing to add
    run.party[1].health = max1 - 1;     // one short: clamps exactly at the maximum
    run.party[2].health = -1;           // unclaimed sentinel: skipped (already full)
    run.party[3].health = 1;            // far from full: gains the full amount

    const auto outcome = game::ApplyEventChoice(boot.registry, rest);
    CHECK(outcome.has_value());
    CHECK(run.party[0].health == max0);
    CHECK(run.party[1].health == max1);
    CHECK(run.party[2].health == -1);
    const int gain3 = std::min(heal, max3 - 1);
    CHECK(run.party[3].health == 1 + gain3);
    CHECK(outcome->hpRestored == 1 + gain3);
}

void DamagePartyStopsAtOneHp() {
    FBoot boot(UniquePath("events-damage-floor"));
    StartFresh(boot);
    const auto def = game::FindEventDef(boot.registry, kFixtureSpringEvent);
    CHECK(def.has_value() && def->choices.size() >= 2u);
    const game::FEventChoice& drink = def->choices[1];
    int damage = 0;
    for (const auto& effect : drink.effects) {
        if (effect.kind == game::kEventEffectDamageParty) damage = effect.amount;
    }
    CHECK(damage >= 2);  // a 3 HP member must be able to reach the floor

    auto& run = boot.run();
    const int max3 = game::DungeonMemberMaxHealth(boot.registry, run.party[3]);
    CHECK(max3 > 0);
    run.party[0].health = 3;
    run.party[1].health = 2;
    run.party[2].health = 1;  // already at the floor
    run.party[3].health = -1;  // unclaimed = full: the flat damage still materialises it

    const auto preview = game::PreviewEventChoiceLoss(boot.registry, drink);
    CHECK(preview.size() == run.party.size());
    CHECK(preview[0].loss == std::min(damage, 2));
    CHECK(preview[1].loss == std::min(damage, 1));
    CHECK(preview[2].loss == 0);
    CHECK(preview[3].loss == std::min(damage, max3 - 1));

    const auto outcome = game::ApplyEventChoice(boot.registry, drink);
    CHECK(outcome.has_value());
    CHECK(run.party[0].health == 1);  // 3 - 5 clamps at the 1 HP floor, never 0
    CHECK(run.party[1].health == 1);
    CHECK(run.party[2].health == 1);  // no loss at the floor
    CHECK(run.party[3].health == max3 - preview[3].loss);  // the sentinel paid from full max
    CHECK(run.party[3].health >= 1);
    for (const auto& member : run.party) CHECK(member.health >= 1);
    CHECK(outcome->hpLost == preview[0].loss + preview[1].loss + preview[2].loss + preview[3].loss);
}

void ChestCostMatchesFraction() {
    FBoot boot(UniquePath("events-chest-fraction"));
    StartFresh(boot);
    CHECK(EnterEvent(boot).has_value());
    const auto def = game::FindEventDef(boot.registry, game::kLegacySeedEventId);
    CHECK(def.has_value() && def->choices.size() >= 2u);
    const game::FEventChoice& cache = def->choices[1];
    const double fraction = game::DungeonRecovery(boot.registry).eventChestHpCostFraction;
    CHECK(fraction > 0.0);
    int pct = 0;
    for (const auto& effect : cache.effects) {
        if (effect.kind == game::kEventEffectDamagePartyPct) pct = effect.amount;
    }
    CHECK(pct == static_cast<int>(std::lround(fraction * 100.0)));

    auto& run = boot.run();
    run.party[0].health = -1;  // unclaimed: effective HP = full max
    run.party[1].health = game::DungeonMemberMaxHealth(boot.registry, run.party[1]) - 3;
    run.party[2].health = 1;   // the floor: an honest 0 cost
    run.party[3].health = 5;
    std::vector<int> effective;
    std::vector<int> before;
    for (const auto& member : run.party) {
        const int maxHealth = game::DungeonMemberMaxHealth(boot.registry, member);
        CHECK(maxHealth > 0);
        effective.push_back(game::DungeonEffectiveHealth(boot.registry, member));
        before.push_back(member.health);
        CHECK(game::ChestHpLossFor(boot.registry, member) ==
              game::ChestHpLoss(maxHealth, game::DungeonEffectiveHealth(boot.registry, member), fraction));
    }
    CHECK(effective[0] == game::DungeonMemberMaxHealth(boot.registry, run.party[0]));

    const auto preview = game::PreviewEventChoiceLoss(boot.registry, cache);
    CHECK(preview.size() == run.party.size());
    for (std::size_t index = 0; index < run.party.size(); ++index) {
        CHECK(preview[index].loss == game::ChestHpLossFor(boot.registry, run.party[index]));
    }

    const auto resolved = game::ApplyRunIntent(boot.registry, game::ERunIntent::ChooseEventOption, 1);
    CHECK(resolved.has_value() && resolved->exit == game::ERunExit::None);
    CHECK(run.phase == game::EDungeonPhase::Event);
    for (std::size_t index = 0; index < run.party.size(); ++index) {
        const int expected = preview[index].loss > 0 ? effective[index] - preview[index].loss : before[index];
        CHECK(run.party[index].health == expected);  // displayed loss == applied loss
    }
    CHECK(run.message.find(cache.resultText) != std::string::npos);
    CHECK(run.message.find("party HP -") != std::string::npos);  // the outcome summary states the cost
}

void GainMaterialsIsRunScope() {
    FBoot boot(UniquePath("events-scope"));
    StartFresh(boot);
    auto& run = boot.run();
    const int before = run.materials;
    const game::gameplay::FEffectSpec spec{.kind = std::string(game::kEventEffectGainMaterials), .amount = 1};

    // A Battle-scope dispatch must refuse a Run effect: event effects never fire from a battle hook.
    const game::FEffectContext battle{.scope = game::gameplay::FEffectScope::Battle, .source = "event-test"};
    CHECK(!game::ApplyEffect(boot.registry, battle, spec));
    CHECK(run.materials == before);

    const game::FEffectContext runScope{.scope = game::gameplay::FEffectScope::Run, .source = "event-test"};
    CHECK(game::ApplyEffect(boot.registry, runScope, spec));
    CHECK(run.materials == before + 1);
}

void ReplaceLegacyShrine() {
    // Against the frozen fixture: both choices carry exactly the loaded balance values.
    {
        FBoot boot(UniquePath("events-seed"));
        StartFresh(boot);
        const int heal = game::DungeonBalance(boot.registry, "event_heal");
        const int materials = game::DungeonBalance(boot.registry, "event_materials");
        const int pct = static_cast<int>(
            std::lround(game::DungeonRecovery(boot.registry).eventChestHpCostFraction * 100.0));
        const auto catalog = game::EventCatalog(boot.registry);
        CHECK(!catalog.empty());
        CHECK(catalog.front().id == game::kLegacySeedEventId);  // seed first in the registration order
        const game::FEventDef& seed = catalog.front();
        CHECK(seed.title == "A Quiet Shrine");
        CHECK(seed.body == "A quiet shrine offers shelter, or a sealed cache of reinforcement materials.");
        CHECK(seed.tags == std::vector<std::string>{std::string(game::kLegacySeedPool)});
        CHECK(seed.choices.size() == 2u);

        const game::FEventChoice& rest = seed.choices[0];
        CHECK(rest.text == "Rest at the shrine  /  heal " + std::to_string(heal) + " HP");
        CHECK(rest.effects.size() == 1u);
        CHECK(rest.effects[0].kind == game::kEventEffectRestorePartyHp);
        CHECK(rest.effects[0].amount == heal);
        CHECK(rest.resultText == "The party rests at the shrine.");

        const game::FEventChoice& cache = seed.choices[1];
        const std::string cost = pct > 0 ? "  /  costs party HP" : "";
        CHECK(cache.text == "Open the cache  /  gain " + std::to_string(materials) + " materials" + cost);
        CHECK(cache.effects.size() == (pct > 0 ? 2u : 1u));
        CHECK(cache.effects[0].kind == game::kEventEffectGainMaterials);
        CHECK(cache.effects[0].amount == materials);
        if (pct > 0) {
            CHECK(cache.effects[1].kind == game::kEventEffectDamagePartyPct);
            CHECK(cache.effects[1].amount == pct);
        }
        CHECK(!cache.resultText.empty());  // the amounts come from the applied outcome line, not the text
    }
    // Retuned balance in an override root: the seed is rebuilt from data on every catalog read (boot order
    // loads balance after FGameplayServices, so the numbers cannot be cached in the table).
    {
        FTempAssets assets("events-seed-retune");
        constexpr int kRetunedHeal = 42;
        constexpr int kRetunedMaterials = 6;
        auto document = FixtureDoc("dungeon.json");
        document["balance"]["event_heal"] = kRetunedHeal;
        document["balance"]["event_materials"] = kRetunedMaterials;
        assets.Write("dungeon.json", document);
        FBoot boot(UniquePath("events-seed-retune-boot"), assets.Roots());
        StartFresh(boot);
        const auto seed = game::FindEventDef(boot.registry, game::kLegacySeedEventId);
        CHECK(seed.has_value());
        CHECK(seed->choices[0].text == "Rest at the shrine  /  heal " + std::to_string(kRetunedHeal) + " HP");
        CHECK(seed->choices[0].effects[0].amount == game::DungeonBalance(boot.registry, "event_heal"));
        CHECK(seed->choices[1].text.find("gain " + std::to_string(kRetunedMaterials) + " materials") !=
              std::string::npos);
        CHECK(seed->choices[1].effects[0].amount == game::DungeonBalance(boot.registry, "event_materials"));
    }
}

void EventIncomeBindsD3() {
    FBoot boot(UniquePath("events-income-d3"));
    StartFresh(boot);
    const auto seed = game::FindEventDef(boot.registry, game::kLegacySeedEventId);
    CHECK(seed.has_value() && seed->choices.size() >= 2u);
    int seeded = 0;
    for (const auto& effect : seed->choices[1].effects) {
        if (effect.kind == game::kEventEffectGainMaterials) seeded = effect.amount;
    }
    CHECK(seeded > 0);
    CHECK(seeded == game::DungeonBalance(boot.registry, "event_materials"));  // one canonical source

    // D-3: the SHIP dossier authors the event payout as 3 — read the shipped file itself.
    const json ship = ReadJsonFile(std::string(kShipAssetRoot) + "/data/dungeon.json");
    CHECK(ship["balance"]["event_materials"] == 3);
}

void ChestChoiceShowsPerUnitLoss() {
    FBoot boot(UniquePath("events-loss-ui"));
    StartFresh(boot);
    CHECK(EnterEvent(boot).has_value());
    const auto def = game::FindEventDef(boot.registry, game::kLegacySeedEventId);
    CHECK(def.has_value() && def->choices.size() >= 2u);
    const game::FEventChoice& cache = def->choices[1];
    auto& run = boot.run();
    run.party[2].health = 1;  // one honest 0 in the list

    const auto preview = game::PreviewEventChoiceLoss(boot.registry, cache);
    CHECK(preview.size() == run.party.size());
    std::vector<int> effective;
    std::vector<int> before;
    for (std::size_t index = 0; index < run.party.size(); ++index) {
        CHECK(preview[index].memberName == PreviewMemberName(boot, index));  // the view prints the same name
        effective.push_back(game::DungeonEffectiveHealth(boot.registry, run.party[index]));
        before.push_back(run.party[index].health);
    }

    // The choices stage states the cost before the click: the choice text and one loss per member.
    const std::string log = RenderEventModal(boot);
    CHECK(log.find("costs party HP") != std::string::npos);
    CHECK(log.find("HP cost:") != std::string::npos);
    for (const auto& entry : preview) {
        CHECK(log.find(entry.memberName + " " + std::to_string(entry.loss)) != std::string::npos);
    }

    const auto resolved = game::ApplyRunIntent(boot.registry, game::ERunIntent::ChooseEventOption, 1);
    CHECK(resolved.has_value());
    for (std::size_t index = 0; index < run.party.size(); ++index) {
        const int expected = preview[index].loss > 0 ? effective[index] - preview[index].loss : before[index];
        CHECK(run.party[index].health == expected);  // displayed loss == applied loss
    }
}

// ---------------------------------------------------------------------------------------------
// Party stats
// ---------------------------------------------------------------------------------------------

void StatSourceIsPartyBestPure() {
    FBoot boot(UniquePath("events-stat-best"));
    StartFresh(boot);
    auto& run = boot.run();
    constexpr std::string_view kStat = "attack";

    const game::FEventStatReading reading = game::EventPartyStat(boot.registry, kStat);
    CHECK(reading.resolved);
    CHECK(reading.value > 0);
    // Fresh run: the best attack is the warrior's resolved base (the fixture party's authored leader).
    const auto* warriorUnit = game::ResolveRunMemberUnit(boot.registry, "party.warrior");
    CHECK(warriorUnit != nullptr);
    CHECK(reading.value == game::EventUnitBaseStat(*warriorUnit, kStat));
    CHECK(reading.memberName == game::MemberName(Member(run, "party.warrior")));

    // Pure: the same input yields the same reading and the query mutates nothing.
    const game::FEventStatReading again = game::EventPartyStat(boot.registry, kStat);
    CHECK(again.value == reading.value && again.memberName == reading.memberName);
    for (const auto& member : run.party) CHECK(member.bonusAttack == 0);

    // Ties keep party order; only a strictly larger value takes the lead.
    auto& archer = Member(run, "party.archer");
    const auto* archerUnit = game::ResolveRunMemberUnit(boot.registry, archer.id);
    CHECK(archerUnit != nullptr);
    const int archerBase = game::EventUnitBaseStat(*archerUnit, kStat);
    CHECK(archerBase < reading.value);
    archer.bonusAttack = reading.value - archerBase;  // exact tie
    const game::FEventStatReading tied = game::EventPartyStat(boot.registry, kStat);
    CHECK(tied.value == reading.value);
    CHECK(tied.memberName == game::MemberName(Member(run, "party.warrior")));
    archer.bonusAttack = reading.value - archerBase + 1;  // strictly ahead
    const game::FEventStatReading ahead = game::EventPartyStat(boot.registry, kStat);
    CHECK(ahead.value == reading.value + 1);
    CHECK(ahead.memberName == game::MemberName(archer));

    // No run: unresolved, never a guessed number.
    boot.registry.ctx().erase<game::FDungeonRun>();
    const game::FEventStatReading missing = game::EventPartyStat(boot.registry, kStat);
    CHECK(!missing.resolved);
    CHECK(missing.value == 0);
}

void StatVocabularyMatchesUpgrades() {
    FBoot boot(UniquePath("events-stat-vocabulary"));
    StartFresh(boot);
    const auto& growth = boot.registry.ctx().get<game::FDungeonGrowth>();
    std::set<std::string> fromUpgrades;
    for (const auto& upgrade : growth.statUpgrades) fromUpgrades.insert(upgrade.stat);
    std::set<std::string> vocabulary;
    for (const auto stat : game::kEventCheckStats) vocabulary.insert(std::string(stat));
    CHECK(std::size(game::kEventCheckStats) == 6u);
    CHECK(vocabulary.size() == 6u);            // no duplicate spellings
    CHECK(fromUpgrades == vocabulary);         // the gate speaks exactly the six upgrade stats

    // And the bonus reader knows every vocabulary name (the other half of the six-stat contract).
    game::FRunMember probe;
    probe.bonusHealth = 1;
    probe.bonusAttack = 2;
    probe.bonusMagic = 3;
    probe.bonusDefense = 4;
    probe.bonusMagicDefense = 5;
    probe.bonusSpeed = 6;
    CHECK(game::DungeonMemberStatBonus(probe, "health") == 1);
    CHECK(game::DungeonMemberStatBonus(probe, "attack") == 2);
    CHECK(game::DungeonMemberStatBonus(probe, "magic_attack") == 3);
    CHECK(game::DungeonMemberStatBonus(probe, "defense") == 4);
    CHECK(game::DungeonMemberStatBonus(probe, "magic_defense") == 5);
    CHECK(game::DungeonMemberStatBonus(probe, "speed") == 6);
    CHECK(game::DungeonMemberStatBonus(probe, "luck") == 0);
    for (const auto stat : game::kEventCheckStats) CHECK(!game::EventStatLabel(stat).empty());
    CHECK(game::EventStatLabel("luck") == "luck");  // unknown stats keep the raw id
}

void EventStatUsesResolvedUnit() {
    FBoot boot(UniquePath("events-stat-unit"));
    StartFresh(boot);
    auto& run = boot.run();

    const auto* warriorUnit = game::ResolveRunMemberUnit(boot.registry, "party.warrior");
    CHECK(warriorUnit != nullptr);
    const game::FEventStatReading attack = game::EventPartyStat(boot.registry, "attack");
    CHECK(attack.resolved);
    CHECK(attack.value == game::EventUnitBaseStat(*warriorUnit, "attack"));
    CHECK(game::EventUnitBaseStat(*warriorUnit, "luck") == 0);  // outside the vocabulary: no guess

    // A bought bonus shifts a non-first member's resolved base past everyone: base(ResolveRunMemberUnit)
    // + DungeonMemberStatBonus is the whole reading.
    constexpr int kBonusPastEveryone = 50;
    auto& healer = Member(run, "party.healer");
    healer.bonusSpeed = kBonusPastEveryone;
    const auto* healerUnit = game::ResolveRunMemberUnit(boot.registry, healer.id);
    CHECK(healerUnit != nullptr);
    const game::FEventStatReading speed = game::EventPartyStat(boot.registry, "speed");
    CHECK(speed.resolved);
    CHECK(speed.value == game::EventUnitBaseStat(*healerUnit, "speed") + kBonusPastEveryone);
    CHECK(speed.memberName == game::MemberName(healer));
}

// ---------------------------------------------------------------------------------------------
// Dispatch and pools
// ---------------------------------------------------------------------------------------------

void RoomTypeDispatch() {
    FBoot boot(UniquePath("events-dispatch"));
    const game::FRoomTypeDef* event = game::FindRoomType(boot.registry, game::kRoomTypeEvent);
    CHECK(event != nullptr);
    CHECK(event->id == game::kRoomTypeEvent);
    CHECK(event->modalId == game::kRunModalEvent);
    CHECK(event->requiredSlotKey == game::kRoomSlotKeyEventPool);
    CHECK(game::IsKnownRoomType(boot.registry, game::kRoomTypeEvent));
    CHECK(game::FindRoomType(boot.registry, "shop") == nullptr);
    CHECK(game::FindRoomType(boot.registry, "") == nullptr);

    // The flow opens exactly the table's modal for the event slot.
    StartFresh(boot);
    WinRoom(boot);
    SkipOfferIfActive(boot);
    const auto result = game::ApplyRunIntent(boot.registry, game::ERunIntent::ContinueReward);
    CHECK(result.has_value());
    CHECK(result->exit == game::ERunExit::ShowModal);
    CHECK(result->modalId == event->modalId);
    CHECK(boot.run().phase == game::EDungeonPhase::Event);
}

void EventPoolCarriedFromSlot() {
    // The override roots turn both walked event slots into the "test" pool: the loaded slot carries it.
    FTempAssets assets("events-slot-pool");
    assets.Write("dungeon.json", TestPoolDungeon());
    FBoot boot(UniquePath("events-slot-pool-boot"), assets.Roots());
    StartFresh(boot);
    const auto* sequence = game::FindRunSequence(boot.registry);
    CHECK(sequence != nullptr);
    auto& run = boot.run();
    const auto* slot = sequence->GapEvent(sequence->PositionOf(run.depth - 1), run.room);
    CHECK(slot != nullptr);
    CHECK(slot->type == game::kRoomTypeEvent);
    CHECK(slot->eventPool == "test");  // carried from the document, not re-derived

    const auto event = EnterEvent(boot);
    CHECK(event.has_value());
    CHECK(event->eventId != game::kLegacySeedEventId);  // the pick used the slot's pool
    const auto def = game::FindEventDef(boot.registry, event->eventId);
    CHECK(def.has_value());
    CHECK(game::EventInPool(*def, slot->eventPool));

    // The shipped/fixture slot names "common", and the loaded slot says exactly the document's value.
    FBoot plain(UniquePath("events-slot-pool-plain"));
    StartFresh(plain);
    const auto* plainSequence = game::FindRunSequence(plain.registry);
    CHECK(plainSequence != nullptr);
    const auto* plainSlot =
        plainSequence->GapEvent(plainSequence->PositionOf(plain.run().depth - 1), plain.run().room);
    CHECK(plainSlot != nullptr);
    const json fixtureDoc = FixtureDoc("dungeon.json");
    CHECK(plainSlot->eventPool ==
          fixtureDoc["themes"][0]["dungeons"][0]["rooms"][1]["event_pool"].get<std::string>());
}

void UnknownRoomTypeFailsBoot() {
    FTempAssets assets("events-room-type");
    const json base = FixtureDoc("dungeon.json");
    auto document = base;
    document["themes"][0]["dungeons"][0]["rooms"][1]["type"] = "shop";  // not battle, not registered
    assets.Write("dungeon.json", document);
    CHECK(!ContentBoots(assets.Roots()));
    assets.Write("dungeon.json", base);
    CHECK(ContentBoots(assets.Roots()));
}

void BattleRemainsImplicit() {
    FBoot boot(UniquePath("events-battle-implicit"));
    CHECK(game::FindRoomType(boot.registry, game::kRoomTypeBattle) == nullptr);  // never in the table
    CHECK(game::IsKnownRoomType(boot.registry, game::kRoomTypeBattle));          // but always known
    CHECK(!game::IsKnownRoomType(boot.registry, "shop"));
    CHECK(!game::IsKnownRoomType(boot.registry, ""));
    CHECK(!game::IsKnownRoomType(boot.registry, "Event"));  // ids are exact spellings
}

void EmptyPoolFailsBoot() {
    FTempAssets assets("events-empty-pool");
    const json base = FixtureDoc("dungeon.json");
    auto document = base;
    document["themes"][0]["dungeons"][0]["rooms"][1]["event_pool"] = "nothing";
    assets.Write("dungeon.json", document);
    CHECK(!ContentBoots(assets.Roots()));  // pool ∩ (seed ∪ authored) = ∅
    assets.Write("dungeon.json", base);
    CHECK(ContentBoots(assets.Roots()));
}

void ShippedEventsFileIsEmptyInW9() {
    const json shipped = ReadJsonFile(std::string(kShipAssetRoot) + "/data/events.json");
    CHECK(shipped["schema_version"] == game::kEventSchemaVersion);
    CHECK(shipped["events"].is_array());
    CHECK(shipped["events"].empty());  // W9 ships the seed only; authored events arrive with WP-23

    // The loader contract side: an empty list is a normal document, not an error.
    entt::registry registry;
    game::FAssetManager::Initialize(registry, ShipAssetRoots());
    const auto defs = game::LoadEventDefs(registry.ctx().get<game::FAssetManager>());
    CHECK(defs.has_value());
    CHECK(defs->empty());
}

// ---------------------------------------------------------------------------------------------
// Pick
// ---------------------------------------------------------------------------------------------

void EventPickGolden() {
    // A synthetic catalog: the candidate list and the hash are the whole rule, so the pick is the
    // documented index into the recomputed candidates — no implementation-defined distribution involved.
    std::vector<game::FEventDef> catalog;
    const auto add = [&catalog](std::string id, std::vector<std::string> tags) {
        game::FEventDef def;
        def.id = std::move(id);
        def.tags = std::move(tags);
        catalog.push_back(std::move(def));
    };
    add("t.a", {"pool"});
    add("t.b", {"pool"});
    add("t.c", {"pool"});
    add("t.d", {"pool"});
    add("t.e", {"pool"});
    add("t.other", {"elsewhere"});

    const std::vector<std::string> seen = {"t.c"};
    const std::uint64_t hash = game::EventPickHash(kSeed, 2, 1);
    CHECK(hash == 0xE9802A99E4F51DDBull);  // golden — update together with the FEventSection.cpp pick constants
    std::vector<std::string> candidates;
    for (const auto& def : catalog) {
        if (game::EventInPool(def, "pool") && std::ranges::find(seen, def.id) == seen.end()) {
            candidates.push_back(def.id);
        }
    }
    CHECK(candidates.size() == 4u);
    const auto picked = game::PickEventId(catalog, "pool", seen, kSeed, 2, 1);
    CHECK(picked.has_value());
    CHECK(*picked == candidates[hash % candidates.size()]);
    CHECK(*picked == "t.e");  // golden (candidates[t.a, t.b, t.d, t.e]; index 3)
    CHECK(game::PickEventId(catalog, "pool", seen, kSeed, 2, 1) == picked);  // deterministic

    // Empty pool = every event, in catalog order.
    std::vector<std::string> all;
    for (const auto& def : catalog) all.push_back(def.id);
    const auto every = game::PickEventId(catalog, "", {}, kSeed, 2, 1);
    CHECK(every.has_value());
    CHECK(*every == all[hash % all.size()]);
    CHECK(*every == "t.other");  // golden (all six; index 5)

    // A different seen set moves the candidate list; the pick follows the same index rule.
    const std::vector<std::string> seen2 = {"t.a", "t.e"};
    std::vector<std::string> candidates2;
    for (const auto& def : catalog) {
        if (game::EventInPool(def, "pool") && std::ranges::find(seen2, def.id) == seen2.end()) {
            candidates2.push_back(def.id);
        }
    }
    CHECK(candidates2.size() == 3u);
    const auto moved = game::PickEventId(catalog, "pool", seen2, kSeed, 2, 1);
    CHECK(moved.has_value());
    CHECK(*moved == candidates2[hash % candidates2.size()]);
    CHECK(*moved == "t.d");  // golden (candidates2[t.b, t.c, t.d]; index 2)

    // The same formula against the real (fixture) catalog.
    FBoot boot(UniquePath("events-pick-golden"));
    StartFresh(boot);
    const auto realCatalog = game::EventCatalog(boot.registry);
    std::vector<std::string> pool;
    for (const auto& def : realCatalog) {
        if (game::EventInPool(def, "")) pool.push_back(def.id);
    }
    CHECK(pool.size() == 4u);  // seed + the three authored fixture events
    const std::uint64_t realHash = game::EventPickHash(kSeed, 1, 0);
    CHECK(realHash == 0xCFE8A7D01FDEBDE6ull);  // golden — update together with the FEventSection.cpp pick constants
    const auto real = game::PickEventId(realCatalog, "", {}, kSeed, 1, 0);
    CHECK(real.has_value());
    CHECK(*real == pool[realHash % pool.size()]);
    CHECK(*real == "event.test_spring");  // golden (seed, sealed_cache, spring, altar; index 2)
}

void EventPickStableAcrossRestart() {
    FTempAssets assets("events-pick-restart");
    assets.Write("dungeon.json", TestPoolDungeon());
    FBoot boot(UniquePath("events-pick-restart-boot"), assets.Roots());
    StartFresh(boot);
    const auto event = EnterEvent(boot);
    CHECK(event.has_value());
    const std::string id = event->eventId;
    CHECK(!id.empty());
    const auto catalog = game::EventCatalog(boot.registry);
    auto& run = boot.run();
    CHECK(game::PickEventId(catalog, "test", {}, run.seed, run.depth, run.room) == id);

    // Restart = save + drop the live run and its module state + continue: the same key re-derives the event.
    CHECK(game::SaveDungeonRun(boot.registry));
    ForgetRunInMemory(boot);
    CHECK(game::ContinueDungeonRun(boot.registry));
    const auto reopened = game::CurrentEventInstance(boot.registry);
    CHECK(reopened.has_value());
    CHECK(reopened->eventId == id);
    CHECK(!reopened->resolved);
}

void EventPickSeparatesGapSlots() {
    const std::uint64_t base = game::EventPickHash(kSeed, 1, 0);
    CHECK(game::EventPickHash(kSeed, 1, 0) == base);        // stable within a key
    CHECK(game::EventPickHash(kSeed, 1, 1) != base);        // a second gap slot salt
    CHECK(game::EventPickHash(kSeed, 2, 0) != base);        // a deeper room
    CHECK(game::EventPickHash(kSeed + 1, 1, 0) != base);    // another run
}

// ---------------------------------------------------------------------------------------------
// Non-repeat
// ---------------------------------------------------------------------------------------------

void EventPickSkipsSeen() {
    // The rule, driven directly: pool "" = the seed + the three authored fixture events; N picks with a
    // growing seen list yield N distinct ids (each pick removes exactly its own candidate).
    {
        FBoot boot(UniquePath("events-skip-seen-rule"));
        StartFresh(boot);
        const auto catalog = game::EventCatalog(boot.registry);
        std::vector<std::string> pool;
        for (const auto& def : catalog) {
            if (game::EventInPool(def, "")) pool.push_back(def.id);
        }
        CHECK(pool.size() == 4u);
        std::vector<std::string> seen;
        for (std::size_t step = 0; step < pool.size(); ++step) {
            const auto picked = game::PickEventId(catalog, "", seen, kSeed, 1, 0);
            CHECK(picked.has_value());
            CHECK(std::ranges::find(seen, *picked) == seen.end());
            seen.push_back(*picked);
        }
        std::vector<std::string> distinct = seen;
        std::sort(distinct.begin(), distinct.end());
        CHECK(std::unique(distinct.begin(), distinct.end()) == distinct.end());
        CHECK(distinct.size() == pool.size());
    }
    // End to end: two event rooms in one run — the second pick skips the first resolved id.
    {
        FTempAssets assets("events-skip-seen-e2e");
        assets.Write("dungeon.json", TestPoolDungeon());
        FBoot boot(UniquePath("events-skip-seen-boot"), assets.Roots());
        StartFresh(boot);
        const auto first = EnterEvent(boot);
        CHECK(first.has_value());
        const std::string firstId = first->eventId;
        const auto firstDef = game::FindEventDef(boot.registry, firstId);
        CHECK(firstDef.has_value());
        CHECK(game::EventInPool(*firstDef, "test"));
        ResolveEvent(boot, 1);  // every fixture choice 1 is ungated
        CHECK(game::EventSeen(boot.registry) == std::vector<std::string>{firstId});

        // Cross a save/continue cycle before the second event: the memory survives it.
        CHECK(game::SaveDungeonRun(boot.registry));
        ForgetRunInMemory(boot);
        CHECK(game::ContinueDungeonRun(boot.registry));
        CHECK(game::EventSeen(boot.registry) == std::vector<std::string>{firstId});

        // Walk dungeon 1 to its camp, then dungeon 2's first event room.
        WinRoom(boot);
        SkipOfferIfActive(boot);
        ContinueRewardStep(boot);
        WinRoom(boot);
        SkipOfferIfActive(boot);
        ContinueRewardStep(boot);
        CHECK(boot.run().phase == game::EDungeonPhase::Camp);
        CHECK(game::ApplyRunIntent(boot.registry, game::ERunIntent::LeaveCamp).has_value());
        CHECK(boot.run().depth == 2 && boot.run().room == 0);

        const auto second = EnterEvent(boot);
        CHECK(second.has_value());
        CHECK(second->eventId != firstId);  // the resolved event is not offered again
        auto& run = boot.run();
        const auto expected = game::PickEventId(game::EventCatalog(boot.registry), "test",
                                                game::EventSeen(boot.registry), run.seed, run.depth, run.room);
        CHECK(expected.has_value());
        CHECK(*expected == second->eventId);
    }
}

void ExhaustedPoolAllowsWholePoolAgain() {
    FBoot boot(UniquePath("events-exhausted"));
    StartFresh(boot);
    const auto catalog = game::EventCatalog(boot.registry);
    std::vector<std::string> seen;
    for (const auto& def : catalog) seen.push_back(def.id);
    const std::vector<std::string> seenBefore = seen;
    std::vector<std::string> pool;
    for (const auto& def : catalog) {
        if (game::EventInPool(def, "")) pool.push_back(def.id);
    }
    const std::uint64_t hash = game::EventPickHash(kSeed, 1, 0);
    const auto picked = game::PickEventId(catalog, "", seen, kSeed, 1, 0);
    CHECK(picked.has_value());  // exhausted is not empty
    CHECK(*picked == pool[hash % pool.size()]);  // the whole pool is the candidate list again
    CHECK(seen == seenBefore);                    // the seen memory is kept, never cleared here
    CHECK(game::PickEventId(catalog, "", seen, kSeed, 1, 0) == picked);
}

void SeenSurvivesContinue() {
    FTempAssets assets("events-seen-continue");
    assets.Write("dungeon.json", TestPoolDungeon());
    FBoot boot(UniquePath("events-seen-continue-boot"), assets.Roots());
    StartFresh(boot);
    const auto event = EnterEvent(boot);
    CHECK(event.has_value());
    const std::string id = event->eventId;

    const auto chosen = game::ApplyRunIntent(boot.registry, game::ERunIntent::ChooseEventOption, 1);
    CHECK(chosen.has_value() && chosen->exit == game::ERunExit::None);
    const std::vector<std::string> seen = game::EventSeen(boot.registry);
    CHECK(seen == std::vector<std::string>{id});

    // The section is the state source: run.ext.event carries the seen list and the resolved instance.
    const json saved = ReadJsonFile(boot.runSave);
    CHECK(saved["run"]["ext"].contains("event"));
    const json& section = saved["run"]["ext"]["event"];
    CHECK(section.at("section_version") == game::kEventSectionVersion);
    CHECK(section.at("event_id") == id);
    CHECK(section.at("choice") == 1);
    CHECK(section.at("seen").is_array() && section.at("seen").size() == 1u && section.at("seen")[0] == id);

    ForgetRunInMemory(boot);
    CHECK(game::ContinueDungeonRun(boot.registry));
    CHECK(game::EventSeen(boot.registry) == seen);
    auto& restored = boot.run();
    CHECK(boot.registry.ctx().get<game::FEventInstance>().resolved);

    // The next pick for the same key avoids the seen id (the pool still has other members).
    const auto next = game::PickEventId(game::EventCatalog(boot.registry), "test",
                                        game::EventSeen(boot.registry), restored.seed, restored.depth,
                                        restored.room);
    CHECK(next.has_value());
    CHECK(*next != id);
}

/// Critic w9-events #1: a result-screen save whose resolved event later left the catalog (removed/renamed) must
/// not strand the run on a screen with nothing to click — the resume continues past the room, and the choice
/// is never paid a second time.
void ResolvedEventMissingFromCatalogContinues() {
    FBoot boot(UniquePath("events-resolved-missing"));
    StartFresh(boot);
    const auto event = EnterEvent(boot);
    CHECK(event.has_value());
    CHECK(game::ApplyRunIntent(boot.registry, game::ERunIntent::ChooseEventOption, 1).has_value());
    const int room = boot.run().room;
    const int materials = boot.run().materials;

    // The checkpoint was written on the result screen; the content then lost that event id.
    json saved = ReadJsonFile(boot.runSave);
    CHECK(saved["run"]["ext"]["event"].at("event_id") == event->eventId);
    saved["run"]["ext"]["event"]["event_id"] = "event.removed_since_save";
    {
        std::ofstream out(boot.runSave, std::ios::trunc);
        CHECK(out.good());
        out << saved.dump(2);
    }

    ForgetRunInMemory(boot);
    CHECK(game::ContinueDungeonRun(boot.registry));
    auto& run = boot.run();
    CHECK(run.phase == game::EDungeonPhase::Battle);  // progressed past the event room
    CHECK(run.room == room + 1);
    CHECK(run.materials == materials);                // no second payment
    CHECK(!boot.registry.ctx().contains<game::FEventInstance>());
    CHECK(!run.advanceRequested);                     // the continue path enters the battle itself
    CHECK(!game::CurrentEventInstance(boot.registry).has_value());

    // The progressed state is what the file holds now (a second resume lands in the same battle).
    const json after = ReadJsonFile(boot.runSave);
    CHECK(!after["run"]["ext"].contains("event") || !after["run"]["ext"]["event"].contains("event_id"));
}

void SeenAbsentIsEmptyList() {
    FBoot boot(UniquePath("events-seen-absent"));
    StartFresh(boot);

    // The section is absent: the serializer receives {} and falls back to "no instance, empty seen".
    const json absent = game::DungeonRunToJson(boot.run());
    boot.registry.ctx().erase<game::FEventInstance>();   // a real resume has no live module state —
    boot.registry.ctx().erase<game::FEventRunState>();   // only the section can restore it
    CHECK(game::UnpackSections(boot.registry, absent).has_value());
    CHECK(game::EventSeen(boot.registry).empty());
    CHECK(!boot.registry.ctx().contains<game::FEventInstance>());

    // A resolved section without a `seen` key: the instance is restored and the memory stays empty
    // (additive — an old save behaves as before the non-repeat rule).
    json document = game::DungeonRunToJson(boot.run());
    document["run"]["ext"]["event"] = json{{"section_version", game::kEventSectionVersion},
                                           {"event_id", std::string(kFixtureSpringEvent)},
                                           {"choice", 0}};
    boot.registry.ctx().erase<game::FEventInstance>();
    boot.registry.ctx().erase<game::FEventRunState>();
    CHECK(game::UnpackSections(boot.registry, document).has_value());
    CHECK(game::EventSeen(boot.registry).empty());
    CHECK(boot.registry.ctx().contains<game::FEventInstance>());
    CHECK(boot.registry.ctx().get<game::FEventInstance>().eventId == kFixtureSpringEvent);
    CHECK(boot.registry.ctx().get<game::FEventInstance>().resolved);
}

void SeenAddedOnResolveOnly() {
    FBoot boot(UniquePath("events-seen-resolve-only"));
    StartFresh(boot);
    const auto event = EnterEvent(boot);
    CHECK(event.has_value());
    const std::string id = event->eventId;

    // Entering the room (and deriving the instance) is not resolving it.
    CHECK(game::EventSeen(boot.registry).empty());
    CHECK(!boot.registry.ctx().get<game::FEventInstance>().resolved);

    // An unresolved resume reopens the same event and still adds nothing to the memory.
    CHECK(game::SaveDungeonRun(boot.registry));
    ForgetRunInMemory(boot);
    CHECK(game::ContinueDungeonRun(boot.registry));
    CHECK(game::EventSeen(boot.registry).empty());
    const auto reopened = game::CurrentEventInstance(boot.registry);
    CHECK(reopened.has_value());
    CHECK(reopened->eventId == id);
    CHECK(!reopened->resolved);

    // Only the resolve adds the id — exactly once.
    const auto resolved = game::ApplyRunIntent(boot.registry, game::ERunIntent::ChooseEventOption, 1);
    CHECK(resolved.has_value() && resolved->exit == game::ERunExit::None);
    CHECK(game::EventSeen(boot.registry) == std::vector<std::string>{id});
    const auto again = game::ApplyRunIntent(boot.registry, game::ERunIntent::ChooseEventOption, 1);
    CHECK(!again.has_value());
    CHECK(game::EventSeen(boot.registry) == std::vector<std::string>{id});
}

void SeenClearedOnNewRun() {
    FBoot boot(UniquePath("events-seen-new-run"));
    StartFresh(boot);
    CHECK(EnterEvent(boot).has_value());
    CHECK(game::ApplyRunIntent(boot.registry, game::ERunIntent::ChooseEventOption, 1).has_value());
    CHECK(!game::EventSeen(boot.registry).empty());

    game::StartRun(boot.registry, kSeed + 1, true);
    CHECK(game::EventSeen(boot.registry).empty());
    const json saved = ReadJsonFile(boot.runSave);
    CHECK(saved["run"]["ext"].contains("event"));            // the section still packs (version only)
    CHECK(!saved["run"]["ext"]["event"].contains("seen"));   // ...but carries no runtime memory
}

void ShippedSeedOnlyPoolStillRepeats() {
    FBoot boot(UniquePath("events-seed-repeat"));
    StartFresh(boot);
    const auto catalog = game::EventCatalog(boot.registry);
    std::size_t common = 0;
    bool seedPresent = false;
    for (const auto& def : catalog) {
        if (!game::EventInPool(def, game::kLegacySeedPool)) continue;
        ++common;
        if (def.id == game::kLegacySeedEventId) seedPresent = true;
    }
    CHECK(seedPresent);
    CHECK(common == 1u);  // the "common" pool is exactly the seed (fixture events are "test")

    const auto first = game::PickEventId(catalog, game::kLegacySeedPool, {}, kSeed, 1, 0);
    CHECK(first.has_value() && *first == game::kLegacySeedEventId);
    // Even with the seed seen, the exhausted pool allows the whole pool again: the same shrine repeats.
    const std::vector<std::string> seen = {std::string(game::kLegacySeedEventId)};
    const auto again = game::PickEventId(catalog, game::kLegacySeedPool, seen, kSeed, 1, 0);
    CHECK(again.has_value() && *again == game::kLegacySeedEventId);
    const auto deeper = game::PickEventId(catalog, game::kLegacySeedPool, {}, kSeed, 2, 3);
    CHECK(deeper.has_value() && *deeper == game::kLegacySeedEventId);
}

// ---------------------------------------------------------------------------------------------
// Two-stage flow, intent payload, log
// ---------------------------------------------------------------------------------------------

void EventOptionIntentCarriesIndex() {
    FBoot boot(UniquePath("events-intent-choice"));
    game::PostRunIntent(boot.registry, game::ERunIntent::ChooseEventOption, 1);
    auto* request = boot.registry.ctx().find<game::FRunIntentRequest>();
    CHECK(request != nullptr);
    CHECK(request->intent == game::ERunIntent::ChooseEventOption);
    CHECK(request->choice == 1);

    // Last wins, and the choice rides along on the replace path too.
    game::PostRunIntent(boot.registry, game::ERunIntent::ChooseEventOption, 3);
    request = boot.registry.ctx().find<game::FRunIntentRequest>();
    CHECK(request != nullptr && request->choice == 3);

    // The default = no choice (ContinueEvent carries none).
    boot.registry.ctx().erase<game::FRunIntentRequest>();
    game::PostRunIntent(boot.registry, game::ERunIntent::ContinueEvent);
    request = boot.registry.ctx().find<game::FRunIntentRequest>();
    CHECK(request != nullptr);
    CHECK(request->intent == game::ERunIntent::ContinueEvent);
    CHECK(request->choice == -1);
}

void HostAcceptsEventOptionAndContinue() {
    FBoot boot(UniquePath("events-host"));
    StartFresh(boot);
    CHECK(EnterEvent(boot).has_value());
    const std::string id = std::string(game::kLegacySeedEventId);
    FEmberModalHostState host(std::string(game::kRunModalEvent), EHostMode::RunPhase);

    // Choose = resolve only: the host stays on the same modal (no transition) and the request is consumed.
    game::PostRunIntent(boot.registry, game::ERunIntent::ChooseEventOption, 1);
    const auto resolved = host.Update(boot.registry, 0.f);
    CHECK((resolved == FStateTransition{}));
    CHECK(!boot.registry.ctx().contains<game::FRunIntentRequest>());
    CHECK(boot.run().phase == game::EDungeonPhase::Event);
    CHECK(boot.registry.ctx().get<game::FEventInstance>().resolved);
    CHECK(game::EventSeen(boot.registry) == std::vector<std::string>{id});

    // Nothing pending: the host is quiet.
    CHECK((host.Update(boot.registry, 0.f) == FStateTransition{}));

    // Continue = advance: NextRoom unwinds to the battle state.
    game::PostRunIntent(boot.registry, game::ERunIntent::ContinueEvent);
    const auto next = host.Update(boot.registry, 0.f);
    CHECK((next == FStateTransition{EStateChangeKind::Root, std::string(kEmberBattleId)}));
    CHECK(boot.run().phase == game::EDungeonPhase::Battle && boot.run().room == 1);
    CHECK(!boot.registry.ctx().contains<game::FEventInstance>());
}

void LegacyChooseEventIntentsMapToFirstTwoOptions() {
    // The rest alias = option 0 + continue in one step.
    {
        FBoot boot(UniquePath("events-legacy-rest"));
        StartFresh(boot);
        CHECK(EnterEvent(boot).has_value());
        auto& run = boot.run();
        const int maxHealth = game::DungeonMemberMaxHealth(boot.registry, run.party[0]);
        CHECK(maxHealth > 1);
        run.party[0].health = maxHealth - 1;  // any positive event_heal restores the party
        const auto result = game::ApplyRunIntent(boot.registry, game::ERunIntent::ChooseEventRest);
        CHECK(result.has_value() && result->exit == game::ERunExit::NextRoom);
        CHECK(run.phase == game::EDungeonPhase::Battle && run.room == 1);
        CHECK(run.party[0].health == maxHealth);
        CHECK(!boot.registry.ctx().contains<game::FEventInstance>());
        const auto& log = boot.registry.ctx().get<game::FRunEventLog>();
        CHECK(!log.tail.empty());
        CHECK(log.tail.back().kind == game::ERunEventKind::EventChosen);
        CHECK(log.tail.back().id == game::kLegacySeedEventId);
        CHECK(log.tail.back().amount == 0);
    }
    // The loot alias = option 1 + continue.
    {
        FBoot boot(UniquePath("events-legacy-loot"));
        StartFresh(boot);
        CHECK(EnterEvent(boot).has_value());
        auto& run = boot.run();
        const auto def = game::FindEventDef(boot.registry, game::kLegacySeedEventId);
        CHECK(def.has_value() && def->choices.size() >= 2u);
        int gain = 0;
        for (const auto& effect : def->choices[1].effects) {
            if (effect.kind == game::kEventEffectGainMaterials) gain = effect.amount;
        }
        CHECK(gain > 0);
        const int before = run.materials;
        const auto result = game::ApplyRunIntent(boot.registry, game::ERunIntent::ChooseEventLoot);
        CHECK(result.has_value() && result->exit == game::ERunExit::NextRoom);
        CHECK(run.phase == game::EDungeonPhase::Battle && run.room == 1);
        CHECK(run.materials == before + gain);
        CHECK(!boot.registry.ctx().contains<game::FEventInstance>());
        const auto& log = boot.registry.ctx().get<game::FRunEventLog>();
        CHECK(log.tail.back().kind == game::ERunEventKind::EventChosen);
        CHECK(log.tail.back().id == game::kLegacySeedEventId);
        CHECK(log.tail.back().amount == 1);
    }
}

void EventCannotBeResolvedTwiceAcrossContinue() {
    FBoot boot(UniquePath("events-no-double"));
    StartFresh(boot);
    const auto event = EnterEvent(boot);
    CHECK(event.has_value());
    const std::string id = event->eventId;
    CHECK(game::ApplyRunIntent(boot.registry, game::ERunIntent::ChooseEventOption, 1).has_value());
    const int materials = boot.run().materials;

    ForgetRunInMemory(boot);
    CHECK(game::ContinueDungeonRun(boot.registry));
    auto& run = boot.run();
    CHECK(run.materials == materials);
    CHECK(boot.registry.ctx().get<game::FEventInstance>().resolved);
    CHECK(boot.registry.ctx().get<game::FEventInstance>().choice == 1);

    // The resolved event refuses another resolve: no second payment, no state change.
    const auto again = game::ApplyRunIntent(boot.registry, game::ERunIntent::ChooseEventOption, 1);
    CHECK(!again.has_value());
    CHECK(!again.error().empty());
    CHECK(run.materials == materials);
    CHECK(game::EventSeen(boot.registry) == std::vector<std::string>{id});
    CHECK(!boot.registry.ctx().contains<game::FRunIntentRequest>());
}

void EventResultSurvivesContinue() {
    FBoot boot(UniquePath("events-result-continue"));
    StartFresh(boot);
    const auto event = EnterEvent(boot);
    CHECK(event.has_value());
    const auto def = game::FindEventDef(boot.registry, event->eventId);
    CHECK(def.has_value() && def->choices.size() >= 2u);
    const std::string id = event->eventId;
    CHECK(game::ApplyRunIntent(boot.registry, game::ERunIntent::ChooseEventOption, 1).has_value());
    const std::string resultMessage = boot.run().message;
    CHECK(resultMessage.find(def->choices[1].resultText) != std::string::npos);
    CHECK(resultMessage.find("party HP -") != std::string::npos);  // the outcome summary is in the result

    ForgetRunInMemory(boot);
    CHECK(game::ContinueDungeonRun(boot.registry));
    auto& run = boot.run();
    CHECK(run.message == resultMessage);  // the result screen reopens, not the choices
    const auto& instance = boot.registry.ctx().get<game::FEventInstance>();
    CHECK(instance.eventId == id);
    CHECK(instance.choice == 1);
    CHECK(instance.resolved);
}

void UnresolvedEventReopensChoices() {
    FTempAssets assets("events-reopen");
    assets.Write("dungeon.json", TestPoolDungeon());
    FBoot boot(UniquePath("events-reopen-boot"), assets.Roots());
    StartFresh(boot);
    const auto event = EnterEvent(boot);
    CHECK(event.has_value());
    const std::string id = event->eventId;
    CHECK(game::SaveDungeonRun(boot.registry));

    ForgetRunInMemory(boot);
    CHECK(game::ContinueDungeonRun(boot.registry));
    const auto reopened = game::CurrentEventInstance(boot.registry);
    CHECK(reopened.has_value());
    CHECK(reopened->eventId == id);  // the pick re-derives the same event
    CHECK(!reopened->resolved);

    // And the reopened instance still resolves normally.
    CHECK(game::ApplyRunIntent(boot.registry, game::ERunIntent::ChooseEventOption, 1).has_value());
    CHECK(boot.registry.ctx().get<game::FEventInstance>().resolved);
    CHECK(game::EventSeen(boot.registry) == std::vector<std::string>{id});
}

void EventChosenLoggedOnce() {
    FBoot boot(UniquePath("events-chosen-once"));
    StartFresh(boot);
    const auto event = EnterEvent(boot);
    CHECK(event.has_value());
    const std::string id = event->eventId;
    CHECK(EventChosenCount(boot.registry) == 0);

    CHECK(game::ApplyRunIntent(boot.registry, game::ERunIntent::ChooseEventOption, 1).has_value());
    CHECK(EventChosenCount(boot.registry) == 1);
    const auto& log = boot.registry.ctx().get<game::FRunEventLog>();
    CHECK(log.tail.back().kind == game::ERunEventKind::EventChosen);
    CHECK(log.tail.back().id == id);
    CHECK(log.tail.back().amount == 1);

    // A refused second resolve logs nothing; the continue does not log either.
    CHECK(!game::ApplyRunIntent(boot.registry, game::ERunIntent::ChooseEventOption, 1).has_value());
    CHECK(EventChosenCount(boot.registry) == 1);
    CHECK(game::ApplyRunIntent(boot.registry, game::ERunIntent::ContinueEvent).has_value());
    CHECK(EventChosenCount(boot.registry) == 1);
}

void EventInstanceClearedOnNewRun() {
    FBoot boot(UniquePath("events-instance-new-run"));
    StartFresh(boot);
    CHECK(EnterEvent(boot).has_value());
    CHECK(boot.registry.ctx().contains<game::FEventInstance>());

    game::StartRun(boot.registry, kSeed + 1, true);
    CHECK(!boot.registry.ctx().contains<game::FEventInstance>());
    CHECK(game::EventSeen(boot.registry).empty());
    CHECK(!game::CurrentEventInstance(boot.registry).has_value());  // a fresh run is in the battle phase
}

} // namespace

int main() {
    std::cout.setf(std::ios::unitbuf);  // a crash must still show how far the suite got
    // Feature tests get no per-test save directory from CMake: isolate every save under the build tree so a
    // run can never touch %APPDATA%\Game01P\EmberDungeon (camp_tests pattern).
    const char* saveDir = nullptr;
#ifdef _MSC_VER
#pragma warning(suppress: 4996)  // std::getenv: read-only, no buffer to overflow
#endif
    saveDir = std::getenv("GAME01P_SAVE_DIR");
    if (saveDir == nullptr || *saveDir == '\0') {
        const std::string path =
            (std::filesystem::current_path() / "out" / "test-saves" / "event_framework_tests").string();
        std::error_code error;
        std::filesystem::create_directories(path, error);
        _putenv_s("GAME01P_SAVE_DIR", path.c_str());
    }

    return game_test::RunTests("event_framework", {
        {"EventSchemaValidated", EventSchemaValidated},
        {"UnknownKeyRejected", UnknownKeyRejected},
        {"ResultTextIsSingleText", ResultTextIsSingleText},
        {"BadCheckStatFailsBoot", BadCheckStatFailsBoot},
        {"CheckGateDisablesWhenUnmet", CheckGateDisablesWhenUnmet},
        {"CheckHasNoFailureBranch", CheckHasNoFailureBranch},
        {"GatedOptionsLeaveAChoice", GatedOptionsLeaveAChoice},
        {"EventHealClampsToMax", EventHealClampsToMax},
        {"DamagePartyStopsAtOneHp", DamagePartyStopsAtOneHp},
        {"ChestCostMatchesFraction", ChestCostMatchesFraction},
        {"GainMaterialsIsRunScope", GainMaterialsIsRunScope},
        {"ReplaceLegacyShrine", ReplaceLegacyShrine},
        {"EventIncomeBindsD3", EventIncomeBindsD3},
        {"ChestChoiceShowsPerUnitLoss", ChestChoiceShowsPerUnitLoss},
        {"StatSourceIsPartyBestPure", StatSourceIsPartyBestPure},
        {"StatVocabularyMatchesUpgrades", StatVocabularyMatchesUpgrades},
        {"EventStatUsesResolvedUnit", EventStatUsesResolvedUnit},
        {"RoomTypeDispatch", RoomTypeDispatch},
        {"EventPoolCarriedFromSlot", EventPoolCarriedFromSlot},
        {"UnknownRoomTypeFailsBoot", UnknownRoomTypeFailsBoot},
        {"BattleRemainsImplicit", BattleRemainsImplicit},
        {"EmptyPoolFailsBoot", EmptyPoolFailsBoot},
        {"ShippedEventsFileIsEmptyInW9", ShippedEventsFileIsEmptyInW9},
        {"EventPickGolden", EventPickGolden},
        {"EventPickStableAcrossRestart", EventPickStableAcrossRestart},
        {"EventPickSeparatesGapSlots", EventPickSeparatesGapSlots},
        {"EventPickSkipsSeen", EventPickSkipsSeen},
        {"ExhaustedPoolAllowsWholePoolAgain", ExhaustedPoolAllowsWholePoolAgain},
        {"SeenSurvivesContinue", SeenSurvivesContinue},
        {"SeenAbsentIsEmptyList", SeenAbsentIsEmptyList},
        {"SeenAddedOnResolveOnly", SeenAddedOnResolveOnly},
        {"SeenClearedOnNewRun", SeenClearedOnNewRun},
        {"ShippedSeedOnlyPoolStillRepeats", ShippedSeedOnlyPoolStillRepeats},
        {"EventOptionIntentCarriesIndex", EventOptionIntentCarriesIndex},
        {"HostAcceptsEventOptionAndContinue", HostAcceptsEventOptionAndContinue},
        {"LegacyChooseEventIntentsMapToFirstTwoOptions", LegacyChooseEventIntentsMapToFirstTwoOptions},
        {"EventCannotBeResolvedTwiceAcrossContinue", EventCannotBeResolvedTwiceAcrossContinue},
        {"EventResultSurvivesContinue", EventResultSurvivesContinue},
        {"UnresolvedEventReopensChoices", UnresolvedEventReopensChoices},
        {"EventChosenLoggedOnce", EventChosenLoggedOnce},
        {"EventInstanceClearedOnNewRun", EventInstanceClearedOnNewRun},
        {"ResolvedEventMissingFromCatalogContinues", ResolvedEventMissingFromCatalogContinues},
    });
}
