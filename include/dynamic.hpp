//=======================================================================
// Copyright Rick Gray 2026.
// Distributed under the MIT License.
// (See accompanying file LICENSE or copy at
//  http://opensource.org/licenses/MIT)
//
// Per-year sustainable-withdrawal calculation via historical backtesting.
//=======================================================================
#pragma once

#include <cstddef>
#include <string>
#include <tuple>
#include <vector>
#include <iosfwd>

#include "portfolio.hpp"
#include "simulation.hpp"

namespace swr {

struct dynamic_input {
    // Portfolio state
    float                   current_balance = 0.0f;
    float                   current_age     = 0.0f;
    size_t                  end_age         = 0;

    // Portfolio composition + data window
    std::vector<allocation> portfolio;
    std::string             inflation;
    Rebalancing             rebalance             = Rebalancing::NONE;
    size_t                  historical_start_year = 0;  // 0 = full range
    size_t                  historical_end_year   = 0;  // 0 = full range

    // Solver target
    float                   target_success      = 80.0f;

    // SSA
    bool                    ssa_enabled         = false;
    float                   ssa_annual_income   = 0.0f;
    size_t                  ssa_start_age       = 0;

    // Smoothing
    bool                    smoothing_enabled    = false;
    float                   smoothing_max_change = 0.10f;
    float                   prior_year_amount    = 0.0f;

    // Solver tuning
    size_t                  withdraw_frequency  = 12;
    float                   fees                = 0.001f;
    float                   solver_tolerance    = 1.0f;
    float                   solver_min_wr_pct   = 0.5f;
    float                   solver_max_wr_pct   = 20.0f;
};

enum class Signal { INCREASE, HOLD, DECREASE, NONE };
std::ostream& operator<<(std::ostream& out, Signal s);

struct per_path_detail {
    size_t start_year;
    size_t start_month;
    bool   success;
    float  terminal_value;
    float  total_withdrawn;
    size_t worst_duration_months; // 0 for successes
};

struct dynamic_result {
    size_t  remaining_horizon_years        = 0;
    float   raw_calculated_withdrawal      = 0.0f;
    bool    smoothing_applied              = false;
    float   smoothed_withdrawal            = 0.0f;
    float   final_spending_budget          = 0.0f;
    float   probability_of_success         = 0.0f;
    float   ssa_offset_this_year           = 0.0f;
    float   portfolio_withdrawal_this_year = 0.0f;
    Signal  signal                         = Signal::NONE;

    std::vector<per_path_detail> per_path_details; // populated when requested

    bool error = false;
    std::string message;
};

dynamic_result compute(const dynamic_input& input, bool collect_per_path = false);

// Pure helpers exposed for unit testing.
size_t compute_remaining_horizon(float current_age, size_t end_age);
size_t compute_social_delay(float current_age, size_t ssa_start_age);
float  apply_smoothing(float raw, float prior_year_amount,
                       float max_change, bool* applied_out);
Signal classify_signal(float final_budget, float prior_year_amount);

} // namespace swr
