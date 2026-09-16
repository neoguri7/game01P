#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <string_view>
#include <utility>

namespace game {

/// One loaded, parsed asset document. Immutable after construction, so anyone holding it cannot change
/// what another system reads (R21: one owner, many readers). It owns the JSON type, which is why no
/// other header has to include nlohmann (R25: the container format stays behind the boundary).
class FAssetDigest {
public:
    FAssetDigest(std::string id, nlohmann::json document) : id_(std::move(id)), document_(std::move(document)) {}

    [[nodiscard]] std::string_view id() const { return id_; }
    [[nodiscard]] const nlohmann::json& json() const { return document_; }

private:
    std::string id_;
    nlohmann::json document_;
};

} // namespace game
