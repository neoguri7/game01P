// WP-21c hub shop: the Unlocks purchase path (FMetaProgress), the seal gate derived from the milestone
// ledger, the rune offer gate (FRewards::CandidatesFor) and the Unlocks tab (FPreparationView).
//
// The no-op guards are the heart of 21c: buying a skill must change MemberSkillPool, buying a rune must add
// it to the reward offer pool, buying a roster row must make the member selectable — and the shipped
// `party.rogue_stub` (a locked identity placeholder) must be refused instead of sold as a no-op. Seals are
// earned through the real ledger path (BossDefeated/DungeonCleared events appended while a run is installed,
// the same contract promotion_tests uses); no real battle is fought.
//
// Content world: the SHIPPED assets (plus a temp override root FIRST for the roster/unlocks rewrites). The
// shipped `units.json` pools, `unlocks.json` gates and `rewards.json` weights are what these consumers read,
// so a mixed frozen-fixture root would be the wrong subject here. Everything a case writes lives under
// `out/` and is removed when the case ends; no test touches %APPDATA%.

#define GAME01P_TEST_SUPPORT_CHECK
#include "../TestSupport.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
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
#include <imgui_internal.h>
#include <nlohmann/json.hpp>

#include "core/EngineState.h"
#include "core/ResourceManager.h"
#include "core/SDLDeleter.h"
#include "gameplay/data/FContentRegistry.h"
#include "gameplay/dungeon/FDungeonRun.h"
#include "gameplay/dungeon/FRewards.h"
#include "gameplay/dungeon/FRunEventLog.h"
#include "gameplay/dungeon/FRunStart.h"
#include "gameplay/meta/FMetaProgress.h"
#include "gameplay/meta/FMetaSave.h"
#include "presentation/FDungeonArt.h"
#include "presentation/FDungeonOverlay.h"
#include "presentation/FPreparationView.h"

namespace {

using nlohmann::json;

constexpr std::uint32_t kSeed = 4400;

// ---------------------------------------------------------------------------------------------
// Scratch roots and file helpers
// ---------------------------------------------------------------------------------------------

std::string UniqueStem(const char* stem) {
    static std::uint32_t serial = 0;
    std::filesystem::create_directories("out");
    return std::string(stem) + "-" + std::to_string(++serial) + "-" +
           std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
}

std::string ReadText(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    CHECK(file.good());
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return buffer.str();
}

json ReadJson(const std::filesystem::path& path) { return json::parse(ReadText(path)); }

void WriteText(const std::filesystem::path& path, std::string_view text) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    CHECK(!error);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    CHECK(file.good());
    file << text;
    CHECK(file.good());
}

void WriteJson(const std::filesystem::path& path, const json& document) {
    WriteText(path, document.dump(2));
}

/// A per-test scratch directory under out/, removed when the case ends (pass or fail).
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

/// Overrides written into <scratch>/data BEFORE the content loads (roster.json, unlocks.json, ...).
using FDataOverrides = std::function<void(const std::filesystem::path& dataDir)>;

/// The booted registry: temp override root first, then the shipped assets. The checkpoint and both save
/// slots live inside the scratch root, so a case is fully isolated.
struct FShopBoot {
    FScratchRoot scratch;  // declared first: destroyed last, after the registry released its files
    entt::registry registry;
    std::string metaPath;

    explicit FShopBoot(const char* stem, const FDataOverrides& overrides = {}) : scratch(stem) {
        if (overrides) overrides(scratch.root / "data");
        game::FAssetManager::Initialize(registry, {scratch.root.string(), kShipAssetRoot});
        CHECK(game::gameplay::FGameplayServices::Initialize(registry));
        CHECK(game::InitializeDungeon(registry, (scratch.root / "checkpoint.json").string()));
        metaPath = (scratch.root / "meta_save.json").string();
    }
    FShopBoot(const FShopBoot&) = delete;
    FShopBoot& operator=(const FShopBoot&) = delete;

    bool InitMeta() { return game::InitializeMetaProgress(registry, metaPath); }
    game::FDungeonRun& run() { return registry.ctx().get<game::FDungeonRun>(); }
};

// ---------------------------------------------------------------------------------------------
// The real ledger path: install a run, append the milestone events (the promotion_tests contract)
// ---------------------------------------------------------------------------------------------

void InstallRun(entt::registry& registry, std::uint32_t seed = kSeed) {
    game::InstallDungeonRunAt(registry, seed, 0);
    CHECK(registry.ctx().contains<game::FDungeonRun>());
}

/// BossDefeated is appended while cleared is still depth - 1 (the real append site).
void FireBossDefeat(entt::registry& registry, int clearedIndex, int ordinal = 0) {
    auto& run = registry.ctx().get<game::FDungeonRun>();
    run.depth = clearedIndex + 1;
    run.room = ordinal;
    run.cleared = clearedIndex;
    game::AppendRunEvent(registry, game::ERunEventKind::BossDefeated, "enemy.guardian");
}

/// DungeonCleared is appended after `cleared` reached depth (the real append site).
void FireDungeonClear(entt::registry& registry, int clearedIndex) {
    auto& run = registry.ctx().get<game::FDungeonRun>();
    run.depth = clearedIndex + 1;
    run.room = 0;
    run.cleared = clearedIndex + 1;
    game::AppendRunEvent(registry, game::ERunEventKind::DungeonCleared);
}

/// Earns seals through the milestone ledger: `bosses` boss breakthroughs (+2 each) then `clears` dungeon
/// clears (+1 each), all at distinct dungeon positions (so every key is new). Asserts the ledger grant and
/// the derived lifetime total agree — the balance and lifetime axes move together only on this path.
void EarnFromLedger(entt::registry& registry, int bosses, int clears) {
    int index = 0;
    for (int count = 0; count < bosses; ++count) FireBossDefeat(registry, index++);
    for (int count = 0; count < clears; ++count) FireDungeonClear(registry, index++);
    const int expected = bosses * 2 + clears;
    CHECK(game::MetaSeals(registry) == expected);
    CHECK(game::LifetimeSealsEarned(registry) == expected);
}

