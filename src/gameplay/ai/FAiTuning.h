#pragma once

#include <iterator>
#include <string_view>

namespace game::ai {

/// Numbers the AI's judgement is built from. Values only (R17/R23): retuning how a monster weighs things is
/// an `assets/data/ai.json` edit, never a code edit. Every field is a whole number so a decision is
/// reproducible bit for bit (R16); "percent" fields are integer percentages.
///
/// Scores are measured in expected HP swing, so "3" always means "worth about three hit points".
struct FAiTuning {
    int killHorizonTurns{2};  // a kill is worth this many turns of the victim's output
    int focusPercent{50};     // extra weight on damage dealt to targets that are already hurt
    int focusStickiness{3};   // flat bonus for staying on the previous target (focus fire)
    int healPercent{100};     // value of one healed HP, in percent of one damage point
    int statusPercent{100};   // scales the value of statuses (dot, control, buff, debuff)
    int supportMinValue{3};   // buffs and heals worth less than this are not worth a skill AP
    int dangerPercent{50};    // weight of "enemies can hit me there next turn" when choosing where to stand
    int trapPercent{100};     // weight of trap damage when choosing where to stand
    int rangeKeepPoints{2};   // per cell of distance kept from enemies while staying in skill range
    int progressPoints{10};   // per cell of progress toward a firing position when no strike is possible yet
    int moveHysteresis{3};    // a move must beat standing still by at least this much
};

struct FAiTuningField {
    std::string_view key; // boundary: asset key
    int FAiTuning::*member;
    int minimum;
    int maximum;
};

/// `invariant:` every FAiTuning member is listed here once, with the same key spelling as ai.json.
inline constexpr FAiTuningField kAiTuningFields[] = {
    {"kill_horizon_turns", &FAiTuning::killHorizonTurns, 0, 8},
    {"focus_percent", &FAiTuning::focusPercent, 0, 400},
    {"focus_stickiness", &FAiTuning::focusStickiness, 0, 100},
    {"heal_percent", &FAiTuning::healPercent, 0, 400},
    {"status_percent", &FAiTuning::statusPercent, 0, 400},
    {"support_min_value", &FAiTuning::supportMinValue, 0, 100},
    {"danger_percent", &FAiTuning::dangerPercent, 0, 400},
    {"trap_percent", &FAiTuning::trapPercent, 0, 400},
    {"range_keep_points", &FAiTuning::rangeKeepPoints, 0, 100},
    {"progress_points", &FAiTuning::progressPoints, 0, 100},
    {"move_hysteresis", &FAiTuning::moveHysteresis, 0, 100},
};

static_assert(std::size(kAiTuningFields) == 11, "kAiTuningFields and FAiTuning drifted apart");

} // namespace game::ai
