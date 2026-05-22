<p align="center">
    <img src="logo.svg">
</p>

Compute sustainable retirement withdrawals using historical backtesting against US market data (stocks, bonds, inflation, plus several alternatives).

Six CLI commands answer six different questions:

- **`constant-dollar`**: "If I withdraw X% per year (inflation-adjusted), how often does that survive my horizon?" (The 4% rule.)
- **`constant-percent`**: "If I withdraw X% of my current balance each year, how often does that survive?"
- **`dynamic-dollar`**: "Given my current balance and remaining years, how much can I sustainably spend this year?"
- **`dynamic-success`**: "Given my balance and a planned budget, how likely is that budget to survive my remaining years?"
- **`coast`**: "Given a retirement target and a horizon, what lump sum today would coast there with no further contributions?"
- **`accumulate`**: "Given a balance and monthly contributions, how long until I reach my target?"

## Quick start

```
drawdown dynamic-dollar -b 850000 -a 67 -e 92 -p "us_stocks:60;us_bonds:40;"
```

```
dynamic-dollar

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

### `constant-dollar`: fixed real-dollar withdrawal (the 4% rule)

Evaluate the success rate of withdrawing a fixed real dollar amount each year. The dollar amount is set at retirement as a percent of the *initial* portfolio, then adjusted nominally for inflation thereafter. This is the canonical 4% rule methodology.

#### Examples

The classic Trinity 4% / 30 years / 60-40 portfolio (short flags):

```
drawdown constant-dollar -w 4 -p "us_stocks:60;us_bonds:40;" -y 30 -r yearly
```

A more conservative FIRE scenario with 3.5% over 50 years, 100% stocks:

```
drawdown constant-dollar --withdrawal-rate 3.5 \
    --portfolio "us_stocks:100;" \
    --inflation us_inflation --years 50 --rebalance yearly
```

Pass/fail against a 95% success target:

```
drawdown constant-dollar --withdrawal-rate 4 --target-success 95 \
    --portfolio "us_stocks:60;us_bonds:40;" \
    --inflation us_inflation --years 30 --rebalance yearly
```

#### Flags

**Required:**

| Short | Long | Meaning |
|---|---|---|
| `-w` | `--withdrawal-rate <pct>` | Withdrawal rate (percent of initial portfolio) |
| `-p` | `--portfolio <spec>` | Portfolio spec, e.g. `"us_stocks:60;us_bonds:40;"` |
| `-y` | `--years <int>` | Horizon length in years |

**Common:**

| Short | Long | Default | Meaning |
|---|---|---|---|
| `-i` | `--inflation <series>` | `us_inflation` | Name of an inflation series (embedded or user-supplied) |
| `-r` | `--rebalance <method>` | `yearly` | `none` \| `monthly` \| `yearly` |
| `-sy` | `--start-year <year>` | `0` | Earliest historical backtest start year (`0` = use full data) |
| `-ey` | `--end-year <year>` | `0` | Latest historical backtest start year (`0` = use full data) |
| `-iv` | `--initial-value <dollars>` | `1000` | Starting portfolio value |
| `-t` | `--target-success <pct>` | `0` | If > 0, output adds PASS/FAIL note vs this target |
| `-j` | `--json` | — | Emit JSON instead of text |
| `-c` | `--csv` | — | Emit CSV per-path output instead of text |

**Advanced:**

| Short | Long | Default | Meaning |
|---|---|---|---|
| `-wf` | `--withdraw-frequency <n>` | `12` | `12` = yearly, `1` = monthly |
| `-f` | `--fees <fraction>` | `0.0005` | TER as a fraction (e.g. `0.0005` = 0.05%, typical broad-market index ETF) |

---

### `constant-percent`: fixed percent of current balance

Withdraw a fixed percent of the *current* (not initial) portfolio balance each year. The dollar amount fluctuates with the portfolio; no inflation adjustment to the percent itself. This is what many mistakenly believe the 4% rule is.

#### Examples

4% of current balance over 30 years on a 60-40 (short flags):

```
drawdown constant-percent -pct 4 -p "us_stocks:60;us_bonds:40;" -y 30 -r yearly
```

Same scenario but with a higher minimum spending floor (3.5% of initial):

```
drawdown constant-percent --percent 4 --minimum-floor 3.5 \
    --portfolio "us_stocks:60;us_bonds:40;" \
    --inflation us_inflation --years 30 --rebalance yearly
