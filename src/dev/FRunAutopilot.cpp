#include "dev/FRunAutopilot.h"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <expected>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <utility>

#include "core/Logger.h"
#include "gameplay/dungeon/FDungeonCamp.h"
#include "gameplay/dungeon/FDungeonRun.h"
#include "gameplay/dungeon/FRewards.h"
#include "gameplay/dungeon/FRunFlow.h"

namespace game::dev {
namespace {

/// `states/FEmberTitleState.h` owns the title catalog id as `kEmberTitleId`; the literal is repeated here so
/// this dev driver stays gameplay + Logger only.
constexpr std::string_view kEmberTitleStateId = "ember.title";

/// The parser ceiling for `camp=+<U>`; the camp's price curve makes more than this meaningless in one run.
constexpr int kMaxCampUpgrades = 32;

/// Camp reinforcements target party member 0 only — the camp view's first expanded member (M-FUN Step-Camp;
/// the header's camp token, critic P5). The buying never falls through to the next member.
constexpr std::size_t kUpgradeMemberIndex = 0;

/// Offer resolves per decision point: `boss_offer="extra"` pays one more offer right after the first decision.
constexpr int kMaxOfferRounds = 4;

std::string_view Trim(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return {};
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

/// Whole-token non-negative integer in [0, max]. std::from_chars never accepts a leading '+', so the camp
/// spec strips its own '+' before calling this.
std::optional<int> ParseCount(std::string_view text, int max) {
    if (text.empty()) return std::nullopt;
    const char* begin = text.data();
    const char* end = begin + text.size();
    int value = 0;
    const auto parsed = std::from_chars(begin, end, value);
    if (parsed.ec != std::errc{} || parsed.ptr != end || value < 0 || value > max) return std::nullopt;
    return value;
}

std::string BadValue(std::string_view what, std::string_view value) {
    return std::string(what) + " '" + std::string(value) + "'";
}

/// `camp=<spec>`: "none" | [offer][+<U>]. nullopt = a bad spec; the caller names it verbatim.
struct FCampSpec {
    bool buyOffer{false};
    int upgrades{0};
};

std::optional<FCampSpec> ParseCampSpec(std::string_view spec) {
    if (spec.empty()) return std::nullopt;
    FCampSpec parsed;
    if (spec.starts_with("offer")) {
        parsed.buyOffer = true;
        spec.remove_prefix(5);
    } else if (spec == "none") {
        return parsed;
    }
    if (spec.empty()) return parsed;  // "offer"
    if (spec.front() != '+') return std::nullopt;
    const auto count = ParseCount(spec.substr(1), kMaxCampUpgrades);
    if (!count) return std::nullopt;
    parsed.upgrades = *count;
    return parsed;
}

/// One offer decision under the reward rule: card N (0-based; an out-of-range N takes the last card) or skip.
struct FOfferPick {
    int index{-1};       // the resolved card index; -1 = skip
    std::string rune;    // the picked rune id, or "skip"
};

/// The reward rule, shared by the reward modal and the camp rune-offer purchase. Resolves the open offer.
FOfferPick ResolveOfferByPolicy(entt::registry& registry, const FAutopilotPolicy& policy) {
    FOfferPick pick;
    auto* rewards = registry.ctx().find<FRewards>();
    if (rewards == nullptr || !rewards->offer.active) return pick;
    if (!policy.rewardSkip && !rewards->offer.candidates.empty()) {
        pick.index = std::min(policy.rewardCard, static_cast<int>(rewards->offer.candidates.size()) - 1);
        pick.rune = rewards->offer.candidates[static_cast<std::size_t>(pick.index)];
    } else {
        pick.rune = "skip";
    }
    (void)ResolveRuneOffer(registry, pick.index);
    return pick;
}

/// WP-27 (fix round 1): fits the picked rune into the first legal socket, the same walk the reward modal's
/// FIT RUNE block uses — the first `run.runes` copy of the id (FindUnfittedCopy's lookup), then members in
/// order, then skill slots in order, taking the first pair ValidateDungeonRuneEquip allows. A pick with no
/// pool copy (a duplicate refund) logs `none: no unfitted copy`; a party with no legal pair logs the first
/// verbatim refusal instead.
void EquipPickedRune(entt::registry& registry, const std::string& runeId) {
    const auto* run = registry.ctx().find<FDungeonRun>();
    if (run == nullptr) return;
    std::optional<std::size_t> runeIndex;
    for (std::size_t index = 0; index < run->runes.size(); ++index) {
        if (run->runes[index] == runeId) {
            runeIndex = index;  // first match, exactly FindUnfittedCopy's lookup
            break;
        }
    }
    if (!runeIndex) {
        LOG_INFO("[autopilot] equip rune={} none: no unfitted copy", runeId);
        return;
    }
    std::optional<std::string> firstProblem;
    for (std::size_t memberIndex = 0; memberIndex < run->party.size(); ++memberIndex) {
        for (std::size_t slotIndex = 0; slotIndex < run->party[memberIndex].skills.size(); ++slotIndex) {
            const auto problem = ValidateDungeonRuneEquip(registry, memberIndex, slotIndex, *runeIndex);
            if (problem) {
                if (!firstProblem) firstProblem = problem;  // verbatim rule text, first in the walk order
                continue;
            }
            if (const auto error = EquipDungeonRune(registry, memberIndex, slotIndex, *runeIndex)) {
                LOG_WARN("[autopilot] equip failed: {}", *error);
            } else {
                LOG_INFO("[autopilot] equip rune={} member={} slot={} skill={}", runeId, memberIndex, slotIndex,
                         run->party[memberIndex].skills[slotIndex].skillId);
            }
            return;
        }
    }
    LOG_INFO("[autopilot] equip rune={} none: {}", runeId,
             firstProblem ? *firstProblem : std::string("no skill slots"));
}

/// Which phase's line an offer resolve writes.
enum class EOfferContext {
    Reward,
    Camp
};

/// Resolves every offer that is open (`boss_offer="extra"` pays a second one right after the first decision),
/// bounded by kMaxOfferRounds, with one line per resolve; equips each non-skip pick when the policy asks for
/// it. Returns the number of resolves (0 = no offer was open).
int ResolveOpenOffers(entt::registry& registry, const FAutopilotPolicy& policy, const FDungeonRun& run,
                      EOfferContext context) {
    int resolves = 0;
    for (; resolves < kMaxOfferRounds; ++resolves) {
        const auto* rewards = registry.ctx().find<FRewards>();
        if (rewards == nullptr || !rewards->offer.active) break;
        const FOfferPick pick = ResolveOfferByPolicy(registry, policy);
        if (context == EOfferContext::Reward) {
            LOG_INFO("[autopilot] reward depth={} room={} pick={} rune={}", run.depth, run.room, pick.index,
                     pick.rune);
        } else {
            LOG_INFO("[autopilot] camp offer=bought pick={}", pick.rune);
        }
        if (policy.equipAuto && pick.rune != "skip") EquipPickedRune(registry, pick.rune);
    }
    return resolves;
}

} // namespace

std::expected<FAutopilotPolicy, std::string> ParseAutopilotPolicy(std::string_view text) {
    const std::string_view trimmed = Trim(text);
    if (trimmed.empty()) return std::unexpected(std::string("empty autopilot policy"));

    // Every field starts at the default policy's value; `continue` is present only when written (header
    // contract), so only that field is overridden here before the tokens are applied.
    FAutopilotPolicy policy;
    policy.campContinue = false;
    bool sawReward = false;
    bool sawEvent = false;
    bool sawChest = false;
    bool sawCamp = false;
    bool sawContinue = false;
    bool sawEquip = false;

    std::size_t pos = 0;
    while (pos <= trimmed.size()) {
        std::size_t comma = trimmed.find(',', pos);
        if (comma == std::string_view::npos) comma = trimmed.size();
        const std::string_view token = Trim(trimmed.substr(pos, comma - pos));
        const std::size_t equals = token.find('=');
        const std::string_view key = Trim(equals == std::string_view::npos ? token : token.substr(0, equals));
        const std::string_view value =
            equals == std::string_view::npos ? std::string_view{} : Trim(token.substr(equals + 1));

        if (key == "continue" && equals == std::string_view::npos) {
            if (sawContinue) return std::unexpected(std::string("duplicate token 'continue'"));
            sawContinue = true;
            policy.campContinue = true;
        } else if (key == "reward") {
            if (sawReward) return std::unexpected(std::string("duplicate token 'reward'"));
            sawReward = true;
            if (value == "skip") {
                policy.rewardSkip = true;
            } else if (value.starts_with("card")) {
                const auto card = ParseCount(value.substr(4), std::numeric_limits<int>::max());
                if (!card) return std::unexpected(BadValue("bad reward value", value));
                policy.rewardCard = *card;
                policy.rewardSkip = false;
            } else {
                return std::unexpected(BadValue("bad reward value", value));
            }
        } else if (key == "event") {
            if (sawEvent) return std::unexpected(std::string("duplicate token 'event'"));
            sawEvent = true;
            if (value == "first") policy.eventOption = 0;
            else if (value == "second") policy.eventOption = 1;
            else return std::unexpected(BadValue("bad event value", value));
        } else if (key == "chest") {
            if (sawChest) return std::unexpected(std::string("duplicate token 'chest'"));
            sawChest = true;
            if (value == "open") policy.chestOnEvenDungeons = true;
            else if (value == "never") policy.chestOnEvenDungeons = false;
            else return std::unexpected(BadValue("bad chest value", value));
        } else if (key == "camp") {
            if (sawCamp) return std::unexpected(std::string("duplicate token 'camp'"));
            sawCamp = true;
            const auto spec = ParseCampSpec(value);
            if (!spec) return std::unexpected(BadValue("bad camp spec", value));
            policy.campBuyOffer = spec->buyOffer;
            policy.campUpgrades = spec->upgrades;
        } else if (key == "equip") {
            if (sawEquip) return std::unexpected(std::string("duplicate token 'equip'"));
            sawEquip = true;
            if (value == "auto") policy.equipAuto = true;
            else if (value == "off") policy.equipAuto = false;
            else return std::unexpected(BadValue("bad equip value", value));
        } else {
            return std::unexpected(BadValue("unknown token", token));
        }

        if (comma == trimmed.size()) break;
        pos = comma + 1;
    }
    return policy;
}

FRunAutopilot::FRunAutopilot(FAutopilotPolicy policy, int maxRooms)
    : policy_(std::move(policy)), maxRooms_(maxRooms) {}

EAutopilotStatus FRunAutopilot::tick(entt::registry& registry, std::string_view topStateId) {
    const FDungeonRun* run = registry.ctx().find<FDungeonRun>();
    const std::tuple<int, int, int> key = run != nullptr
        ? std::tuple<int, int, int>{run->depth, run->room, static_cast<int>(run->phase)}
        : std::tuple<int, int, int>{-1, -1, -1};

    if (topStateId != lastTop_ || key != lastKey_) {
        acted_ = false;
        framesSinceAct_ = 0;
        lastTop_ = std::string(topStateId);
        lastKey_ = key;
    }

    if (acted_) {
        ++framesSinceAct_;
        if (framesSinceAct_ >= kAutopilotStuckFrames) {
            LOG_ERROR("[autopilot] stuck at '{}' depth={} room={} after {} frames", topStateId, std::get<0>(key),
                      std::get<1>(key), framesSinceAct_);
            return EAutopilotStatus::Stuck;
        }
        return EAutopilotStatus::Running;
    }

    if (maxRooms_ > 0 && roomsCompleted_ >= maxRooms_ && topStateId != kRunModalReward) {
        LOG_INFO("[autopilot] room limit {} reached at depth={} room={}", maxRooms_, std::get<0>(key),
                 std::get<1>(key));
        return EAutopilotStatus::Finished;
    }

    const auto act = [this]() {
        acted_ = true;
        framesSinceAct_ = 0;
    };

    if (topStateId == kEmberTitleStateId) {
        auto* request = registry.ctx().find<FMenuRequest>();
        if (request == nullptr) request = &registry.ctx().emplace<FMenuRequest>();
        request->value = EMenuRequest::Start;
        request->fresh = true;
        LOG_INFO("[autopilot] title: start fresh run");
        act();
        return EAutopilotStatus::Running;
    }

    if (topStateId == kRunModalReward) {
        if (run != nullptr && run->phase == EDungeonPhase::Reward) {
            const int resolves = ResolveOpenOffers(registry, policy_, *run, EOfferContext::Reward);
            if (resolves == 0) {
                LOG_INFO("[autopilot] reward depth={} room={} pick=none", run->depth, run->room);
            }
            PostRunIntent(registry, ERunIntent::ContinueReward);
            ++roomsCompleted_;
            act();
        }
        return EAutopilotStatus::Running;
    }

    if (topStateId == kRunModalEvent) {
        if (run != nullptr) {
            const bool loot = (policy_.chestOnEvenDungeons && run->depth % 2 == 0) || policy_.eventOption == 1;
            PostRunIntent(registry, loot ? ERunIntent::ChooseEventLoot : ERunIntent::ChooseEventRest);
            LOG_INFO("[autopilot] event depth={} room={} choice={}", run->depth, run->room, loot ? "loot" : "rest");
            act();
        }
        return EAutopilotStatus::Running;
    }

    if (topStateId == kRunModalCamp) {
        if (run == nullptr) return EAutopilotStatus::Running;

        if (policy_.campBuyOffer) {
            if (const auto problem = ValidateCampRuneOfferPurchase(registry)) {
                LOG_INFO("[autopilot] camp offer=skipped reason={}", *problem);
            } else if (const auto error = PurchaseCampRuneOffer(registry)) {
                LOG_INFO("[autopilot] camp offer=skipped reason={}", *error);
            } else {
                (void)ResolveOpenOffers(registry, policy_, *run, EOfferContext::Camp);
            }
        }

        // Reinforcements: member 0 only (the camp view's candidates[0] for the first expanded member, M-FUN
        // Step-Camp). Each of the U purchases takes the lowest upgrade index member 0 accepts; no legal
        // upgrade — or an empty party — stops the buying.
        if (const auto* growth = registry.ctx().find<FDungeonGrowth>();
            growth != nullptr && !run->party.empty()) {
            for (int bought = 0; bought < policy_.campUpgrades; ++bought) {
                bool purchased = false;
                for (std::size_t upgradeIndex = 0; upgradeIndex < growth->statUpgrades.size(); ++upgradeIndex) {
                    if (ValidateDungeonStatUpgrade(registry, kUpgradeMemberIndex, upgradeIndex)) continue;
                    if (const auto error = UpgradeDungeonStat(registry, kUpgradeMemberIndex, upgradeIndex)) {
                        LOG_WARN("[autopilot] camp upgrade failed: {}", *error);
                        continue;
                    }
                    const FStatUpgrade& upgrade = growth->statUpgrades[upgradeIndex];
                    const std::string& stat = upgrade.stat.empty() ? upgrade.displayName : upgrade.stat;
                    LOG_INFO("[autopilot] camp upgrade member={} stat={} materials_left={}", kUpgradeMemberIndex, stat,
                             run->materials);
                    purchased = true;
                    break;
                }
                if (!purchased) break;
            }
        }

        if (policy_.campContinue) {
            PostRunIntent(registry, ERunIntent::LeaveCamp);
            LOG_INFO("[autopilot] camp descend depth={}", run->depth);
            act();
            return EAutopilotStatus::Running;
        }
        LOG_INFO("[autopilot] camp: no continue token, stopping");
        return EAutopilotStatus::Finished;
    }

    if (topStateId == kRunModalRunEnd) {
        LOG_INFO("[autopilot] run end depth={} room={} cleared={}", run != nullptr ? run->depth : std::get<0>(key),
                 run != nullptr ? run->room : std::get<1>(key), run != nullptr ? run->cleared : -1);
        return EAutopilotStatus::Finished;
    }

    // Battle, prepare, menus: nothing to drive (battles belong to GAME01P_AUTOPLAY).
    return EAutopilotStatus::Running;
}

} // namespace game::dev
