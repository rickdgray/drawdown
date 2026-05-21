# drawdown

Compute sustainable retirement withdrawals using historical backtesting against US market data (stocks, bonds, inflation, plus several alternatives).

Three CLI commands answer three different questions:

- **`constant_dollar`**: "If I withdraw X% per year (inflation-adjusted), how often does that survive my horizon?" (The 4% rule.)
- **`constant_percent`**: "If I withdraw X% of my current balance each year, how often does that survive?"
- **`dynamic_dollar`**: "Given my current balance and remaining years, how much can I sustainably spend this year?"

## Quick start

```
drawdown dynamic_dollar --balance 850000 --current_age 67 --end_age 92 \
    --portfolio "us_stocks:60;us_bonds:40;"
```

```
dynamic_dollar

Inputs
  Current balance:      $850000
  Current age:          67.00
  End age:              92
  Remaining horizon:    25 years
  Target success:       80.0%

Results
  Raw calculated:       $43538
  Smoothed:             $43538
  Final budget:         $43538
  Success at budget:    80.0%
  SSA offset this year: $0
  Portfolio withdrawal: $43538
  Signal vs prior:      none
```

## Commands

### `constant_dollar`: fixed real-dollar withdrawal (the 4% rule)

Evaluate the success rate of withdrawing a fixed real dollar amount each year. The dollar amount is set at retirement as a percent of the *initial* portfolio, then adjusted nominally for inflation thereafter. This is the canonical 4% rule methodology.

#### Examples

The classic Trinity 4% / 30 years / 60-40 portfolio:

```
drawdown constant_dollar --wr 4 \
    --portfolio "us_stocks:60;us_bonds:40;" \
    --inflation us_inflation --years 30 --rebalance yearly
```

A more conservative FIRE scenario with 3.5% over 50 years, 100% stocks:

```
drawdown constant_dollar --wr 3.5 \
    --portfolio "us_stocks:100;" \
    --inflation us_inflation --years 50 --rebalance yearly
```

Pass/fail against a 95% success target:

```
drawdown constant_dollar --wr 4 --target_success 95 \
    --portfolio "us_stocks:60;us_bonds:40;" \
    --inflation us_inflation --years 30 --rebalance yearly
```

#### Flags

**Required:**

| Flag | Meaning |
|---|---|
| `--wr <pct>` | Withdrawal rate (percent of initial portfolio) |
| `--portfolio <spec>` | Portfolio spec, e.g. `"us_stocks:60;us_bonds:40;"` |
| `--years <int>` | Horizon length in years |

**Common:**

| Flag | Default | Meaning |
|---|---|---|
| `--inflation <series>` | `us_inflation` | Name of an inflation series (embedded or user-supplied) |
| `--rebalance <method>` | `none` | `none` \| `monthly` \| `yearly` \| `threshold` |
| `--start_year <year>` | `0` | Earliest historical backtest start year (`0` = use full data) |
| `--end_year <year>` | `0` | Latest historical backtest start year (`0` = use full data) |
| `--initial_value <dollars>` | `1000` | Starting portfolio value |
| `--target_success <pct>` | `0` | If > 0, output adds PASS/FAIL note vs this target |
| `--json` | — | Emit JSON instead of text |
| `--csv` | — | Emit CSV per-path output instead of text |

**Advanced:**

| Flag | Default | Meaning |
|---|---|---|
| `--withdraw_frequency <n>` | `12` | `12` = yearly, `1` = monthly |
| `--fees <fraction>` | `0.001` | TER as a fraction (e.g. `0.001` = 0.1%) |

---

### `constant_percent`: fixed percent of current balance

Withdraw a fixed percent of the *current* (not initial) portfolio balance each year. The dollar amount fluctuates with the portfolio; no inflation adjustment to the percent itself. This is what many mistakenly believe the 4% rule is.

#### Examples

4% of current balance over 30 years on a 60-40:

```
drawdown constant_percent --pct 4 \
    --portfolio "us_stocks:60;us_bonds:40;" \
    --inflation us_inflation --years 30 --rebalance yearly
```

Same scenario but with a higher minimum spending floor (3.5% of initial):

