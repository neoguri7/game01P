#include "gameplay/dungeon/FRunewordInfo.h"

#include <algorithm>

#include "gameplay/combat/FRuneword.h"
#include "gameplay/dungeon/FRewards.h"

namespace game {
namespace {

[[nodiscard]] std::string RuneName(const gameplay::FContentRegistry& content, const std::string& runeId) {
    const auto* rune = content.runes.find(runeId);
    return rune != nullptr ? rune->displayName : runeId;
}

[[nodiscard]] bool Contains(const std::vector<std::string>& ids, const std::string& id) {
    return std::ranges::find(ids, id) != ids.end();
}

} // namespace

std::vector<FRunewordEntry> KnownRunewords(entt::registry& registry) {
    std::vector<FRunewordEntry> result;
    const auto* content = registry.ctx().find<gameplay::FContentRegistry>();
    if (content == nullptr) return result;
    for (const gameplay::FEngineContent& engine : content->engines.rows) {
        if (engine.effectKind.empty()) continue;
        const bool ownsOne = std::ranges::any_of(
            engine.requiresRunes, [&](const std::string& id) { return IsRuneOwned(registry, id); });
        if (!ownsOne) continue;
        FRunewordEntry entry{engine.id, engine.name, engine.text, {}};
        for (const std::string& id : engine.requiresRunes) entry.runeNames.push_back(RuneName(*content, id));
        result.push_back(std::move(entry));
    }
    return result;
}

std::string RunewordRecipeLine(const FRunewordEntry& entry) {
    std::string runes;
    for (const std::string& name : entry.runeNames) {
        if (!runes.empty()) runes += " + ";
        runes += name;
    }
    return entry.name + ": " + runes + ": " + entry.text;
}

std::string RunewordCardLine(entt::registry& registry, std::string_view runeId) {
    const auto* content = registry.ctx().find<gameplay::FContentRegistry>();
    if (content == nullptr) return {};
    bool ingredient = false;
    for (const gameplay::FEngineContent& engine : content->engines.rows) {
        if (engine.effectKind.empty() || !Contains(engine.requiresRunes, std::string{runeId})) continue;
        ingredient = true;
        const bool completes = std::ranges::all_of(engine.requiresRunes, [&](const std::string& id) {
            return id == runeId || IsRuneOwned(registry, id);
        });
        if (completes) return "Runeword: completes " + engine.name + ": " + engine.text;
    }
    return ingredient ? "Runeword ingredient" : std::string{};
}

std::string RunewordSlotLine(const gameplay::FContentRegistry& content, const FSkillSlot& slot) {
    if (const gameplay::FEngineContent* active = ActiveRuneword(content, slot)) {
        return "Runeword: " + active->name + ": " + active->text;
    }
    for (const gameplay::FEngineContent& engine : content.engines.rows) {
        if (engine.effectKind.empty()) continue;
        std::string missing;
        int held = 0;
        for (const std::string& id : engine.requiresRunes) {
            if (Contains(slot.runeIds, id)) {
                ++held;
            } else {
                if (!missing.empty()) missing += ", ";
                missing += RuneName(content, id);
            }
        }
        if (held == 0) continue;
        return "Runeword " + std::to_string(held) + "/" + std::to_string(engine.requiresRunes.size()) + ": " +
               engine.name + " needs " + missing;
    }
    return {};
}

} // namespace game
