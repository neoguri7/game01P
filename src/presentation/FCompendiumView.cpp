#include "presentation/FCompendiumView.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <imgui.h>

#include "core/EngineState.h"
#include "gameplay/data/FContentRegistry.h"
#include "presentation/FModalViewRegistry.h"
#include "presentation/FUiTheme.h"

namespace game {
namespace {

// The content loader owns the closed vocabularies; it keeps these strings file-local, so restating them
// here is the smallest possible coupling (a typo is caught by the "every unit/enemy has a role" test).
constexpr std::string_view kShapeZone = "zone";
constexpr std::string_view kTagHeal = "Heal";
constexpr std::string_view kTagSupport = "Support";

// The list pane is measured from the widest title; this is only the floor (a short tab stays usable) and the
// body height reserves the Close row, so the panes scroll instead of clipping at 1.25.
constexpr float kCompendiumListMinWidth = 140.f;
constexpr float kCompendiumBodyHeight = -56.f;

std::string Join(const std::vector<std::string>& values) {
    std::string joined;
    for (const std::string& value : values) {
        if (!joined.empty()) joined += ", ";
        joined += value;
    }
    return joined;
}

std::string StatusName(const gameplay::FContentRegistry& content, std::string_view id) {
    if (id.empty()) return "None";
    if (const auto* status = content.statuses.find(id)) return status->displayName;
    return std::string(id);
}

/// "Mending Light, Arcane Bolt" — the enemy tab lists the skills it can use.
std::string SkillNames(const gameplay::FContentRegistry& content, const std::vector<std::string>& ids) {
    std::string joined;
    for (const std::string& id : ids) {
        if (!joined.empty()) joined += ", ";
        if (const auto* skill = content.skills.find(id)) joined += skill->displayName;
        else joined += id;
    }
    return joined.empty() ? "None" : joined;
}

std::string DescribeRuneEffect(const gameplay::FContentRegistry& content, const gameplay::FRuneContent& rune) {
    std::string effect;
    const auto append = [&effect](std::string part) {
        if (!effect.empty()) effect += "; ";
        effect += std::move(part);
    };
    if (!rune.statusOnHit.empty()) append("applies " + StatusName(content, rune.statusOnHit) + " on hit");
    if (!rune.shapeOverride.empty()) append("changes the shape to " + rune.shapeOverride);
    if (rune.powerDelta != 0) append((rune.powerDelta > 0 ? "+" : "") + std::to_string(rune.powerDelta) + " power");
    if (rune.apDelta != 0) append((rune.apDelta > 0 ? "+" : "") + std::to_string(rune.apDelta) + " AP");
    if (rune.rangeDelta != 0) append((rune.rangeDelta > 0 ? "+" : "") + std::to_string(rune.rangeDelta) + " range");
    if (rune.areaRadiusDelta != 0) {
        append((rune.areaRadiusDelta > 0 ? "+" : "") + std::to_string(rune.areaRadiusDelta) + " area radius");
    }
    if (rune.statusDurationDelta != 0) append("+" + std::to_string(rune.statusDurationDelta) + " status duration");
    if (rune.statusMagnitudeDelta != 0) append("+" + std::to_string(rune.statusMagnitudeDelta) + " status magnitude");
    if (rune.lifeOnHit != 0) append("heals the attacker for " + std::to_string(rune.lifeOnHit) + " per hit");
    return effect.empty() ? "Tag rule only (no numeric change)." : effect;
}

std::string DescribeStatus(const gameplay::FStatusContent& status) {
    const std::string chance = std::to_string(status.magnitude) + "% chance to lose the next action";
    if (status.kind == gameplay::kStatusKindDot) {
        return "Damage over time: " + std::to_string(status.magnitude) + " damage per turn.";
    }
    if (status.kind == gameplay::kStatusKindControl) {
        if (status.mode == gameplay::kStatusModeOnApply) return "Control: " + chance + " (rolled once, on apply).";
        return "Control: " + chance + " (rolled at turn start).";
    }
    if (status.kind == gameplay::kStatusKindStat) {
        return std::string("Stat ") + (status.magnitude >= 0 ? "buff: +" : "debuff: ") +
            std::to_string(status.magnitude) + " to the affected stat.";
    }
    if (status.kind == gameplay::kStatusKindMoveLock) {
        return "Move lock: denies the next turn's movement, not its skills.";
    }
    return "Status effect.";
}

/// The tab's rows, built straight from the content tables (ordered as the tables are authored).
void BuildSkills(const gameplay::FContentRegistry& content, std::vector<FCompendiumEntry>& entries) {
    for (const gameplay::FSkillContent& skill : content.skills.rows) {
        FCompendiumEntry entry;
        entry.id = skill.id;
        entry.title = skill.displayName;
        entry.body = "Tags: " + (skill.tags.empty() ? std::string("none") : Join(skill.tags));
        entry.facts.emplace_back("AP cost", std::to_string(skill.apCost));
        entry.facts.emplace_back("Range", std::to_string(skill.range));
        entry.facts.emplace_back("Power", std::to_string(skill.power));
        entry.facts.emplace_back("Shape", skill.shape);
        entry.facts.emplace_back("Target", skill.target);
        entry.facts.emplace_back("Rune sockets", std::to_string(skill.runeSockets));
        if (!skill.damageType.empty()) entry.facts.emplace_back("Damage type", skill.damageType);
        if (!skill.appliesStatus.empty()) entry.facts.emplace_back("Applies", StatusName(content, skill.appliesStatus));
        entries.push_back(std::move(entry));
    }
}

void BuildRunes(const gameplay::FContentRegistry& content, std::vector<FCompendiumEntry>& entries) {
    for (const gameplay::FRuneContent& rune : content.runes.rows) {
        FCompendiumEntry entry;
        entry.id = rune.id;
        entry.title = rune.displayName;
        entry.body = DescribeRuneEffect(content, rune);
        entry.facts.emplace_back("Adds tags", rune.addsTags.empty() ? "none" : Join(rune.addsTags));
        if (!rune.requiresAllTags.empty()) entry.facts.emplace_back("Requires all", Join(rune.requiresAllTags));
        if (!rune.requiresAnyTags.empty()) entry.facts.emplace_back("Requires any", Join(rune.requiresAnyTags));
        if (!rune.excludesTags.empty()) entry.facts.emplace_back("Excludes", Join(rune.excludesTags));
        if (!rune.statusId.empty()) entry.facts.emplace_back("Status affected", StatusName(content, rune.statusId));
        entries.push_back(std::move(entry));
    }
}

void BuildStatuses(const gameplay::FContentRegistry& content, std::vector<FCompendiumEntry>& entries) {
    for (const gameplay::FStatusContent& status : content.statuses.rows) {
        FCompendiumEntry entry;
        entry.id = status.id;
        entry.title = status.displayName;
        entry.body = DescribeStatus(status);
        entry.facts.emplace_back("Kind", status.kind);
        entry.facts.emplace_back("Magnitude", std::to_string(status.magnitude));
        entry.facts.emplace_back("Duration", std::to_string(status.durationTurns) + " turns");
        if (status.speedPct != gameplay::kStatusSpeedPctNeutral) {
            entry.facts.emplace_back("Speed", std::to_string(status.speedPct) + "%");
        }
        if (status.kind == gameplay::kStatusKindControl) {
            entry.facts.emplace_back("Mode", status.mode);
            entry.facts.emplace_back("Chance cap", std::to_string(status.chanceCap) + "%");
        }
        entries.push_back(std::move(entry));
    }
}

void BuildEnemies(const gameplay::FContentRegistry& content, std::vector<FCompendiumEntry>& entries) {
    for (const gameplay::FUnitContent& unit : content.units.rows) {
        if (!IsEnemyUnit(unit)) continue;
        FCompendiumEntry entry;
        entry.id = unit.id;
        entry.title = unit.displayName;
        entry.body = "Skills: " + SkillNames(content, unit.skillIds);
        entry.facts.emplace_back("Role", DeriveUnitRole(content, unit));
        entry.facts.emplace_back("Health", std::to_string(static_cast<int>(unit.maxHealth)));
        entry.facts.emplace_back("Speed", std::to_string(static_cast<int>(unit.speed)));
        entry.facts.emplace_back("Attack", std::to_string(unit.attack));
        entry.facts.emplace_back("Magic attack", std::to_string(unit.magicAttack));
        entry.facts.emplace_back("Defense", std::to_string(unit.defense));
        entry.facts.emplace_back("Magic defense", std::to_string(unit.magicDefense));
        entry.facts.emplace_back("Move AP", std::to_string(unit.moveAp));
        entry.facts.emplace_back("Skill AP", std::to_string(unit.skillAp));
        entries.push_back(std::move(entry));
    }
}

/// The view's own selection state; context-owned so it survives frames without a static. The active tab lives
/// in the ImGui tab bar itself, so no second copy can drift from what the player sees.
struct FCompendiumUiState {
    std::string selectedId;
};

/// Rows in a tab without building them (the tab label carries the count). Kept next to the builders so a new
/// tab can never show a stale number.
int CompendiumTabCount(const gameplay::FContentRegistry& content, ECompendiumTab tab) {
    switch (tab) {
    case ECompendiumTab::Skills: return static_cast<int>(content.skills.rows.size());
    case ECompendiumTab::Runes: return static_cast<int>(content.runes.rows.size());
    case ECompendiumTab::Statuses: return static_cast<int>(content.statuses.rows.size());
    case ECompendiumTab::Enemies: {
        int enemies = 0;
        for (const gameplay::FUnitContent& unit : content.units.rows) enemies += IsEnemyUnit(unit) ? 1 : 0;
        return enemies;
    }
    }
    return 0;
}

/// The entry list and detail pane. Widths come from the measured text (the fixed 280/150 columns clipped at
/// 1.25): the list fits its widest title and the fact labels share one measured column.
void DrawCompendiumTabBody(const std::vector<FCompendiumEntry>& entries, FCompendiumUiState& state) {
    const auto selected = std::find_if(entries.begin(), entries.end(),
        [&state](const FCompendiumEntry& entry) { return entry.id == state.selectedId; });
    if (selected == entries.end() && !entries.empty()) state.selectedId = entries.front().id;

    const ImGuiStyle& style = ImGui::GetStyle();
    float widestTitle = 0.f;
    for (const FCompendiumEntry& entry : entries) {
        widestTitle = std::max(widestTitle, ImGui::CalcTextSize(entry.title.c_str()).x);
    }
    const float available = ImGui::GetContentRegionAvail().x;
    const float listWidth = std::clamp(widestTitle + style.ItemSpacing.x * 3.f,
                                       kCompendiumListMinWidth, available * .4f);
    ImGui::BeginChild("Compendium list", {listWidth, kCompendiumBodyHeight}, ImGuiChildFlags_Borders);
    for (const FCompendiumEntry& entry : entries) {
        ImGui::PushID(entry.id.c_str());
        if (ImGui::Selectable(entry.title.c_str(), entry.id == state.selectedId)) state.selectedId = entry.id;
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("Compendium detail", {0.f, kCompendiumBodyHeight}, ImGuiChildFlags_Borders);
    const auto shown = std::find_if(entries.begin(), entries.end(),
        [&state](const FCompendiumEntry& entry) { return entry.id == state.selectedId; });
    if (shown == entries.end()) {
        ImGui::TextDisabled("Nothing in this tab.");
        ImGui::EndChild();
        return;
    }
    ui::HeadingText(shown->title.c_str(), 22.f);
    if (!shown->body.empty()) ImGui::TextWrapped("%s", shown->body.c_str());
    if (!shown->facts.empty()) {
        ImGui::Separator();
        // The label column is measured from the widest label (a fixed 150px clipped at 1.25). A table keeps
        // the offset independent of child-window padding, which SameLine() measures from the window edge.
        float labelWidth = 0.f;
        for (const auto& [label, value] : shown->facts) {
            (void)value;
            labelWidth = std::max(labelWidth, ImGui::CalcTextSize(label.c_str()).x);
        }
        if (ImGui::BeginTable("Compendium facts", 2, ImGuiTableFlags_SizingFixedFit)) {
            ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthFixed, labelWidth + style.ItemSpacing.x * 2.f);
            ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
            for (const auto& [label, value] : shown->facts) {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", label.c_str());
                ImGui::TableNextColumn();
                ImGui::TextWrapped("%s", value.c_str());
            }
            ImGui::EndTable();
        }
    }
    ImGui::EndChild();
}

} // namespace

const char* CompendiumTabName(ECompendiumTab tab) {
    switch (tab) {
    case ECompendiumTab::Skills: return "Skills";
    case ECompendiumTab::Runes: return "Runes";
    case ECompendiumTab::Statuses: return "Statuses";
    case ECompendiumTab::Enemies: return "Enemies";
    }
    return "Skills";
}

bool IsEnemyUnit(const gameplay::FUnitContent& unit) { return unit.className.empty(); }

std::string DeriveUnitRole(const gameplay::FContentRegistry& content, const gameplay::FUnitContent& unit) {
    bool support = false;
    bool zone = false;
    int maxRange = 0;
    for (const std::string& id : unit.skillIds) {
        const auto* skill = content.skills.find(id);
        if (skill == nullptr) continue;
        maxRange = std::max(maxRange, skill->range);
        for (const std::string& tag : skill->tags) {
            if (tag == kTagHeal || tag == kTagSupport) support = true;
        }
        if (skill->shape == kShapeZone) zone = true;
    }
    if (support) return "Support";
    if (zone) return "Zone";
    if (maxRange >= 2) return "Ranged";
    return "Melee";
}

std::vector<FCompendiumEntry> BuildCompendiumTab(entt::registry& registry, ECompendiumTab tab) {
    std::vector<FCompendiumEntry> entries;
    const auto* content = registry.ctx().find<gameplay::FContentRegistry>();
    if (content == nullptr) return entries;
    switch (tab) {
    case ECompendiumTab::Skills: BuildSkills(*content, entries); break;
    case ECompendiumTab::Runes: BuildRunes(*content, entries); break;
    case ECompendiumTab::Statuses: BuildStatuses(*content, entries); break;
    case ECompendiumTab::Enemies: BuildEnemies(*content, entries); break;
    }
    return entries;
}

void RenderCompendiumView(entt::registry& registry) {
    const auto* window = registry.ctx().find<FEngineWindowState>();
    const auto* content = registry.ctx().find<gameplay::FContentRegistry>();
    if (window == nullptr || content == nullptr) return;
    auto* state = registry.ctx().find<FCompendiumUiState>();
    if (state == nullptr) state = &registry.ctx().emplace<FCompendiumUiState>();

    const float width = std::min(960.f, static_cast<float>(window->width) - 60.f);
    const float height = std::min(660.f, static_cast<float>(window->height) - 40.f);
    ImGui::SetNextWindowPos({window->width * .5f, window->height * .5f}, ImGuiCond_Always, {.5f, .5f});
    ImGui::SetNextWindowSize({width, height}, ImGuiCond_Always);
    ImGui::OpenPopup("Compendium");
    if (!ImGui::BeginPopupModal("Compendium", nullptr, ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize)) {
        return;
    }
    ui::DecorateCurrentWindow();
    ui::HeadingText("COMPENDIUM", 26.f);
    if (ImGui::BeginTabBar("Compendium tabs")) {
        for (int index = 0; index < kCompendiumTabCount; ++index) {
            const auto tab = static_cast<ECompendiumTab>(index);
            // The count tells the player the tab is complete and the tab bar itself shows which tab is open
            // (raised + underlined), so the active tab never depends on a colour.
            char label[64];
            std::snprintf(label, sizeof label, "%s (%d)", CompendiumTabName(tab), CompendiumTabCount(*content, tab));
            if (!ImGui::BeginTabItem(label)) continue;
            DrawCompendiumTabBody(BuildCompendiumTab(registry, tab), *state);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::Separator();
    if (ui::Button("Close", {-1.f, 40.f})) {
        RequestModalViewClose(registry);
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

} // namespace game
