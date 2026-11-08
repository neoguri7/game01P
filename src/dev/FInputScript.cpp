#include "dev/FInputScript.h"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace game::dev {

namespace {

constexpr std::string_view kWhitespace = " \t";

[[nodiscard]] std::string_view Trim(std::string_view text)
{
    const std::size_t first = text.find_first_not_of(kWhitespace);
    if (first == std::string_view::npos) {
        return {};
    }
    const std::size_t last = text.find_last_not_of(kWhitespace);
    return text.substr(first, last - first + 1);
}

[[nodiscard]] std::vector<std::string_view> SplitTokens(std::string_view text)
{
    std::vector<std::string_view> tokens;
    std::size_t position = 0;
    while (position < text.size()) {
        const std::size_t start = text.find_first_not_of(kWhitespace, position);
        if (start == std::string_view::npos) {
            break;
        }
        const std::size_t end = text.find_first_of(kWhitespace, start);
        tokens.push_back(text.substr(start, (end == std::string_view::npos ? text.size() : end) - start));
        if (end == std::string_view::npos) {
            break;
        }
        position = end;
    }
    return tokens;
}

[[nodiscard]] bool ParseFrameNumber(std::string_view token, std::uint32_t& value)
{
    const auto result = std::from_chars(token.data(), token.data() + token.size(), value);
    return result.ec == std::errc{} && result.ptr == token.data() + token.size() && value >= 1;
}

[[nodiscard]] bool ParseFloatValue(std::string_view token, float& value)
{
    const auto result = std::from_chars(token.data(), token.data() + token.size(), value);
    return result.ec == std::errc{} && result.ptr == token.data() + token.size();
}

[[nodiscard]] std::optional<std::uint8_t> ParseButtonWord(std::string_view token)
{
    if (token == "left") {
        return static_cast<std::uint8_t>(SDL_BUTTON_LEFT);
    }
    if (token == "right") {
        return static_cast<std::uint8_t>(SDL_BUTTON_RIGHT);
    }
    if (token == "middle") {
        return static_cast<std::uint8_t>(SDL_BUTTON_MIDDLE);
    }
    return std::nullopt;
}

[[nodiscard]] std::unexpected<FInputScriptError> Fail(int line, EScriptError kind, std::string what)
{
    return std::unexpected(FInputScriptError{
        .line = line,
        .kind = kind,
        .message = "input script line " + std::to_string(line) + ": " + std::move(what),
    });
}

/// Parses one non-blank, comment-free line and appends its command on success.
[[nodiscard]] std::expected<void, FInputScriptError> ParseCommandLine(
    int lineNumber,
    std::string_view line,
    std::uint32_t& previousFrame,
    bool& hasPreviousFrame,
    std::vector<FScriptCommand>& commands)
{
    const std::size_t frameEnd = line.find_first_of(kWhitespace);
    const std::string_view frameToken = line.substr(0, frameEnd);
    std::uint32_t frame = 0;
    if (!ParseFrameNumber(frameToken, frame)) {
        return Fail(lineNumber, EScriptError::BadFrame,
                    "frame number '" + std::string(frameToken) + "' is not an integer >= 1");
    }
    if (hasPreviousFrame && frame < previousFrame) {
        return Fail(lineNumber, EScriptError::FrameOrder,
                    "frame " + std::to_string(frame) + " is before the previous frame " + std::to_string(previousFrame));
    }
    previousFrame = frame;
    hasPreviousFrame = true;

    if (frameEnd == std::string_view::npos) {
        return Fail(lineNumber, EScriptError::UnknownCommand, "missing command");
    }
    const std::string_view rest = Trim(line.substr(frameEnd + 1));
    if (rest.empty()) {
        return Fail(lineNumber, EScriptError::UnknownCommand, "missing command");
    }
    const std::size_t verbEnd = rest.find_first_of(kWhitespace);
    const std::string_view verb = rest.substr(0, verbEnd);
    const std::string_view arguments =
        verbEnd == std::string_view::npos ? std::string_view{} : Trim(rest.substr(verbEnd + 1));

    FScriptCommand command;
    command.frame = frame;
    command.line = lineNumber;

    if (verb == "move") {
        const std::vector<std::string_view> tokens = SplitTokens(arguments);
        if (tokens.size() != 2 || !ParseFloatValue(tokens[0], command.x) || !ParseFloatValue(tokens[1], command.y)) {
            return Fail(lineNumber, EScriptError::BadArguments, "move expects: <x> <y>");
        }
        command.kind = EScriptCommand::Move;
    } else if (verb == "click" || verb == "down" || verb == "up") {
        const std::vector<std::string_view> tokens = SplitTokens(arguments);
        if ((tokens.size() != 2 && tokens.size() != 3) || !ParseFloatValue(tokens[0], command.x) ||
            !ParseFloatValue(tokens[1], command.y)) {
            return Fail(lineNumber, EScriptError::BadArguments,
                        std::string(verb) + " expects: <x> <y> [left|right|middle]");
        }
        if (tokens.size() == 3) {
            const std::optional<std::uint8_t> button = ParseButtonWord(tokens[2]);
            if (!button) {
                return Fail(lineNumber, EScriptError::BadButton,
                            "unknown button '" + std::string(tokens[2]) + "' (expected left|right|middle)");
            }
            command.button = *button;
        }
        if (verb == "click") {
            command.kind = EScriptCommand::Click;
        } else if (verb == "down") {
            command.kind = EScriptCommand::Down;
        } else {
            command.kind = EScriptCommand::Up;
        }
    } else if (verb == "key") {
        // Header "Key names": resolve the scancode first (no OS keymap needed under the dummy driver) and take
        // its default keycode; only an unknown scancode name falls back to the keycode table.
        const std::string name(arguments);
        SDL_Keycode key = SDLK_UNKNOWN;
        const SDL_Scancode scancode = SDL_GetScancodeFromName(name.c_str());
        if (scancode != SDL_SCANCODE_UNKNOWN) {
            key = SDL_GetKeyFromScancode(scancode, SDL_KMOD_NONE, false);
        } else {
            key = SDL_GetKeyFromName(name.c_str());
        }
        if (key == SDLK_UNKNOWN) {
            return Fail(lineNumber, EScriptError::UnknownKey, "unknown key '" + name + "'");
        }
        command.kind = EScriptCommand::Key;
        command.key = key;
    } else if (verb == "expect") {
        const std::size_t keywordEnd = arguments.find_first_of(kWhitespace);
        const std::string_view keyword = arguments.substr(0, keywordEnd);
        const std::string_view stateId =
            keywordEnd == std::string_view::npos ? std::string_view{} : Trim(arguments.substr(keywordEnd + 1));
        if (keyword != "state" || stateId.empty()) {
            return Fail(lineNumber, EScriptError::BadArguments, "expect expects: state <state id>");
        }
        command.kind = EScriptCommand::ExpectState;
        command.path = std::string(stateId);
    } else if (verb == "wheel") {
        const std::vector<std::string_view> tokens = SplitTokens(arguments);
        if (tokens.size() != 1 || !ParseFloatValue(tokens[0], command.wheel)) {
            return Fail(lineNumber, EScriptError::BadArguments, "wheel expects: <dy>");
        }
        command.kind = EScriptCommand::Wheel;
    } else if (verb == "capture") {
        if (arguments.empty()) {
            return Fail(lineNumber, EScriptError::BadArguments, "capture expects: <png path>");
        }
        command.kind = EScriptCommand::Capture;
        command.path = std::string(arguments);
    } else if (verb == "quit") {
        if (!arguments.empty()) {
            return Fail(lineNumber, EScriptError::BadArguments, "quit takes no arguments");
        }
        command.kind = EScriptCommand::Quit;
    } else {
        return Fail(lineNumber, EScriptError::UnknownCommand, "unknown command '" + std::string(verb) + "'");
    }

    commands.push_back(std::move(command));
    return {};
}

} // namespace

