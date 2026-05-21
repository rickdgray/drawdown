//=======================================================================
// Copyright Rick Gray 2026.
// Distributed under the MIT License.
// (See accompanying file LICENSE or copy at
//  http://opensource.org/licenses/MIT)
//
// Test main for drawdown. All TEST() blocks live here.
//=======================================================================
#include "test_assert.hpp"
#include "dynamic.hpp"
#include "cli.hpp"
#include "output_formatter.hpp"
#include <sstream>
#include <algorithm>

TEST(sanity) {
    CHECK(1 + 1 == 2);
}

// --- Task 3: compute_remaining_horizon

TEST(remaining_horizon_whole_ages) {
    CHECK_EQ(swr::compute_remaining_horizon(65.0f, 92), (size_t)27);
}

TEST(remaining_horizon_fractional_age_rounds_up) {
    CHECK_EQ(swr::compute_remaining_horizon(67.5f, 92), (size_t)25);
    CHECK_EQ(swr::compute_remaining_horizon(67.1f, 92), (size_t)25);
}

TEST(remaining_horizon_at_or_past_end) {
    CHECK_EQ(swr::compute_remaining_horizon(92.0f, 92), (size_t)0);
    CHECK_EQ(swr::compute_remaining_horizon(95.0f, 92), (size_t)0);
}

// --- Task 4: compute_social_delay

TEST(social_delay_fractional_current_age_rounds_up) {
    CHECK_EQ(swr::compute_social_delay(67.5f, 70), (size_t)3);
    CHECK_EQ(swr::compute_social_delay(67.0f, 70), (size_t)3);
    CHECK_EQ(swr::compute_social_delay(67.9f, 70), (size_t)3);
}

TEST(social_delay_at_or_past_start_age) {
    CHECK_EQ(swr::compute_social_delay(70.0f, 70), (size_t)0);
    CHECK_EQ(swr::compute_social_delay(72.0f, 70), (size_t)0);
}

TEST(social_delay_whole_years) {
    CHECK_EQ(swr::compute_social_delay(65.0f, 70), (size_t)5);
}

// --- Task 5: apply_smoothing

TEST(smoothing_raw_within_band_unchanged) {
    bool applied = true;
    float result = swr::apply_smoothing(42500.0f, 42000.0f, 0.10f, &applied);
    CHECK_NEAR(result, 42500.0f, 0.01f);
    CHECK(applied == false);
}

TEST(smoothing_raw_above_upper_capped) {
    bool applied = false;
    float result = swr::apply_smoothing(50000.0f, 42000.0f, 0.10f, &applied);
    CHECK_NEAR(result, 46200.0f, 0.01f); // 42000 * 1.10
    CHECK(applied == true);
}

TEST(smoothing_raw_below_lower_floored) {
    bool applied = false;
    float result = swr::apply_smoothing(30000.0f, 42000.0f, 0.10f, &applied);
    CHECK_NEAR(result, 37800.0f, 0.01f); // 42000 * 0.90
    CHECK(applied == true);
}

TEST(smoothing_disabled_when_max_change_zero) {
    bool applied = true;
    float result = swr::apply_smoothing(99999.0f, 42000.0f, 0.0f, &applied);
    CHECK_NEAR(result, 99999.0f, 0.01f);
    CHECK(applied == false);
}

TEST(smoothing_disabled_when_no_prior) {
    bool applied = true;
    float result = swr::apply_smoothing(99999.0f, 0.0f, 0.10f, &applied);
    CHECK_NEAR(result, 99999.0f, 0.01f);
    CHECK(applied == false);
}

TEST(smoothing_at_exact_boundary) {
    bool applied = true;
    // exact upper: 42000 * 1.10 = 46200
    float result = swr::apply_smoothing(46200.0f, 42000.0f, 0.10f, &applied);
    CHECK_NEAR(result, 46200.0f, 0.01f);
    CHECK(applied == false); // not strictly above, so unchanged
}

// --- Task 6: classify_signal

TEST(signal_no_prior_returns_none) {
    CHECK(swr::classify_signal(42000.0f, 0.0f) == swr::Signal::NONE);
}

