//=======================================================================
// Copyright Rick Gray 2026.
// Distributed under the MIT License.
// (See accompanying file LICENSE or copy at
//  http://opensource.org/licenses/MIT)
//
// `constant_dollar` command: fixed real-dollar withdrawal rate evaluator.
//=======================================================================
#include "commands.hpp"

#include <iostream>
#include <string>

#include "cli.hpp"
#include "data.hpp"
#include "output_formatter.hpp"
#include "portfolio.hpp"
#include "simulation.hpp"

namespace swr::cmd {

namespace {

swr::cli::command_schema constant_dollar_schema() {
    using namespace swr::cli;
    command_schema s;
    s.command_name = "constant_dollar";
    s.one_line_description =
        "Evaluate the success rate of a fixed real-dollar (inflation-adjusted) "
        "withdrawal\n  rate over a portfolio and horizon.";
    s.example_invocation =
        "drawdown constant_dollar --wr 4 --portfolio \"us_stocks:60;us_bonds:40;\" \\\n"
        "    --inflation us_inflation --years 30 --rebalance yearly";

    s.flags = {
        {"wr",                 FlagGroup::REQUIRED, FlagKind::VALUE,    "Withdrawal rate (percent of initial portfolio)", ""},
        {"portfolio",          FlagGroup::REQUIRED, FlagKind::VALUE,    "Portfolio spec, e.g. \"us_stocks:60;us_bonds:40;\"", ""},
        {"years",              FlagGroup::REQUIRED, FlagKind::VALUE,    "Horizon length (years)", ""},

        {"inflation",          FlagGroup::COMMON,   FlagKind::VALUE,    "Inflation series", "us_inflation"},
        {"rebalance",          FlagGroup::COMMON,   FlagKind::VALUE,    "none | monthly | yearly | threshold", "none"},
        {"start_year",         FlagGroup::COMMON,   FlagKind::VALUE,    "Earliest historical backtest start year (0 = full data)", "0"},
        {"end_year",           FlagGroup::COMMON,   FlagKind::VALUE,    "Latest historical backtest start year (0 = full data)", "0"},
        {"initial_value",      FlagGroup::COMMON,   FlagKind::VALUE,    "Starting portfolio value (dollars)", "1000"},
        {"target_success",     FlagGroup::COMMON,   FlagKind::VALUE,    "If > 0, emit pass/fail vs this target (percent)", "0"},
        {"json",               FlagGroup::COMMON,   FlagKind::PRESENCE, "Emit JSON output", ""},
        {"csv",                FlagGroup::COMMON,   FlagKind::PRESENCE, "Emit CSV per-path output", ""},

        {"withdraw_frequency", FlagGroup::ADVANCED, FlagKind::VALUE,    "12 = yearly, 1 = monthly", "12"},
        {"fees",               FlagGroup::ADVANCED, FlagKind::VALUE,    "TER as fraction", "0.001"},
    };
    s.mutually_exclusive.push_back({"json", "csv"});
    return s;
}

} // namespace

int constant_dollar(const std::vector<std::string>& args) {
    auto schema = constant_dollar_schema();
    swr::output::Mode mode = swr::output::Mode::TEXT;

    swr::cli::parsed_args p;
    try {
        p = swr::cli::parse_flags(args, schema);
    } catch (const std::exception& e) {
        for (auto& a : args) {
            if (a == "--json") mode = swr::output::Mode::JSON;
            else if (a == "--csv") mode = swr::output::Mode::CSV;
        }
        swr::output::emit_error(std::cerr, "constant_dollar", e.what(), mode);
        return 1;
    }

    if (p.help_requested) {
        swr::cli::render_help(std::cout, schema);
        return 0;
    }

    if (swr::cli::get_presence(p, "json")) mode = swr::output::Mode::JSON;
    if (swr::cli::get_presence(p, "csv"))  mode = swr::output::Mode::CSV;

    swr::scenario sc;
    try {
        sc.portfolio = swr::parse_portfolio(swr::cli::get_value(p, schema, "portfolio"), false);
        swr::normalize_portfolio(sc.portfolio);
        sc.years    = static_cast<size_t>(std::stoul(swr::cli::get_value(p, schema, "years")));
        sc.wr       = std::stof(swr::cli::get_value(p, schema, "wr"));
        sc.rebalance = swr::parse_rebalance(swr::cli::get_value(p, schema, "rebalance"));
        sc.initial_value = std::stof(swr::cli::get_value(p, schema, "initial_value"));
        sc.withdraw_frequency = static_cast<size_t>(std::stoul(swr::cli::get_value(p, schema, "withdraw_frequency")));
        sc.fees = std::stof(swr::cli::get_value(p, schema, "fees"));
        sc.wmethod = swr::WithdrawalMethod::STANDARD;

        std::string inflation = swr::cli::get_value(p, schema, "inflation");
        sc.values         = swr::load_values(sc.portfolio);
        sc.inflation_data = swr::load_inflation(sc.values, inflation);

        size_t sy = static_cast<size_t>(std::stoul(swr::cli::get_value(p, schema, "start_year")));
        size_t ey = static_cast<size_t>(std::stoul(swr::cli::get_value(p, schema, "end_year")));
        sc.start_year = sy > 0 ? sy : sc.inflation_data.front().year;
        sc.end_year   = ey > 0 ? ey : sc.inflation_data.back().year;

    } catch (const std::exception& e) {
        swr::output::emit_error(std::cerr, "constant_dollar",
            std::string("flag parse error: ") + e.what(), mode);
        return 1;
    }

    auto res = swr::simulation(sc);
    if (res.error) {
        swr::output::emit_error(std::cerr, "constant_dollar", res.message, mode);
        return 1;
    }

    float target_success = std::stof(swr::cli::get_value(p, schema, "target_success"));

    // Build report
    swr::output::report rep;
    rep.command_name = "constant_dollar";
    {
        swr::output::section sec;
        sec.name  = "inputs";
        sec.title = "Inputs";
        sec.fields.push_back({"Portfolio", swr::cli::get_value(p, schema, "portfolio"),
                              swr::output::field::Hint::NONE});
        sec.fields.push_back({"Inflation series", swr::cli::get_value(p, schema, "inflation"),
                              swr::output::field::Hint::NONE});
        sec.fields.push_back({"Horizon", (int64_t)sc.years, swr::output::field::Hint::YEARS});
        sec.fields.push_back({"Initial value", (double)sc.initial_value, swr::output::field::Hint::DOLLARS});
        sec.fields.push_back({"Withdrawal rate", (double)sc.wr, swr::output::field::Hint::PERCENT});
        sec.fields.push_back({"Rebalance", swr::cli::get_value(p, schema, "rebalance"),
                              swr::output::field::Hint::NONE});
        sec.fields.push_back({"Withdraw frequency", (int64_t)sc.withdraw_frequency, swr::output::field::Hint::INTEGER});
        sec.fields.push_back({"Fees (TER)", (double)(sc.fees * 100.0f), swr::output::field::Hint::PERCENT});
        rep.sections.push_back(sec);
    }
    {
        swr::output::section sec;
        sec.name  = "results";
        sec.title = "Results";
        sec.fields.push_back({"Success rate", (double)res.success_rate, swr::output::field::Hint::PERCENT});
        sec.fields.push_back({"Successes", (int64_t)res.successes, swr::output::field::Hint::INTEGER});
        sec.fields.push_back({"Failures", (int64_t)res.failures, swr::output::field::Hint::INTEGER});
        sec.fields.push_back({"Terminal value (avg)", (double)res.tv_average, swr::output::field::Hint::DOLLARS});
        sec.fields.push_back({"Terminal value (median)", (double)res.tv_median, swr::output::field::Hint::DOLLARS});
        sec.fields.push_back({"Terminal value (min)", (double)res.tv_minimum, swr::output::field::Hint::DOLLARS});
        sec.fields.push_back({"Terminal value (max)", (double)res.tv_maximum, swr::output::field::Hint::DOLLARS});
        sec.fields.push_back({"Total withdrawn per path", (double)res.total_withdrawn, swr::output::field::Hint::DOLLARS});
        if (res.worst_duration > 0) {
            sec.fields.push_back({"Worst duration (months)", (int64_t)res.worst_duration, swr::output::field::Hint::INTEGER});
            sec.fields.push_back({"Worst start year", (int64_t)res.worst_starting_year, swr::output::field::Hint::INTEGER});
        }
        rep.sections.push_back(sec);
    }

    if (target_success > 0.0f) {
        if (res.success_rate >= target_success) {
            rep.notes.push_back("PASS: success rate meets target (" +
                std::to_string(target_success) + "%)");
        } else {
            rep.notes.push_back("FAIL: success rate below target (" +
                std::to_string(target_success) + "%)");
        }
    }

    if (mode == swr::output::Mode::CSV) {
        rep.csv_data.column_headers = {"start_year", "start_month", "success",
            "terminal_value", "total_withdrawn", "worst_duration_months"};
        rep.csv_data.preamble_comments.push_back("command: constant_dollar");
        // Reconstruct per-path rows. Use terminal_values vector as parallel to start years.
        size_t idx = 0;
        for (size_t y = sc.start_year;
             y + sc.years <= sc.end_year && idx < res.terminal_values.size();
             ++y) {
            swr::output::csv_row row;
            row.cells.push_back((int64_t)y);
            row.cells.push_back((int64_t)1);
            float tv = res.terminal_values[idx];
            row.cells.push_back(tv > 0.0f);
            row.cells.push_back((double)tv);
            row.cells.push_back((double)res.total_withdrawn);
            row.cells.push_back(std::monostate{});
            rep.csv_data.rows.push_back(row);
            ++idx;
        }
    }

    swr::output::emit(std::cout, rep, mode);
    return 0;
}

} // namespace swr::cmd
