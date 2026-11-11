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
#include <vector>

#include "core/Logger.h"
#include "dev/FEnginePolicy.h"
#include "gameplay/dungeon/FDungeonCamp.h"
#include "gameplay/dungeon/FDungeonRun.h"
#include "gameplay/dungeon/FEventContent.h"
#include "gameplay/dungeon/FEventEffects.h"
#include "gameplay/dungeon/FEventSection.h"
#include "gameplay/dungeon/FRewards.h"
#include "gameplay/dungeon/FRunFlow.h"
#include "gameplay/dungeon/FRunPath.h"
#include "presentation/FPreparationView.h"

namespace game::dev {
namespace {

/// `states/FEmberTitleState.h` owns the title catalog id as `kEmberTitleId`; the literal is repeated here so
/// this dev driver stays gameplay + Logger only.
constexpr std::string_view kEmberTitleStateId = "ember.title";
/// Same rule for the preparation screen (`states/FEmberPreparationState.h` owns `kEmberPrepareId`).
constexpr std::string_view kEmberPrepareStateId = "ember.prepare";

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
    int index{-1};          // the resolved card index; -1 = skip
    std::string rune;       // the picked rune id, or "skip"
    std::string_view rule;  // WP-00d reward=engine: "complete" | "start" | "card0"; empty = no rule suffix
};

/// The reward rule, shared by the reward modal and the camp rune-offer purchase. Resolves the open offer.
FOfferPick ResolveOfferByPolicy(entt::registry& registry, const FAutopilotPolicy& policy) {
    FOfferPick pick;
    auto* rewards = registry.ctx().find<FRewards>();
    if (rewards == nullptr || !rewards->offer.active) return pick;
    if (!policy.rewardSkip && !rewards->offer.candidates.empty()) {
        if (policy.rewardEngine) {
            // WP-00d: score every card BEFORE the resolve (ScoreEngineCard reads ownership): the highest
            // score wins, the lowest index on ties; all zeros -> card 0.
            int bestScore = 0;
            std::size_t bestIndex = 0;
            for (std::size_t index = 0; index < rewards->offer.candidates.size(); ++index) {
                const int score = ScoreEngineCard(registry, rewards->offer.candidates[index]);
                if (score > bestScore) {
                    bestScore = score;
                    bestIndex = index;
                }
            }
            pick.index = static_cast<int>(bestIndex);
            if (bestScore == kEngineCardScoreComplete) {
                pick.rule = "complete";
            } else if (bestScore == kEngineCardScoreStart) {
                pick.rule = "start";
            } else {
                pick.rule = "card0";
            }
        } else {
            pick.index = std::min(policy.rewardCard, static_cast<int>(rewards->offer.candidates.size()) - 1);
        }
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
            if (pick.rule.empty()) {
                LOG_INFO("[autopilot] reward depth={} room={} pick={} rune={}", run.depth, run.room, pick.index,
                         pick.rune);
            } else {
                LOG_INFO("[autopilot] reward depth={} room={} pick={} rune={} rule={}", run.depth, run.room,
                         pick.index, pick.rune, pick.rule);
            }
        } else {
            if (pick.rule.empty()) {
                LOG_INFO("[autopilot] camp offer=bought pick={}", pick.rune);
            } else {
                LOG_INFO("[autopilot] camp offer=bought pick={} rule={}", pick.rune, pick.rule);
            }
        }
        if (pick.rune != "skip") {
            if (policy.equip == EAutopilotEquip::Auto) {
                EquipPickedRune(registry, pick.rune);
            } else if (policy.equip == EAutopilotEquip::Engine) {
                EquipRuneByEnginePolicy(registry, pick.rune);
            }
        }
    }
    return resolves;
}

} // namespace

bool AutopilotShouldRest(const entt::registry& registry, const FAutopilotPolicy& policy) {
    if (policy.campRestBelow <= 0) return false;
    if (ValidateDungeonCampRest(registry).has_value()) return false;
    const FCampRestPreview preview = DungeonCampRestPreview(registry);
    if (preview.partyHpMax <= 0) return false;
    return preview.partyHp * 100 / preview.partyHpMax < policy.campRestBelow;
}

