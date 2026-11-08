#pragma once

#include <SDL3/SDL.h>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace game::dev {

/// WP-00c M1 (dev only): a plain-text script of timed input for windowless runs (`--input-script <file>`).
/// The engine injects each frame's commands with SDL_PushEvent right before its SDL_PollEvent loop, so ImGui
/// and the game see ordinary SDL events (no OS window, no focus, no global cursor needed).
///
/// One command per line; `#` starts a comment (whole line or trailing); blank lines are ignored:
///   <frame> move <x> <y>
///   <frame> click <x> <y> [left|right|middle]   press at the point; see "Click timing" below
///   <frame> down <x> <y> [left|right|middle]
///   <frame> up <x> <y> [left|right|middle]
///   <frame> key <SDL key name>                   key down this frame, up on frame+1 (see "Key names")
///   <frame> wheel <dy>                           vertical wheel at the last known pointer position
///   <frame> capture <png path>                   save this frame (after ImGui draws) without quitting
///   <frame> quit                                 stop cleanly after this frame (exit code 0)
///   <frame> expect state <state id>              the app-flow top state when frame <frame> begins must be
///                                                <state id> (e.g. ember.camp); a mismatch fails the run (exit != 0)
/// Coordinates are window pixels (logical, same space as `--window`). <frame> is the engine frame index
/// (1-based; the same number `--frames N --capture` captures). Frames are absolute and non-decreasing.
/// A key name with spaces (e.g. "Left Shift") is the rest of the line after `key`.
///
/// Key names (critic P1b): resolved with SDL_GetScancodeFromName first, then the keycode is
/// SDL_GetKeyFromScancode(scancode, SDL_KMOD_NONE, false); only when that scancode is SDL_SCANCODE_UNKNOWN fall
/// back to SDL_GetKeyFromName + SDL_GetScancodeFromKey. Both key.key and key.scancode are always set (the game
/// reads scancodes, ImGui reads keycodes).
///
/// Click timing (critic P2): FInputState.uiCapturesMouse is the PREVIOUS frame's io.WantCaptureMouse, so a press
/// on a HUD button from a new point would also reach the grid. When the press point differs from the last pointer
/// position, `click` and `down` push the motion at <frame>, the button-down at <frame>+1 (and a click's up at
/// <frame>+2); at the current point the down is at <frame> and the click's up at <frame>+1. `up` pushes motion
/// first only when the point changed, then the release on the same frame. Scripts never read SDL device state
/// (SDL_GetMouseState/KeyboardState are not updated by pushed events).
/// Limit: do not script a `move` to another point in the frame of a new-point `click`/`down` or the frame after —
/// the delayed press would then re-push motion and press in one frame (the P2 leak). Click, then move.

enum class EScriptCommand : std::uint8_t { Move, Click, Down, Up, Key, Wheel, Capture, Quit, ExpectState };

struct FScriptCommand {
    std::uint32_t frame{0};
    EScriptCommand kind{EScriptCommand::Move};
    float x{0.f};
    float y{0.f};
    std::uint8_t button{SDL_BUTTON_LEFT};  // SDL_BUTTON_LEFT / SDL_BUTTON_RIGHT / SDL_BUTTON_MIDDLE
    SDL_Keycode key{SDLK_UNKNOWN};
    float wheel{0.f};
    std::string path;  // Capture: the PNG path; ExpectState: the expected state id
    int line{0};       // 1-based source line, for messages
};

/// The parse error kinds (one per rejected shape; the test pins each).
enum class EScriptError : std::uint8_t {
    BadFrame,        // missing / non-integer / zero frame number
    FrameOrder,      // a frame number smaller than the previous command's
    UnknownCommand,  // a verb that is not in the list above
    BadArguments,    // wrong argument count or a non-numeric coordinate / wheel value
    BadButton,       // a button word other than left/right/middle
    UnknownKey,      // neither the scancode name nor the key name resolves
    FileUnreadable,  // LoadInputScript only
};

struct FInputScriptError {
    int line{0};  // 1-based; 0 for FileUnreadable
    EScriptError kind{EScriptError::BadArguments};
    std::string message;  // "input script line <n>: <what>" — the text main() prints before exiting non-zero
};

/// Pure parser (no SDL video needed; key names resolve through SDL_GetKeyFromName, which works without SDL_Init).
[[nodiscard]] std::expected<std::vector<FScriptCommand>, FInputScriptError> ParseInputScript(std::string_view text);

/// Reads the file and parses it.
[[nodiscard]] std::expected<std::vector<FScriptCommand>, FInputScriptError> LoadInputScript(const std::string& path);

/// What a frame's injection asks the engine to do besides the pushed events.
struct FInputScriptFrame {
    std::vector<std::string> captures;  // PNG paths to save after this frame's ImGui draw, in script order
    bool quit{false};                   // stop the loop cleanly after this frame
    std::vector<FScriptCommand> expects;  // ExpectState commands due this frame (path = state id, line = source)
    std::size_t pushed{0};              // SDL events pushed this frame (diagnostics/tests)
};

/// Per-frame injector. Holds the parsed commands and the queued follow-ups (delayed button-downs and the `up`
/// half of click/key), ordered by due frame.
class FInputScript {
public:
    explicit FInputScript(std::vector<FScriptCommand> commands);

    /// Pushes every event due at `frame` (pending releases from frame-1 first, then this frame's commands in
    /// script order) with SDL_PushEvent, stamped with `window` as windowID and SDL_GetTicksNS() as timestamp.
    /// Mouse motion is pushed before each button/wheel event at its point, so hover state is correct even
    /// without a separate `move`. Call once per frame, before the SDL_PollEvent loop; frames may be skipped
    /// (commands whose frame already passed are injected at the next call, never dropped).
    FInputScriptFrame inject(std::uint32_t frame, SDL_WindowID window);

    /// True when every command and pending release has been injected.
    [[nodiscard]] bool finished() const noexcept;

private:
    std::vector<FScriptCommand> commands_;
    std::size_t next_{0};
    std::vector<FScriptCommand> pendingReleases_;  // delayed downs + releases; frame = the frame they are due
    float lastX_{0.f};
    float lastY_{0.f};
};

} // namespace game::dev
