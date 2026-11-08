#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <entt/entt.hpp>

#include "gameplay/dungeon/FDungeonRun.h"
#include "gameplay/dungeon/FSaveSections.h"

namespace game {

/// The camp price table as authored in `data/dungeon camp` (WP-10). Boot validates it: both curves must be
/// nonempty, positive and non-decreasing; the cap and the discount are never negative. All amounts are
/// materials (the camp's only constraint — D-17: no action counter).
struct FCampTables {
    int actionsPerCamp{0};                  // 0 = unlimited purchases (D-17 A; the cap is materials alone)
    std::vector<int> upgradeCostSteps;      // price of the n-th reinforcement purchase, run-cumulative
    std::vector<int> runeOfferDrawCostSteps; // price of the n-th camp rune-offer purchase, run-cumulative
    int runeOfferCampCap{0};                // rune offers buyable per camp (0 disables the sink)
    int specialDiscount{0};                 // discount on the first rune-offer purchase of each camp
    int respecCost{0};                      // flat price of the node reallocation (camp.respec_cost)
};

/// The run-cumulative camp counters. Module state: it is saved in `run.ext.camp` and never in the core run
/// keys (WP-07 contract). A new run installs zeros; Continue unpacks the saved values before the run is
/// installed, so the serializer never reads FDungeonRun.
struct FCampProgress {
    std::uint32_t upgradesTotal{0};      // upgrade curve index — +1 on every successful reinforcement
    std::uint32_t runeDrawsTotal{0};     // rune-offer curve index — +1 on every successful camp purchase
    std::uint32_t campDepth{0};          // the depth runeDrawsThisCamp is valid for
    std::uint32_t runeDrawsThisCamp{0};  // valid only while campDepth == run.depth (derived reset)
};

/// The camp shop as the view sees it: the prices at the current progress. `BuildCampSession` derives it
/// from the loaded tables + the saved counters — nothing here is stored beyond the counters themselves.
struct FCampSession {
    int upgradeCost{0};          // next reinforcement purchase price
    int paidHealCost{0};         // recovery.camp_heal_cost
    int paidHealPercent{0};      // recovery.camp_heal_percent of each member's run max HP
    int respecCost{0};           // camp.respec_cost
    int runeOfferCost{0};        // next camp rune-offer price, after the first-offer discount
    bool runeOfferDiscounted{false};  // true when runeOfferCost carries the special discount
    std::uint32_t runeDrawsThisCamp{0};  // effective (derived reset) draws bought this camp
    int runeOfferCampCap{0};
    std::uint32_t runeDrawsTotal{0};
    std::uint32_t upgradesTotal{0};
};

/// The camp price tables from the registry context (installed by InitializeDungeon). A missing entry is a
/// wiring error (logged once): the tables read as empty, never as an in-code default price list.
[[nodiscard]] const FCampTables& DungeonCampTables(const entt::registry& registry);
/// The mutable run-cumulative counters. A missing entry is a wiring bug: every run installs one.
[[nodiscard]] FCampProgress& DungeonCampProgress(entt::registry& registry);
/// Read-only view of the counters (nullopt when no run installed the module).
[[nodiscard]] const FCampProgress* FindCampProgress(const entt::registry& registry);

/// `steps[min(index, steps.size() - 1)]`; an empty table (synthetic registry only) reads as 0.
[[nodiscard]] int UpgradeStepCost(const FCampTables& tables, std::uint32_t purchases);
[[nodiscard]] int RuneDrawStepCost(const FCampTables& tables, std::uint32_t purchases);
/// The next reinforcement price at the current progress.
[[nodiscard]] int UpgradeCostOf(const FCampTables& tables, const FCampProgress& progress);
/// The per-camp draw count only counts while the saved depth matches the run's: leaving the camp bumps the
/// run's depth, so the next camp starts at 0 without a reset write.
[[nodiscard]] std::uint32_t CampRuneDrawsThisCamp(const FCampProgress& progress, int runDepth);
/// The next camp rune-offer price, including the first-offer discount of this camp (never below 0).
[[nodiscard]] int RuneDrawCostOf(const FCampTables& tables, const FCampProgress& progress, int runDepth);

/// The camp shop snapshot for the view (prices + the open offer's cap state).
[[nodiscard]] FCampSession BuildCampSession(const entt::registry& registry);

/// The reference basket (the five services) at the current progress, in the WP-10 order: the next two
/// reinforcement prices, paid heal, respec, the next rune offer. `basket` = their sum, `cheapestTwo` = the
/// sum of the two smallest — the budget invariant is `basket > held >= cheapestTwo`.
struct FCampPrices {
    int upgrade{0};
    int nextUpgrade{0};
    int heal{0};
    int respec{0};
    int offer{0};
};
[[nodiscard]] FCampPrices CampPricesOf(const entt::registry& registry);
[[nodiscard]] int CampBasketTotal(const FCampPrices& prices);
[[nodiscard]] int CampCheapestTwo(const FCampPrices& prices);

/// WP-28 (M1): the camp entry metric's denominator — the sum of the two cheapest prices among the services
/// the party can actually buy now (affordability deliberately ignored: the `Validate*` functions run with
/// the run's materials temporarily raised, so a poor party still counts what it could buy). Fewer than two
/// purchasable services: the sum of what exists; none: 0. `purchasableCount` (optional) receives the
/// number of purchasable services.
[[nodiscard]] int CampCheapestTwoPurchasable(entt::registry& registry, int* purchasableCount = nullptr);

/// Camp entry report (WP-10 E1b, M1 denominator via WP-28): one LOG_INFO line with hoard_ratio = materials
/// / the purchasable two (0 when nothing is buyable). A report metric only — never a pass/fail condition.
void LogCampEntryReport(entt::registry& registry);

/// The `camp` save section (id "camp", version 1): upgrades_total / rune_draws_total / camp_depth /
/// rune_draws_this_camp. Registered once through the composition root; no static self-registration.
[[nodiscard]] FSaveSectionSerializer CampSaveSection();

/// Can the run buy one more camp rune offer? Returns the reason it cannot, or nullopt.
[[nodiscard]] std::optional<std::string> ValidateCampRuneOfferPurchase(entt::registry& registry);
/// Buys one rune offer: validates first (including the legal-candidate pool via Peek — never charge for an
/// offer that cannot open), charges the (possibly discounted) price, rolls the candidates through the
/// WP-17 API, opens the offer, bumps the counters, appends one CampSpent line and saves.
[[nodiscard]] std::optional<std::string> PurchaseCampRuneOffer(entt::registry& registry);

} // namespace game
