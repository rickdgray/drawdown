//=======================================================================
// Copyright Rick Gray 2026.
// Distributed under the MIT License.
// (See accompanying file LICENSE or copy at
//  http://opensource.org/licenses/MIT)
//
// Unified text/JSON/CSV output for drawdown commands.
//=======================================================================
#pragma once

#include <iosfwd>
#include <string>
#include <vector>
#include <variant>
#include <cstdint>

namespace swr::output {

enum class Mode { TEXT, JSON, CSV };

// A field is a (label, value) pair where value is one of several types.
// The renderer formats appropriately per mode.
struct field {
    std::string label;
    std::variant<std::monostate, double, int64_t, std::string, bool> value;
    // Optional render hints (used in text mode mostly):
    enum class Hint { NONE, DOLLARS, PERCENT, INTEGER, YEARS } hint = Hint::NONE;

    // Convenience constructors
    field(std::string lbl, double v, Hint h = Hint::NONE)
        : label(std::move(lbl)), value(v), hint(h) {}
    field(std::string lbl, int64_t v, Hint h = Hint::NONE)
        : label(std::move(lbl)), value(v), hint(h) {}
    field(std::string lbl, std::string v, Hint h = Hint::NONE)
        : label(std::move(lbl)), value(std::move(v)), hint(h) {}
    field(std::string lbl, bool v, Hint h = Hint::NONE)
        : label(std::move(lbl)), value(v), hint(h) {}
    field() = default;
};

// Section: a labeled list of fields (used for inputs and results).
struct section {
    std::string         name;   // "inputs", "results" — used as JSON key
    std::string         title;  // "Inputs", "Results" — used in text mode
    std::vector<field>  fields;
};

// CSV row data: each row is a list of typed cells.
using csv_cell = std::variant<std::monostate, double, int64_t, std::string, bool>;
struct csv_row {
    std::vector<csv_cell> cells;
};

struct csv_block {
    std::vector<std::string>      column_headers;
    std::vector<std::string>      preamble_comments; // lines starting with "# "
    std::vector<csv_row>          rows;
};

struct report {
    std::string              command_name;
    std::vector<section>     sections;            // typically [inputs, results]
    std::vector<std::string> notes;               // optional
    csv_block                csv_data;            // only used when mode == CSV
};

void emit(std::ostream& out, const report& r, Mode mode);

// Emit an error report (terse), routed to the same mode-aware path.
void emit_error(std::ostream& out, const std::string& command_name,
                const std::string& message, Mode mode);

} // namespace swr::output