```

#### Flags

**Required:**

| Short | Long | Meaning |
|---|---|---|
| `-pct` | `--percent <pct>` | Percent of current balance withdrawn each year |
| `-p` | `--portfolio <spec>` | Portfolio spec, e.g. `"us_stocks:60;us_bonds:40;"` |
| `-y` | `--years <int>` | Horizon length in years |

**Common:**

| Short | Long | Default | Meaning |
|---|---|---|---|
| `-i` | `--inflation <series>` | `us_inflation` | Name of an inflation series (embedded or user-supplied) |
| `-r` | `--rebalance <method>` | `yearly` | `none` \| `monthly` \| `yearly` |
| `-sy` | `--start-year <year>` | `0` | Earliest historical backtest start year (`0` = use full data) |
| `-ey` | `--end-year <year>` | `0` | Latest historical backtest start year (`0` = use full data) |
| `-iv` | `--initial-value <dollars>` | `1000` | Starting portfolio value |
| `-mf` | `--minimum-floor <pct>` | `3.0` | Minimum annual spending as a percent of *initial* portfolio. Prevents extreme drawdowns by setting a floor on what gets withdrawn even when the current balance is very low. |
| `-j` | `--json` | — | Emit JSON instead of text |
| `-c` | `--csv` | — | Emit CSV per-path output instead of text |

**Advanced:**

| Short | Long | Default | Meaning |
|---|---|---|---|
| `-wf` | `--withdraw-frequency <n>` | `12` | `12` = yearly, `1` = monthly |
| `-f` | `--fees <fraction>` | `0.0005` | TER as a fraction (e.g. `0.0005` = 0.05%, typical broad-market index ETF) |

---

### `dynamic-dollar`: per-year sustainable withdrawal

A per-year point query. Given the current portfolio balance, current age, fixed end age, and a target success rate, find this year's sustainable annual withdrawal by historical backtesting over the remaining horizon. Designed to be re-run annually with updated balance and age.

#### Examples

The basic question: "I'm 67, have $850K, plan to live until 92, and want 80% success" (short flags):

```
drawdown dynamic-dollar -b 850000 -a 67 -e 92 -p "us_stocks:60;us_bonds:40;"
```

With Social Security starting at age 70 ($24K/year):

```
drawdown dynamic-dollar --balance 850000 \
    --current-age 67 --end-age 92 \
    --portfolio "us_stocks:60;us_bonds:40;" \
    --inflation us_inflation \
    --ssa-income 24000 --ssa-start-age 70
```

Next year's run, with smoothing to prevent year-over-year shocks:

```
drawdown dynamic-dollar --balance 820000 \
    --current-age 68 --end-age 92 \
    --portfolio "us_stocks:60;us_bonds:40;" \
    --inflation us_inflation \
    --ssa-income 24000 --ssa-start-age 70 \
    --smoothing 0.10 --prior-amount 43538
```

A more conservative target (90% success):

```
drawdown dynamic-dollar --balance 850000 \
    --current-age 67 --end-age 92 --target-success 90 \
    --portfolio "us_stocks:60;us_bonds:40;" \
    --inflation us_inflation
