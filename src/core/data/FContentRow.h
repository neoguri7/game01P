#pragma once

#include "core/data/FContentValue.h"

#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace game {

/// One schema-projected content row: values[i] belongs to schema field i (FContentSchema::fields order).
/// Accessors return the caller's fallback when the slot is missing, so a decoder stays branch-free;
/// a wrong type cannot reach here because FContentLoader rejected it at load time.
struct FContentRow {
    std::vector<FContentValue> values;

    [[nodiscard]] const FContentValue* at(std::size_t index) const {
        return index < values.size() ? &values[index] : nullptr;
    }

    [[nodiscard]] std::string text(std::size_t index) const {
        const FContentValue* slot = at(index);
        const std::string* text = slot ? slot->asText() : nullptr;
        return text ? *text : std::string{};
    }

    [[nodiscard]] double number(std::size_t index, double fallback) const {
        const FContentValue* slot = at(index);
        const double* number = slot ? slot->asNumber() : nullptr;
        return number ? *number : fallback;
    }

    /// AP and range fields are authored as whole numbers, so the cast is exact for every value a data
    /// file may carry; a fractional value would be truncated rather than rejected (loader cannot tell
    /// an int field from a double field: both are declared `Number`).
    [[nodiscard]] int integer(std::size_t index, int fallback) const {
        return static_cast<int>(number(index, static_cast<double>(fallback)));
    }

    [[nodiscard]] bool boolean(std::size_t index, bool fallback) const {
        const FContentValue* slot = at(index);
        const bool* boolean = slot ? slot->asBoolean() : nullptr;
        return boolean ? *boolean : fallback;
    }

    [[nodiscard]] std::vector<std::string> textArray(std::size_t index) const {
        const FContentValue* slot = at(index);
        const std::vector<std::string>* array = slot ? slot->asTextArray() : nullptr;
        return array ? *array : std::vector<std::string>{};
    }

    /// Nested accessors (WP-26): `object` yields the single row of an Object slot (nullptr when the
    /// slot is absent or carries no rows), `objectArray` yields the rows of an ObjectArray slot (empty
    /// span otherwise). The loader guarantees the declared type on every present slot.
    [[nodiscard]] const FContentRow* object(std::size_t index) const;
    [[nodiscard]] std::span<const FContentRow> objectArray(std::size_t index) const;
};

// Defined after the struct: using the stored `std::vector<FContentRow>` needs FContentRow complete,
// and the class is complete only once its definition has closed.
inline const FContentRow* FContentRow::object(std::size_t index) const {
    const FContentValue* slot = at(index);
    const std::vector<FContentRow>* rows = slot ? slot->asRows() : nullptr;
    return (rows && !rows->empty()) ? &rows->front() : nullptr;
}

inline std::span<const FContentRow> FContentRow::objectArray(std::size_t index) const {
    const FContentValue* slot = at(index);
    const std::vector<FContentRow>* rows = slot ? slot->asRows() : nullptr;
    return rows ? std::span<const FContentRow>{*rows} : std::span<const FContentRow>{};
}

} // namespace game
