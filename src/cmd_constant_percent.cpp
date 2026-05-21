//=======================================================================
// Copyright Rick Gray 2026.
// Distributed under the MIT License.
// (See accompanying file LICENSE or copy at
//  http://opensource.org/licenses/MIT)
//
// `constant_percent` command: fixed-percent-of-balance withdrawal evaluator.
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

swr::cli::command_schema constant_percent_schema() {
    using namespace swr::cli;
    command_schema s;
    s.command_name = "constant-percent";
    s.one_line_description =
        "Evaluate the success rate of withdrawing a fixed percent of the\n"
        "  current portfolio balance each year (no inflation adjustment).";
    s.example_invocation =
        "drawdown constant-percent -pct 4 -p \"us_stocks:60;us_bonds:40;\" -y 30 -r yearly\n"
        "  drawdown constant-percent --percent 4 --portfolio \"us_stocks:60;us_bonds:40;\" \\\n"
        "    --inflation us_inflation --years 30 --rebalance yearly";

    s.flags = {
        {"percent",            "pct", FlagGroup::REQUIRED, FlagKind::VALUE,    "Percent of current balance withdrawn each year", ""},
        {"portfolio",          "p",   FlagGroup::REQUIRED, FlagKind::VALUE,    "Portfolio spec, e.g. \"us_stocks:60;us_bonds:40;\"", ""},
        {"years",              "y",   FlagGroup::REQUIRED, FlagKind::VALUE,    "Horizon length (years)", ""},

        {"inflation",          "i",   FlagGroup::COMMON,   FlagKind::VALUE,    "Inflation series", "us_inflation"},
        {"rebalance",          "r",   FlagGroup::COMMON,   FlagKind::VALUE,    "none | monthly | yearly | threshold", "none"},
        {"start-year",         "sy",  FlagGroup::COMMON,   FlagKind::VALUE,    "Earliest historical backtest start year (0 = full data)", "0"},
        {"end-year",           "ey",  FlagGroup::COMMON,   FlagKind::VALUE,    "Latest historical backtest start year (0 = full data)", "0"},
        {"initial-value",      "iv",  FlagGroup::COMMON,   FlagKind::VALUE,    "Starting portfolio value (dollars)", "1000"},
        {"minimum-floor",      "mf",  FlagGroup::COMMON,   FlagKind::VALUE,    "Minimum annual spending floor as percent of initial", "3.0"},
        {"json",               "j",   FlagGroup::COMMON,   FlagKind::PRESENCE, "Emit JSON output", ""},
        {"csv",                "c",   FlagGroup::COMMON,   FlagKind::PRESENCE, "Emit CSV per-path output", ""},

        {"withdraw-frequency", "wf",  FlagGroup::ADVANCED, FlagKind::VALUE,    "12 = yearly, 1 = monthly", "12"},
        {"fees",               "f",   FlagGroup::ADVANCED, FlagKind::VALUE,    "TER as fraction", "0.001"},
    };
    s.mutually_exclusive.push_back({"json", "csv"});
    return s;
}

} // namespace

int constant_percent(const std::vector<std::string>& args) {
    auto schema = constant_percent_schema();
    swr::output::Mode mode = swr::output::Mode::TEXT;

    swr::cli::parsed_args p;
    try {
        p = swr::cli::parse_flags(args, schema);
    } catch (const std::exception& e) {
        for (auto& a : args) {
            if (a == "--json" || a == "-j") mode = swr::output::Mode::JSON;
            else if (a == "--csv" || a == "-c") mode = swr::output::Mode::CSV;
        }
        swr::output::emit_error(std::cerr, "constant-percent", e.what(), mode);
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
        sc.wr       = std::stof(swr::cli::get_value(p, schema, "percent"));
        sc.rebalance = swr::parse_rebalance(swr::cli::get_value(p, schema, "rebalance"));
        sc.initial_value = std::stof(swr::cli::get_value(p, schema, "initial-value"));
        sc.withdraw_frequency = static_cast<size_t>(std::stoul(swr::cli::get_value(p, schema, "withdraw-frequency")));
        sc.fees = std::stof(swr::cli::get_value(p, schema, "fees"));
        sc.wmethod = swr::WithdrawalMethod::CURRENT;
        sc.minimum = std::stof(swr::cli::get_value(p, schema, "minimum-floor")) / 100.0f;

        std::string inflation = swr::cli::get_value(p, schema, "inflation");
        sc.values         = swr::load_values(sc.portfolio);
        sc.inflation_data = swr::load_inflation(sc.values, inflation);

        size_t sy = static_cast<size_t>(std::stoul(swr::cli::get_value(p, schema, "start-year")));
        size_t ey = static_cast<size_t>(std::stoul(swr::cli::get_value(p, schema, "end-year")));
        sc.start_year = sy > 0 ? sy : sc.inflation_data.front().year;
        sc.end_year   = ey > 0 ? ey : sc.inflation_data.back().year;

    } catch (const std::exception& e) {
        swr::output::emit_error(std::cerr, "constant-percent",
            std::string("flag parse error: ") + e.what(), mode);
        return 1;
    }

    auto res = swr::simulation(sc);
    if (res.error) {
        swr::output::emit_error(std::cerr, "constant-percent", res.message, mode);
        return 1;
    }

    swr::output::report rep;
    rep.command_name = "constant-percent";
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
        sec.fields.push_back({"Withdrawal pct", (double)sc.wr, swr::output::field::Hint::PERCENT});
        sec.fields.push_back({"Minimum floor", (double)(sc.minimum * 100.0f), swr::output::field::Hint::PERCENT});
        sec.fields.push_back({"Rebalance", swr::cli::get_value(p, schema, "rebalance"),
                              swr::output::field::Hint::NONE});
        std::string freq_label = (sc.withdraw_frequency == 12) ? "yearly"
                               : (sc.withdraw_frequency == 1)  ? "monthly"
                               : "every " + std::to_string(sc.withdraw_frequency) + " months";
        sec.fields.push_back({"Withdraw frequency", freq_label,
                              swr::output::field::Hint::NONE});
        sec.fields.push_back({"Fees (TER)", (double)(sc.fees * 100.0f), swr::output::field::Hint::PERCENT});
        rep.sections.push_back(sec);
    }
    {
        swr::output::section sec;
        sec.name  = "results";
        sec.title = "Results";
        sec.fields.push_back({"Success rate", (double)res.success_rate, swr::output::field::Hint::PERCENT});
        sec.fields.push_back({"Spending (avg)", (double)res.spending_average, swr::output::field::Hint::DOLLARS});
        sec.fields.push_back({"Spending (median)", (double)res.spending_median, swr::output::field::Hint::DOLLARS});
        sec.fields.push_back({"Spending (min)", (double)res.spending_minimum, swr::output::field::Hint::DOLLARS});
        sec.fields.push_back({"Spending (max)", (double)res.spending_maximum, swr::output::field::Hint::DOLLARS});
        sec.fields.push_back({"Terminal value (avg)", (double)res.tv_average, swr::output::field::Hint::DOLLARS});
        sec.fields.push_back({"Terminal value (median)", (double)res.tv_median, swr::output::field::Hint::DOLLARS});
        sec.fields.push_back({"Terminal value (min)", (double)res.tv_minimum, swr::output::field::Hint::DOLLARS});
        rep.sections.push_back(sec);
    }

    if (mode == swr::output::Mode::CSV) {
        rep.csv_data.column_headers = {"start_year", "start_month", "success",
            "terminal_value", "total_withdrawn", "worst_duration_months"};
        rep.csv_data.preamble_comments.push_back("command: constant-percent");
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
