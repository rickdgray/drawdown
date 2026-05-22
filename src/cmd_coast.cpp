//=======================================================================
// Copyright Rick Gray 2026.
// Distributed under the MIT License.
// (See accompanying file LICENSE or copy at
//  http://opensource.org/licenses/MIT)
//
// `coast` command: find the lump-sum balance that coasts to a retirement
// target with no further contributions.
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

swr::cli::command_schema coast_schema() {
    using namespace swr::cli;
    command_schema s;
    s.command_name = "coast";
    s.one_line_description =
        "Given a retirement target and a horizon, what lump sum today would\n"
        "  coast there with no further contributions? Uses historical backtesting\n"
        "  with real-dollar (inflation-adjusted) accounting.";
    s.example_invocation =
        "drawdown coast --target 1000000 --years 20 "
        "--portfolio \"us_stocks:80;us_bonds:20;\"\n"
        "  drawdown coast --target 2500000 --current-age 33 --retirement-age 50 \\\n"
        "    --portfolio \"us_stocks:80;us_bonds:20;\" --target-success 90";

    s.flags = {
        {"target",             "tg",  FlagGroup::REQUIRED, FlagKind::VALUE,    "Retirement target in today's dollars", ""},
        {"portfolio",          "p",   FlagGroup::REQUIRED, FlagKind::VALUE,    "Portfolio spec, e.g. \"us_stocks:80;us_bonds:20;\"", ""},

        {"years",              "y",   FlagGroup::COMMON,   FlagKind::VALUE,    "Horizon in years (alternative to --current-age + --retirement-age)", "0"},
        {"current-age",        "a",   FlagGroup::COMMON,   FlagKind::VALUE,    "Current age (used with --retirement-age to derive years)", "0"},
        {"retirement-age",     "ra",  FlagGroup::COMMON,   FlagKind::VALUE,    "Retirement age (used with --current-age to derive years)", "0"},
        {"balance",            "b",   FlagGroup::COMMON,   FlagKind::VALUE,    "If set, skip solving and report probability at this balance", "0"},
        {"csv",                "c",   FlagGroup::COMMON,   FlagKind::PRESENCE, "Emit CSV per-path output", ""},
        {"inflation",          "i",   FlagGroup::COMMON,   FlagKind::VALUE,    "Inflation series, e.g. \"us_inflation\"", "us_inflation"},
        {"rebalance",          "r",   FlagGroup::COMMON,   FlagKind::VALUE,    "none | monthly | yearly", "yearly"},
        {"target-success",     "t",   FlagGroup::COMMON,   FlagKind::VALUE,    "Target success rate (percent)", "80"},
        {"json",               "j",   FlagGroup::COMMON,   FlagKind::PRESENCE, "Emit JSON output", ""},

        {"start-year",         "sy",  FlagGroup::ADVANCED, FlagKind::VALUE,    "Earliest historical backtest start year (0 = full data)", "0"},
        {"end-year",           "ey",  FlagGroup::ADVANCED, FlagKind::VALUE,    "Latest historical backtest start year (0 = full data)", "0"},
        {"search-max-balance", "smb", FlagGroup::ADVANCED, FlagKind::VALUE,    "Upper bound for binary search (0 = 2*target)", "0"},
        {"balance-tolerance",  "bt",  FlagGroup::ADVANCED, FlagKind::VALUE,    "Binary-search stopping tolerance (dollars)", "100"},
    };
    s.mutually_exclusive.push_back({"json", "csv"});
    return s;
}

} // namespace

