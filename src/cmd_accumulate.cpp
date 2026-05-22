//=======================================================================
// Copyright Rick Gray 2026.
// Distributed under the MIT License.
// (See accompanying file LICENSE or copy at
//  http://opensource.org/licenses/MIT)
//
// `accumulate` command: given a balance and monthly contributions, how
// long until the portfolio reaches the target?
//=======================================================================
#include "commands.hpp"

#include <cmath>
#include <iostream>
#include <sstream>
#include <string>

#include "cli.hpp"
#include "growth.hpp"
#include "output_formatter.hpp"
#include "portfolio.hpp"

namespace swr::cmd {

namespace {

swr::cli::command_schema accumulate_schema() {
    using namespace swr::cli;
    command_schema s;
    s.command_name = "accumulate";
    s.one_line_description =
        "Given a balance and monthly contributions, how long until I reach\n"
        "  my target? Uses historical backtesting with real-dollar accounting.";
    s.example_invocation =
        "drawdown accumulate --balance 100000 --contribution 3000 \\\n"
        "    --target 1000000 --portfolio \"us_stocks:80;us_bonds:20;\"\n"
        "  drawdown accumulate -b 519000 -mc 5083 -tg 2500000 "
        "-p \"us_stocks:80;us_bonds:20;\" -a 33";

    s.flags = {
        {"balance",         "b",  FlagGroup::REQUIRED, FlagKind::VALUE,
         "Current portfolio balance (today's dollars)", ""},
        {"contribution",    "mc", FlagGroup::REQUIRED, FlagKind::VALUE,
         "Monthly contribution (today's dollars, constant in real terms)", ""},
        {"target",          "tg", FlagGroup::REQUIRED, FlagKind::VALUE,
         "Target balance (today's dollars)", ""},
        {"portfolio",       "p",  FlagGroup::REQUIRED, FlagKind::VALUE,
         "Portfolio spec, e.g. \"us_stocks:80;us_bonds:20;\"", ""},

        {"inflation",       "i",  FlagGroup::COMMON,   FlagKind::VALUE,
         "Inflation series, e.g. \"us_inflation\"", "us_inflation"},
        {"rebalance",       "r",  FlagGroup::COMMON,   FlagKind::VALUE,
         "none | monthly | yearly", "yearly"},
        {"target-success",  "t",  FlagGroup::COMMON,   FlagKind::VALUE,
         "Target success rate (percent)", "80"},
        {"current-age",     "a",  FlagGroup::COMMON,   FlagKind::VALUE,
         "Current age (used to project the reached age in output)", "0"},
        {"json",            "j",  FlagGroup::COMMON,   FlagKind::PRESENCE,
         "Emit JSON output", ""},
        {"csv",             "c",  FlagGroup::COMMON,   FlagKind::PRESENCE,
         "Emit CSV per-path output", ""},

        {"start-year",      "sy", FlagGroup::ADVANCED, FlagKind::VALUE,
         "Earliest historical backtest start year (0 = full data)", "0"},
        {"end-year",        "ey", FlagGroup::ADVANCED, FlagKind::VALUE,
         "Latest historical backtest start year (0 = full data)", "0"},
        {"max-years",       "my", FlagGroup::ADVANCED, FlagKind::VALUE,
         "Maximum years to scan before giving up", "60"},
    };
    s.mutually_exclusive.push_back({"json", "csv"});
    return s;
}

} // namespace

int accumulate(const std::vector<std::string>& args) {
    auto schema = accumulate_schema();
    swr::output::Mode mode = swr::output::Mode::TEXT;

    swr::cli::parsed_args p;
    try {
        p = swr::cli::parse_flags(args, schema);
    } catch (const std::exception& e) {
        for (auto& a : args) {
            if (a == "--json" || a == "-j") mode = swr::output::Mode::JSON;
            else if (a == "--csv" || a == "-c") mode = swr::output::Mode::CSV;
        }
        swr::output::emit_error(std::cerr, "accumulate", e.what(), mode);
        return 1;
    }

    if (p.help_requested) {
        swr::cli::render_help(std::cout, schema);
        return 0;
    }

    if (swr::cli::get_presence(p, "json")) mode = swr::output::Mode::JSON;
    if (swr::cli::get_presence(p, "csv"))  mode = swr::output::Mode::CSV;

    float  balance         = 0.0f;
    float  contribution    = 0.0f;
    float  target          = 0.0f;
    float  target_success  = 80.0f;
    float  current_age     = 0.0f;
    std::string inflation  = "us_inflation";
    std::string rebalance_str = "yearly";
    size_t start_year      = 0;
    size_t end_year        = 0;
    size_t max_years       = 60;
    std::vector<swr::allocation> portfolio;

    try {
        balance       = std::stof(swr::cli::get_value(p, schema, "balance"));
        contribution  = std::stof(swr::cli::get_value(p, schema, "contribution"));
        target        = std::stof(swr::cli::get_value(p, schema, "target"));
        target_success = std::stof(swr::cli::get_value(p, schema, "target-success"));
        current_age   = std::stof(swr::cli::get_value(p, schema, "current-age"));
        inflation     = swr::cli::get_value(p, schema, "inflation");
        rebalance_str = swr::cli::get_value(p, schema, "rebalance");
        start_year    = static_cast<size_t>(
            std::stoul(swr::cli::get_value(p, schema, "start-year")));
        end_year      = static_cast<size_t>(
            std::stoul(swr::cli::get_value(p, schema, "end-year")));
        max_years     = static_cast<size_t>(
            std::stoul(swr::cli::get_value(p, schema, "max-years")));

        portfolio = swr::parse_portfolio(swr::cli::get_value(p, schema, "portfolio"), false);
        swr::normalize_portfolio(portfolio);
    } catch (const std::exception& e) {
        swr::output::emit_error(std::cerr, "accumulate",
            std::string("flag parse error: ") + e.what(), mode);
        return 1;
    }

    if (balance < 0.0f) {
        swr::output::emit_error(std::cerr, "accumulate",
            "--balance must be >= 0", mode);
        return 1;
    }
    if (contribution <= 0.0f) {
        swr::output::emit_error(std::cerr, "accumulate",
            "--contribution must be positive", mode);
        return 1;
    }
    if (target <= 0.0f) {
        swr::output::emit_error(std::cerr, "accumulate",
            "--target must be positive", mode);
        return 1;
    }

    // Build accumulate_input
    swr::accumulate_input ain;
    ain.base.initial_balance      = balance;
    ain.base.monthly_contribution = contribution;
    ain.base.target               = target;
    ain.base.portfolio            = portfolio;
    ain.base.inflation_series     = inflation;
    ain.base.rebalance            = swr::parse_rebalance(rebalance_str);
    ain.base.historical_start_year = start_year;
    ain.base.historical_end_year   = end_year;
    ain.target_success             = target_success;
    ain.max_years_to_scan          = max_years;

    auto ar = swr::solve_accumulate(ain);
    if (ar.error) {
        swr::output::emit_error(std::cerr, "accumulate", ar.message, mode);
        return 1;
    }

    // Portfolio string for display
    std::string portfolio_str;
    for (auto& alloc : portfolio)
        portfolio_str += alloc.asset + ":" + std::to_string((int)alloc.allocation) + ";";

    // Rebalance string for display
    std::stringstream reb_ss;
    reb_ss << ain.base.rebalance;

    swr::output::report rep;
    rep.command_name = "accumulate";

    {
        swr::output::section sec;
        sec.name  = "inputs";
        sec.title = "Inputs";
        sec.fields.push_back({"Current balance",      (double)balance,
                              swr::output::field::Hint::DOLLARS});
        sec.fields.push_back({"Monthly contribution", (double)contribution,
                              swr::output::field::Hint::DOLLARS});
        sec.fields.push_back({"Target",               (double)target,
                              swr::output::field::Hint::DOLLARS});
        sec.fields.push_back({"Portfolio",            portfolio_str,
                              swr::output::field::Hint::NONE});
        sec.fields.push_back({"Inflation series",     inflation,
                              swr::output::field::Hint::NONE});
        sec.fields.push_back({"Rebalance",            reb_ss.str(),
                              swr::output::field::Hint::NONE});
        sec.fields.push_back({"Target success",       (double)target_success,
                              swr::output::field::Hint::PERCENT});
        rep.sections.push_back(sec);
    }
    {
        swr::output::section sec;
        sec.name  = "results";
        sec.title = "Results";
        sec.fields.push_back({"Years to target",       (int64_t)ar.required_years,
                              swr::output::field::Hint::YEARS});
        sec.fields.push_back({"Probability at result", (double)ar.probability_at_result,
                              swr::output::field::Hint::PERCENT});
        if (current_age > 0.0f) {
            size_t reached_age = static_cast<size_t>(
                std::ceil(current_age) + ar.required_years);
            sec.fields.push_back({"Projected reached age", (int64_t)reached_age,
                                  swr::output::field::Hint::INTEGER});
        }
        sec.fields.push_back({"Median terminal",       (double)ar.detail.median_terminal_value,
                              swr::output::field::Hint::DOLLARS});
        sec.fields.push_back({"Min terminal",          (double)ar.detail.min_terminal_value,
                              swr::output::field::Hint::DOLLARS});
        sec.fields.push_back({"Max terminal",          (double)ar.detail.max_terminal_value,
                              swr::output::field::Hint::DOLLARS});
        rep.sections.push_back(sec);
    }

    if (mode == swr::output::Mode::CSV) {
        rep.csv_data.column_headers = {"start_year", "reached_target", "terminal_value"};
        std::stringstream pre;
        pre << "command: accumulate, years: " << ar.required_years
            << ", target: " << (int)target;
        rep.csv_data.preamble_comments.push_back(pre.str());
        for (auto& d : ar.detail.per_path_details) {
            swr::output::csv_row row;
            row.cells.push_back((int64_t)d.start_year);
            row.cells.push_back(d.reached_target);
            row.cells.push_back((double)d.terminal_value);
            rep.csv_data.rows.push_back(row);
        }
    }

    swr::output::emit(std::cout, rep, mode);
    return 0;
}

} // namespace swr::cmd
