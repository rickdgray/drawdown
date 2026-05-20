# `dynamic_dollar` — design spec

**Date:** 2026-05-19
**Status:** Approved (pending implementation plan)
**Author:** Rick Gray

---

## 1. Goal

Add a new `dynamic_dollar` CLI subcommand that, for a given current portfolio
balance, current age, fixed end age, and target success rate, computes this
year's sustainable annual withdrawal by historical backtesting over the
remaining horizon. Single-person SSA offset (with start age) and rate-of-change
smoothing (capped against a prior-year amount) are supported as optional
behaviors. This is a **per-year point query** — not a multi-year forward
simulator.

Ship alongside the new command:

- **Redesign two retained commands as single-point queries.** The existing
  `swr` and `current_wr` are research-style sweep/solver tools whose behavior
  doesn't fit alongside the new per-year point query. They get reshaped (not
  just renamed) into single-scenario evaluators:
  - `swr` → **`constant_dollar`** (single-WR evaluator). Drops the built-in
    "sweep 6%→2% and return the highest WR meeting 95%" behavior. The new
    command evaluates one WR over one portfolio and reports the success rate.
  - `current_wr` → **`constant_percent`** (single-pct evaluator). Drops the
    portfolio-allocation sweep, the WR sweep, and the STANDARD-methodology
    override. The new command evaluates one fixed percent of current balance
    over one portfolio and reports success rate + spending stats.
- **Delete `failsafe`.** Its sweep-and-solve behavior is functionally
  subsumed by `dynamic_dollar` (set `--current_age 0 --end_age <years>
  --balance <initial>` and you get the same answer). Its portfolio-allocation
  sweep and multi-threshold reporting are dropped — users can drive sweeps
  with shell loops if needed.
- **Conversion** of the three remaining commands from positional args to
  flag-style.
- **Unified output** template across all three commands (text default,
  `--json`, `--csv`).
- **Cleanup** of the existing codebase: drop the HTTP server stack, ~28 unused
  analytical subcommands, the cpp-httplib dependency and submodule, the web
  Dockerfile, sonar configuration, and the committed `compile_commands.json`.

Out of scope:

- Multi-year forward simulator (per-year point query is the chosen mode).
- Parametric Monte Carlo (return mean + stddev). Historical backtesting only.
- PMT fallback. Trivial enough for a spreadsheet; not worth programming.
- Two-person SSA. Single-person only; user supplies start age.
- Removing dead fields from `swr::scenario` (e.g. `glidepath`,
  `vanguard_max_increase`, `dwz_floor`, `flexibility_*`). Phase 1 leaves
  `simulation.hpp` and the `scenario` struct untouched. A Phase 2 cleanup
  may follow as a separate spec.

---

## 2. Architecture

### Module layout (after Phase 1)

```
include/
  data.hpp          (unchanged)
  portfolio.hpp     (unchanged)
  simulation.hpp    (unchanged)
  dynamic.hpp       (NEW — input/result types + compute())
  output_formatter.hpp  (NEW — text/JSON/CSV rendering)
src/
  data.cpp          (unchanged)
  portfolio.cpp     (unchanged)
  simulation.cpp    (mostly unchanged; minor tweaks only if SSA mapping requires)
  dynamic.cpp       (NEW)
  output_formatter.cpp  (NEW)
  main.cpp          (dramatically slimmed: ~4012 → ~600-800 lines)
  test_dynamic.cpp  (NEW — home-grown test main)
```

### Dependency direction

```
dynamic.hpp        ─depends on─▶  simulation.hpp ─▶ portfolio.hpp, data.hpp
output_formatter   ─depends on─▶  (standalone, takes data via key/value maps)
main.cpp           ─depends on─▶  dynamic.hpp, simulation.hpp, output_formatter.hpp
test_dynamic.cpp   ─depends on─▶  dynamic.hpp (+ a tiny assertion header)
```

### Responsibility split

