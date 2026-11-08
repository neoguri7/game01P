#include "presentation/FPreparationView.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <imgui.h>
#include <nlohmann/json.hpp>

#include "core/AssetManager.h"
#include "core/EngineState.h"
#include "core/FAssetDigest.h"
#include "gameplay/combat/FSkillResolver.h"
#include "gameplay/data/FContentRegistry.h"
#include "gameplay/meta/FMetaProgress.h"
#include "presentation/FDungeonViews.h"
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
/// click). Its position is pinned in ONE place (WP-09h F7): the panel's content bottom edge minus the
/// footer's measured height. The tab bodies are measured to end exactly on this line, so the Start/Back
/// buttons land on the same pixels in every tab — the old "push down when there is room" let
/// Expedition's footer drift 7px lower (each tab body left a different trailing ItemSpacing.y behind).
void DrawHubFooter(entt::registry& registry, FPreparationScreen& screen) {
    ImGui::SetCursorPosY(ImGui::GetWindowContentRegionMax().y - HubFooterHeight(screen));

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

    // The body-height rule every tab shares (WP-09h F7): the columns get everything above the pinned
    // footer minus what this tab still draws below them (the start row, support, hint) — no fixed band,
    // so the four loadouts fit at 1280x800, nothing clips at larger scales, and a wrapped reason line is
    // accounted for.
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

// --- Unlocks tab ([WP-21c] body: the purchase table; every rule stays in FMetaProgress) ----------------

const char* UnlockKindLabel(std::string_view kind) {
    if (kind == "skill") return "Skill";
    if (kind == "rune") return "Rune";
    if (kind == "roster") return "Recruit";
    return "Unlock";
}

/// Defensive status text for an offer whose reason is empty (the evaluator authors every reason).
const char* UnlockStatusFallback(EUnlockStatus status) {
    switch (status) {
    case EUnlockStatus::Purchasable: return "Ready";
    case EUnlockStatus::Owned: return "Unlocked";
    case EUnlockStatus::InsufficientSeals: return "Not enough seals";
    case EUnlockStatus::BelowSealGate: return "Seal gate not reached";
    case EUnlockStatus::Unavailable: return "Unavailable";
    case EUnlockStatus::MetaDisabled: return "meta disabled";
    }
    return "Unavailable";
}

/// The player-facing name of an offer's row: skill/rune display name, roster row name ("???" for a stub
/// or an unknown row). No raw id reaches the player here or in the tooltip (WP-09h F6): the tooltip body
/// is authored text from UnlockOfferDetail, and only a debug build appends the id as a dim last line.
std::string UnlockDisplayName(entt::registry& registry, const gameplay::FContentRegistry* content,
                              const FMetaUnlockRow& row) {
    if (row.kind == "skill") {
        const gameplay::FSkillContent* skill = content != nullptr ? content->skills.find(row.id) : nullptr;
        if (skill != nullptr && !skill->displayName.empty()) return skill->displayName;
        return "Unknown skill";
    }
    if (row.kind == "rune") {
        const gameplay::FRuneContent* rune = content != nullptr ? content->runes.find(row.id) : nullptr;
        if (rune != nullptr && !rune->displayName.empty()) return rune->displayName;
        return "Unknown rune";
    }
    if (row.kind == "roster") {
        for (const FMetaRosterRow& member : MetaRosterCatalog(registry)) {
            if (member.id == row.id) return member.name.empty() ? std::string{"???"} : member.name;
        }
        return "???";
    }
    return "Unknown unlock";
}

/// "Alden, Rowan" — every live roster member whose unit's AUTHORED pool (units.json skills) carries the
/// skill. The authored pool, not MemberSkillPool: the gated pool hides the very skill being offered.
std::string UnlockSkillOwners(const gameplay::FContentRegistry* content, const std::string& skillId,
                              const std::vector<FRosterChoice>& roster) {
    std::string owners;
    for (const FRosterChoice& choice : roster) {
        const gameplay::FUnitContent* unit =
            content != nullptr && !choice.unitId.empty() ? content->units.find(choice.unitId) : nullptr;
        if (unit == nullptr) continue;
        if (std::find(unit->skillIds.begin(), unit->skillIds.end(), skillId) == unit->skillIds.end()) continue;
        if (!owners.empty()) owners += ", ";
        owners += choice.name.empty() ? std::string{"???"} : choice.name;
    }
    return owners;
}

/// [WP-09h F6] One skill's effect line for the tooltip. There is no authored description field, so the
/// line is the authored numbers the battle preview reads — the vocabulary RuneEffectSummary uses for
/// runes. The status shows its display name; a status row missing from content is skipped, never printed
/// raw.
std::string SkillEffectText(const gameplay::FContentRegistry* content, const gameplay::FSkillContent& skill) {
    std::string text;
    const auto append = [&text](const std::string& part) {
        if (!text.empty()) text += " · ";
        text += part;
    };
    if (skill.power != 0) append("Power " + std::to_string(skill.power));
    if (skill.range != 0) append("Range " + std::to_string(skill.range));
    if (skill.shape != "single") append("Shape: " + skill.shape);
    if (!skill.appliesStatus.empty()) {
        const gameplay::FStatusContent* status =
            content != nullptr ? content->statuses.find(skill.appliesStatus) : nullptr;
        if (status != nullptr && !status->displayName.empty()) append("Applies " + status->displayName);
    }
    return text;
}

/// [WP-09h F6] "Rowan, Mira" — every non-locked roster member whose AUTHORED pool (units.json `skills`)
/// carries at least one skill this rune legally attaches to. ValidateSkillSlot is the one rule call
/// (socket count + the §3.2 tag rules) — this only asks it, it never re-judges. The authored pool, not
/// MemberSkillPool: the gate hides unowned content, and this column names owners, not current draft
/// legality. A zero-socket skill falls out naturally (ValidateSkillSlot rejects it).
std::string RuneFittingMembers(entt::registry& registry, const gameplay::FContentRegistry* content,
                               const std::string& runeId) {
    std::string owners;
    if (content == nullptr) return owners;
    for (const FRosterChoice& choice : BuildRosterChoices(registry)) {
        if (choice.availability == ERosterAvailability::LockedStub || choice.unitId.empty()) continue;
        const gameplay::FUnitContent* unit = content->units.find(choice.unitId);
        if (unit == nullptr) continue;
        bool fits = false;
        for (const std::string& skillId : unit->skillIds) {
            if (!ValidateSkillSlot(FSkillSlot{skillId, {runeId}}, *content).has_value()) {
                fits = true;
                break;
            }
        }
        if (!fits) continue;
        if (!owners.empty()) owners += ", ";
        owners += choice.name.empty() ? std::string{"???"} : choice.name;
    }
    return owners;
}

/// [WP-09h F6] Raw content ids are not player-facing text: a debug build appends the offer's id to the
/// tooltip as a dim last line, a release build never shows it at all.
#ifndef NDEBUG
constexpr bool kUnlockTooltipShowsRawId = true;
#else
constexpr bool kUnlockTooltipShowsRawId = false;
#endif

void DrawUnlocksTab(entt::registry& registry, FPreparationScreen& screen) {
    const float footerHeight = HubFooterHeight(screen);
    // The body-height rule every tab shares (WP-09h F7): the child takes all the space above the pinned
    // footer. Nothing is drawn after it, so its height is exactly the free height minus the footer.
    const float listHeight = std::max(ImGui::GetContentRegionAvail().y - footerHeight, kHubBodyMinHeight);
    ImGui::BeginChild("Unlock list", {0.f, listHeight}, ImGuiChildFlags_Borders);
    // [WP-21c] The offer table. BuildUnlockOffers evaluates status/reason live and PurchaseMetaUnlock is the
    // camp's own pattern: the view decides nothing (no cached offers, no local legality).
    const FMetaSave* save = FindMetaSave(registry);
    const bool canBuy = save != nullptr && !MetaDisabled(registry);
    if (save != nullptr) {
        ImGui::TextDisabled("Seals %d   ·   Lifetime earned %d", MetaSeals(registry), LifetimeSealsEarned(registry));
    } else {
        ImGui::TextDisabled("Unlocks need a profile (meta progress is off).");
    }
    ImGui::TextWrapped("Unlocks add options to future expeditions: skills join a hero's pool, runes join the offer pool.");

    const auto* content = registry.ctx().find<gameplay::FContentRegistry>();
    const std::vector<FUnlockOffer> offers = BuildUnlockOffers(registry);
    if (offers.empty()) {
        ImGui::TextDisabled("No unlocks are available in this profile.");
    } else if (ImGui::BeginTable("Unlock offers", canBuy ? 7 : 6,
                                 ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg |
                                     ImGuiTableFlags_SizingFixedFit)) {
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 0.30f);
        ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("For", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Cost (seals)", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Seal gate (lifetime)", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthStretch, 0.30f);
        if (canBuy) ImGui::TableSetupColumn("##buy", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableHeadersRow();
        for (const FUnlockOffer& offer : offers) {
            ImGui::PushID(offer.row.id.c_str());
            // The tooltip body and the "For" column come from ONE helper — the same seam the tests read.
            const FUnlockOfferDetail detail = UnlockOfferDetail(registry, offer.row);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(UnlockDisplayName(registry, content, offer.row).c_str());
            if (ImGui::IsItemHovered() && (!detail.tooltipLines.empty() || kUnlockTooltipShowsRawId)) {
                ImGui::BeginTooltip();
                for (const std::string& line : detail.tooltipLines) {
                    ImGui::TextUnformatted(line.c_str());
                }
#ifndef NDEBUG
                ImGui::TextDisabled("%s", offer.row.id.c_str());
#endif
                ImGui::EndTooltip();
            }
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(UnlockKindLabel(offer.row.kind));
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(detail.forText.c_str());
            ImGui::TableSetColumnIndex(3);
            ImGui::Text("%d", offer.row.cost);
            ImGui::TableSetColumnIndex(4);
            ImGui::Text("%d", offer.row.sealGate);
            ImGui::TableSetColumnIndex(5);
            if (offer.status == EUnlockStatus::Owned) {
                ImGui::TextColored(ui::palette::kGoldBrightV, "Unlocked");
            } else {
                ImGui::TextDisabled("%s", offer.reason.empty() ? UnlockStatusFallback(offer.status)
                                                               : offer.reason.c_str());
            }
            if (canBuy) {
                ImGui::TableSetColumnIndex(6);
                if (offer.status != EUnlockStatus::Owned) {
                    ImGui::BeginDisabled(offer.status != EUnlockStatus::Purchasable);
                    if (ImGui::SmallButton("Buy")) {
                        (void)PurchaseMetaUnlock(registry, offer.row.id);
                    }
                    ImGui::EndDisabled();
                }
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
    DrawHubFooter(registry, screen);
}

// --- Records tab ([WP-21c] body: stats, seals and ladder ranks; the run-history ring is WP-31) --------

/// The rank key as a number; keys are validated positive integer strings (MetaSaveFromJson), a malformed
/// one sorts last and still prints as authored.
int RankKeyValue(std::string_view key) {
    int value = 0;
    const auto parsed = std::from_chars(key.data(), key.data() + key.size(), value);
    return parsed.ec == std::errc{} ? value : std::numeric_limits<int>::max();
}

void DrawRecordsTab(entt::registry& registry, FPreparationScreen& screen) {
    const float footerHeight = HubFooterHeight(screen);
    // The body-height rule every tab shares (WP-09h F7): all the space above the pinned footer.
    const float bodyHeight = std::max(ImGui::GetContentRegionAvail().y - footerHeight, kHubBodyMinHeight);
    ImGui::BeginChild("Record list", {0.f, bodyHeight}, ImGuiChildFlags_Borders);
    // [WP-21c] Read-only: FMetaStats, the live seals/lifetime figures, the owned count and the ladder.
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
        ImGui::Text("Seals: %d", MetaSeals(registry));
        ImGui::Text("Lifetime earned: %d", LifetimeSealsEarned(registry));
        const std::vector<FMetaUnlockRow> catalog = MetaUnlockCatalog(registry);
        int owned = 0;
        for (const FMetaUnlockRow& row : catalog) {
            if (HasMetaUnlock(registry, row.id)) ++owned;
        }
        ImGui::Text("Unlocks owned: %d / %d", owned, static_cast<int>(catalog.size()));

        ui::SectionTitle("RANKS");
        if (save->ladder.empty()) {
            ImGui::TextDisabled("No ranked runs yet.");
        } else {
            // Ascending numeric rank order: the map's string keys would sort "10" before "2".
            std::vector<std::pair<std::string, const FMetaLadderRank*>> ranks;
            ranks.reserve(save->ladder.size());
            for (const auto& [key, rank] : save->ladder) ranks.push_back({key, &rank});
            std::sort(ranks.begin(), ranks.end(), [](const auto& left, const auto& right) {
                return RankKeyValue(left.first) < RankKeyValue(right.first);
            });
            for (const auto& [key, rank] : ranks) {
                ImGui::Text("Rank %s: best dungeon %d   ·   checkpoint %d", key.c_str(), rank->bestDungeon,
                            rank->checkpoint);
            }
        }
    }
    ImGui::EndChild();
    DrawHubFooter(registry, screen);
}

} // namespace

// --- [WP-09h F6] the Unlocks tab's shared detail (header-declared: the view and the tests read one seam) --

FUnlockOfferDetail UnlockOfferDetail(entt::registry& registry, const FMetaUnlockRow& row) {
    FUnlockOfferDetail detail;
    const auto* content = registry.ctx().find<gameplay::FContentRegistry>();
    if (row.kind == "skill") {
        const gameplay::FSkillContent* skill = content != nullptr ? content->skills.find(row.id) : nullptr;
        detail.forText = UnlockSkillOwners(content, row.id, BuildRosterChoices(registry));
        if (skill == nullptr) return detail;  // unknown row: empty fields, never the raw id
        if (!skill->tags.empty()) detail.tooltipLines.push_back(JoinTags(skill->tags));
        detail.tooltipLines.push_back("AP " + std::to_string(skill->apCost));
        const std::string description = SkillEffectText(content, *skill);
        if (!description.empty()) detail.tooltipLines.push_back(description);
        return detail;
    }
    if (row.kind == "rune") {
        const gameplay::FRuneContent* rune = content != nullptr ? content->runes.find(row.id) : nullptr;
        if (rune == nullptr) return detail;
        // One effect line only: RuneEffectSummary already carries the requirement rules inline, so a
        // separate requires_*_tags line would repeat the same text in the same tooltip (review round 1).
        detail.tooltipLines.push_back(RuneEffectSummary(*rune, content));
        detail.forText = RuneFittingMembers(registry, content, row.id);
        return detail;
    }
    // A roster row has no For text and no tooltip body (WP-09h review round 1): the recruit's own name is
    // already the Name column (UnlockDisplayName), and "For" answers who an unlock serves.
    return detail;
}

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