/// Earns at least `seals` through the ledger (+2 per boss, +1 per clear) and returns the exact amount the
/// ledger granted, so a test computes its expected balance from the catalog's own cost/gate values.
int EarnAtLeast(entt::registry& registry, int seals) {
    const int bosses = seals / 2;
    const int clears = seals % 2;
    EarnFromLedger(registry, bosses, clears);
    return bosses * 2 + clears;
}

/// The live unlock row (the catalog is the shop's data; amounts are never hardcoded in a test).
game::FMetaUnlockRow RowOrFail(entt::registry& registry, std::string_view unlockId) {
    const std::optional<game::FMetaUnlockRow> row = game::FindMetaUnlockRow(registry, unlockId);
    CHECK(row.has_value());
    return row.value_or(game::FMetaUnlockRow{});
}

// ---------------------------------------------------------------------------------------------
// Preset meta saves (the injected path pins the file; the loader merges the shipped roster in)
// ---------------------------------------------------------------------------------------------

json LedgerLane(std::initializer_list<const char*> keys) {
    json granted = json::array();
    for (const char* key : keys) granted.push_back(key);
    return json{{"granted", granted}};
}

json LadderOf(std::initializer_list<std::pair<const char*, json>> lanes) {
    json ladder = json::object();
    for (const auto& [rank, lane] : lanes) ladder[rank] = lane;
    return ladder;
}

/// A rank-1 ledger guaranteeing at least `needed` lifetime seals through distinct boss milestones (+2 each).
json BossLedgerLane(int needed) {
    json granted = json::array();
    for (int index = 0; index * 2 < needed; ++index) {
        granted.push_back("theme.ember_crypt/" + std::to_string(index) + "#boss");
    }
    return json{{"granted", granted}};
}

void WritePreset(FShopBoot& boot, int seals, const json& ladder, const json& unlocks = json()) {
    json document = json::object();
    document["schema_version"] = 1;
    document["seals"] = seals;
    document["ladder"] = ladder;
    if (!unlocks.is_null()) document["unlocks"] = unlocks;
    WriteJson(boot.metaPath, document);
}

// ---------------------------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------------------------

