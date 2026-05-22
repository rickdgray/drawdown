//=======================================================================
// Copyright Rick Gray 2026.
// Distributed under the MIT License.
// (See accompanying file LICENSE or copy at
//  http://opensource.org/licenses/MIT)
//
// Per-year sustainable-withdrawal calculation via historical backtesting.
//=======================================================================
#include "dynamic.hpp"

#include <cmath>
#include <ostream>
#include <string>

#include "data.hpp"

namespace swr {

std::ostream& operator<<(std::ostream& out, Signal s) {
    switch (s) {
        case Signal::INCREASE: return out << "increase";
        case Signal::HOLD:     return out << "hold";
        case Signal::DECREASE: return out << "decrease";
        case Signal::NONE:     return out << "none";
    }
    return out;
}

size_t compute_remaining_horizon(float current_age, size_t end_age) {
    if (current_age >= static_cast<float>(end_age)) return 0;
    float diff = static_cast<float>(end_age) - current_age;
    return static_cast<size_t>(std::ceil(diff));
}

size_t compute_social_delay(float current_age, size_t ssa_start_age) {
    if (current_age >= static_cast<float>(ssa_start_age)) return 0;
    float diff = static_cast<float>(ssa_start_age) - current_age;
    return static_cast<size_t>(std::ceil(diff));
}

float apply_smoothing(float raw, float prior_year_amount,
                      float max_change, bool* applied_out) {
    if (prior_year_amount <= 0.0f || max_change <= 0.0f) {
        if (applied_out) *applied_out = false;
        return raw;
    }
    float upper = prior_year_amount * (1.0f + max_change);
    float lower = prior_year_amount * (1.0f - max_change);
    if (raw > upper) {
        if (applied_out) *applied_out = true;
        return upper;
    }
    if (raw < lower) {
        if (applied_out) *applied_out = true;
        return lower;
    }
    if (applied_out) *applied_out = false;
    return raw;
}

Signal classify_signal(float final_budget, float prior_year_amount) {
    if (prior_year_amount <= 0.0f) return Signal::NONE;
    float delta = (final_budget - prior_year_amount) / prior_year_amount;
    if (delta > 0.02f)  return Signal::INCREASE;
    if (delta < -0.02f) return Signal::DECREASE;
    return Signal::HOLD;
}

namespace {

float run_simulation_get_success(swr::scenario& sc, float wr_pct) {
    sc.wr = wr_pct;
    auto results = swr::simulation(sc);
    return results.success_rate;
}

} // namespace

dynamic_result compute(const dynamic_input& input, bool collect_per_path) {
    dynamic_result r;

    // --- Validation
    if (input.current_age >= static_cast<float>(input.end_age)) {
        r.error = true;
        r.message = "current_age must be < end_age";
        return r;
    }
    if (input.current_balance <= 0.0f) {
        r.error = true;
        r.message = "current_balance must be positive";
        return r;
    }

    r.remaining_horizon_years = compute_remaining_horizon(input.current_age,
                                                          input.end_age);
    if (r.remaining_horizon_years == 0) {
        r.error = true;
        r.message = "remaining horizon is 0 years";
        return r;
    }

    // --- Build the scenario
    scenario sc;
    sc.portfolio          = input.portfolio;
    sc.years              = r.remaining_horizon_years;
    sc.rebalance          = input.rebalance;
    sc.initial_value      = input.current_balance;
    sc.withdraw_frequency = input.withdraw_frequency;
    sc.fees               = input.fees;
    sc.wmethod            = WithdrawalMethod::STANDARD;
    // load data
    sc.values         = load_values(sc.portfolio);
    sc.inflation_data = load_inflation(sc.values, input.inflation);

    // historical start/end years
    if (input.historical_start_year > 0)
        sc.start_year = input.historical_start_year;
    else
        sc.start_year = sc.inflation_data.front().year;
    if (input.historical_end_year > 0)
        sc.end_year = input.historical_end_year;
    else
        sc.end_year = sc.inflation_data.back().year;

    // SSA mapping
    if (input.ssa_enabled && input.ssa_annual_income > 0.0f) {
        sc.social_security = true;
        sc.social_coverage = 0.0f;
        sc.social_amount   = input.ssa_annual_income;
        sc.social_delay    = compute_social_delay(input.current_age,
                                                  input.ssa_start_age);
    }

    // --- Solver: binary search for max WR meeting target_success
    float low  = input.solver_min_wr_pct;
    float high = input.solver_max_wr_pct;
    float last_success_at_low = 0.0f;
    float tol_pct = (input.solver_tolerance / input.current_balance) * 100.0f;
    if (tol_pct <= 0.0f) tol_pct = 0.0001f;

    // First, probe at low and high to detect unsupportable / oversized cases.
    float success_at_low = run_simulation_get_success(sc, low);
    if (success_at_low < input.target_success) {
        r.error = true;
        r.message = "even minimum WR (" + std::to_string(low) +
                    "%) fails target_success at this horizon";
        r.raw_calculated_withdrawal = 0.0f;
        return r;
    }
    last_success_at_low = success_at_low;

    float success_at_high = run_simulation_get_success(sc, high);
    if (success_at_high >= input.target_success) {
        r.raw_calculated_withdrawal = (high / 100.0f) * input.current_balance;
        r.smoothing_applied = false;
        r.smoothed_withdrawal = r.raw_calculated_withdrawal;
        r.final_spending_budget = r.raw_calculated_withdrawal;
        r.probability_of_success = success_at_high;
        r.ssa_offset_this_year = (input.ssa_enabled &&
            sc.social_delay == 0) ? input.ssa_annual_income : 0.0f;
        r.portfolio_withdrawal_this_year =
            r.final_spending_budget - r.ssa_offset_this_year;
        r.signal = classify_signal(r.final_spending_budget,
                                   input.prior_year_amount);
        r.message = "WR capped at " + std::to_string(high) +
                    "%; portfolio supports more";
        return r;
    }

    while ((high - low) > tol_pct) {
        float mid = (low + high) / 2.0f;
        float succ = run_simulation_get_success(sc, mid);
        if (succ >= input.target_success) {
            low = mid;
            last_success_at_low = succ;
        } else {
            high = mid;
        }
    }

    float raw_wr_pct = low;
    r.raw_calculated_withdrawal =
        (raw_wr_pct / 100.0f) * input.current_balance;

    // --- Smoothing
    bool smoothing_applied = false;
    float smoothed = r.raw_calculated_withdrawal;
    if (input.smoothing_enabled) {
        smoothed = apply_smoothing(r.raw_calculated_withdrawal,
                                   input.prior_year_amount,
                                   input.smoothing_max_change,
                                   &smoothing_applied);
    }
    r.smoothing_applied = smoothing_applied;
    r.smoothed_withdrawal = smoothed;
    r.final_spending_budget = smoothed;

    // --- Re-simulate at final budget if smoothing changed the value
    if (smoothing_applied) {
        float final_wr_pct =
            (r.final_spending_budget / input.current_balance) * 100.0f;
        r.probability_of_success =
            run_simulation_get_success(sc, final_wr_pct);
    } else {
        r.probability_of_success = last_success_at_low;
    }

    // --- SSA offset this year
    if (input.ssa_enabled && sc.social_delay == 0) {
        r.ssa_offset_this_year = input.ssa_annual_income;
    } else {
        r.ssa_offset_this_year = 0.0f;
    }
    r.portfolio_withdrawal_this_year =
        r.final_spending_budget - r.ssa_offset_this_year;

    // --- Signal
    r.signal = classify_signal(r.final_spending_budget,
                               input.prior_year_amount);

    // --- Per-path detail (optional)
    if (collect_per_path) {
        float final_wr_pct =
            (r.final_spending_budget / input.current_balance) * 100.0f;
        sc.wr = final_wr_pct;
        auto res = swr::simulation(sc);
        // The existing engine doesn't expose per-(start_year,start_month)
        // tuples directly; terminal_values is a flat vector parallel to the
        // iteration order. We reconstruct the (year, month) sequence by
        // walking valid start points the same way the engine does.
        // For Phase 1, emit only terminal values keyed sequentially with the
        // earliest plausible start dates.
        size_t idx = 0;
        for (size_t y = sc.start_year;
             y + sc.years <= sc.end_year && idx < res.terminal_values.size();
             ++y) {
            per_path_detail d;
            d.start_year     = y;
            d.start_month    = 1;
            d.terminal_value = res.terminal_values[idx];
            d.success        = d.terminal_value > 0.0f;
            d.total_withdrawn = res.withdrawn_per_year * sc.years; // approx
            d.worst_duration_months = 0;
            r.per_path_details.push_back(d);
            ++idx;
        }
    }

    return r;
}

evaluate_result evaluate(const evaluate_input& input, bool collect_per_path) {
    evaluate_result r;

    // --- Validation
    if (input.budget <= 0.0f) {
        r.error = true;
        r.message = "budget must be positive";
        return r;
    }
    if (input.current_age >= static_cast<float>(input.end_age)) {
        r.error = true;
        r.message = "current_age must be < end_age";
        return r;
    }
    if (input.current_balance <= 0.0f) {
        r.error = true;
        r.message = "current_balance must be positive";
        return r;
    }

    r.remaining_horizon_years = compute_remaining_horizon(input.current_age,
                                                          input.end_age);
    if (r.remaining_horizon_years == 0) {
        r.error = true;
        r.message = "remaining horizon is 0 years";
        return r;
    }

    r.budget = input.budget;
    r.comparison_target_success = input.comparison_target_success;

    // --- Build the scenario (same as compute())
    scenario sc;
    sc.portfolio          = input.portfolio;
    sc.years              = r.remaining_horizon_years;
    sc.rebalance          = input.rebalance;
    sc.initial_value      = input.current_balance;
    sc.withdraw_frequency = input.withdraw_frequency;
    sc.fees               = input.fees;
    sc.wmethod            = WithdrawalMethod::STANDARD;
    sc.values         = load_values(sc.portfolio);
    sc.inflation_data = load_inflation(sc.values, input.inflation);

    if (input.historical_start_year > 0)
        sc.start_year = input.historical_start_year;
    else
        sc.start_year = sc.inflation_data.front().year;
    if (input.historical_end_year > 0)
        sc.end_year = input.historical_end_year;
    else
        sc.end_year = sc.inflation_data.back().year;

    // SSA mapping
    size_t social_delay = 0;
    if (input.ssa_enabled && input.ssa_annual_income > 0.0f) {
        sc.social_security = true;
        sc.social_coverage = 0.0f;
        sc.social_amount   = input.ssa_annual_income;
        social_delay = compute_social_delay(input.current_age,
                                            input.ssa_start_age);
        sc.social_delay    = social_delay;
    }

    // --- Single simulation at the requested budget
    float budget_wr_pct = (input.budget / input.current_balance) * 100.0f;
    sc.wr = budget_wr_pct;
    auto results = swr::simulation(sc);
    r.probability_of_success = results.success_rate;

    // --- Per-path detail at the user's exact budget
    if (collect_per_path) {
        size_t idx = 0;
        for (size_t y = sc.start_year;
             y + sc.years <= sc.end_year && idx < results.terminal_values.size();
             ++y) {
            per_path_detail d;
            d.start_year     = y;
            d.start_month    = 1;
            d.terminal_value = results.terminal_values[idx];
            d.success        = d.terminal_value > 0.0f;
            d.total_withdrawn = results.withdrawn_per_year * sc.years;
            d.worst_duration_months = 0;
            r.per_path_details.push_back(d);
            ++idx;
        }
    }

    // --- SSA offset this year
    if (input.ssa_enabled && social_delay == 0) {
        r.ssa_offset_this_year = input.ssa_annual_income;
    } else {
        r.ssa_offset_this_year = 0.0f;
    }
    r.portfolio_withdrawal_this_year = input.budget - r.ssa_offset_this_year;

    // --- Signal
    r.signal = classify_signal(input.budget, input.prior_year_amount);

    // --- Comparison: binary search for max budget at comparison_target_success
    float low  = input.solver_min_wr_pct;
    float high = input.solver_max_wr_pct;
    float tol_pct = (input.solver_tolerance / input.current_balance) * 100.0f;
    if (tol_pct <= 0.0f) tol_pct = 0.0001f;

    float success_at_low = run_simulation_get_success(sc, low);
    if (success_at_low < input.comparison_target_success) {
        r.comparison_supported = false;
        r.comparison_max_budget = 0.0f;
    } else {
        r.comparison_supported = true;

        float success_at_high = run_simulation_get_success(sc, high);
        if (success_at_high >= input.comparison_target_success) {
            r.comparison_max_budget = (high / 100.0f) * input.current_balance;
        } else {
            while ((high - low) > tol_pct) {
                float mid = (low + high) / 2.0f;
                float succ = run_simulation_get_success(sc, mid);
                if (succ >= input.comparison_target_success) {
                    low = mid;
                } else {
                    high = mid;
                }
            }
            r.comparison_max_budget = (low / 100.0f) * input.current_balance;
        }
    }

    return r;
}

} // namespace swr