TEST(signal_within_deadband_is_hold) {
    // prior = 42000, deadband = +/- 2% = (41160, 42840)
    CHECK(swr::classify_signal(42000.0f, 42000.0f) == swr::Signal::HOLD);
    CHECK(swr::classify_signal(42310.0f, 42000.0f) == swr::Signal::HOLD); // +0.74%
    CHECK(swr::classify_signal(41200.0f, 42000.0f) == swr::Signal::HOLD); // -1.90%
}

TEST(signal_above_deadband_is_increase) {
    // +2.01% threshold
    CHECK(swr::classify_signal(42841.0f, 42000.0f) == swr::Signal::INCREASE); // +2.0024%
    CHECK(swr::classify_signal(45000.0f, 42000.0f) == swr::Signal::INCREASE); // +7.14%
}

TEST(signal_below_deadband_is_decrease) {
    CHECK(swr::classify_signal(41159.0f, 42000.0f) == swr::Signal::DECREASE); // -2.0024%
    CHECK(swr::classify_signal(39000.0f, 42000.0f) == swr::Signal::DECREASE); // -7.14%
}

// --- Task 8/9: CLI parser basics + error paths

static swr::cli::command_schema make_test_schema() {
    using namespace swr::cli;
    command_schema s;
    s.command_name = "test_cmd";
    s.flags.push_back({"foo", "f", FlagGroup::REQUIRED, FlagKind::VALUE,
                       "a required value flag", ""});
    s.flags.push_back({"bar", "b", FlagGroup::COMMON, FlagKind::VALUE,
                       "an optional value flag with default", "default_bar"});
    s.flags.push_back({"verbose", "v", FlagGroup::COMMON, FlagKind::PRESENCE,
                       "a presence flag", ""});
    return s;
}

TEST(cli_parses_space_separated_value) {
    auto schema = make_test_schema();
    auto p = swr::cli::parse_flags({"--foo", "hello"}, schema);
    CHECK_EQ(swr::cli::get_value(p, schema, "foo"), std::string("hello"));
}

TEST(cli_parses_equals_separated_value) {
    auto schema = make_test_schema();
    auto p = swr::cli::parse_flags({"--foo=hello"}, schema);
    CHECK_EQ(swr::cli::get_value(p, schema, "foo"), std::string("hello"));
}

TEST(cli_uses_default_for_missing_optional) {
    auto schema = make_test_schema();
    auto p = swr::cli::parse_flags({"--foo", "x"}, schema);
    CHECK_EQ(swr::cli::get_value(p, schema, "bar"), std::string("default_bar"));
}

TEST(cli_presence_flag_no_value) {
    auto schema = make_test_schema();
    auto p = swr::cli::parse_flags({"--foo", "x", "--verbose"}, schema);
    CHECK(swr::cli::get_presence(p, "verbose"));
}

TEST(cli_presence_flag_absent) {
    auto schema = make_test_schema();
    auto p = swr::cli::parse_flags({"--foo", "x"}, schema);
    CHECK(!swr::cli::get_presence(p, "verbose"));
}

TEST(cli_missing_required_flag_throws) {
    auto schema = make_test_schema();
    bool threw = false;
    try {
        swr::cli::parse_flags({"--bar", "x"}, schema); // foo is required, missing
    } catch (const std::runtime_error& e) {
        threw = true;
        std::string msg = e.what();
        CHECK(msg.find("foo") != std::string::npos);
    }
    CHECK(threw);
}

TEST(cli_unknown_flag_throws) {
    auto schema = make_test_schema();
    bool threw = false;
    try {
        swr::cli::parse_flags({"--foo", "x", "--mystery"}, schema);
    } catch (const std::runtime_error& e) {
        threw = true;
        std::string msg = e.what();
        CHECK(msg.find("mystery") != std::string::npos);
    }
    CHECK(threw);
}

TEST(cli_value_flag_missing_value_throws) {
    auto schema = make_test_schema();
    bool threw = false;
    try {
        swr::cli::parse_flags({"--foo"}, schema); // dangling --foo
    } catch (const std::runtime_error&) {
        threw = true;
    }
    CHECK(threw);
}