| Responsibility | Module |
|---|---|
| Historical CSV loading, monthly iteration, portfolio rebalancing, per-path success/failure | `simulation` (existing) |
| Per-year-recalc framing: balance/age/end_age → annual budget | `dynamic` (new) |
| Binary search over candidate WRs against the target success rate | `dynamic` (new) |
| SSA mapping (start_age + current_age → existing `social_delay`) | `dynamic` (new) |
| Smoothing: rate-of-change cap given prior_year_amount | `dynamic` (new) |
| Signal classification (`increase` / `hold` / `decrease`) | `dynamic` (new) |
| CLI flag parsing | `main.cpp` (tiny shared parser) |
| Output rendering: text, JSON, CSV | `output_formatter` (new) |

### Final binary surface

After Phase 1, `swr_calculator` exposes exactly three subcommands:

- `constant_dollar` — evaluate a single fixed (real) dollar withdrawal scenario
- `constant_percent` — evaluate a fixed-percent-of-current-balance scenario
- `dynamic_dollar` — the new per-year point query (also subsumes the
  old `failsafe`-style max-WR solver)

---

## 3. Data types

New file `include/dynamic.hpp`:

```cpp
namespace swr {

struct dynamic_input {
    // Portfolio state (current reality)
    float                       current_balance;       // dollars
    float                       current_age;           // years, fractional allowed
    size_t                      end_age;               // fixed planning horizon

    // Portfolio composition + data window
    std::vector<allocation>     portfolio;
    std::string                 inflation;             // inflation series name
    Rebalancing                 rebalance = Rebalancing::NONE;
    size_t                      historical_start_year = 0;  // 0 = full data range
    size_t                      historical_end_year   = 0;  // 0 = full data range

    // Solver target
    float                       target_success = 80.0f; // percent

    // SSA (single person)
    bool                        ssa_enabled        = false;
    float                       ssa_annual_income  = 0.0f;
    size_t                      ssa_start_age      = 0;

    // Smoothing (rate-of-change cap)
    bool                        smoothing_enabled    = false;
    float                       smoothing_max_change = 0.10f; // 10% default
    float                       prior_year_amount    = 0.0f;  // 0 = unset

    // Solver tuning
    size_t                      withdraw_frequency = 12;     // 12 = yearly
    float                       fees               = 0.001f; // 0.1% TER
    float                       solver_tolerance   = 1.0f;   // dollars
    float                       solver_min_wr_pct  = 0.5f;   // floor
    float                       solver_max_wr_pct  = 20.0f;  // ceiling
};

enum class Signal { INCREASE, HOLD, DECREASE, NONE };
std::ostream& operator<<(std::ostream&, Signal);

struct dynamic_result {
    size_t  remaining_horizon_years;
    float   raw_calculated_withdrawal;       // unsmoothed solver output
    bool    smoothing_applied;               // true only if cap was actually hit
    float   smoothed_withdrawal;             // == raw when smoothing disabled or band not hit
    float   final_spending_budget;           // what to actually spend
    float   probability_of_success;          // success rate at final_spending_budget
    float   ssa_offset_this_year;            // 0 if SSA disabled or not yet started
    float   portfolio_withdrawal_this_year;  // final_spending_budget - ssa_offset_this_year
    Signal  signal;                          // NONE if no prior_year_amount

    // Optional per-path detail (populated when CSV output is requested)
    std::vector<std::tuple<size_t, size_t, bool, float, float, size_t>>
        per_path_details;  // (start_year, start_month, success, terminal_value, total_withdrawn, worst_duration_months)

    bool        error = false;
    std::string message;
};

dynamic_result compute(const dynamic_input& input, bool collect_per_path = false);

} // namespace swr
```

Notes on type choices:

- `current_age` is `float` (the spec allows fractional ages, e.g. 67.5).
  `end_age` is `size_t` (a planning constant in whole years).
- `solver_min_wr_pct` and `solver_max_wr_pct` are exposed on the struct but
  defaulted; the CLI doesn't expose them by default to keep the help clean.
- `smoothed_withdrawal` is always populated (kept flat — no `optional`).
- `smoothing_applied = true` only when the cap was *actually* hit (the raw
  exceeded the band). With smoothing enabled but raw inside the band,
  `smoothing_applied = false` and `smoothed_withdrawal == raw`.
- `per_path_details` is optional and populated only when CSV output is
  requested; an empty vector otherwise keeps the result lightweight.
- `error` + `message` mirror `swr::results` for consistency.

---

## 4. CLI design

### Conventions (shared across all three commands)

