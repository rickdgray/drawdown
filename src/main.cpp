//=======================================================================
// Copyright Baptiste Wicht 2019-2024.
// Copyright Rick Gray 2026.
// Distributed under the MIT License.
// (See accompanying file LICENSE or copy at
//  http://opensource.org/licenses/MIT)
//=======================================================================
#include <iostream>
#include <string>
#include <vector>

#include "commands.hpp"

namespace {

std::vector<std::string> parse_args(int argc, const char* argv[]) {
    std::vector<std::string> args;

    for (int i = 0; i < argc - 1; ++i) {
        args.emplace_back(argv[i + 1]);
    }

    return args;
}

} // namespace

int main(int argc, const char* argv[]) {
    auto args = parse_args(argc, argv);

    if (args.empty()) {
        std::cout << "Usage: drawdown <command> [options]\n"
                  << "Commands:\n"
                  << "  constant_dollar       Evaluate a fixed-WR scenario\n"
                  << "  constant_percent      Fixed % of current balance each year\n"
                  << "  dynamic_dollar        Per-year sustainable withdrawal\n"
                  << "\nRun '<command> --help' for command-specific options.\n";
        return 1;
    }

    const auto& command = args[0];
    std::vector<std::string> sub_args(args.begin() + 1, args.end());

    if (command == "dynamic_dollar")        return swr::cmd::dynamic_dollar(sub_args);
    else if (command == "constant_dollar")  return swr::cmd::constant_dollar(sub_args);
    else if (command == "constant_percent") return swr::cmd::constant_percent(sub_args);

    std::cout << "Unhandled command \"" << command << "\"\n";
    return 1;
}
