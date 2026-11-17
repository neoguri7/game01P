#pragma once

#include <string_view>
#include <utility>
#include <vector>

namespace game {

/// Ordered content rows with id lookup. Linear scan on purpose: asset tables are small, and a hash
/// container would make iteration order (and therefore any seed-free behaviour) implementation-defined (R16).
template <typename TRow>
struct FContentTable {
    std::vector<TRow> rows;

    [[nodiscard]] const TRow* find(std::string_view id) const {
        for (const TRow& row : rows) {
            if (row.id == id) {
                return &row;
            }
        }
        return nullptr;
    }
};

} // namespace game