- Long flags only (`--foo`).
- Value form `--foo value` or `--foo=value`.
- Presence flags take no value (`--json`, `--csv`).
- Help structured as **Required / Common / Advanced**, in that order, with
  per-command description and example invocation.
- All commands accept `--help` and `--json` / `--csv` (mutually exclusive;
  text is default).

### Shared flag names

| Flag | Meaning | Used by |
|---|---|---|
| `--portfolio` | `"us_stocks:60,us_bonds:40"` style | all |
| `--inflation` | Inflation series name | all |
| `--rebalance` | `none \| monthly \| yearly \| threshold` | all |
| `--start_year` | Earliest backtest start year | all |
| `--end_year` | Latest backtest start year | all |
| `--years` | Horizon length (years) | constant_dollar, constant_percent |
| `--initial_value` | Starting portfolio (dollars) | constant_dollar, constant_percent |
| `--target_success` | Target success rate (percent) | dynamic_dollar (also optional pass/fail check on constant_dollar) |
| `--withdraw_frequency` | 1 (monthly) or 12 (yearly) | all |
| `--fees` | TER as a fraction (e.g. `0.001` = 0.1%) | all |
| `--json` | Machine-readable JSON output | all |
| `--csv` | Per-path tabular CSV output | all |

### `dynamic_dollar` flag schema

**Required:**
- `--balance <dollars>` — current portfolio balance
- `--current_age <years>` — fractional allowed
- `--end_age <years>`
- `--portfolio <spec>`
- `--inflation <series>`

**Common:**
- `--target_success <pct>` (default 80)
- `--rebalance <method>` (default `none`)
- `--ssa_income <dollars>` (default 0, disabled)
- `--ssa_start_age <years>` (required if `--ssa_income > 0`)
- `--smoothing <max_pct_fraction>` (default 0, disabled; e.g. `0.10` = ±10%)
- `--prior_amount <dollars>` (required if `--smoothing > 0`; also enables
  `signal` classification even when smoothing is off)
- `--json`
- `--csv`

**Advanced:**
- `--start_year <year>` (default: full data range)
- `--end_year <year>` (default: full data range)
- `--withdraw_frequency <1|12>` (default 12)
- `--fees <fraction>` (default 0.001)
- `--solver_tolerance <dollars>` (default 1)

### `constant_dollar` flag schema

**Required:** `--wr <pct>`, `--portfolio`, `--inflation`, `--years`
**Common:** `--rebalance`, `--start_year`, `--end_year`, `--initial_value`,
`--target_success` (optional; if set, emit pass/fail against it),
`--json`, `--csv`
**Advanced:** `--withdraw_frequency`, `--fees`

### `constant_percent` flag schema

**Required:** `--pct <pct>`, `--portfolio`, `--inflation`, `--years`
**Common:** `--rebalance`, `--start_year`, `--end_year`, `--initial_value`,
`--minimum_floor` (default 3.0; percent of initial), `--json`, `--csv`
**Advanced:** `--withdraw_frequency`, `--fees`

### Validation errors

All errors written to stderr, exit code 1. Sample messages:

- `Error: --smoothing > 0 requires --prior_amount`
- `Error: --ssa_income > 0 requires --ssa_start_age`
- `Error: --current_age must be < --end_age`
- `Error: unknown portfolio asset "xyz" (delegated message from data loader)`
- `Error: --json and --csv are mutually exclusive`

In `--json` mode, the error is JSON:

```json
{ "command": "dynamic_dollar", "error": "--smoothing > 0 requires --prior_amount" }
```

---

## 5. Output formats

All three commands emit the same three-section structure in **text** and **JSON**
modes: `inputs`, `results`, optional `notes`. **CSV** mode emits per-path detail
rows.

### Text (default)

```
<COMMAND_NAME>

Inputs
  <key: value lines>

Results
  <key: value lines>

[Notes
  <optional warnings>]
```

#### Example — `dynamic_dollar`