```

#### Flags

**Required:**

| Short | Long | Meaning |
|---|---|---|
| `-b` | `--balance <dollars>` | Current portfolio balance |
| `-a` | `--current-age <years>` | Your current age (float allowed, e.g. `67.5`) |
| `-e` | `--end-age <years>` | Planning end age (integer) |
| `-p` | `--portfolio <spec>` | Portfolio spec, e.g. `"us_stocks:60;us_bonds:40;"` |

**Common:**

| Short | Long | Default | Meaning |
|---|---|---|---|
| `-i` | `--inflation <series>` | `us_inflation` | Name of an inflation series (embedded or user-supplied) |
| `-t` | `--target-success <pct>` | `80` | Target success rate. The solver finds the highest WR that hits at least this success rate. |
| `-r` | `--rebalance <method>` | `yearly` | `none` \| `monthly` \| `yearly` |
| `-si` | `--ssa-income <dollars>` | `0` | Annual Social Security income (set to non-zero to enable). |
| `-sa` | `--ssa-start-age <years>` | `0` | Age SSA income begins. Required if `--ssa-income > 0`. |
| `-s` | `--smoothing <fraction>` | `0` | Max year-over-year change in spending, e.g. `0.10` = ±10%. Caps swings caused by market volatility. Requires `--prior-amount`. |
| `-pa` | `--prior-amount <dollars>` | `0` | Last year's spending budget. When set, populates the *signal* field (`increase` / `hold` / `decrease`) and is used by `--smoothing` if enabled. |
| `-j` | `--json` | — | Emit JSON instead of text |
| `-c` | `--csv` | — | Emit CSV per-path output instead of text |

**Advanced:**

| Short | Long | Default | Meaning |
|---|---|---|---|
| `-sy` | `--start-year <year>` | `0` | Earliest historical backtest start year (`0` = use full data) |
| `-ey` | `--end-year <year>` | `0` | Latest historical backtest start year (`0` = use full data) |
| `-wf` | `--withdraw-frequency <n>` | `12` | `12` = yearly, `1` = monthly |
| `-f` | `--fees <fraction>` | `0.0005` | TER as a fraction (e.g. `0.0005` = 0.05%, typical broad-market index ETF) |
| `-st` | `--solver-tolerance <dollars>` | `1` | Binary-search stopping tolerance. Smaller = more iterations, more precision. |

---

### `dynamic-success`: success probability for a planned budget

The inverse of `dynamic-dollar`. Given the current portfolio balance, current age, fixed end age, and a planned annual spending budget, return the historical probability that budget survives the remaining horizon. Also reports the maximum sustainable budget at a comparison target success rate, so you can see your planned budget alongside the historically safe threshold.

#### Examples

The basic question: "I'm 67, have $850K, plan to live until 92, and want to spend $50K/year. How likely is that?" (short flags):

```
drawdown dynamic-success -b 850000 -a 67 -e 92 -bd 50000 -p "us_stocks:60;us_bonds:40;"
```

With Social Security starting at age 70 ($24K/year), checking a $60K total budget:

```
drawdown dynamic-success --balance 850000 \
    --current-age 67 --end-age 92 \
    --budget 60000 \
    --portfolio "us_stocks:60;us_bonds:40;" \
    --inflation us_inflation \
    --ssa-income 24000 --ssa-start-age 70
```

Checking a borderline budget vs a more conservative 90% comparison target:

```
drawdown dynamic-success --balance 850000 \
    --current-age 67 --end-age 92 \
    --budget 43000 \
    --portfolio "us_stocks:60;us_bonds:40;" \
    --target-success 90