TEST(cli_mutually_exclusive_throws) {
    using namespace swr::cli;
    command_schema s;
    s.command_name = "tx";
    s.flags.push_back({"json", "j", FlagGroup::COMMON, FlagKind::PRESENCE, "", ""});
    s.flags.push_back({"csv",  "c", FlagGroup::COMMON, FlagKind::PRESENCE, "", ""});
    s.mutually_exclusive.push_back({"json", "csv"});
    bool threw = false;
    try {
        parse_flags({"--json", "--csv"}, s);
    } catch (const std::runtime_error& e) {
        threw = true;
        std::string msg = e.what();
        CHECK(msg.find("json") != std::string::npos);
        CHECK(msg.find("csv")  != std::string::npos);
    }
    CHECK(threw);
}

TEST(cli_help_skips_required_check) {
    auto schema = make_test_schema();
    auto p = swr::cli::parse_flags({"--help"}, schema);
    CHECK(p.help_requested);
}

// --- Task 10: render_help

TEST(cli_render_help_groups_flags) {
    auto schema = make_test_schema();
    schema.one_line_description = "test description";
    std::stringstream ss;
    swr::cli::render_help(ss, schema);
    std::string out = ss.str();
    CHECK(out.find("Required") != std::string::npos);
    CHECK(out.find("Common")   != std::string::npos);
    CHECK(out.find("--foo")    != std::string::npos);
    CHECK(out.find("--bar")    != std::string::npos);
    CHECK(out.find("--verbose")!= std::string::npos);
    CHECK(out.find("default_bar") != std::string::npos);
}

// --- Task 12: output_formatter tests

static swr::output::report make_test_report() {
    using namespace swr::output;
    report r;
    r.command_name = "test_cmd";
    section inputs;
    inputs.name = "inputs";
    inputs.title = "Inputs";
    inputs.fields.push_back({"Current balance", 850000.0,
                             field::Hint::DOLLARS});
    inputs.fields.push_back({"Current age", 67.5, field::Hint::NONE});
    r.sections.push_back(inputs);

    section results;
    results.name = "results";
    results.title = "Results";
    results.fields.push_back({"Final budget", 42310.0,
                              field::Hint::DOLLARS});
    results.fields.push_back({"Success at budget", 80.4,
                              field::Hint::PERCENT});
    r.sections.push_back(results);
    return r;
}

TEST(output_text_contains_command_and_sections) {
    std::stringstream ss;
    auto r = make_test_report();
    swr::output::emit(ss, r, swr::output::Mode::TEXT);
    std::string s = ss.str();
    CHECK(s.find("test_cmd") != std::string::npos);
    CHECK(s.find("Inputs")   != std::string::npos);
    CHECK(s.find("Results")  != std::string::npos);
    CHECK(s.find("$850,000") != std::string::npos);
    CHECK(s.find("80.4%")    != std::string::npos);
}

TEST(output_json_contains_command_and_sections) {
    std::stringstream ss;
    auto r = make_test_report();
    swr::output::emit(ss, r, swr::output::Mode::JSON);
    std::string s = ss.str();
    CHECK(s.find("\"command\"") != std::string::npos);
    CHECK(s.find("\"inputs\"")  != std::string::npos);
    CHECK(s.find("\"results\"") != std::string::npos);
    CHECK(s.find("\"current_balance\"") != std::string::npos);
    CHECK(s.find("\"success_at_budget\"") != std::string::npos);
}

TEST(output_csv_header_and_rows) {
    using namespace swr::output;
    report r;
    r.command_name = "test_cmd";
    r.csv_data.preamble_comments = {"hello world"};
    r.csv_data.column_headers = {"start_year", "success", "terminal_value"};
    r.csv_data.rows = {
        {{int64_t(1871), true,  double(1234567.0)}},
        {{int64_t(1872), false, double(0.0)}},
    };
    std::stringstream ss;
    emit(ss, r, Mode::CSV);
    std::string s = ss.str();
    CHECK(s.find("# hello world") != std::string::npos);
    CHECK(s.find("start_year,success,terminal_value") != std::string::npos);
    CHECK(s.find("1871,1,") != std::string::npos);
    CHECK(s.find("1872,0,0.00") != std::string::npos);
}

