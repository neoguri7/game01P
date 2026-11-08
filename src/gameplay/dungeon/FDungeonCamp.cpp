#include "gameplay/dungeon/FDungeonCamp.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include "core/Logger.h"
#include "gameplay/dungeon/FRewards.h"
#include "gameplay/dungeon/FRunEventLog.h"

namespace game {
namespace {

constexpr const char* kCampSectionId = "camp";
constexpr int kCampSectionVersion = 1;
constexpr const char* kRuneOfferPurchaseId = "rune_offer";

/// One non-negative counter read. An absent key keeps the default; a wrong type or a negative value marks
/// the section corrupt (the caller resets the whole module state — counters only ever make prices too high
/// or too low, never brick the run).
void ReadCounter(const nlohmann::json& section, const char* key, std::uint32_t& destination, bool& corrupt) {
    const auto found = section.find(key);
    if (found == section.end()) return;
    if (!found->is_number_integer() && !found->is_number_unsigned()) {
        corrupt = true;
        return;
    }
    const auto value = found->get<std::int64_t>();
    if (value < 0 || value > static_cast<std::int64_t>(std::numeric_limits<std::uint32_t>::max())) {
        corrupt = true;
        return;
    }
    destination = static_cast<std::uint32_t>(value);
}

nlohmann::json PackCamp(entt::registry& registry) {
    const auto* progress = registry.ctx().find<FCampProgress>();
    if (progress == nullptr) return nlohmann::json::object();  // no module state: the section's default
    return nlohmann::json{
        {"upgrades_total", progress->upgradesTotal},
        {"rune_draws_total", progress->runeDrawsTotal},
        {"camp_depth", progress->campDepth},
        {"rune_draws_this_camp", progress->runeDrawsThisCamp},
    };
}

/// The unpack is deliberately forgiving, like the offers section: a corrupt counter resets the module state
/// to its default (zero) instead of failing the whole load. It must not read FDungeonRun — on the continue
/// path the section is unpacked before the run is installed.
void UnpackCamp(entt::registry& registry, const nlohmann::json& section) {
    FCampProgress fresh;
    bool corrupt = false;
    if (section.is_object() && !section.empty()) {
        ReadCounter(section, "upgrades_total", fresh.upgradesTotal, corrupt);
        ReadCounter(section, "rune_draws_total", fresh.runeDrawsTotal, corrupt);
        ReadCounter(section, "camp_depth", fresh.campDepth, corrupt);
        ReadCounter(section, "rune_draws_this_camp", fresh.runeDrawsThisCamp, corrupt);
    }
    if (corrupt) {
        LOG_WARN("The saved camp progress is unusable; it resets to zero (prices restart at the first step).");
        fresh = FCampProgress{};
    }
    registry.ctx().erase<FCampProgress>();
    registry.ctx().emplace<FCampProgress>(fresh);
}

/// WP-28 (M1): the camp hoard metric must drop a service only when it cannot be bought at all, never
/// because the party is poor. The `Validate*` functions are the one definition of "buyable", so they are
/// run with the purse temporarily raised instead of re-stating their conditions in a second place (a copy
/// would drift silently). Scoped, restored on destruction, never saved.
class FFullyFundedRun {
public:
    explicit FFullyFundedRun(entt::registry& registry) : run_(registry.ctx().find<FDungeonRun>()) {
        if (run_ != nullptr) {
            saved_ = run_->materials;
            run_->materials = std::numeric_limits<int>::max();
        }
    }
    ~FFullyFundedRun() {
        if (run_ != nullptr) run_->materials = saved_;
    }
    FFullyFundedRun(const FFullyFundedRun&) = delete;
    FFullyFundedRun& operator=(const FFullyFundedRun&) = delete;

private:
    FDungeonRun* run_{nullptr};
    int saved_{0};
};

} // namespace

const FCampTables& DungeonCampTables(const entt::registry& registry) {
    if (const auto* tables = registry.ctx().find<FCampTables>()) return *tables;
    // A missing table is a wiring error, never a content default: no camp prices live in code. Empty steps
    // read as 0 — the failure is loud once so it cannot hide.
    static const FCampTables kEmpty;
    static bool reported = false;
    if (!reported) {
        reported = true;
        LOG_ERROR("FCampTables is missing: InitializeDungeon did not load the camp price block.");
    }
    return kEmpty;
}

FCampProgress& DungeonCampProgress(entt::registry& registry) {
    return registry.ctx().get<FCampProgress>();
}

const FCampProgress* FindCampProgress(const entt::registry& registry) {
    return registry.ctx().find<FCampProgress>();
}

int UpgradeStepCost(const FCampTables& tables, std::uint32_t purchases) {
    if (tables.upgradeCostSteps.empty()) return 0;
    const auto index = std::min<std::size_t>(purchases, tables.upgradeCostSteps.size() - 1);
    return tables.upgradeCostSteps[index];
}

int RuneDrawStepCost(const FCampTables& tables, std::uint32_t purchases) {
    if (tables.runeOfferDrawCostSteps.empty()) return 0;
    const auto index = std::min<std::size_t>(purchases, tables.runeOfferDrawCostSteps.size() - 1);
    return tables.runeOfferDrawCostSteps[index];
}

int UpgradeCostOf(const FCampTables& tables, const FCampProgress& progress) {
    return UpgradeStepCost(tables, progress.upgradesTotal);
}

std::uint32_t CampRuneDrawsThisCamp(const FCampProgress& progress, int runDepth) {
    if (runDepth < 0 || progress.campDepth != static_cast<std::uint32_t>(runDepth)) return 0;
    return progress.runeDrawsThisCamp;
}

int RuneDrawCostOf(const FCampTables& tables, const FCampProgress& progress, int runDepth) {
    const int base = RuneDrawStepCost(tables, progress.runeDrawsTotal);
    const int discount = CampRuneDrawsThisCamp(progress, runDepth) == 0 ? tables.specialDiscount : 0;
    return std::max(0, base - discount);
}

FCampSession BuildCampSession(const entt::registry& registry) {
    FCampSession session;
    const auto* tables = registry.ctx().find<FCampTables>();
    if (tables == nullptr) return session;
    const auto* run = registry.ctx().find<FDungeonRun>();
    const int depth = run != nullptr ? run->depth : -1;
    static const FCampProgress kNoProgress;
    const auto* progress = registry.ctx().find<FCampProgress>();
    const FCampProgress& state = progress != nullptr ? *progress : kNoProgress;
    const auto& recovery = DungeonRecovery(registry);
    session.upgradeCost = UpgradeCostOf(*tables, state);
    session.paidHealCost = recovery.campHealCost;
    session.paidHealPercent = recovery.campHealPercent;
    session.respecCost = tables->respecCost;
    session.runeOfferCost = RuneDrawCostOf(*tables, state, depth);
    session.runeOfferDiscounted = CampRuneDrawsThisCamp(state, depth) == 0 && tables->specialDiscount > 0;
    session.runeDrawsThisCamp = CampRuneDrawsThisCamp(state, depth);
    session.runeOfferCampCap = tables->runeOfferCampCap;
    session.runeDrawsTotal = state.runeDrawsTotal;
    session.upgradesTotal = state.upgradesTotal;
    return session;
}

FCampPrices CampPricesOf(const entt::registry& registry) {
    FCampPrices prices;
    const auto* tables = registry.ctx().find<FCampTables>();
    if (tables == nullptr) return prices;
    const auto* run = registry.ctx().find<FDungeonRun>();
    static const FCampProgress kNoProgress;
    const auto* progress = registry.ctx().find<FCampProgress>();
    const FCampProgress& state = progress != nullptr ? *progress : kNoProgress;
    const int depth = run != nullptr ? run->depth : -1;
    const auto& recovery = DungeonRecovery(registry);
    prices.upgrade = UpgradeCostOf(*tables, state);
    prices.nextUpgrade = UpgradeStepCost(*tables, state.upgradesTotal + 1);
    prices.heal = recovery.campHealCost;
    prices.respec = tables->respecCost;
    prices.offer = RuneDrawStepCost(*tables, state.runeDrawsTotal);
    return prices;
}

int CampBasketTotal(const FCampPrices& prices) {
    return prices.upgrade + prices.nextUpgrade + prices.heal + prices.respec + prices.offer;
}

int CampCheapestTwo(const FCampPrices& prices) {
    const int values[] = {prices.upgrade, prices.nextUpgrade, prices.heal, prices.respec, prices.offer};
    int sorted[5];
    std::copy(std::begin(values), std::end(values), sorted);
    std::sort(std::begin(sorted), std::end(sorted));
    return sorted[0] + sorted[1];
}

int CampCheapestTwoPurchasable(entt::registry& registry, int* purchasableCount) {
    if (purchasableCount != nullptr) *purchasableCount = 0;
    const auto* run = registry.ctx().find<FDungeonRun>();
    const auto* progress = registry.ctx().find<FCampProgress>();
    if (run == nullptr || progress == nullptr || run->phase != EDungeonPhase::Camp) return 0;

    const FCampPrices prices = CampPricesOf(registry);
    std::vector<int> costs;

    // Reinforcement: count the purchases still legal within the run caps, floor((cap - bonus)/amount).
    // The validator's condition (`bonus + amount > cap` rejects) answers one purchase; the second price
    // entry needs "would a second purchase also be legal", which only the sequence count can say. Direct
    // arithmetic chosen over a hypothetical purchase mutation of the live run.
    int purchases = 0;
    if (const auto* growth = registry.ctx().find<FDungeonGrowth>()) {
        for (const FRunMember& member : run->party) {
            for (const FStatUpgrade& upgrade : growth->statUpgrades) {
                if (upgrade.amount <= 0) continue;
                const int headroom = upgrade.cap - DungeonMemberStatBonus(member, upgrade.stat);
                if (headroom >= upgrade.amount) purchases += headroom / upgrade.amount;
            }
        }
    }
    if (purchases >= 1) costs.push_back(prices.upgrade);
    if (purchases >= 2) costs.push_back(prices.nextUpgrade);

    {
        // Heal/respec/offer gate on affordability only through the validators, so the raised purse keeps the
        // metric about availability. (PeekRuneCandidates inside the offer validator consumes nothing.)
        const FFullyFundedRun funded{registry};
        if (!ValidateDungeonCampHeal(registry).has_value()) costs.push_back(prices.heal);
        if (!ValidateDungeonRespec(registry).has_value()) costs.push_back(prices.respec);
        if (!ValidateCampRuneOfferPurchase(registry).has_value()) costs.push_back(prices.offer);
    }

    if (purchasableCount != nullptr) *purchasableCount = static_cast<int>(costs.size());
    if (costs.empty()) return 0;                 // no division by zero in the log's hoard_ratio
    if (costs.size() == 1) return costs.front(); // fewer than two: the sum of what exists
    std::sort(costs.begin(), costs.end());
    return costs[0] + costs[1];
}

void LogCampEntryReport(entt::registry& registry) {
    const auto* run = registry.ctx().find<FDungeonRun>();
    const auto* tables = registry.ctx().find<FCampTables>();
    if (run == nullptr || tables == nullptr) return;
    const FCampPrices prices = CampPricesOf(registry);
    // WP-28 (M1): the denominator counts only the services that are buyable at all (respec without
    // allocated nodes, an offer with no legal pool / used-up cap and a heal with nothing to restore are
    // not part of a minimal camp visit); affordability is deliberately ignored — see the function.
    int purchasable = 0;
    const int cheapestTwo = CampCheapestTwoPurchasable(registry, &purchasable);
    const double hoard = cheapestTwo > 0 ? static_cast<double>(run->materials) / cheapestTwo : 0.0;
    LOG_INFO("[camp] hoard_ratio={:.2f} materials={} cheapest_two={} purchasable={} basket={} upgrades={} draws={}",
             hoard, run->materials, cheapestTwo, purchasable, CampBasketTotal(prices),
             FindCampProgress(registry) != nullptr ? FindCampProgress(registry)->upgradesTotal : 0u,
             FindCampProgress(registry) != nullptr ? FindCampProgress(registry)->runeDrawsTotal : 0u);
}

FSaveSectionSerializer CampSaveSection() {
    FSaveSectionSerializer serializer;
    serializer.id = kCampSectionId;
    serializer.version = kCampSectionVersion;
    serializer.pack = PackCamp;
    serializer.unpack = UnpackCamp;
    return serializer;
}

std::optional<std::string> ValidateCampRuneOfferPurchase(entt::registry& registry) {
    const auto* run = registry.ctx().find<FDungeonRun>();
    const auto* tables = registry.ctx().find<FCampTables>();
    if (run == nullptr || tables == nullptr) return "Run state is unavailable.";
    if (run->phase != EDungeonPhase::Camp) return "Rune offers can be bought at camp only.";
    const auto* progress = registry.ctx().find<FCampProgress>();
    if (progress == nullptr) return "Camp progress is unavailable.";
    auto* rewards = registry.ctx().find<FRewards>();
    if (rewards == nullptr) return "The reward offer module is unavailable.";
    if (rewards->offer.active) return "Resolve the open rune offer first.";
    if (CampRuneDrawsThisCamp(*progress, run->depth) >= static_cast<std::uint32_t>(tables->runeOfferCampCap)) {
        return "This camp's rune offers are used up.";
    }
    const int cost = RuneDrawCostOf(*tables, *progress, run->depth);
    if (run->materials < cost) return "Not enough reinforcement materials.";
    // The pool gate runs BEFORE charging: an offer that cannot open must never cost materials. The peek
    // does not consume a draw index; the actual roll happens after the charge and returns the same list.
    const auto minimum = static_cast<std::size_t>(std::max(1, rewards->tables.minLegalCandidates));
    if (PeekRuneCandidates(registry).size() < minimum) return "No legal rune candidates are available.";
    return std::nullopt;
}

std::optional<std::string> PurchaseCampRuneOffer(entt::registry& registry) {
    if (const auto problem = ValidateCampRuneOfferPurchase(registry)) return problem;
    auto& run = registry.ctx().get<FDungeonRun>();
    auto& tables = registry.ctx().get<FCampTables>();
    auto& progress = registry.ctx().get<FCampProgress>();
    auto& rewards = registry.ctx().get<FRewards>();

    const int cost = RuneDrawCostOf(tables, progress, run.depth);
    run.materials -= cost;
    // The same gate the validation used, re-checked against the rolled list: a short draw here would mean
    // the pool changed between validation and charge (it cannot — the call is synchronous).
    auto candidates = RollRuneCandidates(registry);  // consumes one draw index of the (seed, depth, room) grid
    const auto minimum = static_cast<std::size_t>(std::max(1, rewards.tables.minLegalCandidates));
    if (candidates.size() < minimum) {
        LOG_ERROR("Camp rune offer came up short after validation; the purchase is refunded.");
        run.materials += cost;
        return "No legal rune candidates are available.";
    }
    rewards.offer.active = true;
    rewards.offer.candidates = std::move(candidates);
    progress.runeDrawsTotal += 1;
    progress.runeDrawsThisCamp = CampRuneDrawsThisCamp(progress, run.depth) + 1;
    progress.campDepth = static_cast<std::uint32_t>(run.depth);
    // WP-19a: one line per purchase; the amount is what was actually paid (after the discount).
    AppendRunEvent(registry, ERunEventKind::CampSpent, kRuneOfferPurchaseId, cost, -1);
    SaveDungeonRun(registry);
    return std::nullopt;
}

} // namespace game
