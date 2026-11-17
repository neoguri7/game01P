#pragma once

#include "core/data/EContentFieldType.h"

#include <string>
#include <variant>
#include <vector>

namespace game {

struct FContentRow; // stored by value in the last alternative; FContentRow.h includes this header (no cycle)

/// One validated content value, tagged with the type its schema declared. Produced only by
/// FContentLoader, so the active alternative always matches `type` (R22: validation happens once).
///
/// Nested objects share the last alternative (WP-26, no shared_ptr): `Object` always carries exactly
/// one `FContentRow`, `ObjectArray` carries one row per element (possibly none). The declared type
/// disambiguates; accessors go through FContentRow::object()/objectArray().
struct FContentValue {
    EContentFieldType type = EContentFieldType::Text;
    std::variant<std::string, double, bool, std::vector<std::string>, std::vector<double>,
                 std::vector<FContentRow>> value;

    [[nodiscard]] const std::string* asText() const { return std::get_if<std::string>(&value); }
    [[nodiscard]] const double* asNumber() const { return std::get_if<double>(&value); }
    [[nodiscard]] const bool* asBoolean() const { return std::get_if<bool>(&value); }
    [[nodiscard]] const std::vector<std::string>* asTextArray() const { return std::get_if<std::vector<std::string>>(&value); }
    [[nodiscard]] const std::vector<double>* asNumberArray() const { return std::get_if<std::vector<double>>(&value); }
    [[nodiscard]] const std::vector<FContentRow>* asRows() const { return std::get_if<std::vector<FContentRow>>(&value); }
};

} // namespace game