bool Contains(const std::vector<std::string>& values, std::string_view value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

std::size_t MemberIndex(const game::FPreparationDraft& draft, std::string_view memberId) {
    const auto found = std::find(draft.setup.party.begin(), draft.setup.party.end(), std::string(memberId));
    CHECK(found != draft.setup.party.end());
    return found != draft.setup.party.end()
        ? static_cast<std::size_t>(found - draft.setup.party.begin())
        : draft.setup.party.size();
}

bool MemberLoadoutHas(const game::FRunMember& member, std::string_view skillId) {
    for (const game::FSkillSlot& slot : member.skills) {
        if (slot.skillId == skillId) return true;
    }
    return false;
}

const game::FRunMember* FindRunMember(const game::FDungeonRun& run, std::string_view memberId) {
    for (const game::FRunMember& member : run.party) {
        if (member.id == memberId) return &member;
    }
    return nullptr;
}

const game::FRosterChoice* FindChoice(const std::vector<game::FRosterChoice>& choices, std::string_view memberId) {
    for (const game::FRosterChoice& choice : choices) {
        if (choice.memberId == memberId) return &choice;
    }
    return nullptr;
}

const game::FUnlockOffer* FindOffer(const std::vector<game::FUnlockOffer>& offers, std::string_view unlockId) {
    for (const game::FUnlockOffer& offer : offers) {
        if (offer.row.id == unlockId) return &offer;
    }
    return nullptr;
}

/// The written save's `unlocks` array (the persisted proof of a purchase).
json PersistedUnlocks(const FShopBoot& boot) {
    const json document = ReadJson(boot.metaPath);
    const auto unlocks = document.find("unlocks");
    if (unlocks == document.end() || !unlocks->is_array()) return json::array();
    return *unlocks;
}

/// How often a purchased id appears in the persisted `unlocks` array (json{"id"} would brace-initialize an
/// array, so compare the decoded strings instead).
std::size_t CountUnlock(const json& unlocks, std::string_view unlockId) {
    std::size_t count = 0;
    for (const json& entry : unlocks) {
        if (entry.is_string() && entry.get<std::string>() == unlockId) ++count;
    }
    return count;
}

/// The player-facing name the Unlocks tab is expected to print for a row (the view's own rule, repeated
/// here so the test does not reach into the translation unit).
std::string ExpectedRowName(entt::registry& registry, const game::FMetaUnlockRow& row) {
    const auto* content = registry.ctx().find<game::gameplay::FContentRegistry>();
    if (row.kind == "skill") {
        const game::gameplay::FSkillContent* skill = content != nullptr ? content->skills.find(row.id) : nullptr;
        return skill != nullptr && !skill->displayName.empty() ? skill->displayName : "Unknown skill";
    }
    if (row.kind == "rune") {
        const game::gameplay::FRuneContent* rune = content != nullptr ? content->runes.find(row.id) : nullptr;
        return rune != nullptr && !rune->displayName.empty() ? rune->displayName : "Unknown rune";
    }
    if (row.kind == "roster") {
        for (const game::FMetaRosterRow& member : game::MetaRosterCatalog(registry)) {
            if (member.id == row.id) return member.name.empty() ? std::string{"???"} : member.name;
        }
        return "???";
    }
    return "Unknown unlock";
}

// ---------------------------------------------------------------------------------------------
// The three no-op guards: a purchase must change the next run (skill, rune, roster)
// ---------------------------------------------------------------------------------------------

void PurchaseRevealsSkillNextRun() {
    FShopBoot boot("shop-skill");
    CHECK(boot.InitMeta());
    const game::FMetaUnlockRow hook = RowOrFail(boot.registry, "skill.hook");
    InstallRun(boot.registry);
    // Earn exactly what the row asks for: both its seal gate and its price (2 per boss, 1 per clear).
    const int earned = EarnAtLeast(boot.registry, std::max(hook.cost, hook.sealGate));
    CHECK(game::MetaSeals(boot.registry) == earned);

    game::FPreparationDraft draft =
        game::MakeDefaultDraft(boot.registry, game::FRunStartRequest{true, -1});
    const std::size_t warrior = MemberIndex(draft, "party.warrior");

    // Before the purchase: absent from the pool and refused by the draft editor (a refused pick is a no-op).
    CHECK(!Contains(game::MemberSkillPool(boot.registry, "party.warrior"), "skill.hook"));
    const std::vector<std::string> before = draft.setup.skills[warrior];
    CHECK(!game::SetDraftSkill(boot.registry, draft, warrior, 0, "skill.hook"));
    CHECK(draft.setup.skills[warrior] == before);

    CHECK(game::PurchaseMetaUnlock(boot.registry, "skill.hook") == game::EUnlockStatus::Purchasable);
    CHECK(game::HasMetaUnlock(boot.registry, "skill.hook"));
    CHECK(game::MetaSeals(boot.registry) == earned - hook.cost);  // the price was deducted

    // After: the authored pool no longer hides it, the draft can equip it, and the run carries it.
    CHECK(Contains(game::MemberSkillPool(boot.registry, "party.warrior"), "skill.hook"));
    CHECK(game::SetDraftSkill(boot.registry, draft, warrior, 0, "skill.hook"));
    CHECK(draft.setup.skills[warrior][0] == "skill.hook");
    CHECK(!game::ValidateRunSetup(boot.registry, draft.setup).has_value());

    CHECK(game::StartDungeonRunFromSetup(boot.registry, kSeed + 1, true, -1, draft.setup));
    const game::FRunMember* member = FindRunMember(boot.run(), "party.warrior");
    CHECK(member != nullptr);
    CHECK(member != nullptr && MemberLoadoutHas(*member, "skill.hook"));
    CHECK(member != nullptr && member->skills.size() == draft.setup.skills[warrior].size());
}

void PurchaseAddsRuneToOfferPool() {
    FShopBoot boot("shop-rune");
    CHECK(boot.InitMeta());
    InstallRun(boot.registry);
    // The authored party can legally fit Inferno (the mage's firebolt/flame_field carry Fire).
    CHECK(!game::RuneFittingSkillNames(boot.registry, "rune.inferno").empty());

    // Before: across many draws the gated rune never appears in the candidates.
    int beforeDraws = 0;
    int beforeHits = 0;
    for (int draw = 0; draw < 128; ++draw) {
        const std::vector<std::string> candidates = game::RollRuneCandidates(boot.registry);
        CHECK(!candidates.empty());
        ++beforeDraws;
        if (Contains(candidates, "rune.inferno")) ++beforeHits;
    }
    CHECK(beforeHits == 0);

    const game::FMetaUnlockRow inferno = RowOrFail(boot.registry, "rune.inferno");
    const int earned = EarnAtLeast(boot.registry, std::max(inferno.cost, inferno.sealGate));
    CHECK(game::PurchaseMetaUnlock(boot.registry, "rune.inferno") == game::EUnlockStatus::Purchasable);
    CHECK(game::MetaSeals(boot.registry) == earned - inferno.cost);

    // After: the rune is in the unowned pool, so a broad sweep of draws must offer it (weight 4 over the
    // shipped pool; 256 draws make a miss astronomically unlikely and the draw stream is deterministic).
    int afterDraws = 0;
    int afterHits = 0;
    for (int draw = 0; draw < 256; ++draw) {
        const std::vector<std::string> candidates = game::RollRuneCandidates(boot.registry);
        CHECK(!candidates.empty());
        for (const std::string& candidate : candidates) {
            CHECK(!game::RuneFittingSkillNames(boot.registry, candidate).empty());
        }
        ++afterDraws;
        if (Contains(candidates, "rune.inferno")) ++afterHits;
    }
    // Diagnostic before the assertion: CHECK throws the rest of the case away on failure.
    std::cout << "[report] inferno offer pool: before draws=" << beforeDraws << " hits=" << beforeHits
              << "; after draws=" << afterDraws << " hits=" << afterHits << "\n";
    CHECK(afterHits >= 1);
}

void PurchaseUnlocksRosterEntry() {
    FShopBoot boot("shop-roster", [](const std::filesystem::path& data) {
        // A non-locked roster target with a resolved unit, and its kind=roster unlock row.
        json roster = ReadJson("assets/data/roster.json");
        roster["roster"].push_back({{"id", "party.scout"},
                                    {"name", "Nyx"},
                                    {"trait", "Quick"},
                                    {"enabled", true},
                                    {"unit", "char.archer"}});
        WriteJson(data / "roster.json", roster);
        json unlocks = ReadJson("assets/data/unlocks.json");
        unlocks["unlocks"].push_back({{"id", "party.scout"},
                                      {"kind", "roster"},
                                      {"cost", 3},
                                      {"seal_gate", 3}});
        WriteJson(data / "unlocks.json", unlocks);
    });
    CHECK(boot.InitMeta());
    InstallRun(boot.registry);
    EarnFromLedger(boot.registry, 1, 1);  // 3 seals and lifetime 3: the scout's gate and price

    // Before: the unlocked roster target is gated out of selection even with a free party slot.
    CHECK(!game::IsRosterMemberSelectable(boot.registry, "party.scout"));
    const std::vector<game::FRosterChoice> before = game::BuildRosterChoices(boot.registry);
    const game::FRosterChoice* scout = FindChoice(before, "party.scout");
    CHECK(scout != nullptr);
    CHECK(scout != nullptr && scout->availability == game::ERosterAvailability::NeedsUnlock);
    CHECK(scout != nullptr && scout->unitId == "char.archer");
    CHECK(scout != nullptr && !scout->reason.empty());

    game::FPreparationDraft draft =
        game::MakeDefaultDraft(boot.registry, game::FRunStartRequest{true, -1});
    CHECK(game::ToggleDraftMember(boot.registry, draft, "party.mage"));   // a free slot
    CHECK(!game::ToggleDraftMember(boot.registry, draft, "party.scout"));  // still gated

    CHECK(game::PurchaseMetaUnlock(boot.registry, "party.scout") == game::EUnlockStatus::Purchasable);
    CHECK(game::HasMetaUnlock(boot.registry, "party.scout"));
    CHECK(game::IsRosterMemberSelectable(boot.registry, "party.scout"));
    const std::vector<game::FRosterChoice> after = game::BuildRosterChoices(boot.registry);
    const game::FRosterChoice* unlocked = FindChoice(after, "party.scout");
    CHECK(unlocked != nullptr);
    CHECK(unlocked != nullptr && unlocked->availability == game::ERosterAvailability::Selectable);
    CHECK(unlocked != nullptr && unlocked->reason.empty());

    // The purchase changes the next run: the member joins the draft and the installed run.
    CHECK(game::ToggleDraftMember(boot.registry, draft, "party.scout"));
    const std::size_t scoutIndex = MemberIndex(draft, "party.scout");
    CHECK(draft.setup.skills[scoutIndex] == game::DefaultMemberLoadout(boot.registry, "party.scout"));
    CHECK(!game::ValidateRunSetup(boot.registry, draft.setup).has_value());

    CHECK(game::StartDungeonRunFromSetup(boot.registry, kSeed + 2, true, -1, draft.setup));
    const game::FRunMember* member = FindRunMember(boot.run(), "party.scout");
    CHECK(member != nullptr);
    const auto* unit = game::ResolveRunMemberUnit(boot.registry, "party.scout");
    CHECK(unit != nullptr && unit->id == "char.archer");
    CHECK(member != nullptr && member->skills.size() == draft.setup.skills[scoutIndex].size());
}

void PurchaseRefusesLockedStubRoster() {
    FShopBoot boot("shop-locked-stub");
    // Enough funds for the stub's gate and price (and for the positive control): the refusal must be the
    // no-op guard, not a shortfall.
    const game::FMetaUnlockRow stubRow = RowOrFail(boot.registry, "party.rogue_stub");
    const game::FMetaUnlockRow hook = RowOrFail(boot.registry, "skill.hook");
    const int funds = std::max({stubRow.cost, stubRow.sealGate, hook.cost, hook.sealGate});
    WritePreset(boot, funds, LadderOf({{"1", BossLedgerLane(funds)}}));
    CHECK(boot.InitMeta());
    CHECK(game::MetaSeals(boot.registry) == funds);
    CHECK(game::LifetimeSealsEarned(boot.registry) >= funds);

    const game::FUnlockOffer stub = game::EvaluateUnlockOffer(boot.registry, "party.rogue_stub");
    CHECK(stub.status == game::EUnlockStatus::Unavailable);
    CHECK(!stub.reason.empty());
    // Positive control: with the same funds a real row is purchasable.
    CHECK(game::EvaluateUnlockOffer(boot.registry, "skill.hook").status == game::EUnlockStatus::Purchasable);

    const std::string before = ReadText(boot.metaPath);
    CHECK(game::PurchaseMetaUnlock(boot.registry, "party.rogue_stub") == game::EUnlockStatus::Unavailable);
    CHECK(game::MetaSeals(boot.registry) == funds);  // nothing was spent
    CHECK(!game::HasMetaUnlock(boot.registry, "party.rogue_stub"));  // nothing was stored
    CHECK(ReadText(boot.metaPath) == before);  // no save at all
}

// ---------------------------------------------------------------------------------------------
// The purchase mechanics: deduction, persistence, refusals
// ---------------------------------------------------------------------------------------------

void PurchaseDeductsAndPersists() {
    FShopBoot boot("shop-persist");
    CHECK(boot.InitMeta());
    const game::FMetaUnlockRow bash = RowOrFail(boot.registry, "skill.bash");
    InstallRun(boot.registry);
    const int earned = EarnAtLeast(boot.registry, std::max(bash.cost, bash.sealGate));

    CHECK(game::PurchaseMetaUnlock(boot.registry, "skill.bash") == game::EUnlockStatus::Purchasable);
    CHECK(game::MetaSeals(boot.registry) == earned - bash.cost);
    CHECK(game::LifetimeSealsEarned(boot.registry) == earned);  // spending never lowers the lifetime

    // ONE store mutation, ONE save: the persisted document already carries the deducted balance and the
    // unlock together (there is no observable paid-but-unowned intermediate state).
    const json document = ReadJson(boot.metaPath);
    CHECK(document.at("seals").get<int>() == earned - bash.cost);
    const json unlocks = PersistedUnlocks(boot);
    CHECK(CountUnlock(unlocks, "skill.bash") == 1);

    // A restart on the same file keeps the purchase and the balance.
    game::ShutdownMetaProgress(boot.registry);
    CHECK(game::FindMetaSave(boot.registry) == nullptr);
    CHECK(game::InitializeMetaProgress(boot.registry, boot.metaPath));
    CHECK(game::HasMetaUnlock(boot.registry, "skill.bash"));
    CHECK(game::MetaSeals(boot.registry) == earned - bash.cost);
    CHECK(game::LifetimeSealsEarned(boot.registry) == earned);
    CHECK(game::FindMetaSave(boot.registry) != nullptr);
    CHECK(game::FindMetaSave(boot.registry)->unlocks.size() == 1);
}

void PurchaseRejectsInsufficientSeals() {
    FShopBoot boot("shop-insufficient");
    // Balance 2, lifetime 4: the crafted state names a row whose gate is met but whose price is not, and a
    // cheaper positive control. Both are picked from the catalog, so the case follows the shop's data.
    WritePreset(boot, 2,
                LadderOf({{"1", LedgerLane({"theme.ember_crypt/0#boss", "theme.ember_crypt/1#boss"})}}));
    CHECK(boot.InitMeta());
    CHECK(game::MetaSeals(boot.registry) == 2);
    CHECK(game::LifetimeSealsEarned(boot.registry) == 4);

    const std::vector<game::FMetaUnlockRow> catalog = game::MetaUnlockCatalog(boot.registry);
    const game::FMetaUnlockRow* shortfall = nullptr;
    const game::FMetaUnlockRow* affordable = nullptr;
    for (const game::FMetaUnlockRow& row : catalog) {
        if (row.kind == "roster") continue;  // the shipped stub is Unavailable before the funds matter
        if (shortfall == nullptr && row.sealGate <= 4 && row.cost > 2) shortfall = &row;
        if (affordable == nullptr && row.sealGate <= 4 && row.cost <= 2) affordable = &row;
    }
    CHECK(shortfall != nullptr && affordable != nullptr);
    if (shortfall == nullptr || affordable == nullptr) return;

    const game::FUnlockOffer short_offer = game::EvaluateUnlockOffer(boot.registry, shortfall->id);
    CHECK(short_offer.status == game::EUnlockStatus::InsufficientSeals);
    const int missing = shortfall->cost - 2;
    CHECK(short_offer.reason.find(std::to_string(missing) + (missing == 1 ? " more seal" : " more seals")) !=
          std::string::npos);
    const std::string before = ReadText(boot.metaPath);
    CHECK(game::PurchaseMetaUnlock(boot.registry, shortfall->id) == game::EUnlockStatus::InsufficientSeals);
    CHECK(game::MetaSeals(boot.registry) == 2);
    CHECK(!game::HasMetaUnlock(boot.registry, shortfall->id));
    CHECK(ReadText(boot.metaPath) == before);

    // Positive control: the same state buys the affordable row.
    CHECK(game::EvaluateUnlockOffer(boot.registry, affordable->id).status == game::EUnlockStatus::Purchasable);
    CHECK(game::PurchaseMetaUnlock(boot.registry, affordable->id) == game::EUnlockStatus::Purchasable);
    CHECK(game::MetaSeals(boot.registry) == 2 - affordable->cost);
}

void PurchaseRejectsAlreadyUnlocked() {
    FShopBoot boot("shop-owned");
    WritePreset(boot, 2,
                LadderOf({{"1", LedgerLane({"theme.ember_crypt/0#boss"})}}),
                json::array({"skill.hook"}));
    CHECK(boot.InitMeta());
    CHECK(game::HasMetaUnlock(boot.registry, "skill.hook"));
    CHECK(game::EvaluateUnlockOffer(boot.registry, "skill.hook").status == game::EUnlockStatus::Owned);

    const std::string before = ReadText(boot.metaPath);
    CHECK(game::PurchaseMetaUnlock(boot.registry, "skill.hook") == game::EUnlockStatus::Owned);
    CHECK(game::MetaSeals(boot.registry) == 2);  // no second charge
    const json unlocks = PersistedUnlocks(boot);
    CHECK(CountUnlock(unlocks, "skill.hook") == 1);  // no duplicate row
    CHECK(ReadText(boot.metaPath) == before);  // a refused purchase never saves

    // Positive control: an unowned row in the same state goes through.
    const std::vector<game::FMetaUnlockRow> catalog = game::MetaUnlockCatalog(boot.registry);
    const game::FMetaUnlockRow* affordable = nullptr;
    for (const game::FMetaUnlockRow& row : catalog) {
        if (row.kind == "roster" || row.id == "skill.hook") continue;
        if (affordable == nullptr && row.sealGate <= 2 && row.cost <= 2) affordable = &row;
    }
    CHECK(affordable != nullptr);
    if (affordable == nullptr) return;
    CHECK(game::EvaluateUnlockOffer(boot.registry, affordable->id).status == game::EUnlockStatus::Purchasable);
    CHECK(game::PurchaseMetaUnlock(boot.registry, affordable->id) == game::EUnlockStatus::Purchasable);
    CHECK(game::MetaSeals(boot.registry) == 2 - affordable->cost);
}

void PurchaseDisabledWhenMetaDisabled() {
    FShopBoot boot("shop-disabled");
    // A file from a newer build: the session must be read-only, the file byte-identical after any attempt.
    WriteJson(boot.metaPath, json{{"schema_version", 2}, {"seals", 5}});
    const std::string before = ReadText(boot.metaPath);
    CHECK(boot.InitMeta());
    CHECK(game::MetaDisabled(boot.registry));
    CHECK(game::FindMetaSave(boot.registry) != nullptr);  // a store exists; it is just read-only
    CHECK(game::MetaSeals(boot.registry) == 0);
    CHECK(game::LifetimeSealsEarned(boot.registry) == 0);
    CHECK(game::EvaluateUnlockOffer(boot.registry, "skill.hook").status == game::EUnlockStatus::MetaDisabled);

    CHECK(game::PurchaseMetaUnlock(boot.registry, "skill.hook") == game::EUnlockStatus::MetaDisabled);
    CHECK(!game::HasMetaUnlock(boot.registry, "skill.hook"));
    CHECK(game::MetaSeals(boot.registry) == 0);
    CHECK(ReadText(boot.metaPath) == before);  // untouched, not reset and not overwritten
}

void PurchaseRefusedWithoutMeta() {
    FShopBoot boot("shop-no-meta");
    CHECK(game::FindMetaSave(boot.registry) == nullptr);
    CHECK(game::MetaDisabled(boot.registry));
    CHECK(!std::filesystem::exists(boot.metaPath));

    // The catalog is still readable through the asset boundary, but nothing can be bought without a profile.
    const game::FUnlockOffer offer = game::EvaluateUnlockOffer(boot.registry, "skill.hook");
    CHECK(offer.row.id == "skill.hook");
    CHECK(offer.status == game::EUnlockStatus::MetaDisabled);
    CHECK(game::PurchaseMetaUnlock(boot.registry, "skill.hook") == game::EUnlockStatus::MetaDisabled);
    CHECK(game::MetaSeals(boot.registry) == 0);
    CHECK(game::LifetimeSealsEarned(boot.registry) == 0);
    CHECK(!game::HasMetaUnlock(boot.registry, "skill.hook"));
    CHECK(!std::filesystem::exists(boot.metaPath));  // no store, no file
}

// ---------------------------------------------------------------------------------------------
// The seal gate: lifetime EARNED seals, derived from the ledger, never lowered by spending
// ---------------------------------------------------------------------------------------------

void SealGateUsesLifetimeEarned() {
    FShopBoot boot("shop-seal-gate");
    // The ledger spans two ranks: rank 1 one clear (+1), rank 2 one boss (+2) -> lifetime 3.
    WritePreset(boot, 0,
                LadderOf({{"1", LedgerLane({"theme.ember_crypt/0#clear"})},
                          {"2", LedgerLane({"theme.ashen_reach/0#boss"})}}));
    CHECK(boot.InitMeta());
    CHECK(game::LifetimeSealsEarned(boot.registry) == 3);  // 1 + 2, summed across ranks
    CHECK(game::MetaSeals(boot.registry) == 0);

    // The rows the crafted state exercises: one the balance starves, one whose LIFETIME gate is not met even
    // though the balance covers its price, and one that is legal on both axes.
    const std::vector<game::FMetaUnlockRow> catalog = game::MetaUnlockCatalog(boot.registry);
    const game::FMetaUnlockRow* starved = nullptr;
    const game::FMetaUnlockRow* below = nullptr;
    const game::FMetaUnlockRow* buy = nullptr;
    for (const game::FMetaUnlockRow& row : catalog) {
        if (row.kind == "roster") continue;  // the shipped stub is Unavailable before the funds matter
        if (starved == nullptr && row.sealGate <= 3 && row.cost > 0) starved = &row;
        if (below == nullptr && row.sealGate > 3 && row.cost <= 5) below = &row;
        if (buy == nullptr && row.sealGate <= 3 && row.cost <= 5) buy = &row;
    }
    CHECK(starved != nullptr && below != nullptr && buy != nullptr);
    if (starved == nullptr || below == nullptr || buy == nullptr) return;

    // Gate met, purse empty: the status is the balance refusal, not the gate.
    CHECK(game::EvaluateUnlockOffer(boot.registry, starved->id).status == game::EUnlockStatus::InsufficientSeals);

    // Balance only: a direct grant moves the purse, never the lifetime gate.
    game::GrantSeals(boot.registry, 5, "seal gate test");
    CHECK(game::MetaSeals(boot.registry) == 5);
    CHECK(game::LifetimeSealsEarned(boot.registry) == 3);

    // The balance covers `below`, but the lifetime gate does not: BelowSealGate, not InsufficientSeals.
    const game::FUnlockOffer gated = game::EvaluateUnlockOffer(boot.registry, below->id);
    const int missing = below->sealGate - 3;
    CHECK(gated.status == game::EUnlockStatus::BelowSealGate);
    CHECK(gated.reason.find(std::to_string(missing) + (missing == 1 ? " more seal" : " more seals")) !=
          std::string::npos);
    CHECK(gated.reason.find("lifetime 3/" + std::to_string(below->sealGate)) != std::string::npos);
    const std::string before = ReadText(boot.metaPath);
    CHECK(game::PurchaseMetaUnlock(boot.registry, below->id) == game::EUnlockStatus::BelowSealGate);
    CHECK(game::MetaSeals(boot.registry) == 5);
    CHECK(ReadText(boot.metaPath) == before);

    // A gate-legal row buys normally; the purchase lowers the balance and leaves the lifetime untouched.
    CHECK(game::PurchaseMetaUnlock(boot.registry, buy->id) == game::EUnlockStatus::Purchasable);
    CHECK(game::MetaSeals(boot.registry) == 5 - buy->cost);
    CHECK(game::LifetimeSealsEarned(boot.registry) == 3);
}

// ---------------------------------------------------------------------------------------------
// The save stays options-only (NoPowerFields) after a purchase
// ---------------------------------------------------------------------------------------------

/// Recursive key walk over the WRITTEN meta save. The ladder's direct children are dynamic rank keys and
/// the promotions' direct children are dynamic roster member ids; every other key must be whitelisted.
/// A key like "attack"/"health"/"bonus"/"stat" fails the case (Q18: the meta sells options, never power).
void ScanMetaSaveKeys(const json& value, const std::string& path) {
    static const std::set<std::string> kAllowed = {
        "schema_version", "saved_at", "seals", "unlocks", "roster", "ladder", "stats", "promotions",
        "id", "name", "trait", "medals", "enabled", "locked",
        "best_dungeon", "checkpoint", "granted", "grand",
        "runs", "clears", "best_depth", "cycle_best",
        "progress", "tier"};
    if (value.is_object()) {
        for (auto it = value.begin(); it != value.end(); ++it) {
            if (path == "ladder") {
                CHECK(game::IsValidMetaLadderRankKey(it.key()));
            } else if (path == "promotions") {
                CHECK(it.key().starts_with("party."));  // roster member ids, not a stat table
            } else {
                // Diagnostic before the assertion (CHECK throws the rest of the case away).
                if (!kAllowed.contains(it.key())) {
                    std::cout << "[report] forbidden meta save key '" << it.key() << "' at " << path << "\n";
                }
                CHECK(kAllowed.contains(it.key()));
            }
            ScanMetaSaveKeys(it.value(), path.empty() ? it.key() : path + "/" + it.key());
        }
    } else if (value.is_array()) {
        for (const json& entry : value) ScanMetaSaveKeys(entry, path + "[]");
    }
}

void NoPowerFieldsAfterPurchase() {
    FShopBoot boot("shop-no-power");
    CHECK(boot.InitMeta());
    InstallRun(boot.registry);
    // A boss milestone also writes the promotions block, so the scan covers both additive shapes.
    FireBossDefeat(boot.registry, 0);                                    // +2 seals, lifetime 2
    CHECK(game::PurchaseMetaUnlock(boot.registry, "skill.hook") == game::EUnlockStatus::Purchasable);
    FireBossDefeat(boot.registry, 1);                                    // +2 seals
    FireDungeonClear(boot.registry, 2);                                  // +1 seal
    CHECK(game::MetaSeals(boot.registry) == 3);
    CHECK(game::PurchaseMetaUnlock(boot.registry, "skill.mark") == game::EUnlockStatus::Purchasable);

    const json document = ReadJson(boot.metaPath);
    const auto unlocks = document.find("unlocks");
    CHECK(unlocks != document.end() && unlocks->is_array());
    CHECK(unlocks != document.end() && CountUnlock(*unlocks, "skill.hook") == 1);
    CHECK(unlocks != document.end() && CountUnlock(*unlocks, "skill.mark") == 1);
    const auto promotions = document.find("promotions");
    // Not vacuous: the boss milestones really wrote the promotion records the walk must allow.
    CHECK(promotions != document.end() && promotions->is_object());
    CHECK(promotions != document.end() && promotions->size() == game::kRunPartySize);

    ScanMetaSaveKeys(document, "");
}

// ---------------------------------------------------------------------------------------------
// The offer table: catalog order, English reasons, one status per crafted state
// ---------------------------------------------------------------------------------------------

/// Player-facing reasons: capitalized English prose. The HUD font has no Hangul, so a Hangul code point in
/// a reason is the failure this pins; typographic punctuation (the em dash) is allowed.
bool LooksEnglish(std::string_view text) {
    if (text.empty() || text.front() < 'A' || text.front() > 'Z') return false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const unsigned char byte = static_cast<unsigned char>(text[i]);
        if (byte < 0x20) return false;
        // Hangul Jamo U+1100..U+11FF encode as E1 84..87 xx; syllables U+AC00..U+D7A3 as EA..ED xx xx.
        if (byte == 0xE1 && i + 1 < text.size()) {
            const unsigned char second = static_cast<unsigned char>(text[i + 1]);
            if (second >= 0x84 && second <= 0x87) return false;
        }
        if (byte >= 0xEA && byte <= 0xED) return false;
    }
    return true;
}

