//=======================================================================
// Copyright Rick Gray 2026.
// Distributed under the MIT License.
// (See accompanying file LICENSE or copy at
//  http://opensource.org/licenses/MIT)
//
// Historical-backtest accumulation engine (real-dollar accounting).
//=======================================================================
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "portfolio.hpp"
#include "simulation.hpp"  // for Rebalancing enum

namespace swr {

struct growth_input {
    float                   initial_balance      = 0.0f;
    float                   monthly_contribution = 0.0f;
    float                   target               = 0.0f;
    size_t                  years                = 0;
    std::vector<allocation> portfolio;
    std::string             inflation_series     = "us_inflation";
    Rebalancing             rebalance            = Rebalancing::YEARLY;
    size_t                  historical_start_year = 0;
    size_t                  historical_end_year   = 0;
};

struct growth_per_path {
    size_t start_year;
    bool   reached_target;
    float  terminal_value;
};

struct growth_result {
    float  probability_of_reaching_target = 0.0f;
    size_t paths_evaluated                = 0;
    size_t paths_successful               = 0;
    float  median_terminal_value          = 0.0f;
    float  min_terminal_value             = 0.0f;
    float  max_terminal_value             = 0.0f;
    std::vector<growth_per_path> per_path_details;
    bool         error = false;
    std::string  message;
};

growth_result evaluate_growth(const growth_input& input, bool collect_per_path = false);

struct coast_input {
    growth_input base;                // monthly_contribution forced to 0 internally
    float        target_success      = 80.0f;
    float        search_min_balance  = 0.0f;
    float        search_max_balance  = 0.0f;
    float        balance_tolerance   = 100.0f;
};

struct coast_result {
    float         required_balance      = 0.0f;
    float         probability_at_result = 0.0f;
    growth_result detail;
    bool          error = false;
    std::string   message;
};

coast_result solve_coast(const coast_input& input);

struct accumulate_input {
    growth_input base;                // base.years is ignored; we scan
    float        target_success      = 80.0f;
    size_t       max_years_to_scan   = 60;
};

struct accumulate_result {
    size_t        required_years        = 0;
    float         probability_at_result = 0.0f;
    growth_result detail;
    bool          error = false;
    std::string   message;
};

accumulate_result solve_accumulate(const accumulate_input& input);

} // namespace swr
