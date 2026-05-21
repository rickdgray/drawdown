//=======================================================================
// Copyright Rick Gray 2026.
// Distributed under the MIT License.
// (See accompanying file LICENSE or copy at
//  http://opensource.org/licenses/MIT)
//
// Tiny flag parser implementation.
//=======================================================================
#include "cli.hpp"

#include <cctype>
#include <map>
#include <ostream>
#include <stdexcept>

namespace swr::cli {

namespace {

// Scan a value for `-<flag>` substrings that look like the user forgot a space.
// Returns the matched flag name (without leading dash) if a known flag is
// found embedded, or empty string otherwise.
std::string find_concatenated_flag(
    const std::string& value,
    const std::map<std::string, const flag_spec*>& by_name,
    const std::map<std::string, const flag_spec*>& by_short)
{
    // Only look mid-value (skip position 0 to allow negative numbers as values).
    for (size_t i = 1; i < value.size(); ++i) {
        if (value[i] != '-') continue;
        size_t j = i + 1;
        while (j < value.size() &&
               (std::isalnum(static_cast<unsigned char>(value[j])) ||
                value[j] == '-' || value[j] == '_')) {
            ++j;
        }
        if (j == i + 1) continue;
        std::string candidate = value.substr(i + 1, j - i - 1);
        if (by_short.count(candidate) || by_name.count(candidate)) {
            return candidate;
        }
    }
    return "";
}

} // namespace


parsed_args parse_flags(const std::vector<std::string>& args,
                        const command_schema& schema) {
    parsed_args result;

    // Build quick lookups: by long name and by short name.
    std::map<std::string, const flag_spec*> by_name;
    std::map<std::string, const flag_spec*> by_short;
    for (auto& f : schema.flags) {
        by_name[f.name] = &f;
        if (!f.short_name.empty()) by_short[f.short_name] = &f;
    }

    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];

        if (a == "--help") {
            result.help_requested = true;
            continue;
        }

        const flag_spec* spec = nullptr;
        std::optional<std::string> inline_value;
        std::string long_name; // canonical long name, used as key in result maps

        if (a.rfind("--", 0) == 0) {
            // Long flag: --name or --name=value
            std::string body = a.substr(2);
            auto eq_pos = body.find('=');
            if (eq_pos != std::string::npos) {
                long_name    = body.substr(0, eq_pos);
                inline_value = body.substr(eq_pos + 1);
            } else {
                long_name = body;
            }
            auto it = by_name.find(long_name);
            if (it == by_name.end()) {
                throw std::runtime_error("unknown flag: --" + long_name);
            }
            spec = it->second;
        } else if (a.rfind("-", 0) == 0 && a.size() > 1) {
            // Short flag: -x or -x=value
            std::string body = a.substr(1);
            auto eq_pos = body.find('=');
            std::string short_key;
            if (eq_pos != std::string::npos) {
                short_key    = body.substr(0, eq_pos);
                inline_value = body.substr(eq_pos + 1);
            } else {
                short_key = body;
            }
            auto it = by_short.find(short_key);
            if (it == by_short.end()) {
                throw std::runtime_error("unknown flag: -" + short_key);
            }
            spec      = it->second;
            long_name = spec->name; // key by long name
        } else {
            throw std::runtime_error(
                "unexpected positional arg: '" + a + "'. "
                "Each flag and its value are separate tokens. "
                "Did you forget a space?");
        }

        if (spec->kind == FlagKind::PRESENCE) {
            if (inline_value.has_value()) {
                throw std::runtime_error("flag --" + long_name +
                                         " does not take a value");
            }
            result.presence[long_name] = true;
        } else {
            std::string value;
            if (inline_value.has_value()) {
                value = *inline_value;
            } else {
                if (i + 1 >= args.size()) {
                    throw std::runtime_error("flag --" + long_name +
                                             " requires a value");
                }
                value = args[++i];
            }
            // Detect cases like `-si 30000-sa` where the user forgot a space
            // and the value swallowed what looks like another flag.
            std::string concat = find_concatenated_flag(value, by_name, by_short);
            if (!concat.empty()) {
                throw std::runtime_error(
                    "flag --" + long_name + " value '" + value +
                    "' appears to contain the flag '-" + concat +
                    "'. Did you forget a space between values?");
            }
            result.values[long_name]   = value;
            result.presence[long_name] = true;
        }
    }

    // Required-flag check (skipped when --help requested).
    if (!result.help_requested) {
        for (auto& f : schema.flags) {
            if (f.group == FlagGroup::REQUIRED &&
                result.presence.find(f.name) == result.presence.end()) {
                throw std::runtime_error("missing required flag: --" + f.name);
            }
        }
    }

    // Mutually-exclusive pairs.
    for (auto& [a_name, b_name] : schema.mutually_exclusive) {
        if (get_presence(result, a_name) && get_presence(result, b_name)) {
            throw std::runtime_error("flags --" + a_name + " and --" +
                                     b_name + " are mutually exclusive");
        }
    }

    return result;
}

void render_help(std::ostream& out, const command_schema& schema) {
    out << "Usage: drawdown " << schema.command_name << " [OPTIONS]\n";
    if (!schema.one_line_description.empty()) {
        out << "\n  " << schema.one_line_description << "\n";
    }

    auto emit_group = [&](FlagGroup g, const char* label) {
        bool first = true;
        for (auto& f : schema.flags) {
            if (f.group != g) continue;
            if (f.name == "help") continue;
            if (first) {
                out << "\n" << label << ":\n";
                first = false;
            }
            // Build left column: "-short, --long <value>" or "      --long <value>"
            std::string left;
            if (!f.short_name.empty()) {
                left = "  -" + f.short_name + ",  --" + f.name;
            } else {
                // No short name so indent same as if short were present but blank
                left = "       --" + f.name;
            }
            if (f.kind == FlagKind::VALUE) left += " <value>";
            out << left;
            // pad to column 36
            const size_t col = 36;
            if (left.size() < col) {
                out << std::string(col - left.size(), ' ');
            } else {
                out << " ";
            }
            out << f.description;
            if (!f.default_value.empty() && f.kind == FlagKind::VALUE) {
                out << " (default: " << f.default_value << ")";
            }
            out << "\n";
        }
    };

    emit_group(FlagGroup::REQUIRED, "Required");
    emit_group(FlagGroup::COMMON,   "Common");
    emit_group(FlagGroup::ADVANCED, "Advanced");

    if (!schema.example_invocation.empty()) {
        out << "\nExample:\n  " << schema.example_invocation << "\n";
    }
}

std::string get_value(const parsed_args& p, const command_schema& schema,
                      const std::string& flag) {
    auto it = p.values.find(flag);
    if (it != p.values.end()) return it->second;
    for (auto& f : schema.flags) {
        if (f.name == flag) return f.default_value;
    }
    return "";
}

bool get_presence(const parsed_args& p, const std::string& flag) {
    auto it = p.presence.find(flag);
    return it != p.presence.end() && it->second;
}

} // namespace swr::cli
