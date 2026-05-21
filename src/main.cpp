//=======================================================================G
// Copyright Baptiste Wicht 2019-2024.
// Copyright Rick Gray 2026.
// Distributed under the MIT License.
// (See accompanying file LICENSE or copy at
//  http://opensource.org/licenses/MIT)
//=======================================================================

#include <cmath>
#include <format>
#include <string>
#include <iostream>
#include <string_view>
#include <sstream>

#include "data.hpp"
#include "portfolio.hpp"
#include "simulation.hpp"
#include "cli.hpp"
#include "dynamic.hpp"
#include "output_formatter.hpp"

namespace {

std::vector<std::string> parse_args(int argc, const char* argv[]) {
    std::vector<std::string> args;

    for (int i = 0; i < argc - 1; ++i) {
        args.emplace_back(argv[i + 1]);
    }

    return args;
}

swr::cli::command_schema dynamic_dollar_schema() {
    using namespace swr::cli;
    command_schema s;
    s.command_name = "dynamic_dollar";
    s.one_line_description =
        "Compute this year's sustainable withdrawal given current portfolio "
        "reality,\n  using historical backtesting over the remaining horizon.";
    s.example_invocation =
        "drawdown dynamic_dollar --balance 850000 --current_age 67 "
        "--end_age 92 \\\n    --portfolio \"us_stocks:60;us_bonds:40;\" "
        "--inflation us_inflation";

    s.flags = {
        {"balance",            FlagGroup::REQUIRED, FlagKind::VALUE,    "Current portfolio balance (dollars)", ""},
        {"current_age",        FlagGroup::REQUIRED, FlagKind::VALUE,    "Your current age (float allowed)", ""},
        {"end_age",            FlagGroup::REQUIRED, FlagKind::VALUE,    "Planning end age (integer)", ""},
        {"portfolio",          FlagGroup::REQUIRED, FlagKind::VALUE,    "Portfolio spec, e.g. \"us_stocks:60;us_bonds:40;\"", ""},

        {"inflation",          FlagGroup::COMMON,   FlagKind::VALUE,    "Inflation series, e.g. \"us_inflation\"", "us_inflation"},
        {"target_success",     FlagGroup::COMMON,   FlagKind::VALUE,    "Target success rate (percent)", "80"},
        {"rebalance",          FlagGroup::COMMON,   FlagKind::VALUE,    "none | monthly | yearly | threshold", "none"},
        {"ssa_income",         FlagGroup::COMMON,   FlagKind::VALUE,    "Annual SSA income (dollars; 0 = disabled)", "0"},
        {"ssa_start_age",      FlagGroup::COMMON,   FlagKind::VALUE,    "Age SSA begins (required if --ssa_income > 0)", "0"},
        {"smoothing",          FlagGroup::COMMON,   FlagKind::VALUE,    "Max YoY change as fraction, e.g. 0.10 (0 = disabled)", "0"},
        {"prior_amount",       FlagGroup::COMMON,   FlagKind::VALUE,    "Last year's spending budget (also enables signal)", "0"},
        {"json",               FlagGroup::COMMON,   FlagKind::PRESENCE, "Emit JSON output", ""},
        {"csv",                FlagGroup::COMMON,   FlagKind::PRESENCE, "Emit CSV per-path output", ""},

        {"start_year",         FlagGroup::ADVANCED, FlagKind::VALUE,    "Earliest historical backtest start year (default: full data)", "0"},
        {"end_year",           FlagGroup::ADVANCED, FlagKind::VALUE,    "Latest historical backtest start year (default: full data)", "0"},
        {"withdraw_frequency", FlagGroup::ADVANCED, FlagKind::VALUE,    "12 = yearly, 1 = monthly", "12"},
        {"fees",               FlagGroup::ADVANCED, FlagKind::VALUE,    "TER as fraction", "0.001"},
        {"solver_tolerance",   FlagGroup::ADVANCED, FlagKind::VALUE,    "Binary-search stopping tolerance (dollars)", "1"},
    };
    s.mutually_exclusive.push_back({"json", "csv"});
    return s;
}

int dynamic_dollar_cmd(const std::vector<std::string>& args) {
    auto schema = dynamic_dollar_schema();
    swr::output::Mode mode = swr::output::Mode::TEXT;

    swr::cli::parsed_args p;
    try {
        p = swr::cli::parse_flags(args, schema);
    } catch (const std::exception& e) {
        // Determine mode from raw args so we route error correctly.
        for (auto& a : args) {
            if (a == "--json") mode = swr::output::Mode::JSON;
            else if (a == "--csv") mode = swr::output::Mode::CSV;
        }
        swr::output::emit_error(std::cerr, "dynamic_dollar", e.what(), mode);
        return 1;
    }

    if (p.help_requested) {
        swr::cli::render_help(std::cout, schema);
        return 0;
    }

    if (swr::cli::get_presence(p, "json")) mode = swr::output::Mode::JSON;
    if (swr::cli::get_presence(p, "csv"))  mode = swr::output::Mode::CSV;

    // Build dynamic_input from parsed flags.
    swr::dynamic_input in;
    try {
        in.current_balance =
            std::stof(swr::cli::get_value(p, schema, "balance"));
        in.current_age =
            std::stof(swr::cli::get_value(p, schema, "current_age"));
        in.end_age =
            static_cast<size_t>(std::stoul(swr::cli::get_value(p, schema, "end_age")));
        in.portfolio =
            swr::parse_portfolio(swr::cli::get_value(p, schema, "portfolio"), false);
        swr::normalize_portfolio(in.portfolio);
        in.inflation = swr::cli::get_value(p, schema, "inflation");
        in.rebalance = swr::parse_rebalance(
            swr::cli::get_value(p, schema, "rebalance"));
        in.target_success =
            std::stof(swr::cli::get_value(p, schema, "target_success"));

        float ssa_income = std::stof(swr::cli::get_value(p, schema, "ssa_income"));
        size_t ssa_age   = static_cast<size_t>(
            std::stoul(swr::cli::get_value(p, schema, "ssa_start_age")));
        if (ssa_income > 0.0f) {
            if (ssa_age == 0) {
                swr::output::emit_error(std::cerr, "dynamic_dollar",
                    "--ssa_income > 0 requires --ssa_start_age", mode);
                return 1;
            }
            in.ssa_enabled       = true;
            in.ssa_annual_income = ssa_income;
            in.ssa_start_age     = ssa_age;
        }

        float smoothing = std::stof(swr::cli::get_value(p, schema, "smoothing"));
        float prior     = std::stof(swr::cli::get_value(p, schema, "prior_amount"));
        if (smoothing > 0.0f) {
            if (prior <= 0.0f) {
                swr::output::emit_error(std::cerr, "dynamic_dollar",
                    "--smoothing > 0 requires --prior_amount", mode);
                return 1;
            }
            in.smoothing_enabled    = true;
            in.smoothing_max_change = smoothing;
        }
        in.prior_year_amount = prior;

        in.historical_start_year = static_cast<size_t>(
            std::stoul(swr::cli::get_value(p, schema, "start_year")));
        in.historical_end_year = static_cast<size_t>(
            std::stoul(swr::cli::get_value(p, schema, "end_year")));
        in.withdraw_frequency = static_cast<size_t>(
            std::stoul(swr::cli::get_value(p, schema, "withdraw_frequency")));
        in.fees = std::stof(swr::cli::get_value(p, schema, "fees"));
        in.solver_tolerance = std::stof(
            swr::cli::get_value(p, schema, "solver_tolerance"));
    } catch (const std::exception& e) {
        swr::output::emit_error(std::cerr, "dynamic_dollar",
            std::string("flag parse error: ") + e.what(), mode);
        return 1;
    }

    // Compute.
    bool collect_csv = (mode == swr::output::Mode::CSV);
    auto r = swr::compute(in, collect_csv);
    if (r.error) {
        swr::output::emit_error(std::cerr, "dynamic_dollar", r.message, mode);
        return 1;
    }

    // Build report and emit.
    swr::output::report rep;
    rep.command_name = "dynamic_dollar";
    {
        swr::output::section sec;
        sec.name  = "inputs";
        sec.title = "Inputs";
        sec.fields.push_back({"Current balance", (double)in.current_balance,
                              swr::output::field::Hint::DOLLARS});
        sec.fields.push_back({"Current age", (double)in.current_age,
                              swr::output::field::Hint::NONE});
        sec.fields.push_back({"End age", (int64_t)in.end_age,
                              swr::output::field::Hint::INTEGER});
        sec.fields.push_back({"Remaining horizon",
                              (int64_t)r.remaining_horizon_years,
                              swr::output::field::Hint::YEARS});
        sec.fields.push_back({"Target success", (double)in.target_success,
                              swr::output::field::Hint::PERCENT});
        rep.sections.push_back(sec);
    }
    {
        swr::output::section sec;
        sec.name  = "results";
        sec.title = "Results";
        sec.fields.push_back({"Raw calculated",
                              (double)r.raw_calculated_withdrawal,
                              swr::output::field::Hint::DOLLARS});
        sec.fields.push_back({"Smoothed",
                              (double)r.smoothed_withdrawal,
                              swr::output::field::Hint::DOLLARS});
        sec.fields.push_back({"Final budget",
                              (double)r.final_spending_budget,
                              swr::output::field::Hint::DOLLARS});
        sec.fields.push_back({"Success at budget",
                              (double)r.probability_of_success,
                              swr::output::field::Hint::PERCENT});
        sec.fields.push_back({"SSA offset this year",
                              (double)r.ssa_offset_this_year,
                              swr::output::field::Hint::DOLLARS});
        sec.fields.push_back({"Portfolio withdrawal",
                              (double)r.portfolio_withdrawal_this_year,
                              swr::output::field::Hint::DOLLARS});
        std::stringstream sig_ss;
        sig_ss << r.signal;
        sec.fields.push_back({"Signal vs prior", sig_ss.str(),
                              swr::output::field::Hint::NONE});
        rep.sections.push_back(sec);
    }
    if (!r.message.empty()) rep.notes.push_back(r.message);

    if (mode == swr::output::Mode::CSV) {
        rep.csv_data.column_headers = {
            "start_year", "start_month", "success",
            "terminal_value", "total_withdrawn", "worst_duration_months"};
        std::stringstream pre;
        pre << "command: dynamic_dollar";
        rep.csv_data.preamble_comments.push_back(pre.str());
        for (auto& d : r.per_path_details) {
            swr::output::csv_row row;
            row.cells.push_back((int64_t)d.start_year);
            row.cells.push_back((int64_t)d.start_month);
            row.cells.push_back(d.success);
            row.cells.push_back((double)d.terminal_value);
            row.cells.push_back((double)d.total_withdrawn);
            if (d.worst_duration_months > 0)
                row.cells.push_back((int64_t)d.worst_duration_months);
            else
                row.cells.push_back(std::monostate{});
            rep.csv_data.rows.push_back(row);
        }
    }

    swr::output::emit(std::cout, rep, mode);
    return 0;
}

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

int constant_dollar_cmd(const std::vector<std::string>& args) {
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

swr::cli::command_schema constant_percent_schema() {
    using namespace swr::cli;
    command_schema s;
    s.command_name = "constant_percent";
    s.one_line_description =
        "Evaluate the success rate of withdrawing a fixed percent of the\n"
        "  current portfolio balance each year (no inflation adjustment).";
    s.example_invocation =
        "drawdown constant_percent --pct 4 --portfolio \"us_stocks:60;us_bonds:40;\" \\\n"
        "    --inflation us_inflation --years 30 --rebalance yearly";

    s.flags = {
        {"pct",                FlagGroup::REQUIRED, FlagKind::VALUE,    "Percent of current balance withdrawn each year", ""},
        {"portfolio",          FlagGroup::REQUIRED, FlagKind::VALUE,    "Portfolio spec, e.g. \"us_stocks:60;us_bonds:40;\"", ""},
        {"years",              FlagGroup::REQUIRED, FlagKind::VALUE,    "Horizon length (years)", ""},

        {"inflation",          FlagGroup::COMMON,   FlagKind::VALUE,    "Inflation series", "us_inflation"},
        {"rebalance",          FlagGroup::COMMON,   FlagKind::VALUE,    "none | monthly | yearly | threshold", "none"},
        {"start_year",         FlagGroup::COMMON,   FlagKind::VALUE,    "Earliest historical backtest start year (0 = full data)", "0"},
        {"end_year",           FlagGroup::COMMON,   FlagKind::VALUE,    "Latest historical backtest start year (0 = full data)", "0"},
        {"initial_value",      FlagGroup::COMMON,   FlagKind::VALUE,    "Starting portfolio value (dollars)", "1000"},
        {"minimum_floor",      FlagGroup::COMMON,   FlagKind::VALUE,    "Minimum annual spending floor as percent of initial", "3.0"},
        {"json",               FlagGroup::COMMON,   FlagKind::PRESENCE, "Emit JSON output", ""},
        {"csv",                FlagGroup::COMMON,   FlagKind::PRESENCE, "Emit CSV per-path output", ""},

        {"withdraw_frequency", FlagGroup::ADVANCED, FlagKind::VALUE,    "12 = yearly, 1 = monthly", "12"},
        {"fees",               FlagGroup::ADVANCED, FlagKind::VALUE,    "TER as fraction", "0.001"},
    };
    s.mutually_exclusive.push_back({"json", "csv"});
    return s;
}

int constant_percent_cmd(const std::vector<std::string>& args) {
    auto schema = constant_percent_schema();
    swr::output::Mode mode = swr::output::Mode::TEXT;

    swr::cli::parsed_args p;
    try {
        p = swr::cli::parse_flags(args, schema);
    } catch (const std::exception& e) {
        for (auto& a : args) {
            if (a == "--json") mode = swr::output::Mode::JSON;
            else if (a == "--csv") mode = swr::output::Mode::CSV;
        }
        swr::output::emit_error(std::cerr, "constant_percent", e.what(), mode);
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
        sc.wr       = std::stof(swr::cli::get_value(p, schema, "pct"));
        sc.rebalance = swr::parse_rebalance(swr::cli::get_value(p, schema, "rebalance"));
        sc.initial_value = std::stof(swr::cli::get_value(p, schema, "initial_value"));
        sc.withdraw_frequency = static_cast<size_t>(std::stoul(swr::cli::get_value(p, schema, "withdraw_frequency")));
        sc.fees = std::stof(swr::cli::get_value(p, schema, "fees"));
        sc.wmethod = swr::WithdrawalMethod::CURRENT;
        sc.minimum = std::stof(swr::cli::get_value(p, schema, "minimum_floor")) / 100.0f;

        std::string inflation = swr::cli::get_value(p, schema, "inflation");
        sc.values         = swr::load_values(sc.portfolio);
        sc.inflation_data = swr::load_inflation(sc.values, inflation);

        size_t sy = static_cast<size_t>(std::stoul(swr::cli::get_value(p, schema, "start_year")));
        size_t ey = static_cast<size_t>(std::stoul(swr::cli::get_value(p, schema, "end_year")));
        sc.start_year = sy > 0 ? sy : sc.inflation_data.front().year;
        sc.end_year   = ey > 0 ? ey : sc.inflation_data.back().year;

    } catch (const std::exception& e) {
        swr::output::emit_error(std::cerr, "constant_percent",
            std::string("flag parse error: ") + e.what(), mode);
        return 1;
    }

    auto res = swr::simulation(sc);
    if (res.error) {
        swr::output::emit_error(std::cerr, "constant_percent", res.message, mode);
        return 1;
    }

    swr::output::report rep;
    rep.command_name = "constant_percent";
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
        sec.fields.push_back({"Withdraw frequency", (int64_t)sc.withdraw_frequency, swr::output::field::Hint::INTEGER});
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
        rep.csv_data.preamble_comments.push_back("command: constant_percent");
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

    if (command == "dynamic_dollar") {
        return dynamic_dollar_cmd(sub_args);
    } else if (command == "constant_dollar") {
        return constant_dollar_cmd(sub_args);
    } else if (command == "constant_percent") {
        return constant_percent_cmd(sub_args);
    } else {
        std::cout << "Unhandled command \"" << command << "\"\n";
        return 1;
    }
}