TEST(output_error_text_mode) {
    std::stringstream ss;
    swr::output::emit_error(ss, "test_cmd", "something broke",
                            swr::output::Mode::TEXT);
    CHECK(ss.str().find("Error: something broke") != std::string::npos);
}

TEST(output_error_json_mode) {
    std::stringstream ss;
    swr::output::emit_error(ss, "test_cmd", "something broke",
                            swr::output::Mode::JSON);
    std::string s = ss.str();
    CHECK(s.find("\"command\": \"test_cmd\"") != std::string::npos);
    CHECK(s.find("\"error\": \"something broke\"") != std::string::npos);
}

TEST(compute_basic_no_ssa_no_smoothing) {
    swr::dynamic_input in;
    in.current_balance = 850000.0f;
    in.current_age     = 67.0f;
    in.end_age         = 92;
    in.portfolio       = swr::parse_portfolio("us_stocks:60;us_bonds:40;", false);
    swr::normalize_portfolio(in.portfolio);
    in.inflation       = "us_inflation";
    in.rebalance       = swr::Rebalancing::YEARLY;
    in.target_success  = 80.0f;
    in.withdraw_frequency = 12;
    in.fees            = 0.001f;
    auto r = swr::compute(in);
    CHECK(!r.error);
    CHECK_EQ(r.remaining_horizon_years, (size_t)25);
    // Sanity bounds: typical sustainable WR for 25y 60/40 at 80% target
    // is in the 4-6% range on real data. final budget should be 4-6% of
    // 850k = 34k-51k.
    CHECK(r.final_spending_budget > 25000.0f);
    CHECK(r.final_spending_budget < 70000.0f);
    CHECK(r.signal == swr::Signal::NONE); // no prior_year_amount
}

TEST(dynamic_solver_matches_linear_sweep_reference) {
    // Build the same scenario two ways:
    // 1) via dynamic::compute (binary search on WR)
    // 2) via direct linear sweep over swr::simulation (test-only reference)
    // They should agree on the max WR (within a small tolerance).

    auto portfolio = swr::parse_portfolio("us_stocks:60;us_bonds:40;", false);
    swr::normalize_portfolio(portfolio);
    const float  balance   = 1000000.0f;
    const size_t years     = 25;
    const float  target    = 80.0f;

    // --- dynamic_dollar path
    swr::dynamic_input in;
    in.current_balance   = balance;
    in.current_age       = 65.0f;
    in.end_age           = 65 + years; // 90
    in.portfolio         = portfolio;
    in.inflation         = "us_inflation";
    in.rebalance         = swr::Rebalancing::YEARLY;
    in.target_success    = target;
    in.withdraw_frequency = 12;
    in.fees              = 0.001f;
    in.solver_tolerance  = 100.0f; // $100 = ~0.01% at $1M
    auto r = swr::compute(in);
    CHECK(!r.error);
    float dynamic_wr_pct =
        (r.raw_calculated_withdrawal / balance) * 100.0f;

    // --- Direct linear sweep (test-only reference)
    swr::scenario sc;
    sc.portfolio          = portfolio;
    sc.years              = years;
    sc.rebalance          = swr::Rebalancing::YEARLY;
    sc.initial_value      = balance;
    sc.withdraw_frequency = 12;
    sc.fees               = 0.001f;
    sc.wmethod            = swr::WithdrawalMethod::STANDARD;
    sc.values         = swr::load_values(sc.portfolio);
    sc.inflation_data = swr::load_inflation(sc.values, "us_inflation");
    sc.start_year     = sc.inflation_data.front().year;
    sc.end_year       = sc.inflation_data.back().year;

    // To save time, narrow the sweep around dynamic_wr_pct ±0.5% at fine step.
    const float step = 0.01f;
    const float sweep_lo = std::max(0.5f, dynamic_wr_pct - 0.5f);
    const float sweep_hi = std::min(20.0f, dynamic_wr_pct + 0.5f);
    float linear_max_wr = 0.0f;
    for (float wr = sweep_lo; wr <= sweep_hi + step/2.0f; wr += step) {
        sc.wr = wr;
        auto res = swr::simulation(sc);
        if (res.success_rate >= target) linear_max_wr = wr;
    }

    std::cerr << "[dynamic_solver_matches_linear_sweep_reference]"
              << " dynamic_wr_pct=" << dynamic_wr_pct
              << " linear_max_wr=" << linear_max_wr
              << " gap=" << std::abs(dynamic_wr_pct - linear_max_wr)
              << std::endl;

    // Allow 0.05% tolerance.
    CHECK_NEAR(dynamic_wr_pct, linear_max_wr, 0.05f);
}