```
drawdown constant_percent --pct 4 --minimum_floor 3.5 \
    --portfolio "us_stocks:60;us_bonds:40;" \
    --inflation us_inflation --years 30 --rebalance yearly
```

#### Flags

**Required:**

| Flag | Meaning |
|---|---|
| `--pct <pct>` | Percent of current balance withdrawn each year |
| `--portfolio <spec>` | Portfolio spec, e.g. `"us_stocks:60;us_bonds:40;"` |
| `--years <int>` | Horizon length in years |

**Common:**

| Flag | Default | Meaning |
|---|---|---|
| `--inflation <series>` | `us_inflation` | Name of an inflation series (embedded or user-supplied) |
| `--rebalance <method>` | `none` | `none` \| `monthly` \| `yearly` \| `threshold` |
| `--start_year <year>` | `0` | Earliest historical backtest start year (`0` = use full data) |
| `--end_year <year>` | `0` | Latest historical backtest start year (`0` = use full data) |
| `--initial_value <dollars>` | `1000` | Starting portfolio value |
| `--minimum_floor <pct>` | `3.0` | Minimum annual spending as a percent of *initial* portfolio. Prevents extreme drawdowns by setting a floor on what gets withdrawn even when the current balance is very low. |
| `--json` | — | Emit JSON instead of text |
| `--csv` | — | Emit CSV per-path output instead of text |

**Advanced:**

| Flag | Default | Meaning |
|---|---|---|
| `--withdraw_frequency <n>` | `12` | `12` = yearly, `1` = monthly |
| `--fees <fraction>` | `0.001` | TER as a fraction (e.g. `0.001` = 0.1%) |

---

### `dynamic_dollar`: per-year sustainable withdrawal

A per-year point query. Given the current portfolio balance, current age, fixed end age, and a target success rate, find this year's sustainable annual withdrawal by historical backtesting over the remaining horizon. Designed to be re-run annually with updated balance and age.

#### Examples

The basic question: "I'm 67, have $850K, plan to live until 92, and want 80% success":

```
drawdown dynamic_dollar --balance 850000 \
    --current_age 67 --end_age 92 \
    --portfolio "us_stocks:60;us_bonds:40;" \
    --inflation us_inflation
```

With Social Security starting at age 70 ($24K/year):

```
drawdown dynamic_dollar --balance 850000 \
    --current_age 67 --end_age 92 \
    --portfolio "us_stocks:60;us_bonds:40;" \
    --inflation us_inflation \
    --ssa_income 24000 --ssa_start_age 70
```

Next year's run, with smoothing to prevent year-over-year shocks:

```
drawdown dynamic_dollar --balance 820000 \
    --current_age 68 --end_age 92 \
    --portfolio "us_stocks:60;us_bonds:40;" \
    --inflation us_inflation \
    --ssa_income 24000 --ssa_start_age 70 \
    --smoothing 0.10 --prior_amount 43538
```

A more conservative target (90% success):

```
drawdown dynamic_dollar --balance 850000 \
    --current_age 67 --end_age 92 --target_success 90 \
    --portfolio "us_stocks:60;us_bonds:40;" \
    --inflation us_inflation
```

#### Flags

**Required:**

| Flag | Meaning |
|---|---|
| `--balance <dollars>` | Current portfolio balance |
| `--current_age <years>` | Your current age (float allowed, e.g. `67.5`) |
| `--end_age <years>` | Planning end age (integer) |
| `--portfolio <spec>` | Portfolio spec, e.g. `"us_stocks:60;us_bonds:40;"` |

**Common:**

| Flag | Default | Meaning |
|---|---|---|
| `--inflation <series>` | `us_inflation` | Name of an inflation series (embedded or user-supplied) |
| `--target_success <pct>` | `80` | Target success rate. The solver finds the highest WR that hits at least this success rate. |
| `--rebalance <method>` | `none` | `none` \| `monthly` \| `yearly` \| `threshold` |
| `--ssa_income <dollars>` | `0` | Annual Social Security income (set to non-zero to enable). |
| `--ssa_start_age <years>` | `0` | Age SSA income begins. Required if `--ssa_income > 0`. |
| `--smoothing <fraction>` | `0` | Max year-over-year change in spending, e.g. `0.10` = ±10%. Caps swings caused by market volatility. Requires `--prior_amount`. |
| `--prior_amount <dollars>` | `0` | Last year's spending budget. When set, populates the *signal* field (`increase` / `hold` / `decrease`) and is used by `--smoothing` if enabled. |
| `--json` | — | Emit JSON instead of text |
| `--csv` | — | Emit CSV per-path output instead of text |