```

#### Flags

**Required:**

| Short | Long | Meaning |
|---|---|---|
| `-b` | `--balance <dollars>` | Current portfolio balance |
| `-a` | `--current-age <years>` | Your current age (float allowed, e.g. `67.5`) |
| `-e` | `--end-age <years>` | Planning end age (integer) |
| `-bd` | `--budget <dollars>` | Annual planned spending budget |
| `-p` | `--portfolio <spec>` | Portfolio spec, e.g. `"us_stocks:60;us_bonds:40;"` |

**Common:**

| Short | Long | Default | Meaning |
|---|---|---|---|
| `-i` | `--inflation <series>` | `us_inflation` | Name of an inflation series (embedded or user-supplied) |
| `-t` | `--target-success <pct>` | `80` | Comparison target success rate. The output shows the max budget at this rate alongside your planned budget. |
| `-r` | `--rebalance <method>` | `yearly` | `none` \| `monthly` \| `yearly` |
| `-si` | `--ssa-income <dollars>` | `0` | Annual Social Security income (set to non-zero to enable). |
| `-sa` | `--ssa-start-age <years>` | `0` | Age SSA income begins. Required if `--ssa-income > 0`. |
| `-pa` | `--prior-amount <dollars>` | `0` | Last year's spending budget. When set, populates the *signal* field (`increase` / `hold` / `decrease`). |
| `-j` | `--json` | — | Emit JSON instead of text |
| `-c` | `--csv` | — | Emit CSV per-path output instead of text |

**Advanced:**

| Short | Long | Default | Meaning |
|---|---|---|---|
| `-sy` | `--start-year <year>` | `0` | Earliest historical backtest start year (`0` = use full data) |
| `-ey` | `--end-year <year>` | `0` | Latest historical backtest start year (`0` = use full data) |
| `-wf` | `--withdraw-frequency <n>` | `12` | `12` = yearly, `1` = monthly |
| `-f` | `--fees <fraction>` | `0.0005` | TER as a fraction (e.g. `0.0005` = 0.05%, typical broad-market index ETF) |
| `-st` | `--solver-tolerance <dollars>` | `1` | Comparison binary-search stopping tolerance. Smaller = more iterations, more precision. |

---

### `coast`: lump sum needed to coast to a retirement target

Given a retirement target and a horizon, find the lump sum you need today so that the portfolio grows to the target with no further contributions. Uses historical backtesting with real-dollar (inflation-adjusted) accounting throughout. Solves via binary search.

#### Examples

"I want $1M in 20 years — how much do I need now?" (short flags):

```
drawdown coast --target 1000000 --years 20 --portfolio "us_stocks:80;us_bonds:20;"
```

Derive years from current age and retirement age:

```
drawdown coast --target 2500000 \
    --current-age 33 --retirement-age 50 \
    --portfolio "us_stocks:80;us_bonds:20;" \
    --target-success 90
```

Evaluate the probability at a specific balance rather than solving:

```
drawdown coast --target 1000000 --years 20 \
    --portfolio "us_stocks:80;us_bonds:20;" \
    --balance 400000
```

#### Flags

**Required:**

| Short | Long | Meaning |
|---|---|---|
| `-tg` | `--target <dollars>` | Retirement target in today's dollars |
| `-p` | `--portfolio <spec>` | Portfolio spec, e.g. `"us_stocks:80;us_bonds:20;"` |

**Common:**

| Short | Long | Default | Meaning |
|---|---|---|---|
| `-y` | `--years <int>` | `0` | Horizon in years (alternative to `--current-age` + `--retirement-age`) |
| `-a` | `--current-age <float>` | `0` | Current age (used with `--retirement-age` to derive years) |
| `-ra` | `--retirement-age <int>` | `0` | Retirement age (used with `--current-age` to derive years) |
| `-b` | `--balance <dollars>` | `0` | If set, skip solving and report probability at this balance |
| `-i` | `--inflation <series>` | `us_inflation` | Inflation series |
| `-r` | `--rebalance <method>` | `yearly` | `none` \| `monthly` \| `yearly` |
| `-t` | `--target-success <pct>` | `80` | Target success rate. The solver finds the lowest balance that meets this. |
| `-j` | `--json` | — | Emit JSON instead of text |
| `-c` | `--csv` | — | Emit CSV per-path output instead of text |

**Advanced:**

| Short | Long | Default | Meaning |
|---|---|---|---|
| `-sy` | `--start-year <year>` | `0` | Earliest historical backtest start year |
| `-ey` | `--end-year <year>` | `0` | Latest historical backtest start year |
| `-smb` | `--search-max-balance <dollars>` | `0` | Upper bound for binary search (`0` = `2 × target`) |
| `-bt` | `--balance-tolerance <dollars>` | `100` | Binary-search stopping tolerance |

---

### `accumulate`: years to reach a target with contributions

Given a current balance and a fixed monthly contribution (constant in real, inflation-adjusted terms), find how many years it takes to reach the target with at least the specified success probability. Scans year-by-year.

#### Examples

"I have $100K and add $3K/month — how long to $1M?" (short flags):

```
drawdown accumulate --balance 100000 --contribution 3000 \
    --target 1000000 --portfolio "us_stocks:80;us_bonds:20;"