```
dynamic_dollar

Inputs
  Portfolio:            us_stocks:60, us_bonds:40
  Inflation series:     us_inflation
  Current balance:      $850,000
  Current age:          67.5
  End age:              92
  Remaining horizon:    25 years
  Data window:          1871 – 2025
  Rebalance:            none
  Target success:       80%
  SSA:                  $24,000/yr starting age 70
  Smoothing:            ±10% (prior year: $42,000)
  Withdraw frequency:   yearly
  Fees (TER):           0.10%

Results
  Raw calculated:       $42,310
  Smoothed:             $42,310   (cap not hit)
  Final budget:         $42,310
  Success at budget:    80.4%
  SSA offset this year: $0
  Portfolio withdrawal: $42,310
  Signal vs prior:      hold      (within ±2% of prior)
```

### JSON (`--json`)

```json
{
  "command": "dynamic_dollar",
  "inputs": { ...echo of parsed args, normalized types... },
  "results": { ...command-specific result fields... },
  "notes": [ "...optional list of strings, omitted if empty..." ]
}
```

Conventions:

- `snake_case` field names.
- Dollars as floats, no `$` prefix.
- Percentages as floats (`80.4`, not `"80.4%"`).
- Durations in months as integers.

### CSV (`--csv`)

One row per historical start year used in the backtest. Same header columns
across all three commands.

```
# command: dynamic_dollar
# evaluated_at: $42,310 final_budget (raw $42,310, smoothed N/A)
# portfolio: us_stocks:60,us_bonds:40
# horizon: 25 years
# success_rate: 80.4
start_year,start_month,success,terminal_value,total_withdrawn,worst_duration_months
1871,1,1,1234567.00,1057750.00,
1872,1,1,1085200.00,1057750.00,
...
1965,9,0,0.00,612000.00,342
```

Columns:

- `success`: `1` or `0`
- `terminal_value`: portfolio value at end of horizon (0 if failed)
- `total_withdrawn`: cumulative withdrawn across the horizon
- `worst_duration_months`: populated only for failed paths

Per-command CSV row semantics:

- `constant_dollar` — outcomes at `--wr`
- `constant_percent` — outcomes at `--pct`
- `dynamic_dollar` — outcomes at `final_spending_budget`

---

## 6. Algorithm (`dynamic_dollar` compute flow)

### High-level

```
1. Validate input.
2. Load historical data series.
3. Build a swr::scenario from input + defaults.
4. Binary-search the maximum WR achieving target_success.
   -> raw_calculated_withdrawal
5. Apply smoothing (if enabled and prior_year_amount > 0).
   -> smoothed_withdrawal
6. Re-simulate once at final_spending_budget to get its true probability_of_success.
7. Classify signal (if prior_year_amount > 0).
8. Build dynamic_result.
```

### Step 3 — scenario construction

Mapping `dynamic_input` → `swr::scenario`:

| dynamic_input | scenario | Notes |
|---|---|---|
| `current_balance` | `initial_value` | the engine treats this as the starting value of the run |
| `end_age - current_age` | `years` | cast to `size_t`, **rounding up** for fractional current_age (conservative — longer horizon → lower computed WR) |
| `portfolio` | `portfolio` | direct |
| `rebalance` | `rebalance` | direct |
| `withdraw_frequency` | `withdraw_frequency` | direct |
| `fees` | `fees` | direct |
| `historical_start_year` / `historical_end_year` | `start_year` / `end_year` | when 0, defaulted from the loaded data series' full valid range |
| `ssa_enabled` + `ssa_annual_income` + `ssa_start_age` | `social_security = true`, `social_amount = ssa_annual_income`, `social_coverage = 0`, `social_delay = max(0, ceil(ssa_start_age - current_age))` | use the dollar-amount path. `social_delay` rounded **up** (conservative). |
| (always) | `wmethod = STANDARD` | the constant-real-dollar methodology, which is what we're solving over |

**Note on SSA mapping:** if the existing `social_coverage`/`social_amount`
interaction in `simulation.cpp` doesn't cleanly support
"amount-only, no coverage," a minimal tweak in `simulation.cpp` is in-scope.
Phase 1 leaves `simulation.hpp` untouched, but the `.cpp` may need a small
adjustment.

### Step 4 — solver (binary search on WR %)

