#include "gameplay/dungeon/FRewardSection.h"

#include <cstdint>
#include <limits>
#include <string>
#include <utility>

#include <nlohmann/json.hpp>

#include "core/Logger.h"
#include "gameplay/data/FContentRegistry.h"
#include "gameplay/dungeon/FRewards.h"
#include "gameplay/dungeon/FSaveSections.h"

namespace game {
namespace {

constexpr int kRewardSectionVersion = 1;

nlohmann::json PackOffers(entt::registry& registry) {
    auto* rewards = registry.ctx().find<FRewards>();
    if (rewards == nullptr) return nlohmann::json::object();  // no module state: the section's default
    return nlohmann::json{
        {"run_seed", rewards->runSeed},
        {"pity", rewards->pity},
        {"next_draw", rewards->nextDrawIndex},
        {"offer",
         nlohmann::json{{"active", rewards->offer.active}, {"candidates", rewards->offer.candidates}}},
        {"boss_extra_pending", rewards->bossExtraPending},
    };
}

/// Reads one non-negative integer, or reports the value as unusable. Never throws at the caller.
[[nodiscard]] bool ReadCount(const nlohmann::json& section, const char* key, int& destination) {
    const auto found = section.find(key);
    if (found == section.end()) return true;  // absent = the default
    if (!found->is_number_integer()) return false;
    const auto value = found->get<std::int64_t>();
    if (value < 0 || value > std::numeric_limits<int>::max()) return false;
    destination = static_cast<int>(value);
    return true;
}

/// The unpack is deliberately forgiving: a corrupt or unknown offer only clears the offer. A throwing unpack
/// would fail the whole load (FSaveSections treats a section failure as an unusable save), and losing the run
/// is never the right answer to a bad rune id. `run_seed` is compared with the installed run later
/// (EnsureRewardsForRun): on the continue path the section is unpacked before the run exists.
void UnpackOffers(entt::registry& registry, const nlohmann::json& section) {
    FRewards fresh;  // the build's tables survive; only the run-scoped state is replaced
    if (const auto* current = registry.ctx().find<FRewards>()) fresh.tables = current->tables;

    bool corrupt = false;
    const auto* content = registry.ctx().find<gameplay::FContentRegistry>();
    if (section.is_object() && !section.empty()) {
        if (const auto found = section.find("run_seed"); found != section.end()) {
            if (found->is_number_integer()) {
                const auto value = found->get<std::int64_t>();
                if (value < 0 || value > std::numeric_limits<std::uint32_t>::max()) corrupt = true;
                else fresh.runSeed = static_cast<std::uint32_t>(value);
            } else {
                corrupt = true;
            }
        }
        if (!ReadCount(section, "pity", fresh.pity)) corrupt = true;
        if (const auto found = section.find("next_draw"); found != section.end()) {
            if (found->is_number_integer()) {
                const auto value = found->get<std::int64_t>();
                if (value < 0 || value > std::numeric_limits<std::uint32_t>::max()) corrupt = true;
                else fresh.nextDrawIndex = static_cast<std::uint32_t>(value);
            } else {
                corrupt = true;
            }
        }
        if (const auto found = section.find("boss_extra_pending"); found != section.end()) {
            if (found->is_boolean()) fresh.bossExtraPending = found->get<bool>();
            else corrupt = true;
        }
        if (const auto offer = section.find("offer"); offer != section.end()) {
            if (!offer->is_object()) {
                corrupt = true;
            } else {
                if (const auto active = offer->find("active"); active != offer->end()) {
                    if (active->is_boolean()) fresh.offer.active = active->get<bool>();
                    else corrupt = true;
                }
                if (const auto candidates = offer->find("candidates"); candidates != offer->end()) {
                    if (!candidates->is_array()) {
                        corrupt = true;
                    } else {
                        for (const auto& candidate : *candidates) {
                            if (!candidate.is_string()) {
                                corrupt = true;
                                break;
                            }
                            const std::string id = candidate.get<std::string>();
                            if (content == nullptr || content->runes.find(id) == nullptr) {
                                corrupt = true;  // a rune this build no longer ships cannot become a card
                                break;
                            }
                            fresh.offer.candidates.push_back(id);
                        }
                    }
                }
            }
        }
    }

    if (corrupt) {
        LOG_WARN("The saved rune offer is unusable; it is dropped (pity and draw cursor are kept).");
        fresh.offer = FRuneOffer{};
        fresh.bossExtraPending = false;
    }
    if (fresh.offer.candidates.empty()) fresh.offer.active = false;

    registry.ctx().erase<FRewards>();
    registry.ctx().emplace<FRewards>(std::move(fresh));
}

} // namespace

void RegisterRewardSection(entt::registry& registry) {
    FSaveSectionSerializer serializer;
    serializer.id = kRewardOfferSectionId;
    serializer.version = kRewardSectionVersion;
    serializer.pack = PackOffers;
    serializer.unpack = UnpackOffers;
    RegisterSaveSection(registry, std::move(serializer));
}

} // namespace game