void UnlockOffersCatalogOrderAndReasons() {
    FShopBoot boot("shop-offers");
    // lifetime 5 (2 + 1 + 2 across rank 1 and rank 2), balance 3, hook already owned.
    WritePreset(boot, 3,
                LadderOf({{"1", LedgerLane({"theme.ember_crypt/0#boss", "theme.ember_crypt/1#clear"})},
                          {"2", LedgerLane({"theme.ashen_reach/0#boss"})}}),
                json::array({"skill.hook"}));
    CHECK(boot.InitMeta());
    CHECK(game::MetaSeals(boot.registry) == 3);
    CHECK(game::LifetimeSealsEarned(boot.registry) == 5);

    const std::vector<game::FUnlockOffer> offers = game::BuildUnlockOffers(boot.registry);
    const json shipped = ReadJson("assets/data/unlocks.json");
    const json& rows = shipped.at("unlocks");
    CHECK(offers.size() == rows.size());
    CHECK(offers.size() == 9);
    for (std::size_t index = 0; index < offers.size() && index < rows.size(); ++index) {
        CHECK(offers[index].row.id == rows[index].at("id").get<std::string>());  // catalog order
        CHECK(!offers[index].reason.empty());  // every row carries its English reason
        if (!LooksEnglish(offers[index].reason)) {
            std::cout << "[report] non-English reason '" << offers[index].reason << "' for "
                      << offers[index].row.id << "\n";
        }
        CHECK(LooksEnglish(offers[index].reason));
    }

    // One crafted state, four decisive statuses (plus the locked-stub no-op guard).
    const game::FUnlockOffer* hook = FindOffer(offers, "skill.hook");
    const game::FUnlockOffer* bash = FindOffer(offers, "skill.bash");
    const game::FUnlockOffer* storm = FindOffer(offers, "skill.storm");
    const game::FUnlockOffer* shackle = FindOffer(offers, "skill.shackle");
    const game::FUnlockOffer* stub = FindOffer(offers, "party.rogue_stub");
    CHECK(hook != nullptr && hook->status == game::EUnlockStatus::Owned);
    CHECK(bash != nullptr && bash->status == game::EUnlockStatus::Purchasable);
    CHECK(storm != nullptr && storm->status == game::EUnlockStatus::BelowSealGate);
    CHECK(shackle != nullptr && shackle->status == game::EUnlockStatus::InsufficientSeals);
    CHECK(stub != nullptr && stub->status == game::EUnlockStatus::Unavailable);
    if (bash != nullptr) CHECK(bash->reason == "Buy for 3 seals");
    if (storm != nullptr) CHECK(storm->reason.find("lifetime 5/6") != std::string::npos);
    // The singular form is part of the player-facing text ("1 more seal", not "1 more seals").
    if (storm != nullptr) CHECK(storm->reason.find("1 more seal") != std::string::npos);
    if (shackle != nullptr) CHECK(shackle->reason.find("1 more seal") != std::string::npos);
}