```
low  = solver_min_wr_pct       // default 0.5
high = solver_max_wr_pct       // default 20.0
tol_pct = solver_tolerance / current_balance * 100

last_success_rate = 0
while (high - low) > tol_pct:
    mid = (low + high) / 2
    success = simulate(scenario_with_wr = mid).success_rate
    if success >= target_success:
        low = mid
        last_success_rate = success
    else:
        high = mid

raw_wr_pct = low
raw_calculated_withdrawal = raw_wr_pct / 100 * current_balance
```

**Monotonicity:** success rate is monotonically non-increasing in WR (more
spending never improves survival), so binary search is sound.

**Edge cases:**

- **Even `solver_min_wr_pct` fails the target** → portfolio is unsupportable.
  Set `raw_calculated_withdrawal = 0`, `error = true`,
  `message = "even minimum WR fails target_success at this horizon"`. Output
  reports this and exits 1.
- **`solver_max_wr_pct` still meets the target** → portfolio is oversized
  for the horizon. Return `solver_max_wr_pct × balance`, add a note:
  `"WR capped at <max>%; portfolio supports more"`. No error.

### Step 5 — smoothing

```
smoothed = raw
smoothing_applied = false

if smoothing_enabled and prior_year_amount > 0:
    upper = prior_year_amount * (1 + smoothing_max_change)
    lower = prior_year_amount * (1 - smoothing_max_change)
    if raw > upper:
        smoothed = upper
        smoothing_applied = true
    elif raw < lower:
        smoothed = lower
        smoothing_applied = true

final_spending_budget = smoothed
```

### Step 6 — re-simulate at final budget

If `smoothing_applied`, `final_spending_budget != raw_calculated_withdrawal`,
and we need the success probability *at the final budget*. Convert the
smoothed dollar amount back to a WR percent:

```
final_wr_pct = final_spending_budget / current_balance * 100
```

Run one more `swr::simulation` with `scenario.wr = final_wr_pct`.
`probability_of_success = that_simulation.success_rate`. Negligible cost
(one simulation vs. ~25 in the binary search).

If smoothing didn't apply, `probability_of_success = last_success_rate` from
the solver loop — no extra simulation needed.

### Step 7 — signal classification

```
if prior_year_amount > 0:
    delta = (final_spending_budget - prior_year_amount) / prior_year_amount
    if delta > +0.02:  signal = INCREASE
    elif delta < -0.02: signal = DECREASE
    else:               signal = HOLD
else:
    signal = NONE
```

The **±2% deadband** is hard-coded. It exists so trivial year-over-year deltas
don't generate noisy `increase`/`decrease` signals (with floats, exact equality
essentially never happens).

---

## 7. Phase 1 cleanup

The deletion work ships alongside the new feature, broken into safe commits.

### Deletions from `main.cpp`

**HTTP server stack (~2300 lines):**

- `server_simple_api`, `server_retirement_api`, `server_fi_planner_api`
- `server_signal_handler`, `install_signal_handler`, `server_ptr`
- `server` command branch in `main()`
- All `#include <httplib.h>` and related references

**Subcommand handlers + dispatch branches:**

Keep handlers only for the redesigned `swr` → `constant_dollar` and
`current_wr` → `constant_percent`, plus the new `dynamic_dollar`. **Delete
`failsafe_scenario` entirely** (its functionality is subsumed by
`dynamic_dollar`). Delete:

`fixed_scenario`, `multiple_swr_scenario`, `withdraw_frequency_scenario`,
`frequency_scenario`, `analysis_scenario`, `portfolio_analysis_scenario`,
`allocation_scenario`, `term_scenario`, `glidepath_scenario`,
`data_graph_scenario`, `data_time_graph_scenario`,
`trinity_success_scenario`, `die_with_zero_scenario`,
`trinity_cash_graphs_scenario`, `trinity_duration_scenario`,
`trinity_tv_scenario`, `trinity_spending_scenario`,
`social_scenario`, `social_pf_scenario`, `income_scenario`,
`rebalance_scenario`, `threshold_rebalance_scenario`,
`trinity_low_yield_scenario`, `flexibility_graph_scenario`,
`flexibility_auto_graph_scenario`, `selection_graph_scenario`,
`trinity_cash_graph_scenario`, `times_graph_scenario`.

**Help functions:** `print_general_help`, `print_fixed_help`, `print_swr_help`,
`print_multiple_wr_help`, `print_withdraw_frequency_help`, `print_frequency_help`.
Replaced with per-command `--help` rendering driven by the flag-parser schema.

