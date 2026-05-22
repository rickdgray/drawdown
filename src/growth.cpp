//=======================================================================
// Copyright Rick Gray 2026.
// Distributed under the MIT License.
// (See accompanying file LICENSE or copy at
//  http://opensource.org/licenses/MIT)
//
// Historical-backtest accumulation engine (real-dollar accounting).
//=======================================================================
#include "growth.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <string>
#include <vector>

#include "data.hpp"

namespace swr {

growth_result evaluate_growth(const growth_input& input, bool collect_per_path) {
    growth_result r;

    // --- Validation
    if (input.years == 0) {
        r.error = true;
        r.message = "years must be > 0";
        return r;
    }
    if (input.target <= 0.0f) {
        r.error = true;
        r.message = "target must be positive";
        return r;
    }
    if (input.portfolio.empty()) {
        r.error = true;
        r.message = "portfolio must not be empty";
        return r;
    }

    // --- Load data
    auto values = load_values(input.portfolio);
    if (values.empty()) {
        r.error = true;
        r.message = "failed to load portfolio data";
        return r;
    }
    auto inflation_data = load_inflation(values, input.inflation_series);
    if (inflation_data.empty()) {
        r.error = true;
        r.message = "failed to load inflation data";
        return r;
    }

    // --- Historical window
    size_t start_year = (input.historical_start_year > 0)
                        ? input.historical_start_year
                        : inflation_data.front().year;
    size_t end_year   = (input.historical_end_year > 0)
                        ? input.historical_end_year
                        : inflation_data.back().year;

    const size_t total_months = input.years * 12;
    const size_t n_assets     = values.size();

    size_t successes = 0;
    size_t attempts  = 0;
    std::vector<float> terminal_values;

    for (size_t y = start_year; y <= end_year; ++y) {
        // Check that this start year has a month=1 entry in all series
        bool valid = is_start_valid(inflation_data, y, 1);
        for (size_t i = 0; i < n_assets && valid; ++i) {
            valid = is_start_valid(values[i], y, 1);
        }
        if (!valid) continue;

        // Locate start iterators
        std::vector<data_vector::const_iterator> asset_its;
        asset_its.reserve(n_assets);
        for (size_t i = 0; i < n_assets; ++i) {
            asset_its.push_back(get_start(values[i], y, 1));
        }
        auto infl_it = get_start(inflation_data, y, 1);

        // Verify there are at least total_months entries from the start.
        // We need total_months steps (each step: read current, advance).
        {
            bool enough = true;
            for (size_t i = 0; i < n_assets && enough; ++i) {
                auto it_check = asset_its[i];
                size_t count = 0;
                while (it_check != values[i].end() && count < total_months) {
                    ++it_check;
                    ++count;
                }
                if (count < total_months) enough = false;
            }
            if (enough) {
                auto infl_check = infl_it;
                size_t count = 0;
                while (infl_check != inflation_data.end() && count < total_months) {
                    ++infl_check;
                    ++count;
                }
                if (count < total_months) enough = false;
            }
            if (!enough) continue;
        }

        // --- Run one path
        // Per-asset balance in real (today's) dollars.
        // allocation is stored as a percentage (e.g. 80.0 for 80%); convert
        // to a fraction by dividing by 100.
        std::vector<float> bal(n_assets);
        for (size_t i = 0; i < n_assets; ++i) {
            bal[i] = input.initial_balance * (input.portfolio[i].allocation / 100.0f);
        }

        // Iterators start at month=1 of the chosen year.
        // Each step: read value at current position, then advance — same
        // pattern as simulation.cpp.
        std::vector<data_vector::const_iterator> cur_its = asset_its;
        auto cur_infl = infl_it;

        for (size_t m = 0; m < total_months; ++m) {
            float infl_ratio = cur_infl->value;   // monthly inflation ratio
            ++cur_infl;

            float total = 0.0f;
            for (size_t i = 0; i < n_assets; ++i) {
                float nom_ratio = cur_its[i]->value;   // nominal monthly return ratio
                ++cur_its[i];
                float real_ratio = nom_ratio / infl_ratio;
                bal[i] *= real_ratio;
                // Add monthly contribution split by target weights (fraction)
                bal[i] += input.monthly_contribution * (input.portfolio[i].allocation / 100.0f);
                total  += bal[i];
            }

            // Rebalance if needed
            bool do_rebalance = false;
            if (input.rebalance == Rebalancing::MONTHLY) {
                do_rebalance = true;
            } else if (input.rebalance == Rebalancing::YEARLY) {
                do_rebalance = ((m + 1) % 12 == 0);
            }
            if (do_rebalance && total > 0.0f) {
                for (size_t i = 0; i < n_assets; ++i) {
                    bal[i] = total * (input.portfolio[i].allocation / 100.0f);
                }
            }
        }

        float terminal = 0.0f;
        for (size_t i = 0; i < n_assets; ++i) terminal += bal[i];

        bool reached = (terminal >= input.target);
        if (reached) ++successes;

        terminal_values.push_back(terminal);

        if (collect_per_path) {
            growth_per_path gp;
            gp.start_year      = y;
            gp.reached_target  = reached;
            gp.terminal_value  = terminal;
            r.per_path_details.push_back(gp);
        }

        ++attempts;
    }

    r.paths_evaluated  = attempts;
    r.paths_successful = successes;

    if (attempts == 0) {
        r.error   = true;
        r.message = "no valid historical paths found";
        return r;
    }

    r.probability_of_reaching_target =
        static_cast<float>(successes) / static_cast<float>(attempts) * 100.0f;

    // Compute terminal value stats
    std::sort(terminal_values.begin(), terminal_values.end());
    r.min_terminal_value = terminal_values.front();
    r.max_terminal_value = terminal_values.back();
    if (terminal_values.size() % 2 == 0) {
        size_t mid = terminal_values.size() / 2;
        r.median_terminal_value = (terminal_values[mid - 1] + terminal_values[mid]) / 2.0f;
    } else {
        r.median_terminal_value = terminal_values[terminal_values.size() / 2];
    }

    return r;
}

coast_result solve_coast(const coast_input& input) {
    coast_result r;

    // Force no contributions for coast
    growth_input probe = input.base;
    probe.monthly_contribution = 0.0f;

    // Auto-set search_max_balance if zero
    float lo = input.search_min_balance;
    float hi = (input.search_max_balance > 0.0f)
               ? input.search_max_balance
               : 2.0f * input.base.target;

    if (hi <= lo) {
        r.error   = true;
        r.message = "search_max_balance must be > search_min_balance";
        return r;
    }

    // Probe at lo — if it already meets target, return immediately
    probe.initial_balance = lo;
    auto r_lo = evaluate_growth(probe);
    if (r_lo.error) {
        r.error   = true;
        r.message = r_lo.message;
        return r;
    }
    if (r_lo.probability_of_reaching_target >= input.target_success) {
        r.required_balance      = lo;
        r.probability_at_result = r_lo.probability_of_reaching_target;
        r.detail                = r_lo;
        return r;
    }

    // Probe at hi — must meet target for binary search to be useful
    probe.initial_balance = hi;
    auto r_hi = evaluate_growth(probe);
    if (r_hi.error) {
        r.error   = true;
        r.message = r_hi.message;
        return r;
    }
    if (r_hi.probability_of_reaching_target < input.target_success) {
        r.error   = true;
        r.message = "even search_max_balance (" + std::to_string((int)hi) +
                    ") fails to meet target_success; raise search_max_balance";
        return r;
    }

    // Binary search: lo is always below target, hi is always at/above target.
    while ((hi - lo) > input.balance_tolerance) {
        float mid = (lo + hi) / 2.0f;
        probe.initial_balance = mid;
        auto r_mid = evaluate_growth(probe);
        if (r_mid.error) {
            r.error   = true;
            r.message = r_mid.message;
            return r;
        }
        if (r_mid.probability_of_reaching_target >= input.target_success) {
            hi = mid;
        } else {
            lo = mid;
        }
    }

    // hi is the lowest balance that meets target
    probe.initial_balance = hi;
    auto r_final = evaluate_growth(probe, true);
    if (r_final.error) {
        r.error   = true;
        r.message = r_final.message;
        return r;
    }

    r.required_balance      = hi;
    r.probability_at_result = r_final.probability_of_reaching_target;
    r.detail                = r_final;
    return r;
}

accumulate_result solve_accumulate(const accumulate_input& input) {
    accumulate_result r;

    growth_input probe = input.base;

    for (size_t y = 1; y <= input.max_years_to_scan; ++y) {
        probe.years = y;
        auto gr = evaluate_growth(probe);
        if (gr.error) {
            r.error   = true;
            r.message = gr.message;
            return r;
        }
        if (gr.probability_of_reaching_target >= input.target_success) {
            r.required_years        = y;
            r.probability_at_result = gr.probability_of_reaching_target;
            // Re-evaluate with per-path detail
            auto gr_detail = evaluate_growth(probe, true);
            r.detail = gr_detail;
            return r;
        }
    }

    r.error   = true;
    r.message = "target not reachable within " +
                std::to_string(input.max_years_to_scan) + " years";
    return r;
}

} // namespace swr