// ---------------------------------------------------------------------------------------------
// The Unlocks tab: every row drawn, "needs a profile" without meta, and no view-side purchase
// ---------------------------------------------------------------------------------------------

/// A headless ImGui fixture with the software renderer and the dungeon art (the FPrepView pattern).
struct FHubView {
    std::unique_ptr<SDL_Surface, decltype(&SDL_DestroySurface)> surface{nullptr, &SDL_DestroySurface};
    FRendererPtr renderer{nullptr};
    FScratchRoot scratch;
    entt::registry registry;
    std::unique_ptr<ImGuiContext, decltype(&ImGui::DestroyContext)> context{nullptr, &ImGui::DestroyContext};

    explicit FHubView(const char* stem) : scratch(stem) {
        game::FAssetManager::Initialize(registry, {scratch.root.string(), kShipAssetRoot});
        CHECK(game::gameplay::FGameplayServices::Initialize(registry));
        CHECK(game::InitializeDungeon(registry, (scratch.root / "checkpoint.json").string()));
        surface.reset(SDL_CreateSurface(1280, 800, SDL_PIXELFORMAT_RGBA8888));
        CHECK(surface != nullptr);
        renderer.reset(SDL_CreateSoftwareRenderer(surface.get()));
        CHECK(renderer != nullptr);
        registry.ctx().emplace<game::FEngineWindowState>().width = 1280;
        registry.ctx().get<game::FEngineWindowState>().height = 800;
        registry.ctx().emplace<game::FResourceManager>().init(renderer.get());
        CHECK(game::InitializeDungeonArt(registry));
        context.reset(ImGui::CreateContext());
        CHECK(context != nullptr);
        auto& io = ImGui::GetIO();
        io.DisplaySize = {1280, 800};
        io.DeltaTime = 1.f / 60.f;
        io.Fonts->Build();
        game::InitializeDungeonUi(registry);
    }
    FHubView(const FHubView&) = delete;
    FHubView& operator=(const FHubView&) = delete;
    ~FHubView() { context.reset(); }