**Output/graph helpers** — each must be verified unused by the three kept
handlers (`single_swr_scenario` → `constant_dollar_cmd`, `current_wr_scenario`
→ `constant_percent_cmd`, and the new `dynamic_dollar_cmd`) **before deletion**.
Candidates:

- `GraphBase`, `Graph` (only used by `*_graph` commands)
- `multiple_wr`, `multiple_wr_graph`, `multiple_wr_sheets`
- `multiple_wr_success_graph`, `multiple_wr_withdrawn_graph`,
  `multiple_wr_errors_graph`, `multiple_wr_duration_graph`,
  `multiple_wr_quality_graph`, `multiple_wr_*_sheets`
- `multiple_wr_tv_graph`, `multiple_wr_avg_tv_graph`, `multiple_wr_tv_sheets`
- `multiple_wr_spending_graph`, `multiple_wr_spending_trend_graph`,
  `multiple_wr_spending_sheets`
- `multiple_rebalance_sheets`, `multiple_rebalance_graph`
- `configure_withdrawal_method`, `csv_print`

Note: the standalone `failsafe_swr(scenario, start, end, step, goal, out)`
worker (line ~645) and its `(title, ...)` overload (line ~659) are also
deleted along with `failsafe_scenario`. The redesigned `constant_dollar`
is now a single-WR evaluator (it does **not** preserve `single_swr_scenario`'s
old built-in 6%→2% sweep behavior — that sweep is dropped).

**Estimated result:** `main.cpp` shrinks from ~4012 to ~600–800 lines.

### Additions to `main.cpp`

- Tiny flag parser (~100 lines): `parsed_args parse_flags(args, schema)`, with
  `schema` describing required/common/advanced flags per command.
- Three handler functions, rewritten to use the flag parser + shared formatter:
  `constant_dollar_cmd`, `constant_percent_cmd`, `dynamic_dollar_cmd`.
- Top-level dispatch + global `--help`.

### New files

- `include/dynamic.hpp` + `src/dynamic.cpp`
- `include/output_formatter.hpp` + `src/output_formatter.cpp`
- `src/test_dynamic.cpp` + `make test` target

### Build system & dependencies

- `Makefile`: drop `-isystem cpp-httplib` from `CXX_FLAGS`. Add `test` and
  `compile_commands` targets.
- `.gitmodules`: remove the `cpp-httplib` submodule entry.
- Delete the `cpp-httplib/` directory.
- Delete `build/Dockerfile.web` and `build/build_web_image.sh`.
- Delete `sonar-project.properties`.
- Delete the committed `compile_commands.json`; add it to `.gitignore`. Add
  `make compile_commands` target that invokes `bear -- make` to regenerate
  (skips gracefully if `bear` is not installed).
- `.github/workflows/make.yml`: verify nothing references deleted targets.

### Repo hygiene

- `README.rst` → `README.md`, converted to Markdown and rewritten to describe
  the three current commands. Delete `README.rst`.
- `LICENSE`: keep MIT. Add a new copyright line for Rick Gray:
  ```
  Copyright Baptiste Wicht 2019-2024.
  Copyright Rick Gray 2026.
  Distributed under the MIT License.
  ```

### Out of scope (deferred Phase 2)

Pruning `scenario` struct fields whose code paths are now dead (`glidepath`,
`vanguard_max_increase`, `dwz_floor`, `flexibility_*`, etc.) and their
handling in `simulation.cpp`. Higher risk — would require careful testing of
the three kept commands. Deferred to a separate spec if/when desired.

---

## 8. Testing

### Framework

Option B — home-grown test main. `src/test_dynamic.cpp` + `make test` target,
with ~50 lines of assertion macros (`CHECK(x)`, `CHECK_NEAR(x, y, tol)`).
No external dependency.

### Unit tests

- **Smoothing math:** raw above upper → smoothed = upper, `smoothing_applied`
  true. Raw below lower → smoothed = lower, true. Raw inside band →
  smoothed = raw, false. Smoothing disabled → smoothed = raw, false.