**Advanced:**

| Flag | Default | Meaning |
|---|---|---|
| `--start_year <year>` | `0` | Earliest historical backtest start year (`0` = use full data) |
| `--end_year <year>` | `0` | Latest historical backtest start year (`0` = use full data) |
| `--withdraw_frequency <n>` | `12` | `12` = yearly, `1` = monthly |
| `--fees <fraction>` | `0.001` | TER as a fraction (e.g. `0.001` = 0.1%) |
| `--solver_tolerance <dollars>` | `1` | Binary-search stopping tolerance. Smaller = more iterations, more precision. |

## Output formats

All three commands support three output modes. `--json` and `--csv` are mutually exclusive.

| Mode | Flag | Format |
|---|---|---|
| text | *(default)* | Human-readable `Inputs` / `Results` / `Notes` sections |
| JSON | `--json` | Flat JSON object: `{ "command": ..., "inputs": ..., "results": ..., "notes": [...] }`. Snake-case keys, dollars as floats (no `$` prefix), percentages as floats (e.g. `80.4`, not `"80.4%"`) |
| CSV | `--csv` | Per-historical-start-year tabular detail with columns `start_year,start_month,success,terminal_value,total_withdrawn,worst_duration_months`. Preceded by `#`-prefixed comment lines describing the scenario |

## Data sources

Historical data series available out of the box:

| Series name | What it covers |
|---|---|
| `us_stocks` | US stock market |
| `us_bonds` | US bonds |
| `us_inflation` | US CPI |
| `ex_us_stocks` | Non-US developed-market stocks |
| `gold` | Gold |
| `commodities` | Broad commodities |
| `cash` | Cash equivalents |

Data runs ~1871 through ~2025 (monthly). All seven series are **embedded into the binary** at compile time. You can run `drawdown` from any directory without co-located data files.

To use your own data, place a CSV named `<series>.csv` in `stock-data/` next to where you run the binary, formatted as `month,year,value` (one row per month, no header). The loader falls back to file I/O for any series name that isn't embedded.

## Build

Linux / WSL only. Requires GCC 15 (or newer) and GNU Make.

```
make                  # default: release_debug build
make release_debug    # builds release_debug/drawdown
make release          # fully optimized
make debug            # debug build
make all              # all three modes
make test             # build and run the unit test suite
make compile_commands # regenerate compile_commands.json via bear (optional)
make clean            # remove build directories
```

A tagged release (`v*` tag pushed to GitHub) builds a portable `drawdown-linux-x86_64` binary via `.github/workflows/release.yml`. The build is static-linked libstdc++/libgcc, stripped, ~1.7 MB plus the embedded historical data.

## Background

This tool was forked from [Baptiste Wicht's swr-calculator](https://github.com/wichtounet/swr-calculator) and reshaped around three point-query commands suitable for ad-hoc retirement planning rather than analytical sweep tables.

The methodology and historical-backtesting approach trace back to the *Trinity Study* (Cooley, Hubbard, Walz 1998) and its many updates. See [thepoorswiss.com/updated-trinity-study](https://thepoorswiss.com/updated-trinity-study/) for an accessible overview.

**Important caveats:**

- These commands answer "could this strategy have survived past US market history?" They do not concretely answer the question "will it survive the future?" Markets can produce sequences worse than anything in the data.
- The simulation uses the actual historical sequence of returns and inflation. It is not parametric Monte Carlo. Order-of-returns risk is baked in.
- Fees, taxes, and account types (taxable / 401k / Roth) are abstracted away. The `--fees` flag captures expense ratios only.

## License

MIT. See [LICENSE](LICENSE).