    /// Queues a tab focus on the REAL tab bar (the tab id hashes against the bar id; the queue is consumed
    /// by the next BeginTabBar in the same frame — the preparation_tests pattern).
    void QueueTab(const char* name) {
        ImGuiWindow* window = ImGui::FindWindowByName("Preparation");
        CHECK(window != nullptr);
        ImGuiTabBar* bar = ImGui::TabBarFindByID(ImHashStr("Hub tabs", 0, window->ID));
        CHECK(bar != nullptr);
        ImGuiTabItem* tab = ImGui::TabBarFindTabByID(bar, ImHashStr(name, 0, bar->ID));
        CHECK(tab != nullptr);
        ImGui::TabBarQueueFocus(bar, tab);
    }

    void DrawFrame(game::FPreparationScreen& screen, const char* selectTab = nullptr) {
        ImGui::NewFrame();
        if (selectTab != nullptr) QueueTab(selectTab);
        game::DrawPreparationScreen(registry, screen);
        ImGui::Render();
    }

    /// Draws one logged frame of the current tab (open + finish inside the captured frame).
    std::string DrawLoggedFrame(game::FPreparationScreen& screen, const std::filesystem::path& logPath) {
        ImGui::NewFrame();
        ImGui::LogToFile(0, logPath.string().c_str());
        game::DrawPreparationScreen(registry, screen);
        ImGui::LogFinish();
        ImGui::Render();
        return ReadText(logPath);
    }