```

With current age to project the year you'll reach the target:

```
drawdown accumulate --balance 519000 --contribution 5083 \
    --target 2500000 \
    --portfolio "us_stocks:80;us_bonds:20;" \
    --current-age 33
```

Using a 90% success target:

```
drawdown accumulate --balance 100000 --contribution 3000 \
    --target 1000000 \
    --portfolio "us_stocks:80;us_bonds:20;" \
    --target-success 90
```

#### Flags

**Required:**

| Short | Long | Meaning |
|---|---|---|
| `-b` | `--balance <dollars>` | Current portfolio balance (today's dollars) |
| `-mc` | `--contribution <dollars>` | Monthly contribution (today's dollars, constant in real terms) |
| `-tg` | `--target <dollars>` | Target balance (today's dollars) |
| `-p` | `--portfolio <spec>` | Portfolio spec, e.g. `"us_stocks:80;us_bonds:20;"` |

**Common:**

| Short | Long | Default | Meaning |
|---|---|---|---|
| `-i` | `--inflation <series>` | `us_inflation` | Inflation series |
| `-r` | `--rebalance <method>` | `yearly` | `none` \| `monthly` \| `yearly` |
| `-t` | `--target-success <pct>` | `80` | Target success rate. The solver returns the first year that hits this. |
| `-a` | `--current-age <float>` | `0` | Current age. If set, output includes the projected age you reach the target. |
| `-j` | `--json` | — | Emit JSON instead of text |
| `-c` | `--csv` | — | Emit CSV per-path output instead of text |

**Advanced:**

| Short | Long | Default | Meaning |
|---|---|---|---|
| `-sy` | `--start-year <year>` | `0` | Earliest historical backtest start year |
| `-ey` | `--end-year <year>` | `0` | Latest historical backtest start year |
| `-my` | `--max-years <int>` | `60` | Maximum years to scan before giving up |

## Output formats

All six commands support three output modes. `--json` and `--csv` are mutually exclusive.

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

Supports Linux (x86_64), macOS (arm64), and Windows (x86_64) — all 64-bit.
Requires GCC 15 (or newer) and GNU Make.

- **Linux:** g++-15 from a package or the ubuntu-toolchain-r/test PPA
- **macOS:** g++-15 via Homebrew (`brew install gcc@15`)
- **Windows:** g++-15 via MSYS2 (`pacman -S mingw-w64-x86_64-gcc make`), run from a MINGW64 shell

(One unified Makefile; same flags across platforms.)

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

A tagged release (`v*` tag pushed to GitHub) builds portable binaries for all three platforms via `.github/workflows/release.yml`. Each binary is static-linked libstdc++/libgcc and stripped:

| Platform | Artifact |
|---|---|
| Linux x86_64 | `drawdown-linux-x86_64` |
| macOS arm64 | `drawdown-macos-arm64` |
| Windows x86_64 | `drawdown-windows-x86_64.exe` |

## Background

This tool was forked from [Baptiste Wicht's swr-calculator](https://github.com/wichtounet/swr-calculator) and reshaped around three point-query commands suitable for ad-hoc retirement planning rather than analytical sweep tables.

The methodology and historical-backtesting approach trace back to the *Trinity Study* (Cooley, Hubbard, Walz 1998) and its many updates. See [thepoorswiss.com/updated-trinity-study](https://thepoorswiss.com/updated-trinity-study/) for an accessible overview.

**Important caveats:**

- These commands answer "could this strategy have survived past US market history?" They do not concretely answer the question "will it survive the future?" Markets can produce sequences worse than anything in the data.
- The simulation uses the actual historical sequence of returns and inflation. It is not parametric Monte Carlo. Order-of-returns risk is baked in.
- Fees, taxes, and account types (taxable / 401k / Roth) are abstracted away. The `--fees` flag captures expense ratios only.

## License

MIT. See [LICENSE](LICENSE).
