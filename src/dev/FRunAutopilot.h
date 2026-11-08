#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include <entt/entt.hpp>

namespace game::dev {

/// WP-00c M2 (dev only): GAME01P_AUTOPILOT=<policy> plays the non-battle run phases through the same model
/// functions the run views call (ResolveRuneOffer, PurchaseCampRuneOffer, UpgradeDungeonStat, PostRunIntent,
/// the title's FMenuRequest) — never through ImGui. Battles stay with GAME01P_AUTOPLAY=1. With GAME01P_SEED
/// the same policy + seed must produce the same run log. Every decision writes one `[autopilot] ...` LOG_INFO.
/// Timing: the autopilot keeps the engine's wall-clock dt (only `--input-script` fixes dt to 1/60): decisions are
/// keyed by run state (top id, depth, room, phase), never by frame, so separate-process runs of one seed produce
/// identical run logs. Wall-time metrics (battle seconds) of a headless run are therefore not comparable with
/// windowed runs (software rendering is ~9 fps in Debug).

/// Environment keys (read with std::getenv by the composition root; SDL_getenv caches the environment).
inline constexpr const char* kAutopilotEnv = "GAME01P_AUTOPILOT";
inline constexpr const char* kAutopilotMaxRoomsEnv = "GAME01P_AUTOPILOT_MAX_ROOMS";

/// The default policy, mirroring the M-FUN harness (mfun-drive.ps1 Step-Reward / Step-Event / Step-Camp /
/// Step-Dungeon). `GAME01P_AUTOPILOT=1` or `default` means this string.
inline constexpr std::string_view kDefaultAutopilotPolicy = "reward=card0,event=first,chest=open,camp=offer+1,continue";

/// Comma-separated tokens, any order, each at most once; unknown tokens are a parse error:
///   reward=card<N> | reward=skip   pick offer card N (0-based; an out-of-range N takes the last card) or skip
///   reward=engine                  WP-00d: score every card BEFORE resolving (FEnginePolicy ScoreEngineCard:
///                                  2 completes a pair, 1 starts one, 0 otherwise / owned duplicate); highest
///                                  score, lowest index on ties; all zeros -> card 0. Applies to the room reward
///                                  AND the camp rune offer (both resolve through the same reward rule)
///   event=first | event=second     the event option (first = shrine rest, second = open the cache)
///   chest=open | chest=never       open: in EVEN dungeons (run depth % 2 == 0) every event takes the cache
///                                  option (M-FUN: shrine on odd dungeons, chest on even); never: event= only
///   camp=<spec>                    spec = [offer][+<U>]: "offer" buys the camp rune offer when legal and
///                                  affordable (then picks with the reward= rule), +U buys up to U stat
///                                  upgrades for party member 0 ONLY, each the first upgrade index that
///                                  ValidateDungeonStatUpgrade accepts (the camp view's candidates[0], M-FUN
///                                  Step-Camp; critic P5); "none" = nothing
///   continue                       leave the camp (descend). Without it the autopilot stops at the camp (exit 0)
///   equip=auto | equip=off         WP-27: after every pick (reward or camp offer) fit the picked rune's first
///                                  copy in `run.runes` into the first legal socket (members in order, then skill
///                                  slots in order, ValidateDungeonRuneEquip → EquipDungeonRune); off = never
///   equip=engine                   WP-00d: fit every pick by the engine rules (FEnginePolicy.h: complete >
///                                  start > fallback > auto) and consolidate engines at every camp visit
/// Tokens left out take the default policy's value, except `continue` (present only when written). `equip` is
/// off unless written, so the default policy keeps the M-FUN harness behaviour (no reward-time fitting).
/// The `equip=` token (WP-27 auto, WP-00d engine).
enum class EAutopilotEquip : std::uint8_t {
    Off,
    Auto,
    Engine
};

struct FAutopilotPolicy {
    int rewardCard{0};
    bool rewardSkip{false};
    bool rewardEngine{false};       // reward=engine (rewardCard stays 0, rewardSkip false)
    int eventOption{0};             // 0 = rest (first), 1 = loot (second)
    bool chestOnEvenDungeons{true};
    bool campBuyOffer{true};
    int campUpgrades{1};
    bool campContinue{true};
    EAutopilotEquip equip{EAutopilotEquip::Off};
};

[[nodiscard]] std::expected<FAutopilotPolicy, std::string> ParseAutopilotPolicy(std::string_view text);

/// What the engine does after a tick.
enum class EAutopilotStatus : std::uint8_t {
    Running,   // keep the loop going
    Finished,  // run end (the run-end state is on top) or the room limit was reached: stop, exit 0
    Stuck      // the same decision point stayed on top for kStuckFrames after acting: stop, exit non-zero
};

/// Frames a decision point may stay on top after the autopilot acted on it before it reports Stuck.
inline constexpr std::uint32_t kAutopilotStuckFrames = 600;

/// The per-process driver. `maxRooms` 0 = no limit.
class FRunAutopilot {
public:
    explicit FRunAutopilot(FAutopilotPolicy policy, int maxRooms = 0);

