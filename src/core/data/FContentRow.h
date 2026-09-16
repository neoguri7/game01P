#pragma once

#include "core/data/FContentValue.h"

#include <cstddef>
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
};

} // namespace game
