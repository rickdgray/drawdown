//=======================================================================
// Copyright Baptiste Wicht 2019-2024.
// Distributed under the MIT License.
// (See accompanying file LICENSE or copy at
//  http://opensource.org/licenses/MIT)
//=======================================================================

#pragma once

#include <vector>
#include <cstdint>

#include "portfolio.hpp"
#include "data.hpp"

namespace swr {

enum class Rebalancing : uint64_t { NONE, MONTHLY, YEARLY };

enum class WithdrawalMethod : uint64_t {
    STANDARD, // Withdraw based on the initial portfolio
    CURRENT,  // Withdraw based on the current portfolio
};

Rebalancing   parse_rebalance(const std::string& str);
std::ostream& operator<<(std::ostream& out, const Rebalancing& rebalance);
std::ostream& operator<<(std::ostream& out, const WithdrawalMethod& wmethod);

struct context {
    float minimum = 0.0f;

    float year_start_value       = 0.0f;
    float year_withdrawn         = 0.0f;
    float last_withdrawal_amount = 0.0f;

    float  withdrawal = 0.0f;

    size_t months       = 0;
    size_t total_months = 0;

    bool end() const {
        return months == total_months;
    }
};

struct scenario {
    std::vector<swr::allocation> portfolio;
    data_vector                  inflation_data;
    std::vector<data_vector>     values;

    size_t              years;
    float               wr;
    size_t              start_year;
    size_t              end_year;
    float               initial_value      = 1000.0f;
    size_t              withdraw_frequency = 1;
    Rebalancing      rebalance = Rebalancing::NONE;
    float            fees      = 0.001f; // TER 0.1% = 0.001
    WithdrawalMethod wmethod   = WithdrawalMethod::STANDARD;
    float            minimum   = 0.03f; // Minimum of 3% * initial

    // Configuration for social security
    bool   social_security = false;
    size_t social_delay    = 0;
    float  social_coverage = 0.0f;
    float  social_amount   = 0.0f;

    bool is_failure(float current_value) const {
        return current_value <= 0.0f;
    }
};

std::ostream& operator<<(std::ostream& out, const scenario& scenario);

struct results {
    size_t successes = 0;
    size_t failures  = 0;

    float success_rate = 0.0f;

    float tv_average = 0.0f;
    float tv_minimum = 0.0f;
    float tv_maximum = 0.0f;
    float tv_median  = 0.0f;

    float  spending_average             = 0.0f;
    float  spending_minimum             = 0.0f;
    float  spending_maximum             = 0.0f;
    float  spending_median              = 0.0f;
    size_t years_large_spending         = 0;
    size_t years_small_spending         = 0;
    size_t years_volatile_up_spending   = 0;
    size_t years_volatile_down_spending = 0;

    size_t worst_duration       = 0;
    size_t worst_starting_month = 0;
    size_t worst_starting_year  = 0;

    size_t lowest_eff_wr_year        = 0;
    size_t lowest_eff_wr_start_year  = 0;
    size_t lowest_eff_wr_start_month = 0;
    float  lowest_eff_wr             = 0.0f;

    size_t highest_eff_wr_year        = 0;
    size_t highest_eff_wr_start_year  = 0;
    size_t highest_eff_wr_start_month = 0;
    float  highest_eff_wr             = 0.0f;

    size_t worst_tv       = 0;
    size_t worst_tv_month = 0;
    size_t worst_tv_year  = 0;
    size_t best_tv        = 0;
    size_t best_tv_month  = 0;
    size_t best_tv_year   = 0;

    float total_withdrawn    = 0;
    float withdrawn_per_year = 0;

    std::string message;
    bool        error = false;

    std::vector<float> terminal_values;
    void               compute_terminal_values(std::vector<float> terminal_values);
    void               compute_spending(std::vector<std::vector<float>>& terminal_values, size_t years);

    void record_failure(size_t months, size_t current_month, size_t current_year) {
        if (!worst_duration || months < worst_duration) {
            worst_duration       = months;
            worst_starting_month = current_month;
            worst_starting_year  = current_year;
        }
    }
};

results simulation(scenario& scenario);

size_t simulations_ran();

} // namespace swr