int coast(const std::vector<std::string>& args) {
    auto schema = coast_schema();
    swr::output::Mode mode = swr::output::Mode::TEXT;

    swr::cli::parsed_args p;
    try {
        p = swr::cli::parse_flags(args, schema);
    } catch (const std::exception& e) {
        for (auto& a : args) {
            if (a == "--json" || a == "-j") mode = swr::output::Mode::JSON;
            else if (a == "--csv" || a == "-c") mode = swr::output::Mode::CSV;
        }
        swr::output::emit_error(std::cerr, "coast", e.what(), mode);
        return 1;
    }

    if (p.help_requested) {
        swr::cli::render_help(std::cout, schema);
        return 0;
    }

    if (swr::cli::get_presence(p, "json")) mode = swr::output::Mode::JSON;
    if (swr::cli::get_presence(p, "csv"))  mode = swr::output::Mode::CSV;

    // Parse flags
    float  target          = 0.0f;
    size_t years           = 0;
    float  current_age     = 0.0f;
    size_t retirement_age  = 0;
    float  balance_probe   = 0.0f;
    float  target_success  = 80.0f;
    std::string inflation  = "us_inflation";
    std::string rebalance_str = "yearly";
    size_t start_year      = 0;
    size_t end_year        = 0;
    float  search_max      = 0.0f;
    float  bal_tolerance   = 100.0f;
    std::vector<swr::allocation> portfolio;

    try {
        target         = std::stof(swr::cli::get_value(p, schema, "target"));
        years          = static_cast<size_t>(std::stoul(swr::cli::get_value(p, schema, "years")));
        current_age    = std::stof(swr::cli::get_value(p, schema, "current-age"));
        retirement_age = static_cast<size_t>(
            std::stoul(swr::cli::get_value(p, schema, "retirement-age")));
        balance_probe  = std::stof(swr::cli::get_value(p, schema, "balance"));
        target_success = std::stof(swr::cli::get_value(p, schema, "target-success"));
        inflation      = swr::cli::get_value(p, schema, "inflation");
        rebalance_str  = swr::cli::get_value(p, schema, "rebalance");
        start_year     = static_cast<size_t>(
            std::stoul(swr::cli::get_value(p, schema, "start-year")));
        end_year       = static_cast<size_t>(
            std::stoul(swr::cli::get_value(p, schema, "end-year")));
        search_max     = std::stof(swr::cli::get_value(p, schema, "search-max-balance"));
        bal_tolerance  = std::stof(swr::cli::get_value(p, schema, "balance-tolerance"));

        portfolio = swr::parse_portfolio(swr::cli::get_value(p, schema, "portfolio"), false);
        swr::normalize_portfolio(portfolio);
    } catch (const std::exception& e) {
        swr::output::emit_error(std::cerr, "coast",
            std::string("flag parse error: ") + e.what(), mode);
        return 1;
    }

    // Derive years from age pair if needed
    bool ages_provided = (current_age > 0.0f && retirement_age > 0);
    if (ages_provided && years == 0) {
        if (current_age >= static_cast<float>(retirement_age)) {
            swr::output::emit_error(std::cerr, "coast",
                "current-age must be < retirement-age", mode);
            return 1;
        }
        float diff = static_cast<float>(retirement_age) - current_age;
        years = static_cast<size_t>(std::ceil(diff));
    }

    if (years == 0) {
        swr::output::emit_error(std::cerr, "coast",
            "must specify --years OR (--current-age + --retirement-age)", mode);
        return 1;
    }
    if (target <= 0.0f) {
        swr::output::emit_error(std::cerr, "coast",
            "--target must be positive", mode);
        return 1;
    }

    // Build base growth_input
    swr::growth_input base;
    base.monthly_contribution = 0.0f;   // coast = no contributions
    base.target               = target;
    base.years                = years;
    base.portfolio            = portfolio;
    base.inflation_series     = inflation;
    base.rebalance            = swr::parse_rebalance(rebalance_str);
    base.historical_start_year = start_year;
    base.historical_end_year   = end_year;

    // Portfolio string for display
    std::string portfolio_str;
    for (auto& alloc : portfolio)
        portfolio_str += alloc.asset + ":" + std::to_string((int)alloc.allocation) + ";";

    // Rebalance string for display
    std::stringstream reb_ss;
    reb_ss << base.rebalance;

    swr::output::report rep;
    rep.command_name = "coast";

    if (balance_probe > 0.0f) {
        // Evaluate-mode: user provided a specific balance
        base.initial_balance = balance_probe;
        bool collect_csv = (mode == swr::output::Mode::CSV);
        auto gr = swr::evaluate_growth(base, collect_csv);
        if (gr.error) {
            swr::output::emit_error(std::cerr, "coast", gr.message, mode);
            return 1;
        }

        {
            swr::output::section sec;
            sec.name  = "inputs";
            sec.title = "Inputs";
            sec.fields.push_back({"Balance",        (double)balance_probe,
                                  swr::output::field::Hint::DOLLARS});
            sec.fields.push_back({"Target",         (double)target,
                                  swr::output::field::Hint::DOLLARS});
            if (ages_provided) {
                std::string age_str = std::to_string((int)current_age) + " -> " +
                                      std::to_string((int)retirement_age) + " (" +
                                      std::to_string(years) + " years)";
                sec.fields.push_back({"Years from age", age_str,
                                      swr::output::field::Hint::NONE});
            }
            sec.fields.push_back({"Years",          (int64_t)years,
                                  swr::output::field::Hint::YEARS});
            sec.fields.push_back({"Portfolio",      portfolio_str,
                                  swr::output::field::Hint::NONE});
            sec.fields.push_back({"Inflation series", inflation,
                                  swr::output::field::Hint::NONE});
            sec.fields.push_back({"Rebalance",      reb_ss.str(),
                                  swr::output::field::Hint::NONE});
            sec.fields.push_back({"Target success", (double)target_success,
                                  swr::output::field::Hint::PERCENT});
            rep.sections.push_back(sec);
        }
        {
            swr::output::section sec;
            sec.name  = "results";
            sec.title = "Results";
            sec.fields.push_back({"Probability",     (double)gr.probability_of_reaching_target,
                                  swr::output::field::Hint::PERCENT});
            sec.fields.push_back({"Median terminal", (double)gr.median_terminal_value,
                                  swr::output::field::Hint::DOLLARS});
            sec.fields.push_back({"Min terminal",    (double)gr.min_terminal_value,
                                  swr::output::field::Hint::DOLLARS});
            sec.fields.push_back({"Max terminal",    (double)gr.max_terminal_value,
                                  swr::output::field::Hint::DOLLARS});
            rep.sections.push_back(sec);
        }

        if (mode == swr::output::Mode::CSV) {
            rep.csv_data.column_headers = {"start_year", "reached_target", "terminal_value"};
            std::stringstream pre;
            pre << "command: coast, balance: " << (int)balance_probe
                << ", target: " << (int)target << ", years: " << years;
            rep.csv_data.preamble_comments.push_back(pre.str());
            for (auto& d : gr.per_path_details) {
                swr::output::csv_row row;
                row.cells.push_back((int64_t)d.start_year);
                row.cells.push_back(d.reached_target);
                row.cells.push_back((double)d.terminal_value);
                rep.csv_data.rows.push_back(row);
            }
        }
    } else {
        // Solve-mode: binary search for required balance
        swr::coast_input cin;
        cin.base              = base;
        cin.target_success    = target_success;
        cin.search_max_balance = search_max;
        cin.balance_tolerance  = bal_tolerance;

        bool collect_csv = (mode == swr::output::Mode::CSV);
        // solve_coast always collects per-path at the result
        auto cr = swr::solve_coast(cin);
        if (cr.error) {
            swr::output::emit_error(std::cerr, "coast", cr.message, mode);
            return 1;
        }

        {
            swr::output::section sec;
            sec.name  = "inputs";
            sec.title = "Inputs";
            sec.fields.push_back({"Target",         (double)target,
                                  swr::output::field::Hint::DOLLARS});
            if (ages_provided) {
                std::string age_str = std::to_string((int)current_age) + " -> " +
                                      std::to_string((int)retirement_age) + " (" +
                                      std::to_string(years) + " years)";
                sec.fields.push_back({"Years from age", age_str,
                                      swr::output::field::Hint::NONE});
            }
            sec.fields.push_back({"Years",          (int64_t)years,
                                  swr::output::field::Hint::YEARS});
            sec.fields.push_back({"Portfolio",      portfolio_str,
                                  swr::output::field::Hint::NONE});
            sec.fields.push_back({"Inflation series", inflation,
                                  swr::output::field::Hint::NONE});
            sec.fields.push_back({"Rebalance",      reb_ss.str(),
                                  swr::output::field::Hint::NONE});
            sec.fields.push_back({"Target success", (double)target_success,
                                  swr::output::field::Hint::PERCENT});
            rep.sections.push_back(sec);
        }
        {
            swr::output::section sec;
            sec.name  = "results";
            sec.title = "Results";
            sec.fields.push_back({"Required balance",     (double)cr.required_balance,
                                  swr::output::field::Hint::DOLLARS});
            sec.fields.push_back({"Probability at result",(double)cr.probability_at_result,
                                  swr::output::field::Hint::PERCENT});
            sec.fields.push_back({"Median terminal",      (double)cr.detail.median_terminal_value,
                                  swr::output::field::Hint::DOLLARS});
            sec.fields.push_back({"Min terminal",         (double)cr.detail.min_terminal_value,
                                  swr::output::field::Hint::DOLLARS});
            sec.fields.push_back({"Max terminal",         (double)cr.detail.max_terminal_value,
                                  swr::output::field::Hint::DOLLARS});
            rep.sections.push_back(sec);
        }

        if (collect_csv) {
            rep.csv_data.column_headers = {"start_year", "reached_target", "terminal_value"};
            std::stringstream pre;
            pre << "command: coast, required_balance: " << (int)cr.required_balance
                << ", target: " << (int)target << ", years: " << years;
            rep.csv_data.preamble_comments.push_back(pre.str());
            for (auto& d : cr.detail.per_path_details) {
                swr::output::csv_row row;
                row.cells.push_back((int64_t)d.start_year);
                row.cells.push_back(d.reached_target);
                row.cells.push_back((double)d.terminal_value);
                rep.csv_data.rows.push_back(row);
            }
        }
    }

    swr::output::emit(std::cout, rep, mode);
    return 0;
}

} // namespace swr::cmd