std::expected<std::vector<FScriptCommand>, FInputScriptError> ParseInputScript(std::string_view text)
{
    std::vector<FScriptCommand> commands;
    std::uint32_t previousFrame = 0;
    bool hasPreviousFrame = false;

    int lineNumber = 0;
    std::size_t position = 0;
    while (position <= text.size()) {
        const std::size_t newline = text.find('\n', position);
        std::string_view line =
            newline == std::string_view::npos ? text.substr(position) : text.substr(position, newline - position);
        ++lineNumber;
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        const std::size_t comment = line.find('#');
        if (comment != std::string_view::npos) {
            line = line.substr(0, comment);
        }
        line = Trim(line);
        if (!line.empty()) {
            const auto result = ParseCommandLine(lineNumber, line, previousFrame, hasPreviousFrame, commands);
            if (!result) {
                return std::unexpected(result.error());
            }
        }
        if (newline == std::string_view::npos) {
            break;
        }
        position = newline + 1;
    }
    return commands;
}

std::expected<std::vector<FScriptCommand>, FInputScriptError> LoadInputScript(const std::string& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::unexpected(FInputScriptError{
            .line = 0,
            .kind = EScriptError::FileUnreadable,
            .message = "input script: cannot read " + path,
        });
    }
    const std::string text{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    return ParseInputScript(text);
}

FInputScript::FInputScript(std::vector<FScriptCommand> commands)
    : commands_(std::move(commands))
{
}

bool FInputScript::finished() const noexcept
{
    return next_ >= commands_.size() && pendingReleases_.empty();
}