- **Signal classification:** delta = 0% → HOLD. ±1.99% → HOLD. +2.01% →
  INCREASE. -2.01% → DECREASE. No `prior_year_amount` → NONE.
- **SSA delay computation (conservative rounding-up):**
  current_age=67.5, ssa_start_age=70 → delay=3 (not 2).
  current_age=70, ssa_start_age=70 → delay=0.
  current_age=72, ssa_start_age=70 → delay=0.
- **Flag parser:** required missing → error. `--x v` and `--x=v` both parse.
  Unknown flag → error. Presence flag (`--json`) takes no value. Mutually
  exclusive flags detected (`--json` + `--csv`).

### Property test

> With **smoothing disabled** and **SSA disabled**, `dynamic::compute()`'s
> binary-search solver produces **the same WR within a small tolerance** as
> a reference linear sweep over the same `swr::scenario`.

The reference linear sweep is implemented **inside the test file** — it is
not production code. The test constructs a `swr::scenario` matching the
`dynamic_input` configuration, sweeps WR from 0.5% to 20% at a fine step
(e.g. 0.01%), and tracks the highest WR achieving `target_success`. The
property test then asserts:

```
|dynamic_solver_wr_pct - linear_sweep_max_wr_pct| <= 0.05
```

The 0.05 tolerance covers the linear sweep's step granularity plus the
binary-search residual. Tighten by reducing the sweep step if needed.

This catches solver bugs (wrong direction, off-by-one), scenario-construction
field-mapping bugs, and WR↔dollar conversion errors.

### Smoke tests

`scripts/smoke.sh` runs each of the three commands with a canonical scenario,
saves output to `scripts/smoke_expected/`. Re-run after changes to spot diffs.
Not unit tests — regression sanity for CLI output. Smoke baselines are
regenerated **only at commit 2** (where output format intentionally changes).
Subsequent commits must leave smoke output byte-identical.

---

## 9. Commit plan & quality gate

Five commits, each independently buildable and **each must pass
`make && make test && bash scripts/smoke.sh`** before proceeding.

| # | Commit | Tests | Pass criteria |
|---|---|---|---|
| 1 | Add `dynamic.hpp/.cpp` + `output_formatter` + tests | unit + smoke (old positional commands) | All unit tests pass. Existing commands unchanged. `dynamic_dollar` works against test scenarios. |
| 2 | Convert + redesign three commands to flag-style point queries + unified formatter | unit + smoke (new flag commands) | Unit tests pass. Smoke baselines regenerated and committed *as part of this commit*. |
| 3 | Delete HTTP server stack + cpp-httplib submodule + web Dockerfile | unit + smoke | Unit tests pass. Smoke unchanged from commit 2. Build still links. |
| 4 | Delete unused subcommand handlers + graph helpers (largest cut, includes `failsafe_scenario` and `failsafe_swr` helpers) | unit + smoke | Unit tests pass. Smoke unchanged. Kept three commands still work. |
| 5 | README.md + LICENSE + .gitignore + Makefile targets + delete sonar config | unit + smoke + `make compile_commands` sanity | Unit tests + smoke unchanged. `make compile_commands` runs without error if `bear` installed; no-ops gracefully otherwise. |

If a commit's tests fail, fix in-place before moving on. **Never proceed with
red tests.**

---

## 10. Open implementation notes (non-blocking)

- The existing `cpp-utils` dependency (`make-utils/flags.mk`,
  `make-utils/cpp-utils.mk`) is needed for the build system macros. The
  `cpp_utils/parallel.hpp` and `cpp_utils/thread_pool.hpp` headers are used
  only by deleted commands; their `#include`s in `main.cpp` go away as part of
  commit 4. The submodule itself stays.
- The `social_security` / `social_coverage` / `social_amount` interaction in
  `simulation.cpp` may need verification at build time. If the existing logic
  only honors `social_coverage` (a fraction-of-spending model) and ignores
  `social_amount` when coverage is 0, a minimal one-branch tweak goes in
  alongside commit 1.
- `README.md` rewrite content needs the three commands' updated `--help` text
  to be quoted accurately; finalized as part of commit 5 after the new help
  text is stabilized.
- Build is Linux-only (Makefile, pthread, POSIX). Implementation must happen
  in WSL or a Linux environment, not native Windows.
