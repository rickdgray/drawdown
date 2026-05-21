//=======================================================================
// Copyright Rick Gray 2026.
// Distributed under the MIT License.
// (See accompanying file LICENSE or copy at
//  http://opensource.org/licenses/MIT)
//
// `dynamic_dollar` command: per-year sustainable withdrawal solver.
//=======================================================================
#include "commands.hpp"

#include <iostream>
#include <sstream>
#include <string>

#include "cli.hpp"
#include "dynamic.hpp"
#include "output_formatter.hpp"
#include "portfolio.hpp"

namespace swr::cmd {

namespace {

swr::cli::command_schema dynamic_dollar_schema() {
    using namespace swr::cli;
    command_schema s;
    s.command_name = "dynamic-dollar";
    s.one_line_description =
        "Compute this year's sustainable withdrawal given current portfolio "
        "reality,\n  using historical backtesting over the remaining horizon.";
    s.example_invocation =
        "drawdown dynamic-dollar -b 850000 -a 67 -e 92 "
        "-p \"us_stocks:60;us_bonds:40;\"\n"
        "  drawdown dynamic-dollar --balance 850000 --current-age 67 "
        "--end-age 92 \\\n    --portfolio \"us_stocks:60;us_bonds:40;\" "
        "--inflation us_inflation";

    s.flags = {
        {"balance",            "b",  FlagGroup::REQUIRED, FlagKind::VALUE,    "Current portfolio balance (dollars)", ""},
        {"current-age",        "a",  FlagGroup::REQUIRED, FlagKind::VALUE,    "Your current age (float allowed)", ""},
        {"end-age",            "e",  FlagGroup::REQUIRED, FlagKind::VALUE,    "Planning end age (integer)", ""},
        {"portfolio",          "p",  FlagGroup::REQUIRED, FlagKind::VALUE,    "Portfolio spec, e.g. \"us_stocks:60;us_bonds:40;\"", ""},

        {"inflation",          "i",  FlagGroup::COMMON,   FlagKind::VALUE,    "Inflation series, e.g. \"us_inflation\"", "us_inflation"},
        {"target-success",     "t",  FlagGroup::COMMON,   FlagKind::VALUE,    "Target success rate (percent)", "80"},
        {"rebalance",          "r",  FlagGroup::COMMON,   FlagKind::VALUE,    "none | monthly | yearly | threshold", "none"},
        {"ssa-income",         "si", FlagGroup::COMMON,   FlagKind::VALUE,    "Annual SSA income (dollars; 0 = disabled)", "0"},
        {"ssa-start-age",      "sa", FlagGroup::COMMON,   FlagKind::VALUE,    "Age SSA begins (required if --ssa-income > 0)", "0"},
        {"smoothing",          "s",  FlagGroup::COMMON,   FlagKind::VALUE,    "Max YoY change as fraction, e.g. 0.10 (0 = disabled)", "0"},
        {"prior-amount",       "pa", FlagGroup::COMMON,   FlagKind::VALUE,    "Last year's spending budget (also enables signal)", "0"},
        {"json",               "j",  FlagGroup::COMMON,   FlagKind::PRESENCE, "Emit JSON output", ""},
        {"csv",                "c",  FlagGroup::COMMON,   FlagKind::PRESENCE, "Emit CSV per-path output", ""},

        {"start-year",         "sy", FlagGroup::ADVANCED, FlagKind::VALUE,    "Earliest historical backtest start year (default: full data)", "0"},
        {"end-year",           "ey", FlagGroup::ADVANCED, FlagKind::VALUE,    "Latest historical backtest start year (default: full data)", "0"},
        {"withdraw-frequency", "wf", FlagGroup::ADVANCED, FlagKind::VALUE,    "12 = yearly, 1 = monthly", "12"},
        {"fees",               "f",  FlagGroup::ADVANCED, FlagKind::VALUE,    "TER as fraction", "0.001"},
        {"solver-tolerance",   "st", FlagGroup::ADVANCED, FlagKind::VALUE,    "Binary-search stopping tolerance (dollars)", "1"},
    };
    s.mutually_exclusive.push_back({"json", "csv"});
    return s;
}

} // namespace

int dynamic_dollar(const std::vector<std::string>& args) {
    auto schema = dynamic_dollar_schema();
    swr::output::Mode mode = swr::output::Mode::TEXT;

    swr::cli::parsed_args p;
    try {
        p = swr::cli::parse_flags(args, schema);
    } catch (const std::exception& e) {
        // Determine mode from raw args so we route error correctly.
        for (auto& a : args) {
            if (a == "--json" || a == "-j") mode = swr::output::Mode::JSON;
            else if (a == "--csv" || a == "-c") mode = swr::output::Mode::CSV;
        }
        swr::output::emit_error(std::cerr, "dynamic-dollar", e.what(), mode);
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
            std::stof(swr::cli::get_value(p, schema, "current-age"));
        in.end_age =
            static_cast<size_t>(std::stoul(swr::cli::get_value(p, schema, "end-age")));
        in.portfolio =
            swr::parse_portfolio(swr::cli::get_value(p, schema, "portfolio"), false);
        swr::normalize_portfolio(in.portfolio);
        in.inflation = swr::cli::get_value(p, schema, "inflation");
        in.rebalance = swr::parse_rebalance(
            swr::cli::get_value(p, schema, "rebalance"));
        in.target_success =
            std::stof(swr::cli::get_value(p, schema, "target-success"));

        float ssa_income = std::stof(swr::cli::get_value(p, schema, "ssa-income"));
        size_t ssa_age   = static_cast<size_t>(
            std::stoul(swr::cli::get_value(p, schema, "ssa-start-age")));
        if (ssa_income > 0.0f) {
            if (ssa_age == 0) {
                swr::output::emit_error(std::cerr, "dynamic-dollar",
                    "--ssa-income > 0 requires --ssa-start-age", mode);
                return 1;
            }
            in.ssa_enabled       = true;
            in.ssa_annual_income = ssa_income;
            in.ssa_start_age     = ssa_age;
        }

        float smoothing = std::stof(swr::cli::get_value(p, schema, "smoothing"));
        float prior     = std::stof(swr::cli::get_value(p, schema, "prior-amount"));
        if (smoothing > 0.0f) {
            if (prior <= 0.0f) {
                swr::output::emit_error(std::cerr, "dynamic-dollar",
                    "--smoothing > 0 requires --prior-amount", mode);
                return 1;
            }
            in.smoothing_enabled    = true;
            in.smoothing_max_change = smoothing;
        }
        in.prior_year_amount = prior;

        in.historical_start_year = static_cast<size_t>(
            std::stoul(swr::cli::get_value(p, schema, "start-year")));
        in.historical_end_year = static_cast<size_t>(
            std::stoul(swr::cli::get_value(p, schema, "end-year")));
        in.withdraw_frequency = static_cast<size_t>(
            std::stoul(swr::cli::get_value(p, schema, "withdraw-frequency")));
        in.fees = std::stof(swr::cli::get_value(p, schema, "fees"));
        in.solver_tolerance = std::stof(
            swr::cli::get_value(p, schema, "solver-tolerance"));
    } catch (const std::exception& e) {
        swr::output::emit_error(std::cerr, "dynamic-dollar",
            std::string("flag parse error: ") + e.what(), mode);
        return 1;
    }

    // Compute.
    bool collect_csv = (mode == swr::output::Mode::CSV);
    auto r = swr::compute(in, collect_csv);
    if (r.error) {
        swr::output::emit_error(std::cerr, "dynamic-dollar", r.message, mode);
        return 1;
    }

    // Build report and emit.
    swr::output::report rep;
    rep.command_name = "dynamic-dollar";
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
        std::string freq_label = (in.withdraw_frequency == 12) ? "yearly"
                               : (in.withdraw_frequency == 1)  ? "monthly"
                               : "every " + std::to_string(in.withdraw_frequency) + " months";
        sec.fields.push_back({"Withdraw frequency", freq_label,
                              swr::output::field::Hint::NONE});
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
        pre << "command: dynamic-dollar";
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

} // namespace swr::cmd