FInputScriptFrame FInputScript::inject(std::uint32_t frame, SDL_WindowID window)
{
    FInputScriptFrame output;

    const auto push = [&output](SDL_Event& event) {
        if (SDL_PushEvent(&event)) {
            ++output.pushed;
        }
    };

    const auto pushMotion = [&](float x, float y) {
        SDL_Event event{};
        event.type = SDL_EVENT_MOUSE_MOTION;
        event.common.timestamp = SDL_GetTicksNS();
        event.motion.windowID = window;
        event.motion.which = 0; // 0 = not a touch device
        event.motion.x = x;
        event.motion.y = y;
        event.motion.xrel = x - lastX_;
        event.motion.yrel = y - lastY_;
        push(event);
        lastX_ = x;
        lastY_ = y;
    };

    const auto pushMotionIfMoved = [&](float x, float y) {
        if (x != lastX_ || y != lastY_) {
            pushMotion(x, y);
        }
    };

    const auto pushButton = [&](bool down, const FScriptCommand& command) {
        pushMotionIfMoved(command.x, command.y);
        SDL_Event event{};
        event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
        event.common.timestamp = SDL_GetTicksNS();
        event.button.windowID = window;
        event.button.which = 0;
        event.button.button = command.button;
        event.button.down = down;
        event.button.clicks = 1;
        event.button.x = command.x;
        event.button.y = command.y;
        push(event);
    };

    const auto pushKey = [&](bool down, const FScriptCommand& command) {
        SDL_Event event{};
        event.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
        event.common.timestamp = SDL_GetTicksNS();
        event.key.windowID = window;
        event.key.which = 0;
        event.key.key = command.key;
        event.key.scancode = SDL_GetScancodeFromKey(command.key, nullptr);
        event.key.down = down;
        event.key.repeat = false;
        event.key.mod = SDL_KMOD_NONE;
        push(event);
    };

    const auto pushWheel = [&](const FScriptCommand& command) {
        SDL_Event event{};
        event.type = SDL_EVENT_MOUSE_WHEEL;
        event.common.timestamp = SDL_GetTicksNS();
        event.wheel.windowID = window;
        event.wheel.which = 0;
        event.wheel.x = 0.f;
        event.wheel.y = command.wheel;
        event.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
        event.wheel.mouse_x = lastX_;
        event.wheel.mouse_y = lastY_;
        push(event);
    };

    // The follow-up half of a click/down/key. The queue stays ordered by due frame (stable for equal dues), so
    // a release can never be overtaken by an event of a later command with an earlier due frame.
    const auto queueRelease = [this](const FScriptCommand& command, EScriptCommand kind, std::uint32_t due) {
        FScriptCommand pending = command;
        pending.kind = kind;
        pending.frame = due;
        const auto position = std::find_if(
            pendingReleases_.begin(),
            pendingReleases_.end(),
            [due](const FScriptCommand& queued) { return queued.frame > due; });
        pendingReleases_.insert(position, std::move(pending));
    };

    // Injects one queued follow-up without re-queueing (a press whose point was left behind moves back first).
    const auto releaseOne = [&](const FScriptCommand& pending) {
        if (pending.kind == EScriptCommand::Down) {
            pushButton(true, pending);
        } else if (pending.key != SDLK_UNKNOWN) {
            pushKey(false, pending);
        } else {
            pushButton(false, pending);
        }
    };

    const auto injectOne = [&](const FScriptCommand& command) {
        switch (command.kind) {
        case EScriptCommand::Move:
            pushMotion(command.x, command.y);
            break;
        case EScriptCommand::Click:
            if (command.x != lastX_ || command.y != lastY_) {
                // Header "Click timing": the game's uiCapturesMouse is one frame behind ImGui's hover, so a
                // press from a new point delays its button-down to the next frame (up one frame later).
                pushMotion(command.x, command.y);
                queueRelease(command, EScriptCommand::Down, command.frame + 1);
                queueRelease(command, EScriptCommand::Up, command.frame + 2);
            } else {
                pushButton(true, command);
                queueRelease(command, EScriptCommand::Up, command.frame + 1);
            }
            break;
        case EScriptCommand::Down:
            if (command.x != lastX_ || command.y != lastY_) {
                pushMotion(command.x, command.y);
                queueRelease(command, EScriptCommand::Down, command.frame + 1);
            } else {
                pushButton(true, command);
            }
            break;
        case EScriptCommand::Up:
            // `up` commands and queued releases share this kind; a key release carries a keycode, a mouse
            // release a button and point. An `up` from a new point moves first and releases the same frame.
            if (command.key != SDLK_UNKNOWN) {
                pushKey(false, command);
            } else {
                pushButton(false, command);
            }
            break;
        case EScriptCommand::Key:
            pushKey(true, command);
            queueRelease(command, EScriptCommand::Up, command.frame + 1);
            break;
        case EScriptCommand::Wheel:
            pushWheel(command);
            break;
        case EScriptCommand::Capture:
            output.captures.push_back(command.path);
            break;
        case EScriptCommand::Quit:
            output.quit = true;
            break;
        case EScriptCommand::ExpectState:
            output.expects.push_back(command);
            break;
        }
    };

    // The follow-ups due by now go first so a click's release is never overtaken by this frame's commands.
    std::size_t released = 0;
    while (released < pendingReleases_.size() && pendingReleases_[released].frame <= frame) {
        releaseOne(pendingReleases_[released]);
        ++released;
    }
    if (released > 0) {
        const auto firstRelease = pendingReleases_.begin();
        pendingReleases_.erase(firstRelease, firstRelease + static_cast<std::ptrdiff_t>(released));
    }

    while (next_ < commands_.size() && commands_[next_].frame <= frame) {
        injectOne(commands_[next_]);
        ++next_;
    }
    return output;
}

} // namespace game::dev
