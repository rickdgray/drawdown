//=======================================================================
// Copyright Rick Gray 2026.
// Distributed under the MIT License.
// (See accompanying file LICENSE or copy at
//  http://opensource.org/licenses/MIT)
//
// Tiny flag parser. Supports long (--x) and short (-x) flags.
// Supports --x value, --x=value, -x value, and -x=value.
// Presence flags take no value.
//=======================================================================
#pragma once

#include <map>
#include <string>
#include <vector>
#include <optional>
#include <iosfwd>

namespace swr::cli {

enum class FlagGroup { REQUIRED, COMMON, ADVANCED };
enum class FlagKind  { VALUE, PRESENCE };

struct flag_spec {
    std::string name;          // long name without leading "--"
    std::string short_name;    // short name without leading "-" (empty = no short)
    FlagGroup   group;
    FlagKind    kind;
    std::string description;
    std::string default_value; // empty if no default; ignored for PRESENCE
};

struct command_schema {
    std::string             command_name;
    std::string             one_line_description;
    std::string             example_invocation; // shown at bottom of --help
    std::vector<flag_spec>  flags;
    // Pairs of flag names that may not appear together (e.g. {"json","csv"}).
    std::vector<std::pair<std::string, std::string>> mutually_exclusive;
};

// Parsed result. presence[flag] = true if the flag was given.
// values[flag] = the string value if a VALUE flag was given.
struct parsed_args {
    std::map<std::string, bool>        presence;
    std::map<std::string, std::string> values;
    bool                               help_requested = false;
};

// Parse argv-style args (already stripped of the command name).
// Throws std::runtime_error on validation failure (missing required,
// unknown flag, mutually-exclusive pair, etc).
parsed_args parse_flags(const std::vector<std::string>& args,
                        const command_schema& schema);

// Render the command's --help text to `out`. Groups by REQUIRED/COMMON/ADVANCED.
void render_help(std::ostream& out, const command_schema& schema);

// Convenience getters with default fallback.
std::string get_value(const parsed_args& p, const command_schema& schema,
                      const std::string& flag);
bool        get_presence(const parsed_args& p, const std::string& flag);

} // namespace swr::cli
