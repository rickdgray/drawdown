# drawdown

Compute sustainable retirement withdrawals using historical backtesting against US market data (stocks, bonds, inflation, plus several alternatives).

Three CLI commands evaluate or solve for withdrawal rates.

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

## Commands

### `constant_dollar` — fixed real-dollar withdrawal (the 4% rule)

Evaluate the success rate of withdrawing a fixed real dollar amount each year — set as a percent of the *initial* portfolio and adjusted nominally for inflation thereafter. This is the canonical 4% rule methodology.

```
drawdown constant_dollar --wr 4 \
    --portfolio "us_stocks:60;us_bonds:40;" \
    --inflation us_inflation --years 30 --rebalance yearly
```

Run `drawdown constant_dollar --help` for the full flag list.

### `constant_percent` — fixed percent of current balance

Withdraw a fixed percent of the *current* (not initial) portfolio balance each year. The dollar amount fluctuates with the portfolio; no inflation adjustment.

```
drawdown constant_percent --pct 4 \
    --portfolio "us_stocks:60;us_bonds:40;" \
    --inflation us_inflation --years 30 --rebalance yearly
```

Run `drawdown constant_percent --help` for the full flag list.

### `dynamic_dollar` — per-year sustainable withdrawal

A per-year point query: given the current portfolio balance, current age, fixed end age, and target success rate, find this year's sustainable annual withdrawal by historical backtesting over the remaining horizon. Supports optional Social Security offset and rate-of-change smoothing.

```
drawdown dynamic_dollar --balance 850000 \
    --current_age 67 --end_age 92 \
    --portfolio "us_stocks:60;us_bonds:40;" \
    --inflation us_inflation --target_success 80
```

Run `drawdown dynamic_dollar --help` for the full flag list.

## Output formats

All three commands support three output modes:

- **text** (default) — human-readable Inputs / Results sections
- **`--json`** — flat JSON object: `{ "command": ..., "inputs": ..., "results": ... }`
- **`--csv`** — per-historical-start-year tabular detail

`--json` and `--csv` are mutually exclusive.

## Data sources

CSV files in `stock-data/` cover US assets and a few alternatives: US stocks, US bonds, US inflation, ex-US stocks, gold, commodities, and cash.

## Background

The original Trinity Study analyses described at [thepoorswiss.com/updated-trinity-study](https://thepoorswiss.com/updated-trinity-study/) provide background on the methodology.

## License

MIT. See [LICENSE](LICENSE).
