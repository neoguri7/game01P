#pragma once

#include "core/data/EContentFieldType.h"

#include <string_view>

namespace game {

/// One declared field of a content row: the key as spelled in the asset, its expected type, and
/// whether a row without it is a load error. Order of declared fields IS the projected row order.
struct FContentField {
    std::string_view key;
    EContentFieldType type = EContentFieldType::Text;
    bool required = true;
};

} // namespace game
