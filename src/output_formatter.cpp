//=======================================================================
// Copyright Rick Gray 2026.
// Distributed under the MIT License.
// (See accompanying file LICENSE or copy at
//  http://opensource.org/licenses/MIT)
//
// Unified text/JSON/CSV output formatter.
//=======================================================================
#include "output_formatter.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <cctype>

namespace swr::output {

namespace {

std::string format_with_commas(double v) {
    // Round to integer (we already use setprecision(0) for dollars),
    // then insert commas every 3 digits from the right.
    bool negative = v < 0;
    long long n = static_cast<long long>(std::round(std::abs(v)));
    std::string num = std::to_string(n);
    std::string out;
    int count = 0;
    for (auto it = num.rbegin(); it != num.rend(); ++it) {
        if (count > 0 && count % 3 == 0) out.push_back(',');
        out.push_back(*it);
        ++count;
    }
    std::reverse(out.begin(), out.end());
    return negative ? "-" + out : out;
}

std::string format_value_text(const field& f) {
    std::stringstream ss;
    ss << std::fixed;
    if (std::holds_alternative<std::monostate>(f.value)) return "";
    if (std::holds_alternative<bool>(f.value)) {
        return std::get<bool>(f.value) ? "yes" : "no";
    }
    if (std::holds_alternative<std::string>(f.value)) {
        return std::get<std::string>(f.value);
    }
    double v = 0.0;
    if (std::holds_alternative<double>(f.value))
        v = std::get<double>(f.value);
    else if (std::holds_alternative<int64_t>(f.value))
        v = static_cast<double>(std::get<int64_t>(f.value));

    switch (f.hint) {
        case field::Hint::DOLLARS:
            return "$" + format_with_commas(v);
        case field::Hint::PERCENT:
            // Use 2 decimals for sub-1% values (e.g. expense ratios like
            // 0.05%) where 1-decimal rounding would mislead. Otherwise 1
            // decimal is plenty (96.7%, 4.0%, etc).
            if (std::abs(v) < 1.0) {
                ss << std::setprecision(2) << v << "%";
            } else {
                ss << std::setprecision(1) << v << "%";
            }
            break;
        case field::Hint::INTEGER:
            ss << std::setprecision(0) << v;
            break;
        case field::Hint::YEARS:
            ss << std::setprecision(0) << v << " years";
            break;
        case field::Hint::NONE:
        default:
            ss << std::setprecision(2) << v;
            break;
    }
    return ss.str();
}

void emit_text(std::ostream& out, const report& r) {
    out << r.command_name << "\n";
    for (auto& sec : r.sections) {
        out << "\n" << sec.title << "\n";
        for (auto& f : sec.fields) {
            std::string left = "  " + f.label + ":";
            out << left;
            if (left.size() < 24) out << std::string(24 - left.size(), ' ');
            else out << " ";
            out << format_value_text(f) << "\n";
        }
    }
    if (!r.notes.empty()) {
        out << "\nNotes\n";
        for (auto& n : r.notes) out << "  " << n << "\n";
    }
}

std::string format_value_json(const field& f) {
    std::stringstream ss;
    ss << std::fixed;
    if (std::holds_alternative<std::monostate>(f.value)) return "null";
    if (std::holds_alternative<bool>(f.value)) {
        return std::get<bool>(f.value) ? "true" : "false";
    }
    if (std::holds_alternative<std::string>(f.value)) {
        const std::string& s = std::get<std::string>(f.value);
        std::stringstream es;
        es << "\"";
        for (char c : s) {
            switch (c) {
                case '"':  es << "\\\""; break;
                case '\\': es << "\\\\"; break;
                case '\n': es << "\\n";  break;
                default:   es << c;      break;
            }
        }
        es << "\"";
        return es.str();
    }
    if (std::holds_alternative<int64_t>(f.value)) {
        ss << std::get<int64_t>(f.value);
        return ss.str();
    }
    ss << std::setprecision(4) << std::get<double>(f.value);
    return ss.str();
}

std::string json_key(const std::string& label) {
    std::string out;
    out.reserve(label.size());
    for (char c : label) {
        if (c == ' ' || c == '-' || c == '/') out += '_';
        else if (c == '(' || c == ')' || c == ',' || c == ':') continue;
        else out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    std::string cleaned;
    bool prev_us = false;
    for (char c : out) {
        if (c == '_') {
            if (!prev_us) cleaned += c;
            prev_us = true;
        } else {
            cleaned += c;
            prev_us = false;
        }
    }
    if (!cleaned.empty() && cleaned.back() == '_') cleaned.pop_back();
    return cleaned;
}

void emit_json(std::ostream& out, const report& r) {
    out << "{\n";
    out << "  \"command\": \"" << r.command_name << "\"";
    for (auto& sec : r.sections) {
        out << ",\n  \"" << sec.name << "\": {";
        for (size_t i = 0; i < sec.fields.size(); ++i) {
            auto& f = sec.fields[i];
            if (i > 0) out << ",";
            out << "\n    \"" << json_key(f.label) << "\": "
                << format_value_json(f);
        }
        out << "\n  }";
    }
    if (!r.notes.empty()) {
        out << ",\n  \"notes\": [";
        for (size_t i = 0; i < r.notes.size(); ++i) {
            if (i > 0) out << ", ";
            out << "\"" << r.notes[i] << "\"";
        }
        out << "]";
    }
    out << "\n}\n";
}

std::string format_cell_csv(const csv_cell& c) {
    std::stringstream ss;
    ss << std::fixed;
    if (std::holds_alternative<std::monostate>(c)) return "";
    if (std::holds_alternative<bool>(c))
        return std::get<bool>(c) ? "1" : "0";
    if (std::holds_alternative<std::string>(c))
        return std::get<std::string>(c);
    if (std::holds_alternative<int64_t>(c)) {
        ss << std::get<int64_t>(c);
        return ss.str();
    }
    ss << std::setprecision(2) << std::get<double>(c);
    return ss.str();
}

void emit_csv(std::ostream& out, const report& r) {
    for (auto& c : r.csv_data.preamble_comments) out << "# " << c << "\n";
    for (size_t i = 0; i < r.csv_data.column_headers.size(); ++i) {
        if (i > 0) out << ",";
        out << r.csv_data.column_headers[i];
    }
    out << "\n";
    for (auto& row : r.csv_data.rows) {
        for (size_t i = 0; i < row.cells.size(); ++i) {
            if (i > 0) out << ",";
            out << format_cell_csv(row.cells[i]);
        }
        out << "\n";
    }
}

} // namespace

void emit(std::ostream& out, const report& r, Mode mode) {
    switch (mode) {
        case Mode::TEXT: emit_text(out, r); return;
        case Mode::JSON: emit_json(out, r); return;
        case Mode::CSV:  emit_csv(out, r); return;
    }
}

void emit_error(std::ostream& out, const std::string& command_name,
                const std::string& message, Mode mode) {
    switch (mode) {
        case Mode::JSON:
            out << "{ \"command\": \"" << command_name
                << "\", \"error\": \"" << message << "\" }\n";
            return;
        case Mode::CSV:
        case Mode::TEXT:
        default:
            out << "Error: " << message << "\n";
            return;
    }
}

} // namespace swr::output