    bool InitMeta() {
        return game::InitializeMetaProgress(registry, (scratch.root / "meta_save.json").string());
    }
};

void UnlocksTabRendersAndBuyIsTheRule() {
    FHubView view("shop-tab");
    entt::registry& registry = view.registry;
    CHECK(game::FindMetaSave(registry) == nullptr);  // meta uninitialized for the first half

    game::FPreparationScreen screen;
    screen.draft = game::MakeDefaultDraft(registry, game::FRunStartRequest{true, -1});

    // Frame 1 settles the freshly created tab bar (its tabs register during that frame); frame 2 focuses
    // Unlocks through the real bar; frame 3 is captured.
    view.DrawFrame(screen);
    view.DrawFrame(screen, "Unlocks");
    CHECK(screen.tab == game::EHubTab::Unlocks);
    CHECK(!registry.ctx().contains<game::FPreparationCommand>());

    const std::string noMetaLog =
        view.DrawLoggedFrame(screen, view.scratch.root / "shop-tab-no-meta.log");
    if (noMetaLog.find("Unlocks need a profile") == std::string::npos) {
        std::cout << "[report] no-meta unlocks log bytes=" << noMetaLog.size() << " content=[" << noMetaLog
                  << "]\n";
    }
    CHECK(noMetaLog.find("Unlocks need a profile (meta progress is off).") != std::string::npos);
    CHECK(!registry.ctx().contains<game::FPreparationCommand>());  // the view posts nothing

    // Meta on: earn exactly hook's gate/price so its row is a live "Buy for <cost> seals" reason.
    CHECK(view.InitMeta());
    const game::FMetaUnlockRow hook = RowOrFail(registry, "skill.hook");
    InstallRun(registry);
    const int earned = EarnAtLeast(registry, std::max(hook.cost, hook.sealGate));
    CHECK(game::MetaSeals(registry) == earned);

    view.DrawFrame(screen, "Unlocks");  // settle after the state change
    const std::string metaLog = view.DrawLoggedFrame(screen, view.scratch.root / "shop-tab-meta.log");
    CHECK(screen.tab == game::EHubTab::Unlocks);
    const std::string sealsLine = "Seals " + std::to_string(earned);
    const std::string lifetimeLine = "Lifetime earned " + std::to_string(earned);
    if (metaLog.find(sealsLine) == std::string::npos) {
        std::cout << "[report] meta unlocks log bytes=" << metaLog.size() << " content=[" << metaLog << "]\n";
    }
    CHECK(metaLog.find(sealsLine) != std::string::npos);
    CHECK(metaLog.find(lifetimeLine) != std::string::npos);

    // Every catalog row is drawn with its player-facing name (never the raw content id).
    const std::vector<game::FMetaUnlockRow> catalog = game::MetaUnlockCatalog(registry);
    CHECK(catalog.size() == 9);
    for (const game::FMetaUnlockRow& row : catalog) {
        const std::string name = ExpectedRowName(registry, row);
        if (metaLog.find(name) == std::string::npos) {
            std::cout << "[report] unlocks row '" << row.id << "' name '" << name << "' missing\n";
        }
        CHECK(metaLog.find(name) != std::string::npos);
        // Player-facing rows never print a raw content id (the id only lives in the hover tooltip).
        if (metaLog.find(row.id) != std::string::npos) {
            std::cout << "[report] raw unlock id '" << row.id << "' reached the tab text\n";
        }
        CHECK(metaLog.find(row.id) == std::string::npos);
    }
    // The view renders the evaluator's reason and performs no purchase on its own ("buy is the rule").
    const std::string buyReason = "Buy for " + std::to_string(hook.cost) + " seals";
    CHECK(metaLog.find(buyReason) != std::string::npos);
    CHECK(game::MetaSeals(registry) == earned);
    CHECK(!game::HasMetaUnlock(registry, "skill.hook"));
    CHECK(!registry.ctx().contains<game::FPreparationCommand>());
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
            (std::filesystem::current_path() / "out" / "test-saves" / "hub_shop_tests").string();
        std::error_code error;
        std::filesystem::create_directories(path, error);
        _putenv_s("GAME01P_SAVE_DIR", path.c_str());
    }

    return game_test::RunTests("hub_shop", {
        {"PurchaseRevealsSkillNextRun", PurchaseRevealsSkillNextRun},
        {"PurchaseAddsRuneToOfferPool", PurchaseAddsRuneToOfferPool},
        {"PurchaseUnlocksRosterEntry", PurchaseUnlocksRosterEntry},
        {"PurchaseRefusesLockedStubRoster", PurchaseRefusesLockedStubRoster},
        {"PurchaseDeductsAndPersists", PurchaseDeductsAndPersists},
        {"PurchaseRejectsInsufficientSeals", PurchaseRejectsInsufficientSeals},
        {"PurchaseRejectsAlreadyUnlocked", PurchaseRejectsAlreadyUnlocked},
        {"PurchaseDisabledWhenMetaDisabled", PurchaseDisabledWhenMetaDisabled},
        {"PurchaseRefusedWithoutMeta", PurchaseRefusedWithoutMeta},
        {"SealGateUsesLifetimeEarned", SealGateUsesLifetimeEarned},
        {"NoPowerFieldsAfterPurchase", NoPowerFieldsAfterPurchase},
        {"UnlockOffersCatalogOrderAndReasons", UnlockOffersCatalogOrderAndReasons},
        {"UnlocksTabRendersAndBuyIsTheRule", UnlocksTabRendersAndBuyIsTheRule},
    });
}