//=======================================================================
// Copyright Rick Gray 2026.
// Distributed under the MIT License.
// (See accompanying file LICENSE or copy at
//  http://opensource.org/licenses/MIT)
//
// Tiny flag parser implementation.
//=======================================================================
#include "cli.hpp"

#include <map>
#include <ostream>
#include <stdexcept>

namespace swr::cli {

parsed_args parse_flags(const std::vector<std::string>& args,
                        const command_schema& schema) {
    parsed_args result;

    // Build a quick lookup of flag specs by name.
    std::map<std::string, const flag_spec*> by_name;
    for (auto& f : schema.flags) by_name[f.name] = &f;

    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];

        if (a == "--help") {
            result.help_requested = true;
            continue;
        }

        if (a.rfind("--", 0) != 0) {
            throw std::runtime_error("unexpected positional arg: " + a);
        }

        std::string name;
        std::optional<std::string> inline_value;
        std::string body = a.substr(2);
        auto eq_pos = body.find('=');
        if (eq_pos != std::string::npos) {
            name = body.substr(0, eq_pos);
            inline_value = body.substr(eq_pos + 1);
        } else {
            name = body;
        }

        auto it = by_name.find(name);
        if (it == by_name.end()) {
            throw std::runtime_error("unknown flag: --" + name);
        }

        const flag_spec* spec = it->second;
        if (spec->kind == FlagKind::PRESENCE) {
            if (inline_value.has_value()) {
                throw std::runtime_error("flag --" + name +
                                         " does not take a value");
            }
            result.presence[name] = true;
        } else {
            std::string value;
            if (inline_value.has_value()) {
                value = *inline_value;
            } else {
                if (i + 1 >= args.size()) {
                    throw std::runtime_error("flag --" + name +
                                             " requires a value");
                }
                value = args[++i];
            }
            result.values[name] = value;
            result.presence[name] = true;
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
    out << "Usage: swr_calculator " << schema.command_name << " [OPTIONS]\n";
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
            // Two-column: name+kind on left, description on right.
            std::string left = "  --" + f.name;
            if (f.kind == FlagKind::VALUE) left += " <value>";
            out << left;
            // pad to column 24
            if (left.size() < 24) {
                out << std::string(24 - left.size(), ' ');
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