    /// One call per frame, after the state machine's update and before rendering. `topStateId` is the id of
    /// the state on top of the app-flow stack (catalog ids: "ember.title", "ember.battle", the kRunModal*
    /// ids from FRunFlow.h, "ember.runEnd"). Acts at most once per decision point, keyed by
    /// (top id, run depth, room, phase): title -> FMenuRequest Start fresh; reward -> resolve the offer by the
    /// reward rule, then PostRunIntent(ContinueReward); event -> ChooseEventRest/ChooseEventLoot; camp ->
    /// offer + upgrades, then PostRunIntent(LeaveCamp) (or Finished without `continue`); run end -> Finished.
    /// The modal host consumes the posted intent on the next update, exactly as for a view button.
    /// WP-00d: after every reward resolve and every camp step (any equip mode) the engines are classified
    /// (FEnginePolicy ClassifyEngines); each engine that just became Armed is recorded once and logged
    /// `[autopilot] engine armed engine=E depth=D member=M slot=S`. The run-end decision point and the
    /// room-limit stop log one `[autopilot] run end engines armed=[E@D,...] unarmed=[E:reason,...]` line.
    EAutopilotStatus tick(entt::registry& registry, std::string_view topStateId);

    /// Rooms finished (one per ContinueReward the autopilot posted). Stops with Finished once this reaches
    /// maxRooms and the reward modal has closed.
    [[nodiscard]] int roomsCompleted() const noexcept { return roomsCompleted_; }
    [[nodiscard]] const FAutopilotPolicy& policy() const noexcept { return policy_; }

private:
    /// WP-00d: one engine recorded as Armed during this run (its first-arming depth).
    struct FArmedEngineRecord {
        std::string engineId;
        int depth{0};
    };

    /// WP-00d: classify the engines (FEnginePolicy ClassifyEngines) and record + log every engine that just
    /// became Armed — once per engine per run.
    void TrackArmedEngines(const entt::registry& registry, int depth);
    /// WP-00d (review P7): the one `[autopilot] run end engines armed=[...] unarmed=[...]` line, emitted at
    /// most once per run, in every equip mode.
    void LogRunEndEngines(const entt::registry& registry);

    FAutopilotPolicy policy_;
    int maxRooms_{0};
    int roomsCompleted_{0};
    bool acted_{false};               // acted on the current decision point
    std::uint32_t framesSinceAct_{0};
    std::string lastTop_;
    std::tuple<int, int, int> lastKey_{-1, -1, -1};  // depth, room, phase of the current decision point
    std::vector<FArmedEngineRecord> armedEngines_;   // WP-00d: engines armed this run, in arming order
    bool runEndEnginesLogged_{false};                // WP-00d: the run-end engines line was already emitted
};

} // namespace game::dev