std::expected<FAutopilotPolicy, std::string> ParseAutopilotPolicy(std::string_view text) {
    const std::string_view trimmed = Trim(text);
    if (trimmed.empty()) return std::unexpected(std::string("empty autopilot policy"));

    // Every field starts at the default policy's value; `continue` is present only when written (header
    // contract), so only that field is overridden here before the tokens are applied.
    FAutopilotPolicy policy;
    policy.campContinue = false;
    bool sawReward = false;
    bool sawEvent = false;
    bool sawFork = false;
    bool sawChest = false;
    bool sawCamp = false;
    bool sawContinue = false;
    bool sawEquip = false;
    bool sawRest = false;

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
            } else if (value == "engine") {
                // WP-00d: score every card before resolving (ResolveOfferByPolicy); rewardCard stays 0.
                policy.rewardEngine = true;
                policy.rewardSkip = false;
                policy.rewardCard = 0;
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
        } else if (key == "fork") {
            if (sawFork) return std::unexpected(std::string("duplicate token 'fork'"));
            sawFork = true;
            if (value == "standard") policy.forkVariant = false;
            else if (value == "variant") policy.forkVariant = true;
            else return std::unexpected(BadValue("bad fork value", value));
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
            if (value == "auto") policy.equip = EAutopilotEquip::Auto;
            else if (value == "off") policy.equip = EAutopilotEquip::Off;
            else if (value == "engine") policy.equip = EAutopilotEquip::Engine;
            else return std::unexpected(BadValue("bad equip value", value));
        } else if (key == "rest") {
            if (sawRest) return std::unexpected(std::string("duplicate token 'rest'"));
            sawRest = true;
            const auto percent = ParseCount(value, 100);
            if (!percent) return std::unexpected(BadValue("bad rest value", value));
            policy.campRestBelow = *percent;
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
    // WP-09h P2: the event stage (1 while the ctx FEventInstance is resolved) joins the decision key. The
    // event room is two-stage — ChooseEventOption keeps (depth, room, phase) and only flips `resolved` — so
    // without the fourth element the continue after a resolve looked like the same decision point and timed
    // out to Stuck after kAutopilotStuckFrames.
    const FEventInstance* eventInstance = registry.ctx().find<FEventInstance>();
    const int eventStage = eventInstance != nullptr && eventInstance->resolved ? 1 : 0;
    const std::tuple<int, int, int, int> key = run != nullptr
        ? std::tuple<int, int, int, int>{run->depth, run->room, static_cast<int>(run->phase), eventStage}
        : std::tuple<int, int, int, int>{-1, -1, -1, -1};

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
        LogRunEndEngines(registry);
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

    if (topStateId == kEmberPrepareStateId) {
        // The preparation screen starts the run from its default draft: the same command its Start button
        // posts. A blocked Start keeps this decision point, so the stuck guard reports it.
        registry.ctx().insert_or_assign<FPreparationCommand>(FPreparationCommand{EPreparationCommand::Start});
        LOG_INFO("[autopilot] prepare: start with the default draft");
        act();
        return EAutopilotStatus::Running;
    }

    if (topStateId == kRunModalReward) {
        if (run != nullptr && run->phase == EDungeonPhase::Reward) {
            const int resolves = ResolveOpenOffers(registry, policy_, *run, EOfferContext::Reward);
            if (resolves == 0) {
                LOG_INFO("[autopilot] reward depth={} room={} pick=none", run->depth, run->room);
            }
            TrackArmedEngines(registry, run->depth);
            PostRunIntent(registry, policy_.forkVariant && IsForkPending(registry) ? ERunIntent::TakeFork
                                                                                   : ERunIntent::ContinueReward);
            ++roomsCompleted_;
            act();
        }
        return EAutopilotStatus::Running;
    }

    if (topStateId == kRunModalEvent) {
        // WP-09h P2: the event room is two-stage (choose the option, then continue the result screen) and
        // gate-aware. The stage rides in the decision key (eventStage): the resolve keeps (depth, room,
        // phase) constant, and only the key change makes the continue a new decision point.
        if (run == nullptr) {
            LOG_ERROR("[autopilot] the event modal is open without a run");
            return EAutopilotStatus::Stuck;
        }
        // PostRunIntent may insert into the context (dense map): the decision data is copied out before the
        // post, and every later line reads these copies.
        const int depth = run->depth;
        const int room = run->room;
        const auto instance = CurrentEventInstance(registry);
        if (!instance.has_value()) {
            LOG_ERROR("[autopilot] event depth={} room={}: no event instance", depth, room);
            return EAutopilotStatus::Stuck;
        }
        if (instance->resolved) {
            // WP-41: an offer the event opened is resolved first (the reward rule), then the fork or continue.
            (void)ResolveOpenOffers(registry, policy_, *run, EOfferContext::Reward);
            PostRunIntent(registry, policy_.forkVariant && IsForkPending(registry) ? ERunIntent::TakeFork
                                                                                   : ERunIntent::ContinueEvent);
            LOG_INFO("[autopilot] event continue depth={} room={}", depth, room);
            act();
            return EAutopilotStatus::Running;
        }
        const auto def = FindEventDef(registry, instance->eventId);
        if (!def.has_value()) {
            LOG_ERROR("[autopilot] event '{}' (depth {} room {}) is not in the catalog", instance->eventId, depth,
                      room);
            return EAutopilotStatus::Stuck;
        }
        const int policyIndex = (policy_.chestOnEvenDungeons && depth % 2 == 0) || policy_.eventOption == 1 ? 1 : 0;
        int choice = -1;
        const bool policyInRange = policyIndex >= 0 && static_cast<std::size_t>(policyIndex) < def->choices.size();
        if (policyInRange && EvaluateEventGate(registry, def->choices[static_cast<std::size_t>(policyIndex)]).open) {
            choice = policyIndex;
        } else {
            // The policy index is shut (or absent): the first open choice. The content contract guarantees
            // gates + 1 <= choices, so this always finds one — a miss is a data/wiring error, not a wait.
            for (std::size_t index = 0; index < def->choices.size(); ++index) {
                if (EvaluateEventGate(registry, def->choices[index]).open) {
                    choice = static_cast<int>(index);
                    break;
                }
            }
        }
        if (choice < 0) {
            LOG_ERROR("[autopilot] event '{}' (depth {} room {}) has no open choice", def->id, depth, room);
            return EAutopilotStatus::Stuck;
        }
        const bool fallback = choice != policyIndex;
        PostRunIntent(registry, ERunIntent::ChooseEventOption, choice);
        LOG_INFO("[autopilot] event depth={} room={} choice={}{}", depth, room, choice,
                 fallback ? " (gate fallback)" : "");
        act();
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

        // WP-00d: one consolidation pass per camp visit (equip=engine): retry pooled runes, then gather split
        // pairs; camp is the only place a move is allowed (free per D-17).
        if (policy_.equip == EAutopilotEquip::Engine) {
            (void)ConsolidateEnginesAtCamp(registry);
        }

        // WP-43: the camp is one choice. A party below the `rest=` threshold rests and buys no stat upgrade.
        const bool rested = AutopilotShouldRest(registry, policy_);
        if (rested) {
            if (const auto error = RestAtDungeonCamp(registry)) {
                LOG_WARN("[autopilot] camp rest failed: {}", *error);
            } else {
                LOG_INFO("[autopilot] camp rest materials={}", run->materials);
            }
        }

        // Reinforcements: member 0 only (the camp view's candidates[0] for the first expanded member, M-FUN
        // Step-Camp). Each of the U purchases takes the lowest upgrade index member 0 accepts; no legal
        // upgrade — or an empty party — stops the buying.
        if (const auto* growth = registry.ctx().find<FDungeonGrowth>();
            !rested && growth != nullptr && !run->party.empty()) {
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

        TrackArmedEngines(registry, run->depth);

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
        LogRunEndEngines(registry);
        LOG_INFO("[autopilot] run end depth={} room={} cleared={}", run != nullptr ? run->depth : std::get<0>(key),
                 run != nullptr ? run->room : std::get<1>(key), run != nullptr ? run->cleared : -1);
        return EAutopilotStatus::Finished;
    }

    // Battle, prepare, menus: nothing to drive (battles belong to GAME01P_AUTOPLAY).
    return EAutopilotStatus::Running;
}

void FRunAutopilot::TrackArmedEngines(const entt::registry& registry, int depth) {
    for (const FEngineArmStatus& status : ClassifyEngines(registry)) {
        if (status.state != EEngineArm::Armed) continue;
        const bool recorded = std::any_of(armedEngines_.begin(), armedEngines_.end(),
                                          [&status](const FArmedEngineRecord& record) {
                                              return record.engineId == status.engineId;
                                          });
        if (recorded) continue;
        armedEngines_.push_back(FArmedEngineRecord{status.engineId, depth});
        LOG_INFO("[autopilot] engine armed engine={} depth={} member={} slot={}", status.engineId, depth,
                 status.member, status.slot);
    }
}

void FRunAutopilot::LogRunEndEngines(const entt::registry& registry) {
    if (runEndEnginesLogged_) return;
    runEndEnginesLogged_ = true;
    const FDungeonRun* run = registry.ctx().find<FDungeonRun>();
    const int depth = run != nullptr ? run->depth : 0;
    std::string armed;
    std::string unarmed;
    for (const FEngineArmStatus& status : ClassifyEngines(registry)) {
        const auto recorded = std::find_if(armedEngines_.begin(), armedEngines_.end(),
                                           [&status](const FArmedEngineRecord& record) {
                                               return record.engineId == status.engineId;
                                           });
        if (recorded != armedEngines_.end()) {
            if (!armed.empty()) armed += ',';
            armed += status.engineId + "@" + std::to_string(recorded->depth);
        } else if (status.state == EEngineArm::Armed) {
            if (!armed.empty()) armed += ',';
            armed += status.engineId + "@" + std::to_string(depth);
        } else {
            if (!unarmed.empty()) unarmed += ',';
            unarmed += status.engineId + ":" + std::string(EngineArmName(status.state));
        }
    }
    LOG_INFO("[autopilot] run end engines armed=[{}] unarmed=[{}]", armed, unarmed);
}

} // namespace game::dev
