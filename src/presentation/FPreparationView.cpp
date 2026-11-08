#include "presentation/FPreparationView.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <imgui.h>
#include <nlohmann/json.hpp>

#include "core/AssetManager.h"
#include "core/EngineState.h"
#include "core/FAssetDigest.h"
#include "gameplay/data/FContentRegistry.h"
#include "gameplay/meta/FMetaProgress.h"
#include "presentation/FUiTheme.h"

namespace game {

const char* HubTabName(EHubTab tab) {
    switch (tab) {
    case EHubTab::Expedition: return "Expedition";
    case EHubTab::Unlocks: return "Unlocks";
    case EHubTab::Records: return "Records";
    }
    return "Expedition";
}

namespace {

// The panel at the title screen's scale rule (design 1160x780 at u = 1): it fits 1280x800 and grows with a
// 1920x1080 window. The bodies scroll instead of clipping at the largest text scale (1.25).
constexpr float kHubPanelWidth = 1160.f;
constexpr float kHubPanelHeight = 780.f;
// A hard lower bound for a scrollable body region (degenerate windows only); real heights are measured.
constexpr float kHubBodyMinHeight = 120.f;
// The footer buttons draw at these fixed heights; HubFooterHeight adds them back with the current text
// metrics, so every tab body knows exactly what the footer consumes at any scale.
constexpr float kHubStartButtonHeight = 44.f;
constexpr float kHubBackButtonHeight = 38.f;
constexpr const char* kStartDungeonLabel = "Start dungeon";

// --- shared text helpers -----------------------------------------------------------------------------

std::string JoinTags(const std::vector<std::string>& tags) {
    std::string joined;
    for (const std::string& tag : tags) {
        if (!joined.empty()) joined += ", ";
        joined += tag;
    }
    return joined;
}

std::string Capitalize(std::string text) {
    if (!text.empty() && text[0] >= 'a' && text[0] <= 'z') {
        text[0] = static_cast<char>(text[0] - 'a' + 'A');
    }
    return text;
}

/// The unit's player-facing name: displayName, then the capitalized class (`FUnitContent::className`).
/// Empty when there is no resolved unit — a raw content id (`char.*`) must never reach the player.
std::string UnitDisplayName(const gameplay::FContentRegistry* content, std::string_view unitId) {
    if (unitId.empty()) return {};
    const gameplay::FUnitContent* unit = content != nullptr ? content->units.find(unitId) : nullptr;
    if (unit == nullptr) return {};
    if (!unit->displayName.empty()) return unit->displayName;
    return Capitalize(unit->className);
}

/// One rule for a skill's label: display name + its tags as a short badge. A row missing from content
/// (a mixed test fixture) reads "Unknown skill" — the raw id must not reach the player either.
std::string SkillLabel(const gameplay::FContentRegistry* content, const std::string& skillId) {
    const gameplay::FSkillContent* skill = content != nullptr ? content->skills.find(skillId) : nullptr;
    std::string label =
        skill != nullptr && !skill->displayName.empty() ? skill->displayName : std::string{"Unknown skill"};
    if (skill != nullptr && !skill->tags.empty()) label += "  [" + JoinTags(skill->tags) + "]";
    return label;
}

const FRosterChoice* FindChoice(const std::vector<FRosterChoice>& choices, std::string_view memberId) {
    const auto found = std::find_if(choices.begin(), choices.end(),
        [memberId](const FRosterChoice& choice) { return choice.memberId == memberId; });
    return found == choices.end() ? nullptr : &*found;
}

std::string MemberDisplayName(const std::vector<FRosterChoice>& roster, const std::string& memberId) {
    if (const FRosterChoice* choice = FindChoice(roster, memberId);
        choice != nullptr && !choice->name.empty()) {
        return choice->name;
    }
    return "???";
}

bool InParty(const FPreparationDraft& draft, std::string_view memberId) {
    return std::find(draft.setup.party.begin(), draft.setup.party.end(), memberId) != draft.setup.party.end();
}

/// "+3 materials   ·   +1 rune offer" — only the parts the preview actually grants; empty when it grants none.
std::string DescribeStartSupport(const FStartSupportPreview& support) {
    std::string text;
    if (support.materials > 0) text = "+" + std::to_string(support.materials) + " materials";
    if (support.forcedRuneOffer) {
        if (!text.empty()) text += "   ·   ";
        text += "+1 rune offer";
    }
    return text;
}

/// The seal part of the common header line: "Seals N", "meta disabled" (a store exists but is read-only), or
/// nothing at all when meta was never initialized.
std::string MetaHeaderText(entt::registry& registry) {
    if (FindMetaSave(registry) == nullptr) return {};
    if (MetaDisabled(registry)) return "meta disabled";
    return "Seals " + std::to_string(MetaSeals(registry));
}

/// ⑦ The authored next-dungeon preview text (the camp view reads the same document key); empty when the
/// document carries none. The text is measured and drawn from this one read.
std::string NextDungeonHintText(entt::registry& registry) {
    const auto* assets = registry.ctx().find<FAssetManager>();
    if (assets == nullptr) return {};
    const std::shared_ptr<const FAssetDigest> document = assets->digest("data/dungeon");
    if (document == nullptr) return {};
    const nlohmann::json& data = document->json();
    const auto hint = data.find("next_dungeon_hint");
    if (hint == data.end() || !hint->is_string()) return {};
    return hint->get<std::string>();
}

void PostPreparationCommand(entt::registry& registry, EPreparationCommand command) {
    registry.ctx().insert_or_assign<FPreparationCommand>(FPreparationCommand{command});
}

// --- measured layout: the footer (common to every tab) and the start-dungeon row ---------------------

/// The footer's real height at the current scale: the two button frames, the blocked reason line wrapped
/// exactly the way TextWrapped draws it, and the item spacing between the parts. Nothing is reserved — a
/// tab body subtracts this and takes the rest.
float HubFooterHeight(const FPreparationScreen& screen) {
    const ImGuiStyle& style = ImGui::GetStyle();
    float height = kHubStartButtonHeight + style.ItemSpacing.y + kHubBackButtonHeight;
    if (!screen.startBlockedReason.empty()) {
        const float wrapWidth = ImGui::GetContentRegionAvail().x;
        height += style.ItemSpacing.y +
                  ImGui::CalcTextSize(screen.startBlockedReason.c_str(), nullptr, false, wrapWidth).y;
    }
    return height;
}

/// ⑧ The footer is common to every tab (a player who opened Unlocks or Records can still start in one
/// click). It pins itself to the panel's bottom edge, so its place never depends on the tab body's length.
void DrawHubFooter(entt::registry& registry, FPreparationScreen& screen) {
    const float top = ImGui::GetWindowHeight() - ImGui::GetStyle().WindowPadding.y - HubFooterHeight(screen);
    if (top > ImGui::GetCursorPosY()) ImGui::SetCursorPosY(top);

    const bool blocked = !screen.startBlockedReason.empty();
    ImGui::BeginDisabled(blocked);
    if (ui::Button("Start expedition", {-1.f, kHubStartButtonHeight})) {
        PostPreparationCommand(registry, EPreparationCommand::Start);
    }
    ImGui::EndDisabled();
    if (blocked) {
        ImGui::PushStyleColor(ImGuiCol_Text, ui::palette::kMutedV);
        ImGui::TextWrapped("%s", screen.startBlockedReason.c_str());
        ImGui::PopStyleColor();
    }
    if (ui::Button("Back to title", {-1.f, kHubBackButtonHeight})) {
        PostPreparationCommand(registry, EPreparationCommand::Cancel);
    }
}

/// Width of one start-dungeon radio (frame + inner spacing + label) — the expression ImGui lays it out
/// with, shared by the wrap decisions and the row's measured height.
float StartRadioWidth(int index) {
    const std::string label = "Dungeon " + std::to_string(index + 1);
    return ImGui::CalcTextSize(label.c_str()).x + ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x;
}

/// The line index of every start-dungeon radio at `width` ("Start dungeon" opens line 0). The drawing
/// wraps by this same vector, so the row's measured height is exact at any scale or width.
std::vector<int> StartDungeonRowLines(const FPreparationScreen& screen, float width) {
    const ImGuiStyle& style = ImGui::GetStyle();
    const int options = std::max(screen.draft.maxStartCleared, 0) + 1;
    std::vector<int> lines;
    lines.reserve(static_cast<std::size_t>(options));
    lines.push_back(0);
    float x = ImGui::CalcTextSize(kStartDungeonLabel).x + style.ItemSpacing.x + StartRadioWidth(0);
    int line = 0;
    for (int index = 1; index < options; ++index) {
        const float cell = StartRadioWidth(index);
        if (x + style.ItemSpacing.x + cell <= width) {
            x += style.ItemSpacing.x + cell;
        } else {
            ++line;
            x = cell;
        }
        lines.push_back(line);
    }
    return lines;
}

/// ④ The start-dungeon row: "Dungeon N" for every startable index (0..maxStartCleared), wrapping exactly
/// where StartDungeonRowLines says so its measured height matches what is drawn.
void DrawStartDungeonRow(FPreparationScreen& screen, const std::vector<int>& lines) {
    ImGui::TextDisabled("%s", kStartDungeonLabel);
    ImGui::SameLine();
    for (int index = 0; index < static_cast<int>(lines.size()); ++index) {
        if (index > 0 && lines[index] == lines[index - 1]) ImGui::SameLine();
        const std::string label = "Dungeon " + std::to_string(index + 1);
        if (ImGui::RadioButton(label.c_str(), screen.draft.startCleared == index)) {
            SetDraftStartCleared(screen.draft, index);
        }
    }
}

// --- Expedition tab ----------------------------------------------------------------------------------

/// ① The roster: one row per member — "Name · Trait — Unit display name" (the whole row is the click
/// target). A locked row is greyed, keeps its reason under it and prints no raw id. The click calls
/// ToggleDraftMember.
void DrawRosterColumn(entt::registry& registry, FPreparationScreen& screen, const std::vector<FRosterChoice>& roster) {
    ui::SectionTitle("ROSTER");
    if (roster.empty()) {
        ImGui::TextDisabled("The roster is empty.");
        return;
    }
    const auto* content = registry.ctx().find<gameplay::FContentRegistry>();
    for (const FRosterChoice& choice : roster) {
        ImGui::PushID(choice.memberId.c_str());
        std::string line = choice.name.empty() ? std::string{"???"} : choice.name;
        if (!choice.trait.empty()) line += "  ·  " + choice.trait;
        const std::string unit = UnitDisplayName(content, choice.unitId);
        if (!unit.empty()) line += "  —  " + unit;
        const bool selectable = choice.availability == ERosterAvailability::Selectable;
        ImGui::BeginDisabled(!selectable);
        if (ImGui::Selectable(line.c_str(), InParty(screen.draft, choice.memberId), 0,
                              {ImGui::GetContentRegionAvail().x, 0.f})) {
            ToggleDraftMember(registry, screen.draft, choice.memberId);
        }
        if (!selectable && !choice.reason.empty()) {
            ImGui::TextWrapped("    %s", choice.reason.c_str());
        }
        ImGui::EndDisabled();
        ImGui::PopID();
    }
}

/// ③ One member's loadout: MemberActiveSkillSlots combos over MemberSkillPool, two per row. Each combo
/// takes half the column and its preview keeps "Name [tags]" clipped to the frame (no overflow); every
/// pick goes through SetDraftSkill (the rules decide swaps and legality). A promoted 5th slot starts a
/// third grid row.
void DrawMemberLoadout(entt::registry& registry, FPreparationScreen& screen, std::size_t memberIndex,
                       const std::string& memberId) {
    const auto* content = registry.ctx().find<gameplay::FContentRegistry>();
    const std::vector<std::string> pool = MemberSkillPool(registry, memberId);
    if (pool.empty()) {
        ImGui::TextDisabled("No skills are available for this member.");
        return;
    }
    const int slotCount = MemberActiveSkillSlots(registry, memberId);
    const std::vector<std::string>* chosen =
        memberIndex < screen.draft.setup.skills.size() ? &screen.draft.setup.skills[memberIndex] : nullptr;
    const float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * .5f;
    for (int slot = 0; slot < slotCount; ++slot) {
        if (slot % 2 != 0) ImGui::SameLine();
        const std::size_t slotIndex = static_cast<std::size_t>(slot);
        const std::string current =
            chosen != nullptr && slotIndex < chosen->size() ? (*chosen)[slotIndex] : std::string{};
        const std::string preview = current.empty() ? "Choose a skill" : SkillLabel(content, current);
        ImGui::PushID(slot);
        ImGui::SetNextItemWidth(half);
        if (ImGui::BeginCombo("##skill", preview.c_str())) {
            for (const std::string& skillId : pool) {
                const bool selected = skillId == current;
                if (ImGui::Selectable(SkillLabel(content, skillId).c_str(), selected)) {
                    SetDraftSkill(registry, screen.draft, memberIndex, slotIndex, skillId);
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::PopID();
    }
}

/// ② The party and ③ the loadouts share one block per member: the header line in placement order ("1.
/// Alden — Warrior", "Empty" for an unassigned slot) IS the party list, and the member's slots sit in a
/// 2-column grid right below it — four loadouts read whole at 1280x800.
void DrawPartyColumn(entt::registry& registry, FPreparationScreen& screen, const std::vector<FRosterChoice>& roster) {
    const auto* content = registry.ctx().find<gameplay::FContentRegistry>();
    for (int index = 0; index < static_cast<int>(kRunPartySize); ++index) {
        const std::size_t slotIndex = static_cast<std::size_t>(index);
        const std::string memberId =
            slotIndex < screen.draft.setup.party.size() ? screen.draft.setup.party[slotIndex] : std::string{};
        if (memberId.empty()) {
            ImGui::TextDisabled("%d.  Empty", index + 1);
            continue;
        }
        ImGui::PushID(memberId.c_str());
        std::string header = MemberDisplayName(roster, memberId);
        const FRosterChoice* choice = FindChoice(roster, memberId);
        const std::string unit = choice != nullptr ? UnitDisplayName(content, choice->unitId) : std::string{};
        if (!unit.empty()) header += " — " + unit;
        ImGui::TextColored(ui::palette::kGoldV, "%d.  %s", index + 1, header.c_str());
        DrawMemberLoadout(registry, screen, slotIndex, memberId);
        ImGui::PopID();
    }
}

void DrawExpeditionTab(entt::registry& registry, FPreparationScreen& screen) {
    const std::vector<FRosterChoice> roster = BuildRosterChoices(registry);
    const float contentWidth = ImGui::GetContentRegionAvail().x;

    // Measure what the tab draws below the columns (the footer is common to every tab) and give the
    // columns all the rest — no fixed band, so the four loadouts fit at 1280x800, nothing clips at larger
    // scales, and a wrapped reason line is accounted for.
    const float footerHeight = HubFooterHeight(screen);
    const std::vector<int> startLines = StartDungeonRowLines(screen, contentWidth);
    float startBlockHeight = static_cast<float>(startLines.back() + 1) * ImGui::GetFrameHeightWithSpacing();
    const std::string support = DescribeStartSupport(PreviewStartSupport(registry, screen.draft.startCleared));
    if (!support.empty()) startBlockHeight += ImGui::GetTextLineHeightWithSpacing();
    const std::string hint = NextDungeonHintText(registry);
    if (!hint.empty()) startBlockHeight += ImGui::GetTextLineHeightWithSpacing();

    const float columnsHeight =
        std::max(ImGui::GetContentRegionAvail().y - footerHeight - startBlockHeight, kHubBodyMinHeight);
    const float rosterWidth = std::clamp(contentWidth * .34f, 240.f, 460.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {10.f, 8.f});
    ImGui::BeginChild("Hub roster", {rosterWidth, columnsHeight}, ImGuiChildFlags_Borders);
    DrawRosterColumn(registry, screen, roster);
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("Hub party", {0.f, columnsHeight}, ImGuiChildFlags_Borders);
    DrawPartyColumn(registry, screen, roster);
    ImGui::EndChild();
    ImGui::PopStyleVar();

    DrawStartDungeonRow(screen, startLines);
    if (!support.empty()) ImGui::TextDisabled("Start support: %s", support.c_str());
    if (!hint.empty()) ImGui::TextDisabled("Next dungeon: %s", hint.c_str());

    DrawHubFooter(registry, screen);
}

// --- Unlocks tab (21b body is read-only; WP-21c adds buying in this one block) ------------------------

void DrawUnlocksTab(entt::registry& registry, FPreparationScreen& screen) {
    const float footerHeight = HubFooterHeight(screen);
    // One item spacing less than the free height: the footer pin then lands exactly on the panel's bottom
    // edge (the same place it takes in the Expedition tab).
    const float listHeight = std::max(ImGui::GetContentRegionAvail().y - footerHeight - ImGui::GetStyle().ItemSpacing.y,
                                      kHubBodyMinHeight);
    ImGui::BeginChild("Unlock list", {0.f, listHeight}, ImGuiChildFlags_Borders);
    const std::vector<FMetaUnlockRow> catalog = MetaUnlockCatalog(registry);
    if (catalog.empty()) {
        ImGui::TextDisabled("The unlock catalog is empty.");
    } else {
        for (const FMetaUnlockRow& row : catalog) {
            ImGui::PushID(row.id.c_str());
            ImGui::Text("%s", row.id.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("(%s  ·  cost %d  ·  seal gate %d)", row.kind.c_str(), row.cost, row.sealGate);
            ImGui::SameLine();
            if (HasMetaUnlock(registry, row.id)) {
                ImGui::TextColored(ui::palette::kGoldBrightV, "Unlocked");
            } else {
                ImGui::TextDisabled("Locked");
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();
    DrawHubFooter(registry, screen);
}

// --- Records tab (21b body is minimal; WP-31 extends it) ----------------------------------------------

void DrawRecordsTab(entt::registry& registry, FPreparationScreen& screen) {
    const float footerHeight = HubFooterHeight(screen);
    const float bodyHeight =
        std::max(ImGui::GetContentRegionAvail().y - footerHeight - ImGui::GetStyle().ItemSpacing.y,
                 kHubBodyMinHeight);
    ImGui::BeginChild("Record list", {0.f, bodyHeight}, ImGuiChildFlags_Borders);
    const FMetaSave* save = FindMetaSave(registry);
    if (save == nullptr) {
        ImGui::TextDisabled("No records yet");
    } else {
        const FMetaStats& stats = save->stats;
        ImGui::Text("Runs: %d", stats.runs);
        ImGui::Text("Dungeons cleared: %d", stats.clears);
        ImGui::Text("Best dungeon: %d", stats.bestDepth);
        ImGui::Text("Cycle best: %d", stats.cycleBest);
        ImGui::Text("Final bosses defeated: %d", stats.grand);
    }
    ImGui::EndChild();
    DrawHubFooter(registry, screen);
}

} // namespace

void DrawPreparationScreen(entt::registry& registry, FPreparationScreen& screen) {
    const auto* window = registry.ctx().find<FEngineWindowState>();
    if (window == nullptr || window->width <= 0 || window->height <= 0) return;

    // Centered, sized from the viewport with the title screen's scale rule (u = min(h/800, w/1160)): one
    // panel serves 1280x800 and 1920x1080, and its tab bodies scroll instead of clipping at 1.25.
    const float width = static_cast<float>(window->width);
    const float height = static_cast<float>(window->height);
    const float u = std::min(height / 800.f, width / 1160.f);
    const ImVec2 size{std::min(width - 48.f, kHubPanelWidth * u), std::min(height - 40.f, kHubPanelHeight * u)};
    const ImVec2 pos{std::floor((width - size.x) * .5f), std::floor((height - size.y) * .5f)};
    if (!ui::BeginPanel("Preparation", pos, size)) {
        ui::EndPanel();
        return;
    }

    ui::HeadingText("PREPARATION", 26.f);
    // The header line is common to every tab: the seal balance (or "meta disabled"; absent without meta)
    // and the start dungeon the draft currently would start at.
    std::string header = MetaHeaderText(registry);
    if (!header.empty()) header += "   ·   ";
    header += "Dungeon " + std::to_string(screen.draft.startCleared + 1);
    ImGui::TextColored(ui::palette::kMutedV, "%s", header.c_str());

    if (ImGui::BeginTabBar("Hub tabs")) {
        for (int index = 0; index < kHubTabCount; ++index) {
            const auto tab = static_cast<EHubTab>(index);
            if (!ImGui::BeginTabItem(HubTabName(tab))) continue;
            screen.tab = tab;
            switch (tab) {
            case EHubTab::Expedition: DrawExpeditionTab(registry, screen); break;
            case EHubTab::Unlocks: DrawUnlocksTab(registry, screen); break;
            case EHubTab::Records: DrawRecordsTab(registry, screen); break;
            }
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    ui::EndPanel();
}

} // namespace game
