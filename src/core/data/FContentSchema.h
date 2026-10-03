#pragma once

#include "core/data/FContentField.h"

#include <span>
#include <string_view>

namespace game {

/// Declares how one content asset becomes typed rows (R22). Field order is the projection order used
/// by FContentLoader, so derivations must keep their field enum in exactly this order (see keeping the
/// `invariant:` note in each content module).
struct FContentSchema {
    std::string_view assetId;  // boundary: logical asset id; only FAssetManager turns it into a path (R25)
    std::string_view rowKey;   // top-level array that holds the row objects
    std::string_view idField;  // must name a Text field: row identity for lookup and diagnostics
    int schemaVersion = 1;
    std::span<const FContentField> fields;
};

} // namespace game
