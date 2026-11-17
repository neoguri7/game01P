#pragma once

#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json_fwd.hpp>

#include "gameplay/ai/FBtNode.h"
#include "gameplay/data/FContentRegistry.h"

namespace game::ai {

/// Reads the authored parameters of one tree node and checks them as it goes. Reading never throws: the first
/// problem is remembered and finish() reports it, together with any key nobody asked for — so a typo in
/// ai.json fails the boot (R19/R22) instead of silently falling back to a default.
class FNodeParams {
public:
    FNodeParams(const nlohmann::json& node, std::string path);

    /// Required whole number in [minimum, maximum].
    [[nodiscard]] int integer(std::string_view key, int minimum, int maximum);
    /// Optional whole number; `fallback` when the key is absent.
    [[nodiscard]] int integer(std::string_view key, int minimum, int maximum, int fallback);
    [[nodiscard]] bool boolean(std::string_view key, bool fallback);
    [[nodiscard]] std::string text(std::string_view key);
    [[nodiscard]] std::string text(std::string_view key, std::string fallback);
    /// Optional array of strings; empty when absent.
    [[nodiscard]] std::vector<std::string> textList(std::string_view key);

    void fail(std::string message);
    [[nodiscard]] const std::string& path() const { return path_; }
    /// The first error (with the node's path), or an unknown-key error; nullopt when every key was understood.
    [[nodiscard]] std::optional<std::string> finish();

private:
    [[nodiscard]] const nlohmann::json* take(std::string_view key);

    const nlohmann::json* node_;
    std::string path_;
    std::vector<std::string> used_;
    std::optional<std::string> error_;
};

using FNodeBuild = std::expected<FBtNodePtr, std::string>;

/// What a node factory receives: its parameters and its already-built children.
struct FNodeSpec {
    FNodeParams& params;
    std::vector<FBtNodePtr>& children;
    const gameplay::FContentRegistry& content;
};

/// Maps the `type` strings of ai.json to node factories. Adding a new leaf or decorator is one `add()` call;
/// nothing else in the loader or the trees changes (the open/closed point for new monster behaviors).
class FAiNodeRegistry {
public:
    using FFactory = std::function<FNodeBuild(FNodeSpec&)>;

    struct FType {
        std::string name;
        int minChildren{0};
        int maxChildren{0};
        FFactory factory;
    };

    void add(std::string name, int minChildren, int maxChildren, FFactory factory);
    [[nodiscard]] const FType* find(std::string_view name) const;

    /// The node vocabulary that ships with the game.
    [[nodiscard]] static FAiNodeRegistry builtin();

private:
    std::vector<FType> types_;
};

} // namespace game::ai