// --- Short-flag parsing

TEST(cli_parses_short_flag) {
    using namespace swr::cli;
    command_schema s;
    s.command_name = "tx";
    s.flags.push_back({"foo", "f", FlagGroup::REQUIRED, FlagKind::VALUE,
                       "required flag with short alias", ""});
    s.flags.push_back({"verbose", "v", FlagGroup::COMMON, FlagKind::PRESENCE,
                       "presence flag with short alias", ""});

    // Short-form value: -f hello
    auto p1 = parse_flags({"-f", "hello"}, s);
    CHECK_EQ(get_value(p1, s, "foo"), std::string("hello"));

    // Short-form value with equals: -f=hello
    auto p2 = parse_flags({"-f=hello"}, s);
    CHECK_EQ(get_value(p2, s, "foo"), std::string("hello"));

    // Short-form presence: -v
    auto p3 = parse_flags({"-f", "x", "-v"}, s);
    CHECK(get_presence(p3, "verbose"));
}

TEST(cli_short_and_long_interchangeable) {
    using namespace swr::cli;
    command_schema s;
    s.command_name = "tx";
    s.flags.push_back({"foo", "f", FlagGroup::REQUIRED, FlagKind::VALUE, "", ""});

    // Result keyed by long name regardless of which form was passed.
    auto p_short = parse_flags({"-f", "v1"}, s);
    auto p_long  = parse_flags({"--foo", "v1"}, s);
    CHECK_EQ(get_value(p_short, s, "foo"), get_value(p_long, s, "foo"));
}

// --- SSA integration in dynamic_dollar

TEST(compute_with_ssa_before_start_age) {
    // current_age 67, ssa starts at 70 — SSA is not yet active this year.
    swr::dynamic_input in;
    in.current_balance = 850000.0f;
    in.current_age     = 67.0f;
    in.end_age         = 92;
    in.portfolio       = swr::parse_portfolio("us_stocks:60;us_bonds:40;", false);
    swr::normalize_portfolio(in.portfolio);
    in.inflation       = "us_inflation";
    in.rebalance       = swr::Rebalancing::YEARLY;
    in.target_success  = 80.0f;
    in.ssa_enabled        = true;
    in.ssa_annual_income  = 24000.0f;
    in.ssa_start_age      = 70;

    auto r = swr::compute(in);
    CHECK(!r.error);
    CHECK_NEAR(r.ssa_offset_this_year, 0.0f, 0.01f);
    // Portfolio carries the full load this year
    CHECK_NEAR(r.portfolio_withdrawal_this_year, r.final_spending_budget, 0.01f);
}

TEST(compute_with_ssa_at_or_past_start_age) {
    // current_age 72, ssa starts at 70 — SSA is active.
    swr::dynamic_input in;
    in.current_balance = 850000.0f;
    in.current_age     = 72.0f;
    in.end_age         = 92;
    in.portfolio       = swr::parse_portfolio("us_stocks:60;us_bonds:40;", false);
    swr::normalize_portfolio(in.portfolio);
    in.inflation       = "us_inflation";
    in.rebalance       = swr::Rebalancing::YEARLY;
    in.target_success  = 80.0f;
    in.ssa_enabled        = true;
    in.ssa_annual_income  = 24000.0f;
    in.ssa_start_age      = 70;

    auto r = swr::compute(in);
    CHECK(!r.error);
    CHECK_NEAR(r.ssa_offset_this_year, 24000.0f, 0.01f);
    CHECK_NEAR(r.portfolio_withdrawal_this_year,
               r.final_spending_budget - 24000.0f, 0.01f);
}

// --- Smoothing integration in dynamic_dollar

TEST(compute_with_smoothing_caps_upward) {
    // Set prior_year_amount far below the unsmoothed solver output.
    // The cap should force smoothed_withdrawal to prior * 1.10.
    swr::dynamic_input in;
    in.current_balance = 850000.0f;
    in.current_age     = 67.0f;
    in.end_age         = 92;
    in.portfolio       = swr::parse_portfolio("us_stocks:60;us_bonds:40;", false);
    swr::normalize_portfolio(in.portfolio);
    in.inflation       = "us_inflation";
    in.rebalance       = swr::Rebalancing::YEARLY;
    in.target_success  = 80.0f;
    in.smoothing_enabled    = true;
    in.smoothing_max_change = 0.10f;
    in.prior_year_amount    = 30000.0f; // ~$43k unsmoothed > $33k cap

    auto r = swr::compute(in);
    CHECK(!r.error);
    CHECK(r.smoothing_applied);
    // smoothed should equal prior * 1.10 = 33000
    CHECK_NEAR(r.smoothed_withdrawal, 33000.0f, 1.0f);
    CHECK_NEAR(r.final_spending_budget, 33000.0f, 1.0f);
    // raw is the un-smoothed solver output, well above the cap
    CHECK(r.raw_calculated_withdrawal > 33000.0f);
}

TEST(compute_with_smoothing_inside_band_unchanged) {
    // Prior matches the unsmoothed budget closely — no capping needed.
    swr::dynamic_input in;
    in.current_balance = 850000.0f;
    in.current_age     = 67.0f;
    in.end_age         = 92;
    in.portfolio       = swr::parse_portfolio("us_stocks:60;us_bonds:40;", false);
    swr::normalize_portfolio(in.portfolio);
    in.inflation       = "us_inflation";
    in.rebalance       = swr::Rebalancing::YEARLY;
    in.target_success  = 80.0f;
    in.smoothing_enabled    = true;
    in.smoothing_max_change = 0.10f;
    in.prior_year_amount    = 43000.0f; // close to the unsmoothed ~$43.5k

    auto r = swr::compute(in);
    CHECK(!r.error);
    CHECK(!r.smoothing_applied);
    CHECK_NEAR(r.smoothed_withdrawal, r.raw_calculated_withdrawal, 0.01f);
}

// --- Embedded data sanity

TEST(embedded_us_stocks_first_and_last_data_points) {
    // load_values() normalises and converts to monthly return ratios, so we
    // check the shape of the data rather than raw CSV values.
    auto portfolio = swr::parse_portfolio("us_stocks:100;", false);
    swr::normalize_portfolio(portfolio);
    auto values = swr::load_values(portfolio);
    CHECK(!values.empty());
    auto& us_stocks = values[0];
    CHECK(!us_stocks.empty());

    // First entry: month=1, year=1871; normalize_data() seeds the base to 1.0f
    auto& first = us_stocks.front();
    CHECK_EQ(first.month, (size_t)1);
    CHECK_EQ(first.year,  (size_t)1871);
    CHECK_NEAR(first.value, 1.0f, 0.01f);

    // Last entry: month=12, year=2025; value is the monthly return ratio
    // raw CSV: 83237634.92 / 83186717.38 ≈ 1.000612
    auto& last = us_stocks.back();
    CHECK_EQ(last.month, (size_t)12);
    CHECK_EQ(last.year,  (size_t)2025);
    CHECK_NEAR(last.value, 1.000612f, 0.0001f);
}

int main() {
    for (auto& tc : test::registry()) {
        test::current_test() = tc.name;
        int fails_before = test::fail_count();
        tc.fn();
        if (test::fail_count() == fails_before) {
            ++test::pass_count();
            std::cout << "PASS " << tc.name << std::endl;
        }
    }

    std::cout << "\n" << test::pass_count() << " passed, "
              << test::fail_count() << " failed" << std::endl;
    return test::fail_count() == 0 ? 0 : 1;
}
