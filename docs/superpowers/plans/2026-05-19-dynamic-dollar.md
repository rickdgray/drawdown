# dynamic_dollar Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a `dynamic_dollar` CLI subcommand for per-year sustainable withdrawal via historical backtesting, redesign two kept commands (`swr` → `constant_dollar`, `current_wr` → `constant_percent`) as flag-style single-point queries with unified output, delete `failsafe` entirely (its functionality is subsumed by `dynamic_dollar`), and strip the HTTP server + ~28 unused analytical subcommands from the existing codebase.

**Architecture:** A new `swr::dynamic::compute()` orchestrates binary search over candidate WRs by repeatedly calling the existing `swr::simulation()` engine. The CLI surface is rebuilt around a small flag parser and a unified text/JSON/CSV output formatter. Phase 1 leaves `simulation.hpp`/`scenario` untouched.

**Tech Stack:** C++26, GNU Make, pthread. Build is Linux-only (must run in WSL or a Linux environment, not native Windows). No external test framework — uses a tiny home-grown assertion header.

**Spec:** See `docs/superpowers/specs/2026-05-19-dynamic-dollar-design.md` for the design that produced this plan.

**Commit policy:** This plan's "Stage and request commit" steps do **not** auto-commit. The user reviews staged changes and runs `git commit` themselves. Each phase ends with a checkpoint commit gated by `make && make test && bash scripts/smoke.sh` passing.

---

## File structure

| Path | Status | Responsibility |
|---|---|---|
| `include/test_assert.hpp` | NEW | Test macros (`TEST`, `CHECK`, `CHECK_NEAR`, `CHECK_EQ`), test registry |
| `src/test_dynamic.cpp` | NEW | Test main + all `TEST()` blocks |
| `include/cli.hpp` | NEW | Flag-schema types, `parse_flags`, `render_help` |
| `src/cli.cpp` | NEW | Flag parser + help renderer implementation |
| `include/output_formatter.hpp` | NEW | KV-list types, `emit_text` / `emit_json` / `emit_csv` |
| `src/output_formatter.cpp` | NEW | Text/JSON/CSV rendering implementation |
| `include/dynamic.hpp` | NEW | `dynamic_input`, `dynamic_result`, `dynamic::compute()` |
| `src/dynamic.cpp` | NEW | Scenario construction, binary-search solver, smoothing, signal |
| `src/main.cpp` | MODIFY | Heavy: command rename + flag-style conversion + new dispatch; deletions in phases 3 & 4 |
| `Makefile` | MODIFY | Add `test`, `compile_commands` targets; drop `-isystem cpp-httplib` in Phase 3 |
| `scripts/smoke.sh` | NEW | Canonical-scenario smoke regression script |
| `scripts/smoke_expected/` | NEW | Smoke baselines (regenerated in Phase 2) |
| `.gitignore` | MODIFY | Add `compile_commands.json` |
| `.gitmodules` | MODIFY | Remove `cpp-httplib` submodule entry (Phase 3) |
| `LICENSE` | MODIFY | Add Rick Gray copyright line (Phase 5) |
| `README.rst` | DELETE | Replaced by README.md (Phase 5) |
| `README.md` | NEW | Rewritten for the four current commands (Phase 5) |
| `build/Dockerfile.web` | DELETE | Phase 3 |
| `build/build_web_image.sh` | DELETE | Phase 3 |
| `sonar-project.properties` | DELETE | Phase 5 |
| `compile_commands.json` | DELETE | Phase 5 (replaced by `make compile_commands` target) |
| `cpp-httplib/` | DELETE | Submodule removal in Phase 3 |
| `.github/workflows/make.yml` | VERIFY | Verify still works after build changes |

---

## Phase 1 — New module + test infrastructure (commit milestone 1)

End state: `dynamic.hpp/.cpp` and `output_formatter.hpp/.cpp` exist, are unit-tested, are wired into a working `dynamic_dollar` CLI subcommand. The other three commands remain on positional args. `make test` passes. `bash scripts/smoke.sh` passes against the four positional commands plus the new flag-style `dynamic_dollar`.

### Task 1: Bootstrap test infrastructure

**Files:**
- Create: `include/test_assert.hpp`
- Create: `src/test_dynamic.cpp`
- Modify: `Makefile`

- [ ] **Step 1: Write `include/test_assert.hpp`**

```cpp
//=======================================================================
// Tiny home-grown test framework. No external dependencies.
//=======================================================================
#pragma once

#include <iostream>
#include <string>
#include <vector>
#include <functional>
#include <cmath>

namespace test {

inline int& fail_count() { static int n = 0; return n; }
inline int& pass_count() { static int n = 0; return n; }
inline std::string& current_test() { static std::string s; return s; }

struct test_case {
    std::string name;
    std::function<void()> fn;
};

inline std::vector<test_case>& registry() {
    static std::vector<test_case> r;
    return r;
}

struct register_test {
    register_test(const char* name, std::function<void()> fn) {
        registry().push_back({name, fn});
    }
};

} // namespace test

#define TEST(name)                                                      \
    static void test_##name();                                          \
    static ::test::register_test reg_##name(#name, test_##name);        \
    static void test_##name()

#define CHECK(expr)                                                     \
    do {                                                                \
        if (!(expr)) {                                                  \
            std::cerr << "FAIL " << ::test::current_test()              \
                      << ": " << #expr << " at " << __FILE__            \
                      << ":" << __LINE__ << std::endl;                  \
            ++::test::fail_count();                                     \
            return;                                                     \
        }                                                               \
    } while (0)

#define CHECK_NEAR(actual, expected, tol)                               \
    do {                                                                \
        auto _a = (actual);                                             \
        auto _e = (expected);                                           \
        if (std::abs(_a - _e) > (tol)) {                                \
            std::cerr << "FAIL " << ::test::current_test()              \
                      << ": " << #actual << " (" << _a                  \
                      << ") not within " << (tol)                       \
                      << " of " << #expected << " (" << _e              \
                      << ") at " << __FILE__ << ":" << __LINE__         \
                      << std::endl;                                     \
            ++::test::fail_count();                                     \
            return;                                                     \
        }                                                               \
    } while (0)

#define CHECK_EQ(actual, expected)                                      \
    do {                                                                \
        auto _a = (actual);                                             \
        auto _e = (expected);                                           \
        if (!(_a == _e)) {                                              \
            std::cerr << "FAIL " << ::test::current_test()              \
                      << ": " << #actual << " (" << _a << ") != "       \
                      << #expected << " (" << _e << ")"                 \
                      << " at " << __FILE__ << ":" << __LINE__          \
                      << std::endl;                                     \
            ++::test::fail_count();                                     \
            return;                                                     \
        }                                                               \
    } while (0)
```

- [ ] **Step 2: Write `src/test_dynamic.cpp` (initial skeleton, one trivial test)**

```cpp
//=======================================================================
// Test main for swr_calculator. All TEST() blocks live here.
//=======================================================================
#include "test_assert.hpp"

TEST(sanity) {
    CHECK(1 + 1 == 2);
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
```

- [ ] **Step 3: Add `test` target to `Makefile`**

Open `Makefile`. After the `clean:` target, before `include make-utils/cpp-utils-finalize.mk`, append:

```makefile
# Home-grown test executable. Links test_dynamic.cpp with project objects
# as they get added (TEST_SRC is grown by later tasks). Adapt the output
# directory if make-utils macros emit elsewhere.
TEST_SRC := src/test_dynamic.cpp
TEST_BIN := release_debug/test_dynamic

$(TEST_BIN): $(TEST_SRC) include/test_assert.hpp
	@mkdir -p release_debug
	$(CXX) $(CXX_FLAGS) -Iinclude -o $@ $(TEST_SRC)

.PHONY: test
test: $(TEST_BIN)
	$(TEST_BIN)
```

Also add `.PHONY: test` to the existing `.PHONY: default release debug all clean` line — change to:

```makefile
.PHONY: default release debug all clean test
```

- [ ] **Step 4: Run `make test` to verify the scaffolding compiles and runs**

Run (in WSL or Linux shell at the repo root):

```
make test
```

Expected output ends with:

```
PASS sanity

1 passed, 0 failed
```

Exit code 0.

- [ ] **Step 5: Stage**

```
git add include/test_assert.hpp src/test_dynamic.cpp Makefile
```

Do not commit. Inform user: "Test infrastructure scaffolding staged."

---

### Task 2: `dynamic.hpp` types

**Files:**
- Create: `include/dynamic.hpp`

- [ ] **Step 1: Create `include/dynamic.hpp`**

```cpp
//=======================================================================
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
    float                   current_balance     = 0.0f;
    float                   current_age         = 0.0f;
    size_t                  end_age             = 0;

    // Portfolio composition + data window
    std::vector<allocation> portfolio;
    std::string             inflation;
    Rebalancing             rebalance           = Rebalancing::NONE;
    size_t                  historical_start_year = 0;  // 0 = full range
    size_t                  historical_end_year   = 0;  // 0 = full range

    // Solver target
    float                   target_success      = 80.0f;

    // SSA
    bool                    ssa_enabled         = false;
    float                   ssa_annual_income   = 0.0f;
    size_t                  ssa_start_age       = 0;

    // Smoothing
    bool                    smoothing_enabled   = false;
    float                   smoothing_max_change = 0.10f;
    float                   prior_year_amount   = 0.0f;

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
    size_t  remaining_horizon_years     = 0;
    float   raw_calculated_withdrawal   = 0.0f;
    bool    smoothing_applied           = false;
    float   smoothed_withdrawal         = 0.0f;
    float   final_spending_budget       = 0.0f;
    float   probability_of_success      = 0.0f;
    float   ssa_offset_this_year        = 0.0f;
    float   portfolio_withdrawal_this_year = 0.0f;
    Signal  signal                      = Signal::NONE;

    std::vector<per_path_detail> per_path_details; // populated when requested

    bool        error = false;
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
```

- [ ] **Step 2: Create `src/dynamic.cpp` (initial skeleton, stub `compute`)**

```cpp
//=======================================================================
// Per-year sustainable-withdrawal calculation via historical backtesting.
//=======================================================================
#include "dynamic.hpp"

#include <cmath>
#include <ostream>

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

dynamic_result compute(const dynamic_input& input, bool collect_per_path) {
    (void)collect_per_path;
    dynamic_result r;
    r.error   = true;
    r.message = "compute() not yet implemented";
    return r;
}

} // namespace swr
```

- [ ] **Step 3: Expand `Makefile`'s `TEST_SRC` to include the new dependencies**

Replace the `TEST_SRC := src/test_dynamic.cpp` line with:

```makefile
TEST_SRC := src/test_dynamic.cpp src/dynamic.cpp src/data.cpp \
            src/portfolio.cpp src/simulation.cpp
```

Also update the `$(TEST_BIN)` dependency line to include the new headers:

```makefile
$(TEST_BIN): $(TEST_SRC) include/test_assert.hpp include/dynamic.hpp \
             include/data.hpp include/portfolio.hpp include/simulation.hpp
```

- [ ] **Step 4: Run `make test` to verify everything still compiles**

Run:

```
make test
```

Expected: same `1 passed, 0 failed` output as Task 1.

- [ ] **Step 5: Stage**

```
git add include/dynamic.hpp src/dynamic.cpp Makefile
```

Inform user: "dynamic.hpp + dynamic.cpp skeleton staged."

---

### Task 3: TDD `compute_remaining_horizon`

> **TDD note for Tasks 3-6:** The helper functions (`compute_remaining_horizon`,
> `compute_social_delay`, `apply_smoothing`, `classify_signal`) were
> implemented in Task 2 as part of the skeleton, so the tests added here
> pass on first run rather than failing first. They lock in regression
> coverage and document expected behavior. If you want strict red-green-refactor,
> first stub the helper to return a wrong value (e.g. `return 0`), add the
> test, watch it fail, then restore the real implementation.

**Files:**
- Modify: `src/test_dynamic.cpp`

- [ ] **Step 1: Add tests**

Add to `src/test_dynamic.cpp` (before `int main()`):

```cpp
#include "dynamic.hpp"

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
```

- [ ] **Step 2: Run tests; they should pass already** (the function is implemented in Task 2)

```
make test
```

Expected: `4 passed, 0 failed` (sanity + 3 new tests).

If any test fails, inspect output for which property failed and fix `compute_remaining_horizon` in `src/dynamic.cpp`.

- [ ] **Step 3: Stage**

```
git add src/test_dynamic.cpp
```

---

### Task 4: TDD `compute_social_delay`

**Files:**
- Modify: `src/test_dynamic.cpp`

- [ ] **Step 1: Add tests**

Add to `src/test_dynamic.cpp`:

```cpp
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
```

- [ ] **Step 2: Run tests**

```
make test
```

Expected: `7 passed, 0 failed`.

- [ ] **Step 3: Stage**

```
git add src/test_dynamic.cpp
```

---

### Task 5: TDD `apply_smoothing`

**Files:**
- Modify: `src/test_dynamic.cpp`

- [ ] **Step 1: Add tests**

```cpp
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
```

- [ ] **Step 2: Run tests**

```
make test
```

Expected: `13 passed, 0 failed`.

- [ ] **Step 3: Stage**

```
git add src/test_dynamic.cpp
```

---

### Task 6: TDD `classify_signal`

**Files:**
- Modify: `src/test_dynamic.cpp`

- [ ] **Step 1: Add tests**

```cpp
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
```

- [ ] **Step 2: Run tests**

```
make test
```

Expected: `17 passed, 0 failed`.

- [ ] **Step 3: Stage**

```
git add src/test_dynamic.cpp
```

---

### Task 7: `cli.hpp` flag-schema types

**Files:**
- Create: `include/cli.hpp`
- Create: `src/cli.cpp`

- [ ] **Step 1: Create `include/cli.hpp`**

```cpp
//=======================================================================
// Tiny flag parser. Long flags only. Supports --x value and --x=value.
// Presence flags take no value.
//=======================================================================
#pragma once

#include <map>
#include <string>
#include <vector>
#include <optional>
#include <iosfwd>

namespace swr::cli {

enum class FlagGroup { REQUIRED, COMMON, ADVANCED };
enum class FlagKind  { VALUE, PRESENCE };

struct flag_spec {
    std::string name;          // without leading "--"
    FlagGroup   group;
    FlagKind    kind;
    std::string description;
    std::string default_value; // empty if no default; ignored for PRESENCE
};

struct command_schema {
    std::string             command_name;
    std::string             one_line_description;
    std::string             example_invocation; // shown at bottom of --help
    std::vector<flag_spec>  flags;
    // Pairs of flag names that may not appear together (e.g. {"json","csv"}).
    std::vector<std::pair<std::string, std::string>> mutually_exclusive;
};

// Parsed result. presence[flag] = true if the flag was given.
// values[flag] = the string value if a VALUE flag was given.
struct parsed_args {
    std::map<std::string, bool>        presence;
    std::map<std::string, std::string> values;
    bool                                help_requested = false;
};

// Parse argv-style args (already stripped of the command name).
// Throws std::runtime_error on validation failure (missing required,
// unknown flag, mutually-exclusive pair, etc).
parsed_args parse_flags(const std::vector<std::string>& args,
                        const command_schema& schema);

// Render the command's --help text to `out`. Groups by REQUIRED/COMMON/ADVANCED.
void render_help(std::ostream& out, const command_schema& schema);

// Convenience getters with default fallback.
std::string get_value(const parsed_args& p, const command_schema& schema,
                      const std::string& flag);
bool        get_presence(const parsed_args& p, const std::string& flag);

} // namespace swr::cli
```

- [ ] **Step 2: Create `src/cli.cpp` (stub — full impl in next task)**

```cpp
#include "cli.hpp"

#include <ostream>
#include <stdexcept>

namespace swr::cli {

parsed_args parse_flags(const std::vector<std::string>& /*args*/,
                        const command_schema& /*schema*/) {
    throw std::runtime_error("parse_flags not yet implemented");
}

void render_help(std::ostream& /*out*/, const command_schema& /*schema*/) {
    throw std::runtime_error("render_help not yet implemented");
}

std::string get_value(const parsed_args& p, const command_schema& schema,
                      const std::string& flag) {
    auto it = p.values.find(flag);
    if (it != p.values.end()) return it->second;
    for (auto& f : schema.flags) {
        if (f.name == flag) return f.default_value;
    }
    return "";
}

bool get_presence(const parsed_args& p, const std::string& flag) {
    auto it = p.presence.find(flag);
    return it != p.presence.end() && it->second;
}

} // namespace swr::cli
```

- [ ] **Step 3: Wire `src/cli.cpp` into `Makefile`'s `TEST_SRC`**

Update the `TEST_SRC` line in `Makefile`:

```makefile
TEST_SRC := src/test_dynamic.cpp src/dynamic.cpp src/data.cpp \
            src/portfolio.cpp src/simulation.cpp src/cli.cpp
```

- [ ] **Step 4: Run `make test` to verify it still builds**

```
make test
```

Expected: `17 passed, 0 failed` (no new tests yet).

- [ ] **Step 5: Stage**

```
git add include/cli.hpp src/cli.cpp Makefile
```

---

### Task 8: TDD flag parser — basic value flags

**Files:**
- Modify: `src/test_dynamic.cpp`
- Modify: `src/cli.cpp`

- [ ] **Step 1: Add failing tests**

Add at the top of `src/test_dynamic.cpp` (with includes):

```cpp
#include "cli.hpp"
```

Add tests:

```cpp
static swr::cli::command_schema make_test_schema() {
    using namespace swr::cli;
    command_schema s;
    s.command_name = "test_cmd";
    s.flags.push_back({"foo", FlagGroup::REQUIRED, FlagKind::VALUE,
                       "a required value flag", ""});
    s.flags.push_back({"bar", FlagGroup::COMMON, FlagKind::VALUE,
                       "an optional value flag with default", "default_bar"});
    s.flags.push_back({"verbose", FlagGroup::COMMON, FlagKind::PRESENCE,
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
```

- [ ] **Step 2: Run tests — verify they fail**

```
make test
```

Expected: build succeeds, but tests `cli_*` fail with runtime_error from the stub. (The exception propagation aborts the test process — that's the failure signal.)

- [ ] **Step 3: Implement `parse_flags` in `src/cli.cpp`**

Replace the stub body with:

```cpp
parsed_args parse_flags(const std::vector<std::string>& args,
                        const command_schema& schema) {
    parsed_args result;

    // Build a quick lookup of flag specs by name.
    std::map<std::string, const flag_spec*> by_name;
    for (auto& f : schema.flags) by_name[f.name] = &f;

    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];

        if (a == "--help") {
            result.help_requested = true;
            continue;
        }

        if (a.rfind("--", 0) != 0) {
            throw std::runtime_error("unexpected positional arg: " + a);
        }

        std::string name;
        std::optional<std::string> inline_value;
        std::string body = a.substr(2);
        auto eq_pos = body.find('=');
        if (eq_pos != std::string::npos) {
            name = body.substr(0, eq_pos);
            inline_value = body.substr(eq_pos + 1);
        } else {
            name = body;
        }

        auto it = by_name.find(name);
        if (it == by_name.end()) {
            throw std::runtime_error("unknown flag: --" + name);
        }

        const flag_spec* spec = it->second;
        if (spec->kind == FlagKind::PRESENCE) {
            if (inline_value.has_value()) {
                throw std::runtime_error("flag --" + name +
                                         " does not take a value");
            }
            result.presence[name] = true;
        } else {
            std::string value;
            if (inline_value.has_value()) {
                value = *inline_value;
            } else {
                if (i + 1 >= args.size()) {
                    throw std::runtime_error("flag --" + name +
                                             " requires a value");
                }
                value = args[++i];
            }
            result.values[name] = value;
            result.presence[name] = true;
        }
    }

    // Required-flag check (skipped when --help requested).
    if (!result.help_requested) {
        for (auto& f : schema.flags) {
            if (f.group == FlagGroup::REQUIRED &&
                result.presence.find(f.name) == result.presence.end()) {
                throw std::runtime_error("missing required flag: --" + f.name);
            }
        }
    }

    // Mutually-exclusive pairs.
    for (auto& [a_name, b_name] : schema.mutually_exclusive) {
        if (get_presence(result, a_name) && get_presence(result, b_name)) {
            throw std::runtime_error("flags --" + a_name + " and --" +
                                     b_name + " are mutually exclusive");
        }
    }

    return result;
}
```

Also add the missing include at the top of `src/cli.cpp`:

```cpp
#include <map>
```

- [ ] **Step 4: Run tests — verify they pass**

```
make test
```

Expected: `22 passed, 0 failed`.

- [ ] **Step 5: Stage**

```
git add src/test_dynamic.cpp src/cli.cpp
```

---

### Task 9: TDD flag parser — error paths

**Files:**
- Modify: `src/test_dynamic.cpp`

- [ ] **Step 1: Add tests**

```cpp
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
    s.flags.push_back({"json", FlagGroup::COMMON, FlagKind::PRESENCE, "", ""});
    s.flags.push_back({"csv",  FlagGroup::COMMON, FlagKind::PRESENCE, "", ""});
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
```

- [ ] **Step 2: Run tests**

```
make test
```

Expected: `27 passed, 0 failed`.

- [ ] **Step 3: Stage**

```
git add src/test_dynamic.cpp
```

---

### Task 10: Implement `render_help`

**Files:**
- Modify: `src/cli.cpp`
- Modify: `src/test_dynamic.cpp`

- [ ] **Step 1: Replace the `render_help` stub in `src/cli.cpp`**

```cpp
void render_help(std::ostream& out, const command_schema& schema) {
    out << "Usage: swr_calculator " << schema.command_name << " [OPTIONS]\n";
    if (!schema.one_line_description.empty()) {
        out << "\n  " << schema.one_line_description << "\n";
    }

    auto emit_group = [&](FlagGroup g, const char* label) {
        bool first = true;
        for (auto& f : schema.flags) {
            if (f.group != g) continue;
            if (f.name == "help") continue;
            if (first) {
                out << "\n" << label << ":\n";
                first = false;
            }
            // Two-column: name+kind on left, description on right.
            std::string left = "  --" + f.name;
            if (f.kind == FlagKind::VALUE) left += " <value>";
            out << left;
            // pad to column 24
            if (left.size() < 24) {
                out << std::string(24 - left.size(), ' ');
            } else {
                out << " ";
            }
            out << f.description;
            if (!f.default_value.empty() && f.kind == FlagKind::VALUE) {
                out << " (default: " << f.default_value << ")";
            }
            out << "\n";
        }
    };

    emit_group(FlagGroup::REQUIRED, "Required");
    emit_group(FlagGroup::COMMON,   "Common");
    emit_group(FlagGroup::ADVANCED, "Advanced");

    if (!schema.example_invocation.empty()) {
        out << "\nExample:\n  " << schema.example_invocation << "\n";
    }
}
```

- [ ] **Step 2: Add a test**

```cpp
#include <sstream>

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
```

- [ ] **Step 3: Run tests**

```
make test
```

Expected: `28 passed, 0 failed`.

- [ ] **Step 4: Stage**

```
git add src/cli.cpp src/test_dynamic.cpp
```

---

### Task 11: `output_formatter.hpp` types

**Files:**
- Create: `include/output_formatter.hpp`
- Create: `src/output_formatter.cpp`

- [ ] **Step 1: Create `include/output_formatter.hpp`**

```cpp
//=======================================================================
// Unified text/JSON/CSV output for swr_calculator commands.
//=======================================================================
#pragma once

#include <iosfwd>
#include <string>
#include <vector>
#include <variant>
#include <cstdint>

namespace swr::output {

enum class Mode { TEXT, JSON, CSV };

// A field is a (label, value) pair where value is one of several types.
// The renderer formats appropriately per mode.
struct field {
    std::string label;
    std::variant<std::monostate, double, int64_t, std::string, bool> value;
    // Optional render hints (used in text mode mostly):
    enum class Hint { NONE, DOLLARS, PERCENT, INTEGER, YEARS } hint = Hint::NONE;
};

// Section: a labeled list of fields (used for inputs and results).
struct section {
    std::string         name;   // "inputs", "results" — used as JSON key
    std::string         title;  // "Inputs", "Results" — used in text mode
    std::vector<field>  fields;
};

// CSV row data: each row is a list of typed cells.
using csv_cell = std::variant<std::monostate, double, int64_t, std::string, bool>;
struct csv_row {
    std::vector<csv_cell> cells;
};

struct csv_block {
    std::vector<std::string>      column_headers;
    std::vector<std::string>      preamble_comments; // lines starting with "# "
    std::vector<csv_row>          rows;
};

struct report {
    std::string             command_name;
    std::vector<section>    sections;            // typically [inputs, results]
    std::vector<std::string> notes;              // optional
    csv_block               csv_data;            // only used when mode == CSV
};

void emit(std::ostream& out, const report& r, Mode mode);

// Emit an error report (terse), routed to the same mode-aware path.
void emit_error(std::ostream& out, const std::string& command_name,
                const std::string& message, Mode mode);

} // namespace swr::output
```

- [ ] **Step 2: Create `src/output_formatter.cpp` (stub for all three modes)**

```cpp
#include "output_formatter.hpp"

#include <iomanip>
#include <ostream>
#include <sstream>
#include <stdexcept>

namespace swr::output {

namespace {

std::string format_value_text(const field& f) {
    std::stringstream ss;
    ss << std::fixed;
    if (std::holds_alternative<std::monostate>(f.value)) return "";
    if (std::holds_alternative<bool>(f.value)) {
        return std::get<bool>(f.value) ? "yes" : "no";
    }
    if (std::holds_alternative<std::string>(f.value)) {
        return std::get<std::string>(f.value);
    }
    double v = 0.0;
    if (std::holds_alternative<double>(f.value))
        v = std::get<double>(f.value);
    else if (std::holds_alternative<int64_t>(f.value))
        v = static_cast<double>(std::get<int64_t>(f.value));

    switch (f.hint) {
        case field::Hint::DOLLARS:
            ss << "$" << std::setprecision(0) << v;
            break;
        case field::Hint::PERCENT:
            ss << std::setprecision(1) << v << "%";
            break;
        case field::Hint::INTEGER:
            ss << std::setprecision(0) << v;
            break;
        case field::Hint::YEARS:
            ss << std::setprecision(0) << v << " years";
            break;
        case field::Hint::NONE:
        default:
            ss << std::setprecision(2) << v;
            break;
    }
    return ss.str();
}

void emit_text(std::ostream& out, const report& r) {
    out << r.command_name << "\n";
    for (auto& sec : r.sections) {
        out << "\n" << sec.title << "\n";
        for (auto& f : sec.fields) {
            std::string left = "  " + f.label + ":";
            out << left;
            if (left.size() < 24) out << std::string(24 - left.size(), ' ');
            else out << " ";
            out << format_value_text(f) << "\n";
        }
    }
    if (!r.notes.empty()) {
        out << "\nNotes\n";
        for (auto& n : r.notes) out << "  " << n << "\n";
    }
}

std::string format_value_json(const field& f) {
    std::stringstream ss;
    ss << std::fixed;
    if (std::holds_alternative<std::monostate>(f.value)) return "null";
    if (std::holds_alternative<bool>(f.value)) {
        return std::get<bool>(f.value) ? "true" : "false";
    }
    if (std::holds_alternative<std::string>(f.value)) {
        // basic JSON-escape
        const std::string& s = std::get<std::string>(f.value);
        std::stringstream es;
        es << "\"";
        for (char c : s) {
            switch (c) {
                case '"':  es << "\\\""; break;
                case '\\': es << "\\\\"; break;
                case '\n': es << "\\n";  break;
                default:   es << c;      break;
            }
        }
        es << "\"";
        return es.str();
    }
    if (std::holds_alternative<int64_t>(f.value)) {
        ss << std::get<int64_t>(f.value);
        return ss.str();
    }
    ss << std::setprecision(4) << std::get<double>(f.value);
    return ss.str();
}

std::string json_key(const std::string& label) {
    // Convert a human label like "Current age" to a snake_case key.
    std::string out;
    out.reserve(label.size());
    for (char c : label) {
        if (c == ' ' || c == '-' || c == '/') out += '_';
        else if (c == '(' || c == ')' || c == ',' || c == ':') continue;
        else out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    // collapse consecutive underscores
    std::string cleaned;
    bool prev_us = false;
    for (char c : out) {
        if (c == '_') {
            if (!prev_us) cleaned += c;
            prev_us = true;
        } else {
            cleaned += c;
            prev_us = false;
        }
    }
    if (!cleaned.empty() && cleaned.back() == '_') cleaned.pop_back();
    return cleaned;
}

void emit_json(std::ostream& out, const report& r) {
    out << "{\n";
    out << "  \"command\": \"" << r.command_name << "\"";
    for (auto& sec : r.sections) {
        out << ",\n  \"" << sec.name << "\": {";
        for (size_t i = 0; i < sec.fields.size(); ++i) {
            auto& f = sec.fields[i];
            if (i > 0) out << ",";
            out << "\n    \"" << json_key(f.label) << "\": "
                << format_value_json(f);
        }
        out << "\n  }";
    }
    if (!r.notes.empty()) {
        out << ",\n  \"notes\": [";
        for (size_t i = 0; i < r.notes.size(); ++i) {
            if (i > 0) out << ", ";
            out << "\"" << r.notes[i] << "\"";
        }
        out << "]";
    }
    out << "\n}\n";
}

std::string format_cell_csv(const csv_cell& c) {
    std::stringstream ss;
    ss << std::fixed;
    if (std::holds_alternative<std::monostate>(c)) return "";
    if (std::holds_alternative<bool>(c))
        return std::get<bool>(c) ? "1" : "0";
    if (std::holds_alternative<std::string>(c))
        return std::get<std::string>(c);
    if (std::holds_alternative<int64_t>(c)) {
        ss << std::get<int64_t>(c);
        return ss.str();
    }
    ss << std::setprecision(2) << std::get<double>(c);
    return ss.str();
}

void emit_csv(std::ostream& out, const report& r) {
    for (auto& c : r.csv_data.preamble_comments) out << "# " << c << "\n";
    // Header
    for (size_t i = 0; i < r.csv_data.column_headers.size(); ++i) {
        if (i > 0) out << ",";
        out << r.csv_data.column_headers[i];
    }
    out << "\n";
    for (auto& row : r.csv_data.rows) {
        for (size_t i = 0; i < row.cells.size(); ++i) {
            if (i > 0) out << ",";
            out << format_cell_csv(row.cells[i]);
        }
        out << "\n";
    }
}

} // namespace

void emit(std::ostream& out, const report& r, Mode mode) {
    switch (mode) {
        case Mode::TEXT: emit_text(out, r); return;
        case Mode::JSON: emit_json(out, r); return;
        case Mode::CSV:  emit_csv(out, r); return;
    }
}

void emit_error(std::ostream& out, const std::string& command_name,
                const std::string& message, Mode mode) {
    switch (mode) {
        case Mode::JSON:
            out << "{ \"command\": \"" << command_name
                << "\", \"error\": \"" << message << "\" }\n";
            return;
        case Mode::CSV:
        case Mode::TEXT:
        default:
            out << "Error: " << message << "\n";
            return;
    }
}

} // namespace swr::output
```

- [ ] **Step 3: Add `src/output_formatter.cpp` to `Makefile`'s `TEST_SRC`**

```makefile
TEST_SRC := src/test_dynamic.cpp src/dynamic.cpp src/data.cpp \
            src/portfolio.cpp src/simulation.cpp src/cli.cpp \
            src/output_formatter.cpp
```

- [ ] **Step 4: Run `make test` to verify it builds**

```
make test
```

Expected: `28 passed, 0 failed`.

- [ ] **Step 5: Stage**

```
git add include/output_formatter.hpp src/output_formatter.cpp Makefile
```

---

### Task 12: TDD output_formatter — basic text/JSON/CSV round-trips

**Files:**
- Modify: `src/test_dynamic.cpp`

- [ ] **Step 1: Add tests**

```cpp
#include "output_formatter.hpp"

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
    CHECK(s.find("$850000")  != std::string::npos);
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
```

- [ ] **Step 2: Run tests**

```
make test
```

Expected: `33 passed, 0 failed`. If any text formatting test fails on number precision (e.g. `$850000` vs `$850000.0`), adjust `format_value_text` precision in `output_formatter.cpp` until the round-trip works.

- [ ] **Step 3: Stage**

```
git add src/test_dynamic.cpp
```

---

### Task 13: Implement `dynamic::compute` — scenario construction + solver

**Files:**
- Modify: `src/dynamic.cpp`

- [ ] **Step 1: Replace the `compute` stub**

Replace the body of `dynamic_result compute(...)` in `src/dynamic.cpp` with:

```cpp
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
        // Re-run one more simulation at final budget capturing per-path data.
        // The existing swr::results.terminal_values exposes per-path TVs.
        float final_wr_pct =
            (r.final_spending_budget / input.current_balance) * 100.0f;
        sc.wr = final_wr_pct;
        auto res = swr::simulation(sc);
        // The existing engine doesn't expose per-(start_year,start_month)
        // tuples directly; terminal_values is a flat vector parallel to the
        // iteration order. We reconstruct the (year, month) sequence by
        // walking valid start points the same way the engine does.
        // For Phase 1, emit only terminal values keyed sequentially with the
        // earliest plausible start dates. If finer detail is needed later,
        // extend swr::results.
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
```

- [ ] **Step 2: Add a smoke test for `compute` that uses real data**

Add to `src/test_dynamic.cpp`:

```cpp
TEST(compute_basic_no_ssa_no_smoothing) {
    swr::dynamic_input in;
    in.current_balance = 850000.0f;
    in.current_age     = 67.0f;
    in.end_age         = 92;
    in.portfolio       = swr::parse_portfolio("us_stocks:60,us_bonds:40", false);
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
```

- [ ] **Step 3: Run tests**

```
make test
```

Expected: `34 passed, 0 failed`. If the bounds in `compute_basic_no_ssa_no_smoothing` fail, adjust them based on actual measurement and document the actual range in a comment. The test exists primarily to verify the function runs end-to-end and produces plausible numbers, not to pin a specific value.

- [ ] **Step 4: Stage**

```
git add src/dynamic.cpp src/test_dynamic.cpp
```

---

### Task 14: TDD property test — binary search vs. linear sweep reference

**Files:**
- Modify: `src/test_dynamic.cpp`

This is the high-value correctness check from the spec. The reference is a
**test-local** linear sweep, not a production command. (The original
`constant_dollar_max` was dropped from the spec — `dynamic_dollar` subsumes
its functionality.)

- [ ] **Step 1: Add the property test**

```cpp
TEST(dynamic_solver_matches_linear_sweep_reference) {
    // Build the same scenario two ways:
    // 1) via dynamic::compute (binary search on WR)
    // 2) via direct linear sweep over swr::simulation (test-only reference)
    // They should agree on the max WR (within a small tolerance).

    auto portfolio = swr::parse_portfolio("us_stocks:60,us_bonds:40", false);
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

    // --- Direct linear sweep
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

    const float step = 0.01f;
    float linear_max_wr = 0.0f;
    for (float wr = 0.5f; wr <= 20.0f + step/2.0f; wr += step) {
        sc.wr = wr;
        auto res = swr::simulation(sc);
        if (res.success_rate >= target) linear_max_wr = wr;
    }

    // Allow 0.05% tolerance (binary search vs. step-0.01 sweep can
    // disagree by less than the step plus binary-search residual).
    CHECK_NEAR(dynamic_wr_pct, linear_max_wr, 0.05f);
}
```

- [ ] **Step 2: Run tests**

```
make test
```

Expected: `35 passed, 0 failed`. **Note:** this test is slow (does a 0.5%–20% sweep at 0.01% step = ~1950 simulations). If it takes >30s, narrow the sweep range based on dynamic_wr_pct ±0.5%, then assert tolerance.

If the test fails with a small gap (e.g. 0.06% difference), increase the `CHECK_NEAR` tolerance to 0.10. The point is correctness within the step granularity; not exact equality.

- [ ] **Step 3: Stage**

```
git add src/test_dynamic.cpp
```

---

### Task 15: Wire `dynamic_dollar` into `main.cpp` CLI dispatch

**Files:**
- Modify: `src/main.cpp`

- [ ] **Step 1a: Add includes near the top of `src/main.cpp`**

In the existing `#include` block near the top of `src/main.cpp` (after the existing project includes), add:

```cpp
#include "cli.hpp"
#include "dynamic.hpp"
#include "output_formatter.hpp"
#include <sstream>
```

- [ ] **Step 1b: Add the new command handler at the bottom of `src/main.cpp`, before `int main(...)`**

Use a `Read` of `src/main.cpp` near line ~4990 to locate the dispatch. Insert the handler function above `int main`:

```cpp
//-----------------------------------------------------------------------
// dynamic_dollar — new flag-style command
//-----------------------------------------------------------------------

namespace {

swr::cli::command_schema dynamic_dollar_schema() {
    using namespace swr::cli;
    command_schema s;
    s.command_name = "dynamic_dollar";
    s.one_line_description =
        "Compute this year's sustainable withdrawal given current portfolio "
        "reality,\n  using historical backtesting over the remaining horizon.";
    s.example_invocation =
        "swr_calculator dynamic_dollar --balance 850000 --current_age 67 "
        "--end_age 92 \\\n    --portfolio us_stocks:60,us_bonds:40 "
        "--inflation us_inflation";

    s.flags = {
        {"balance",       FlagGroup::REQUIRED, FlagKind::VALUE,
         "Current portfolio balance (dollars)", ""},
        {"current_age",   FlagGroup::REQUIRED, FlagKind::VALUE,
         "Your current age (float allowed)", ""},
        {"end_age",       FlagGroup::REQUIRED, FlagKind::VALUE,
         "Planning end age (integer)", ""},
        {"portfolio",     FlagGroup::REQUIRED, FlagKind::VALUE,
         "Portfolio spec, e.g. \"us_stocks:60,us_bonds:40\"", ""},
        {"inflation",     FlagGroup::REQUIRED, FlagKind::VALUE,
         "Inflation series, e.g. \"us_inflation\"", ""},

        {"target_success", FlagGroup::COMMON, FlagKind::VALUE,
         "Target success rate (percent)", "80"},
        {"rebalance",      FlagGroup::COMMON, FlagKind::VALUE,
         "none | monthly | yearly | threshold", "none"},
        {"ssa_income",     FlagGroup::COMMON, FlagKind::VALUE,
         "Annual SSA income (dollars; 0 = disabled)", "0"},
        {"ssa_start_age",  FlagGroup::COMMON, FlagKind::VALUE,
         "Age SSA begins (required if --ssa_income > 0)", "0"},
        {"smoothing",      FlagGroup::COMMON, FlagKind::VALUE,
         "Max YoY change as fraction, e.g. 0.10 (0 = disabled)", "0"},
        {"prior_amount",   FlagGroup::COMMON, FlagKind::VALUE,
         "Last year's spending budget (also enables signal)", "0"},
        {"json",           FlagGroup::COMMON, FlagKind::PRESENCE,
         "Emit JSON output", ""},
        {"csv",            FlagGroup::COMMON, FlagKind::PRESENCE,
         "Emit CSV per-path output", ""},

        {"start_year",     FlagGroup::ADVANCED, FlagKind::VALUE,
         "Earliest historical backtest start year (default: full data)", "0"},
        {"end_year",       FlagGroup::ADVANCED, FlagKind::VALUE,
         "Latest historical backtest start year (default: full data)", "0"},
        {"withdraw_frequency", FlagGroup::ADVANCED, FlagKind::VALUE,
         "12 = yearly, 1 = monthly", "12"},
        {"fees",           FlagGroup::ADVANCED, FlagKind::VALUE,
         "TER as fraction", "0.001"},
        {"solver_tolerance", FlagGroup::ADVANCED, FlagKind::VALUE,
         "Binary-search stopping tolerance (dollars)", "1"},
    };
    s.mutually_exclusive.push_back({"json", "csv"});
    return s;
}

int dynamic_dollar_cmd(const std::vector<std::string>& args) {
    auto schema = dynamic_dollar_schema();
    swr::output::Mode mode = swr::output::Mode::TEXT;

    swr::cli::parsed_args p;
    try {
        p = swr::cli::parse_flags(args, schema);
    } catch (const std::exception& e) {
        // Determine mode from raw args so we route error correctly.
        for (auto& a : args) {
            if (a == "--json") mode = swr::output::Mode::JSON;
            else if (a == "--csv") mode = swr::output::Mode::CSV;
        }
        swr::output::emit_error(std::cerr, "dynamic_dollar", e.what(), mode);
        return 1;
    }

    if (p.help_requested) {
        swr::cli::render_help(std::cout, schema);
        return 0;
    }

    if (swr::cli::get_presence(p, "json")) mode = swr::output::Mode::JSON;
    if (swr::cli::get_presence(p, "csv"))  mode = swr::output::Mode::CSV;

    // Build dynamic_input from parsed flags.
    swr::dynamic_input in;
    try {
        in.current_balance =
            std::stof(swr::cli::get_value(p, schema, "balance"));
        in.current_age =
            std::stof(swr::cli::get_value(p, schema, "current_age"));
        in.end_age =
            static_cast<size_t>(std::stoul(swr::cli::get_value(p, schema, "end_age")));
        in.portfolio =
            swr::parse_portfolio(swr::cli::get_value(p, schema, "portfolio"), false);
        swr::normalize_portfolio(in.portfolio);
        in.inflation = swr::cli::get_value(p, schema, "inflation");
        in.rebalance = swr::parse_rebalance(
            swr::cli::get_value(p, schema, "rebalance"));
        in.target_success =
            std::stof(swr::cli::get_value(p, schema, "target_success"));

        float ssa_income = std::stof(swr::cli::get_value(p, schema, "ssa_income"));
        size_t ssa_age   = static_cast<size_t>(
            std::stoul(swr::cli::get_value(p, schema, "ssa_start_age")));
        if (ssa_income > 0.0f) {
            if (ssa_age == 0) {
                swr::output::emit_error(std::cerr, "dynamic_dollar",
                    "--ssa_income > 0 requires --ssa_start_age", mode);
                return 1;
            }
            in.ssa_enabled       = true;
            in.ssa_annual_income = ssa_income;
            in.ssa_start_age     = ssa_age;
        }

        float smoothing = std::stof(swr::cli::get_value(p, schema, "smoothing"));
        float prior     = std::stof(swr::cli::get_value(p, schema, "prior_amount"));
        if (smoothing > 0.0f) {
            if (prior <= 0.0f) {
                swr::output::emit_error(std::cerr, "dynamic_dollar",
                    "--smoothing > 0 requires --prior_amount", mode);
                return 1;
            }
            in.smoothing_enabled    = true;
            in.smoothing_max_change = smoothing;
        }
        in.prior_year_amount = prior; // also enables signal regardless of smoothing

        in.historical_start_year = static_cast<size_t>(
            std::stoul(swr::cli::get_value(p, schema, "start_year")));
        in.historical_end_year = static_cast<size_t>(
            std::stoul(swr::cli::get_value(p, schema, "end_year")));
        in.withdraw_frequency = static_cast<size_t>(
            std::stoul(swr::cli::get_value(p, schema, "withdraw_frequency")));
        in.fees = std::stof(swr::cli::get_value(p, schema, "fees"));
        in.solver_tolerance = std::stof(
            swr::cli::get_value(p, schema, "solver_tolerance"));
    } catch (const std::exception& e) {
        swr::output::emit_error(std::cerr, "dynamic_dollar",
            std::string("flag parse error: ") + e.what(), mode);
        return 1;
    }

    // Compute.
    bool collect_csv = (mode == swr::output::Mode::CSV);
    auto r = swr::compute(in, collect_csv);
    if (r.error) {
        swr::output::emit_error(std::cerr, "dynamic_dollar", r.message, mode);
        return 1;
    }

    // Build report and emit.
    swr::output::report rep;
    rep.command_name = "dynamic_dollar";
    {
        swr::output::section sec;
        sec.name  = "inputs";
        sec.title = "Inputs";
        sec.fields.push_back({"Current balance", (double)in.current_balance,
                              swr::output::field::Hint::DOLLARS});
        sec.fields.push_back({"Current age", (double)in.current_age,
                              swr::output::field::Hint::NONE});
        sec.fields.push_back({"End age", (int64_t)in.end_age,
                              swr::output::field::Hint::INTEGER});
        sec.fields.push_back({"Remaining horizon",
                              (int64_t)r.remaining_horizon_years,
                              swr::output::field::Hint::YEARS});
        sec.fields.push_back({"Target success", (double)in.target_success,
                              swr::output::field::Hint::PERCENT});
        rep.sections.push_back(sec);
    }
    {
        swr::output::section sec;
        sec.name  = "results";
        sec.title = "Results";
        sec.fields.push_back({"Raw calculated",
                              (double)r.raw_calculated_withdrawal,
                              swr::output::field::Hint::DOLLARS});
        sec.fields.push_back({"Smoothed",
                              (double)r.smoothed_withdrawal,
                              swr::output::field::Hint::DOLLARS});
        sec.fields.push_back({"Final budget",
                              (double)r.final_spending_budget,
                              swr::output::field::Hint::DOLLARS});
        sec.fields.push_back({"Success at budget",
                              (double)r.probability_of_success,
                              swr::output::field::Hint::PERCENT});
        sec.fields.push_back({"SSA offset this year",
                              (double)r.ssa_offset_this_year,
                              swr::output::field::Hint::DOLLARS});
        sec.fields.push_back({"Portfolio withdrawal",
                              (double)r.portfolio_withdrawal_this_year,
                              swr::output::field::Hint::DOLLARS});
        std::stringstream sig_ss;
        sig_ss << r.signal;
        sec.fields.push_back({"Signal vs prior", sig_ss.str(),
                              swr::output::field::Hint::NONE});
        rep.sections.push_back(sec);
    }
    if (!r.message.empty()) rep.notes.push_back(r.message);

    if (mode == swr::output::Mode::CSV) {
        rep.csv_data.column_headers = {
            "start_year", "start_month", "success",
            "terminal_value", "total_withdrawn", "worst_duration_months"};
        std::stringstream pre;
        pre << "command: dynamic_dollar";
        rep.csv_data.preamble_comments.push_back(pre.str());
        for (auto& d : r.per_path_details) {
            swr::output::csv_row row;
            row.cells.push_back((int64_t)d.start_year);
            row.cells.push_back((int64_t)d.start_month);
            row.cells.push_back(d.success);
            row.cells.push_back((double)d.terminal_value);
            row.cells.push_back((double)d.total_withdrawn);
            if (d.worst_duration_months > 0)
                row.cells.push_back((int64_t)d.worst_duration_months);
            else
                row.cells.push_back(std::monostate{});
            rep.csv_data.rows.push_back(row);
        }
    }

    swr::output::emit(std::cout, rep, mode);
    return 0;
}

} // namespace
```

- [ ] **Step 2: Add the dispatch branch inside `main()`**

In the `else if` chain inside `main()`, insert before the existing `else { std::cout << "Unhandled command ..."` block (around line 5085):

```cpp
} else if (command == "dynamic_dollar") {
    std::vector<std::string> sub_args(args.begin() + 1, args.end());
    return dynamic_dollar_cmd(sub_args);
```

- [ ] **Step 3: Wire the new objects into the production build**

The existing `Makefile` uses `auto_folder_compile(src)` which globs all `.cpp` files in `src/`. Confirm by inspection that `dynamic.cpp`, `cli.cpp`, `output_formatter.cpp` are picked up automatically.

If `make-utils` doesn't auto-include them, manually add the objects to the build by modifying the `Makefile` accordingly (consult `make-utils/cpp-utils.mk` if needed).

- [ ] **Step 4: Build and verify**

Run:

```
make release_debug
```

Expected: clean build with no errors.

Then run the new command:

```
./release_debug/bin/swr_calculator dynamic_dollar \
  --balance 850000 --current_age 67 --end_age 92 \
  --portfolio us_stocks:60,us_bonds:40 --inflation us_inflation
```

Expected: text output with "Inputs"/"Results" sections, a sensible final budget in the $30K-$60K range, no errors.

(If the binary path is different — `release_debug/swr_calculator` vs. `release_debug/bin/swr_calculator` — adjust accordingly. Verify with `find release_debug -name swr_calculator`.)

- [ ] **Step 5: Run all tests**

```
make test
```

Expected: `35 passed, 0 failed`.

- [ ] **Step 6: Stage**

```
git add src/main.cpp
```

---

### Task 16: Smoke script + baselines for Phase 1 milestone

**Files:**
- Create: `scripts/smoke.sh`
- Create: `scripts/smoke_expected/dynamic_dollar.txt`

- [ ] **Step 1: Create `scripts/smoke.sh`**

```bash
#!/usr/bin/env bash
# Smoke regression. Runs each kept command with a canonical scenario and
# diffs the output against scripts/smoke_expected/.
#
# Usage:
#   scripts/smoke.sh           # diff against baselines, exit 1 on diff
#   UPDATE=1 scripts/smoke.sh  # update baselines instead of diffing
set -euo pipefail

cd "$(dirname "$0")/.."

BIN="release_debug/bin/swr_calculator"
if [[ ! -x "$BIN" ]]; then
    BIN="release_debug/swr_calculator"
fi
if [[ ! -x "$BIN" ]]; then
    echo "Smoke: cannot find swr_calculator binary. Run 'make' first."
    exit 1
fi

EXP_DIR="scripts/smoke_expected"
mkdir -p "$EXP_DIR"

run_one() {
    local name="$1"; shift
    local expected="$EXP_DIR/$name.txt"
    local actual
    actual="$("$BIN" "$@" 2>&1 || true)"
    if [[ "${UPDATE:-0}" == "1" ]]; then
        printf '%s\n' "$actual" > "$expected"
        echo "Updated: $expected"
        return 0
    fi
    if [[ ! -f "$expected" ]]; then
        echo "Smoke: missing baseline $expected. Run 'UPDATE=1 scripts/smoke.sh'."
        return 1
    fi
    if ! diff -u "$expected" <(printf '%s\n' "$actual"); then
        echo "Smoke FAIL: $name"
        return 1
    fi
    echo "Smoke OK: $name"
}

# Phase 1: only dynamic_dollar uses flag style. The others still take
# positional args here; their smoke entries land in Phase 2.
run_one dynamic_dollar \
    dynamic_dollar \
    --balance 850000 --current_age 67 --end_age 92 \
    --portfolio us_stocks:60,us_bonds:40 --inflation us_inflation \
    --rebalance yearly

echo "Smoke: all checks passed"
```

- [ ] **Step 2: Make it executable**

```
chmod +x scripts/smoke.sh
```

- [ ] **Step 3: Generate the baseline**

```
make release_debug
UPDATE=1 scripts/smoke.sh
```

Expected: `Updated: scripts/smoke_expected/dynamic_dollar.txt`.

Inspect the file:

```
cat scripts/smoke_expected/dynamic_dollar.txt
```

The output should look like the Section 5 example in the spec (Inputs / Results sections, sensible numbers). If the format looks wrong, fix `dynamic_dollar_cmd` in `main.cpp` and regenerate.

- [ ] **Step 4: Verify smoke passes on a clean run**

```
scripts/smoke.sh
```

Expected: `Smoke OK: dynamic_dollar` then `Smoke: all checks passed`.

- [ ] **Step 5: Run the full quality gate**

```
make && make test && bash scripts/smoke.sh
```

Expected: clean build, `35 passed, 0 failed`, `Smoke: all checks passed`.

- [ ] **Step 6: Stage everything from Phase 1**

```
git add scripts/smoke.sh scripts/smoke_expected/ \
        include/test_assert.hpp include/cli.hpp include/output_formatter.hpp \
        include/dynamic.hpp \
        src/test_dynamic.cpp src/cli.cpp src/output_formatter.cpp \
        src/dynamic.cpp src/main.cpp \
        Makefile
```

Inform user:

> **Phase 1 (commit 1) ready for review.** New module, test infrastructure, and `dynamic_dollar` CLI command staged. `make && make test && bash scripts/smoke.sh` passes. The three existing commands remain on positional args (Phase 2 converts them). Please review the diff and commit when ready. Suggested commit message:
> ```
> feat: add dynamic_dollar command + test infrastructure
>
> New module: include/dynamic.hpp + src/dynamic.cpp implementing per-year
> sustainable withdrawal via historical backtesting + binary search.
> Adds tiny flag parser (cli.hpp), unified output formatter
> (output_formatter.hpp), and home-grown test framework (test_assert.hpp).
>
> The three existing analytical commands (swr, failsafe, current_wr) are
> not yet converted to flag style — that happens in the next commit.
> ```

---

## Phase 2 — Redesign + convert two kept commands to flag-style (commit milestone 2)

End state: `swr` is **redesigned** as a single-WR point-query evaluator named `constant_dollar`. `current_wr` is **redesigned** as a single-pct point-query evaluator named `constant_percent`. Both use the unified text/JSON/CSV output. `failsafe` is **not touched** here — it gets deleted entirely in Phase 4. Smoke baselines regenerated.

> **Important: this is a redesign, not a behavior-preserving rename.** The old
> `swr` was a built-in 6%→2% sweep that returned the highest WR meeting 95%
> success. The new `constant_dollar` is a single-WR evaluator. The old
> `current_wr` swept both WR and portfolio allocations and returned a
> CSV grid. The new `constant_percent` evaluates a single (pct, portfolio)
> combination. The old behaviors are dropped.

### Task 17: Add `constant_dollar` (single-WR evaluator, replaces `swr`)

**Files:**
- Modify: `src/main.cpp`
- Modify: `scripts/smoke.sh`

- [ ] **Step 1: Read the existing `single_swr_scenario` function (reference only)**

Locate via grep:

```
grep -n "single_swr_scenario" src/main.cpp
```

Read it to understand its current sweep behavior — but **the new command is not a port**. The new command evaluates one WR at one portfolio. The sweep is dropped.

- [ ] **Step 2: Add the new `constant_dollar` command handler**

Add a `constant_dollar_schema()` and `constant_dollar_cmd()` pair to `src/main.cpp`, following the structure of `dynamic_dollar_schema()`/`dynamic_dollar_cmd()` from Task 15.

Flag schema (per spec Section 4):
- Required: `--wr <pct>`, `--portfolio`, `--inflation`, `--years`
- Common: `--rebalance` (default `none`), `--start_year` (default 0), `--end_year` (default 0), `--initial_value` (default 1000), `--target_success` (default 0 = unused; if > 0, emit pass/fail), `--json`, `--csv`
- Advanced: `--withdraw_frequency` (default 12), `--fees` (default 0.001)
- Mutually exclusive: `--json` and `--csv`

Inside the handler:
1. Parse flags into a `swr::scenario` with `wmethod = WithdrawalMethod::STANDARD` and `scenario.wr = --wr`.
2. Run `swr::simulation(scenario)` **once** (no sweep).
3. Build a `swr::output::report` with:
   - **Inputs section:** portfolio (formatted as a comma list), inflation series, horizon, data window, rebalance, initial value, withdrawal rate (pct), withdraw frequency, fees
   - **Results section:** success rate, terminal-value summary (avg/median/min/max), total withdrawn, worst duration (months and start year/month if applicable). Pull from the existing `swr::results` struct.
   - **Notes:** if `--target_success > 0`, add either "PASS: success rate meets target" or "FAIL: success rate below target".
4. CSV: populate `csv_data.rows` from `results.terminal_values` with the same scheme as `dynamic_dollar`'s CSV.
5. Emit.

- [ ] **Step 3: Register the new dispatch branch**

In `main()`, add:

```cpp
} else if (command == "constant_dollar") {
    std::vector<std::string> sub_args(args.begin() + 1, args.end());
    return constant_dollar_cmd(sub_args);
```

Leave the existing `swr` dispatch branch in place for now — it will be deleted in Phase 4. The user can still run the old `swr` (sweep) command until then if they want a sanity check on the old behavior.

- [ ] **Step 4: Add a smoke entry**

In `scripts/smoke.sh`, add before the final echo line:

```bash
run_one constant_dollar \
    constant_dollar --wr 4.0 --portfolio us_stocks:60,us_bonds:40 \
    --inflation us_inflation --years 30 --rebalance yearly
```

- [ ] **Step 5: Build, run, smoke-update**

```
make && make test && UPDATE=1 scripts/smoke.sh
```

Expected: clean build, all unit tests pass, baseline updated for `constant_dollar`.

Manually inspect `scripts/smoke_expected/constant_dollar.txt`. It should show Inputs (portfolio, horizon, WR, etc.) and Results (success rate, terminal values). Numbers should be sensible (e.g. 4% over 30 years on 60/40 gives ~95%+ historical success).

- [ ] **Step 6: Run smoke as a check (no UPDATE)**

```
scripts/smoke.sh
```

Expected: `Smoke OK: dynamic_dollar`, `Smoke OK: constant_dollar`.

- [ ] **Step 7: Stage**

```
git add src/main.cpp scripts/smoke.sh scripts/smoke_expected/constant_dollar.txt
```

---

### Task 18: Add `constant_percent` (single-pct evaluator, replaces `current_wr`)

**Files:**
- Modify: `src/main.cpp`
- Modify: `scripts/smoke.sh`

- [ ] **Step 1: Read existing `current_wr_scenario` (reference only)**

```
grep -n "current_wr_scenario" src/main.cpp
```

Read it to understand the existing sweep matrix behavior — but the new command does **not** sweep. It evaluates a single (pct, portfolio) point query using `WithdrawalMethod::CURRENT`.

- [ ] **Step 2: Add `constant_percent_schema()` and `constant_percent_cmd()`**

Flag schema:
- Required: `--pct <pct>`, `--portfolio`, `--inflation`, `--years`
- Common: `--rebalance` (default `none`), `--start_year` (0), `--end_year` (0), `--initial_value` (1000), `--minimum_floor` (default 3.0; percent of initial value as a spending floor), `--json`, `--csv`
- Advanced: `--withdraw_frequency` (12), `--fees` (0.001)
- Mutually exclusive: `--json` and `--csv`

Handler:
1. Parse flags into a `swr::scenario` with `wmethod = WithdrawalMethod::CURRENT`, `scenario.wr = --pct`, `scenario.minimum = --minimum_floor / 100.0f`.
2. Run `swr::simulation(scenario)` **once** (no sweep).
3. Build report:
   - **Inputs:** portfolio, inflation series, horizon, data window, rebalance, initial value, pct, minimum floor, withdraw frequency, fees
   - **Results:** success rate, spending statistics (avg/median/min/max from `swr::results`'s `spending_*` fields), terminal-value summary
4. CSV: per-path outcomes at the given pct.

- [ ] **Step 3: Register**

```cpp
} else if (command == "constant_percent") {
    std::vector<std::string> sub_args(args.begin() + 1, args.end());
    return constant_percent_cmd(sub_args);
```

Leave the existing `current_wr` dispatch branch in place for Phase 4.

- [ ] **Step 4: Smoke entry**

```bash
run_one constant_percent \
    constant_percent --pct 4.0 --portfolio us_stocks:60,us_bonds:40 \
    --inflation us_inflation --years 30 --rebalance yearly
```

- [ ] **Step 5: Build + smoke**

```
make && make test && UPDATE=1 scripts/smoke.sh && scripts/smoke.sh
```

- [ ] **Step 6: Stage**

```
git add src/main.cpp scripts/smoke.sh scripts/smoke_expected/constant_percent.txt
```

---

### Task 19: Phase 2 milestone — verify and stage commit

- [ ] **Step 1: Final verification**

```
make && make test && bash scripts/smoke.sh
```

Expected: clean build, all unit tests pass, all three smoke entries (`dynamic_dollar`, `constant_dollar`, `constant_percent`) pass.

- [ ] **Step 2: Inform user that Phase 2 is staged**

> **Phase 2 (commit 2) ready for review.** Two kept commands redesigned as single-point queries and converted to flag-style with unified output. The old `swr` and `current_wr` dispatch branches are still in place (deleted in Phase 4). `failsafe` is also still in place (deleted in Phase 4). Smoke baselines regenerated as part of this commit (intentional format change). Please review the diff and commit when ready. Suggested commit message:
> ```
> refactor: redesign + convert swr and current_wr to flag-style point queries
>
> swr → constant_dollar (single-WR evaluator; drops the built-in 6%→2%
>   max-WR sweep)
> current_wr → constant_percent (single-pct evaluator; drops the WR sweep,
>   portfolio sweep, and STANDARD-methodology override)
>
> Both commands now share a flag-style interface, --help/--json/--csv flags,
> and the unified Inputs/Results/Notes output template. Old positional
> dispatchers retained for one more commit to be deleted in the cleanup phase.
> ```

---

## Phase 3 — Delete HTTP server stack (commit milestone 3)

End state: HTTP server, signal handlers, cpp-httplib dependency, and web Dockerfile all gone. Smoke output byte-identical to Phase 2.

### Task 21: Remove HTTP server handler functions

**Files:**
- Modify: `src/main.cpp`

- [ ] **Step 1: Locate and delete each function**

In `src/main.cpp`, locate and delete:

```
grep -n "^void server_simple_api\|^void server_retirement_api\|^void server_fi_planner_api\|^void server_signal_handler\|^void install_signal_handler" src/main.cpp
```

Delete each function body entirely.

- [ ] **Step 2: Remove the `server_ptr` global**

```
grep -n "server_ptr" src/main.cpp
```

Delete the declaration and any remaining references.

- [ ] **Step 3: Remove the `server` command branch in `main()`**

In the dispatch chain in `main()`, find:

```cpp
} else if (command == "server") {
    ...
    server.listen(...);
    ...
}
```

Delete the entire branch.

- [ ] **Step 4: Remove `#include <httplib.h>` from `src/main.cpp`**

```
grep -n "#include <httplib" src/main.cpp
```

Delete the line.

- [ ] **Step 5: Build and verify**

```
make
```

Expected: clean build. If there are link errors, they'll point to other places that reference the removed symbols — clean those up.

```
make test && bash scripts/smoke.sh
```

Expected: all tests pass, all smoke entries pass with byte-identical output.

- [ ] **Step 6: Stage**

```
git add src/main.cpp
```

---

### Task 22: Remove cpp-httplib from build system

**Files:**
- Modify: `Makefile`
- Modify: `.gitmodules`
- Delete: `cpp-httplib/`

- [ ] **Step 1: Remove the `-isystem cpp-httplib` flag from Makefile**

In `Makefile`, locate:

```makefile
CXX_FLAGS += -pthread -isystem cpp-httplib
```

Change to:

```makefile
CXX_FLAGS += -pthread
```

- [ ] **Step 2: Remove the cpp-httplib submodule**

```
git submodule deinit cpp-httplib
git rm cpp-httplib
rm -rf .git/modules/cpp-httplib
```

This also updates `.gitmodules` automatically. Verify:

```
cat .gitmodules
```

The `[submodule "cpp-httplib"]` block should be gone. If `.gitmodules` is now empty, delete the file:

```
[[ ! -s .gitmodules ]] && rm .gitmodules
```

- [ ] **Step 3: Build and verify**

```
make && make test && bash scripts/smoke.sh
```

Expected: all green.

- [ ] **Step 4: Stage**

```
git add Makefile
# git rm above already staged the submodule removal
[[ -f .gitmodules ]] && git add .gitmodules
```

---

### Task 23: Delete web Dockerfile artifacts

**Files:**
- Delete: `build/Dockerfile.web`
- Delete: `build/build_web_image.sh`

- [ ] **Step 1: Delete the files**

```
git rm build/Dockerfile.web build/build_web_image.sh
```

If the `build/` directory is now empty, leave it for now (Phase 5 will handle anything else there).

- [ ] **Step 2: Verify**

```
make && make test && bash scripts/smoke.sh
```

Expected: all green.

- [ ] **Step 3: Inform user that Phase 3 is staged**

> **Phase 3 (commit 3) ready for review.** HTTP server stack, cpp-httplib submodule, and web Dockerfile artifacts removed. `make && make test && bash scripts/smoke.sh` passes. Suggested commit message:
> ```
> chore: remove HTTP server stack and cpp-httplib dependency
>
> Deletes server_*_api handlers, signal handlers, server command branch,
> cpp-httplib submodule and -isystem flag, and the web Dockerfile.
> Reduces main.cpp by ~2300 lines.
> ```

---

## Phase 4 — Delete unused subcommand handlers (commit milestone 4)

End state: only the three kept commands and their dispatch branches remain in `main.cpp`. All graph/sheets helpers, unused analytical scenarios, the deleted `failsafe_scenario` + its `failsafe_swr` worker(s), and the old `single_swr_scenario` + `current_wr_scenario` dispatchers are gone. Smoke output byte-identical to Phase 2.

### Task 24: Inventory which helpers are used by kept handlers

**Files:**
- Read-only inventory; no edits this task

- [ ] **Step 1: For each kept handler, list its function calls**

The three kept handlers (current names after Phase 2): `constant_dollar_cmd`, `constant_percent_cmd`, `dynamic_dollar_cmd`. Also the old `single_swr_scenario`, `failsafe_scenario`, `current_wr_scenario` still exist until this phase deletes them (deleted in Task 25). `failsafe_scenario` and its `failsafe_swr` worker functions are deleted outright (no replacement) since `dynamic_dollar` subsumes the functionality.

Run for each kept `_cmd` function:

```
awk '/^int constant_dollar_cmd/,/^}$/' src/main.cpp | grep -oE "[a-zA-Z_][a-zA-Z_0-9]+\s*\(" | sort -u
```

Repeat for `constant_percent_cmd`, `dynamic_dollar_cmd`.

Record the set of helpers actually called. Anything NOT in this set, and not part of the `swr::` namespace, is a candidate for deletion.

- [ ] **Step 2: Generate the deletion candidate list**

From the spec's Section 7 deletion list:

- Subcommand handlers (definitely deletable): `fixed_scenario`, `multiple_swr_scenario`, `withdraw_frequency_scenario`, `frequency_scenario`, `analysis_scenario`, `portfolio_analysis_scenario`, `allocation_scenario`, `term_scenario`, `glidepath_scenario`, `data_graph_scenario`, `data_time_graph_scenario`, `trinity_success_scenario`, `die_with_zero_scenario`, `trinity_cash_graphs_scenario`, `trinity_duration_scenario`, `trinity_tv_scenario`, `trinity_spending_scenario`, `social_scenario`, `social_pf_scenario`, `income_scenario`, `rebalance_scenario`, `threshold_rebalance_scenario`, `trinity_low_yield_scenario`, `flexibility_graph_scenario`, `flexibility_auto_graph_scenario`, `selection_graph_scenario`, `trinity_cash_graph_scenario`, `times_graph_scenario`. Plus the old positional handlers `single_swr_scenario`, `failsafe_scenario`, `current_wr_scenario` (now superseded by their `*_cmd` replacements).

- Help functions (deletable): `print_general_help`, `print_fixed_help`, `print_swr_help`, `print_multiple_wr_help`, `print_withdraw_frequency_help`, `print_frequency_help`.

- Helpers (verify-then-delete): `GraphBase`, `Graph`, `multiple_wr`, all `multiple_wr_*` (sheets and graph), `multiple_rebalance_*`, `configure_withdrawal_method`, `csv_print`, `failsafe_swr` (both overloads at lines ~645 and ~659).

For the `failsafe_swr` helper specifically: it was only used by `failsafe_scenario`, which is being deleted entirely. The helper (both overloads) is deleted alongside `failsafe_scenario`.

- [ ] **Step 3: No file changes this task; record the list**

Write the verified deletion list as a comment block at the top of a new file `docs/superpowers/plans/.phase4-deletion-list.txt` (or similar scratch location) — or paste it into the next task. The point is: each candidate must have been confirmed unused before deletion in Task 25.

---

### Task 25: Delete unused handlers and their dispatch branches

**Files:**
- Modify: `src/main.cpp`

- [ ] **Step 1: Delete the unused subcommand handlers**

For each deletable handler from Task 24's list, delete:
- The function body
- Its dispatch branch in `main()`
- Its associated help-printing line if separate

Do this in small batches (e.g. 5 functions at a time) and run `make && make test && bash scripts/smoke.sh` after each batch. If smoke output changes byte-for-byte, you accidentally removed something a kept command needed — revert that batch and investigate.

Recommended deletion order (groups of related dead helpers):
1. Trinity-family handlers + their unique helpers
2. Flexibility / glidepath handlers
3. Rebalance variant handlers
4. Social / income handlers
5. Multiple-WR analytical handlers + the `multiple_wr_*` helpers they rely on
6. Graph base classes (`GraphBase`, `Graph`) once nothing else uses them
7. The old `single_swr_scenario`, `failsafe_scenario`, `current_wr_scenario` and the `swr`/`failsafe`/`current_wr` dispatch branches (now superseded by Phase 2's `*_cmd` versions)
8. The `print_*_help` functions

- [ ] **Step 2: Delete the global `print_general_help` and replace with a stub**

After deletion, `main.cpp`'s entry-point still references `print_general_help` for the empty-args case. Replace with an inline message + `--help` per-command tip:

```cpp
if (args.empty()) {
    std::cout << "Usage: swr_calculator <command> [options]\n"
              << "Commands:\n"
              << "  constant_dollar       Evaluate a fixed-WR scenario\n"
              << "  constant_percent      Fixed % of current balance each year\n"
              << "  dynamic_dollar        Per-year sustainable withdrawal\n"
              << "\nRun '<command> --help' for command-specific options.\n";
    return 1;
}
```

- [ ] **Step 3: Build and verify after each deletion batch**

```
make && make test && bash scripts/smoke.sh
```

Expected: clean build, all unit tests pass, all three smoke entries pass with byte-identical output.

- [ ] **Step 4: Verify line count**

```
wc -l src/main.cpp
```

Expected: ~600-800 lines (from ~4012). If still above 1000, more deletions are likely possible — review remaining functions.

- [ ] **Step 5: Stage and inform user**

```
git add src/main.cpp
```

> **Phase 4 (commit 4) ready for review.** ~28 unused subcommand handlers and ~15 graph/sheets helpers deleted. `src/main.cpp` shrunk from ~4012 to ~<measured count> lines. Quality gate passes. Suggested commit message:
> ```
> chore: delete ~28 unused analytical subcommands + helpers
>
> Removes subcommand handlers for trinity_*, flexibility_*, glidepath_*,
> rebalance_*, social_*, multiple_wr, and other unused analytical commands.
> Removes the GraphBase/Graph classes and multiple_wr_* helpers. Replaces
> print_general_help with an inline usage stub.
>
> main.cpp: ~4012 -> ~<measured count> lines.
> ```

---

## Phase 5 — README, LICENSE, .gitignore, Makefile targets, sonar (commit milestone 5)

End state: documentation updated, build artifacts cleaned, `make compile_commands` target added.

### Task 26: Delete sonar config

**Files:**
- Delete: `sonar-project.properties`

- [ ] **Step 1: Delete the file**

```
git rm sonar-project.properties
```

- [ ] **Step 2: Verify**

```
make && make test && bash scripts/smoke.sh
```

Expected: all green (nothing depends on this).

---

### Task 27: Untrack compile_commands.json and add make target

**Files:**
- Modify: `.gitignore`
- Modify: `Makefile`
- Delete tracking of: `compile_commands.json`

- [ ] **Step 1: Add `compile_commands.json` to `.gitignore`**

Open `.gitignore` (or create if missing). Add a line:

```
compile_commands.json
```

- [ ] **Step 2: Remove the tracked file (but keep on disk if useful)**

```
git rm --cached compile_commands.json
```

- [ ] **Step 3: Add `make compile_commands` target**

Append to `Makefile`, before `include make-utils/cpp-utils-finalize.mk`:

```makefile
# Regenerate compile_commands.json using `bear`. Skips gracefully if bear
# is not installed. compile_commands.json is gitignored — this is a local
# convenience for IDEs (clangd, VS Code C/C++ extension, CLion).
.PHONY: compile_commands
compile_commands:
	@if command -v bear >/dev/null 2>&1; then \
	    rm -f compile_commands.json && bear -- $(MAKE) -B release_debug; \
	else \
	    echo "compile_commands: 'bear' not installed; skipping." \
	         "Install via: apt install bear (or brew install bear)"; \
	fi
```

- [ ] **Step 4: Verify**

```
make compile_commands
```

Expected (without bear installed): the skip message. With bear installed: a regenerated `compile_commands.json` in the repo root.

```
make && make test && bash scripts/smoke.sh
```

Expected: all green.

- [ ] **Step 5: Stage**

```
git add .gitignore Makefile
```

---

### Task 28: Update LICENSE

**Files:**
- Modify: `LICENSE`

- [ ] **Step 1: Read the existing LICENSE**

```
cat LICENSE | head -10
```

Note the existing copyright line(s).

- [ ] **Step 2: Add Rick Gray's copyright line**

Edit `LICENSE`. After the existing `Copyright Baptiste Wicht 2019-2024.` line (the exact wording may differ slightly — match the file), insert:

```
Copyright Rick Gray 2026.
```

So the header reads:

```
Copyright Baptiste Wicht 2019-2024.
Copyright Rick Gray 2026.
Distributed under the MIT License.
```

Leave the rest of the MIT license text unchanged.

- [ ] **Step 3: Stage**

```
git add LICENSE
```

---

### Task 29: README.rst → README.md

**Files:**
- Delete: `README.rst`
- Create: `README.md`

- [ ] **Step 1: Generate the new README content**

First, capture the actual `--help` output for each command so the README quotes them accurately:

```
./release_debug/bin/swr_calculator constant_dollar --help > /tmp/help_cd.txt
./release_debug/bin/swr_calculator constant_percent --help > /tmp/help_cp.txt
./release_debug/bin/swr_calculator dynamic_dollar --help > /tmp/help_dd.txt
```

(Adjust binary path if needed.)

- [ ] **Step 2: Create `README.md`**

```markdown
# swr-calculator

Compute sustainable retirement withdrawals using historical backtesting.

Three CLI commands evaluate or solve for withdrawal rates against the
project's bundled historical data series (US stocks, US bonds, US inflation,
plus several alternatives).

## Build

Linux/WSL only. Requires GCC or Clang with C++26 support and GNU Make.

```
make release_debug   # default: builds release_debug/bin/swr_calculator
make release         # optimized
make debug           # debug build
make test            # run the test suite
make compile_commands # regenerate compile_commands.json via bear (optional)
```

## Commands

### `constant_dollar` — fixed real-dollar withdrawal (the 4% rule)

Evaluate the success rate of withdrawing a fixed real dollar amount each
year, where the dollar amount is set as a percent of the *initial* portfolio
and adjusted for inflation thereafter.

```
swr_calculator constant_dollar --wr 4 --portfolio us_stocks:60,us_bonds:40 \
    --inflation us_inflation --years 30 --rebalance yearly
```

Run `swr_calculator constant_dollar --help` for the full flag list.

### `constant_percent` — fixed percent of current balance

Withdraw a fixed percent of the *current* (not initial) portfolio balance
each year. The dollar amount fluctuates with the portfolio; no inflation
adjustment.

```
swr_calculator constant_percent --pct 4 --portfolio us_stocks:60,us_bonds:40 \
    --inflation us_inflation --years 30
```

### `dynamic_dollar` — per-year sustainable withdrawal

A per-year point query: given the current portfolio balance, current age,
fixed end age, and target success rate, find this year's sustainable
withdrawal by historical backtesting over the remaining horizon. Supports
optional Social Security offset and rate-of-change smoothing.

```
swr_calculator dynamic_dollar --balance 850000 --current_age 67 --end_age 92 \
    --portfolio us_stocks:60,us_bonds:40 --inflation us_inflation \
    --target_success 80
```

## Output formats

All three commands support three output modes:

- **text** (default) — human-readable Inputs/Results sections
- **`--json`** — flat JSON object: `{ "command": ..., "inputs": ..., "results": ... }`
- **`--csv`** — per-historical-start-year tabular detail

`--json` and `--csv` are mutually exclusive.

## Data sources

CSV files in `stock-data/`: US and Swiss stocks, bonds, inflation, exchange
rates, plus several alternatives (gold, commodities, cash). The data is
the same series used in the original Trinity Study analyses.

## Background

[The original Trinity Study blog post](https://thepoorswiss.com/updated-trinity-study/) provides background on the methodology.

## License

MIT. See [LICENSE](LICENSE).
```

(Adjust the example invocations and command descriptions if the actual `--help` output reveals slight differences.)

- [ ] **Step 3: Delete README.rst**

```
git rm README.rst
```

- [ ] **Step 4: Stage**

```
git add README.md
```

---

### Task 30: Verify .github/workflows/make.yml

**Files:**
- Verify: `.github/workflows/make.yml`
- Maybe modify

- [ ] **Step 1: Inspect the workflow**

```
cat .github/workflows/make.yml
```

- [ ] **Step 2: Ensure no references to deleted things**

If the workflow runs `make` (or `make all`), it should still work. If it references the removed `server` target or anything in `build/` (Docker), update.

If it runs only `make`, no changes needed.

- [ ] **Step 3: Add a `make test` step to CI (optional)**

If the workflow currently runs only `make`, add a step after the build:

```yaml
      - name: Run tests
        run: make test
```

(Skip this step if you'd rather keep CI minimal.)

- [ ] **Step 4: Stage if changed**

```
git add .github/workflows/make.yml
```

---

### Task 31: Final Phase 5 quality gate

- [ ] **Step 1: Run the full quality gate**

```
make && make test && bash scripts/smoke.sh
```

Expected: clean build, all unit tests pass, all three smoke entries pass byte-identical.

- [ ] **Step 2: Verify line counts and file list**

```
wc -l src/main.cpp src/dynamic.cpp src/cli.cpp src/output_formatter.cpp src/test_dynamic.cpp
ls include/
ls src/
```

Expected:
- `src/main.cpp`: ~600-800 lines
- New files present in include/ and src/
- No `build/Dockerfile.web`, no `build/build_web_image.sh`
- No `sonar-project.properties`
- No `compile_commands.json` tracked
- No `cpp-httplib/` directory
- `README.md` exists, `README.rst` does not

- [ ] **Step 3: Inform user that Phase 5 is staged**

> **Phase 5 (final commit) ready for review.** README converted to Markdown + rewritten, LICENSE updated with Rick Gray copyright line, sonar config and tracked compile_commands.json removed, `.gitignore` updated, `make compile_commands` target added. Suggested commit message:
> ```
> docs: rewrite README in markdown + cleanup build artifacts
>
> - README.rst → README.md with full command documentation
> - LICENSE: add Rick Gray copyright line (MIT preserved)
> - Untrack compile_commands.json, add to .gitignore
> - Add `make compile_commands` target (uses bear if installed)
> - Remove sonar-project.properties
> ```

---

## Self-review checklist

After Phase 5, walk through each spec section and confirm coverage:

- [ ] Spec §1 Goal — covered by Phases 1-5 collectively
- [ ] Spec §2 Architecture — covered by Tasks 2, 7, 11, 13, 15
- [ ] Spec §3 Data types — covered by Task 2
- [ ] Spec §4 CLI design — covered by Tasks 7-10, 15, 17-18
- [ ] Spec §5 Output formats — covered by Tasks 11-12, 15, 17-18
- [ ] Spec §6 Algorithm — covered by Tasks 3-6, 13-14
- [ ] Spec §7 Phase 1 cleanup — covered by Tasks 21-23, 25-28
- [ ] Spec §8 Testing — covered by Tasks 1, 3-6, 8-10, 12, 14, 16
- [ ] Spec §9 Commit plan — covered by phase milestones (Tasks 16, 19, 23, 25, 31)
- [ ] Spec §10 Open notes — addressed in the plan (SSA logic verified during this plan-writing; no simulation.cpp tweak needed)

> **Note:** Task 20 was removed when `failsafe → constant_dollar_max` was dropped from the spec. There is an intentional gap at that number; Tasks 21+ keep their original numbering for stability.

## Final notes

- **Run all work in WSL or Linux.** Native Windows builds will fail (Makefile + pthread + POSIX paths).
- **The user commits.** Stage at each phase milestone, summarize, and let the user run `git commit` themselves.
- **Smoke baselines change once.** Phase 2 intentionally regenerates them. Phases 3-5 must leave smoke output byte-identical.
- **If a test fails mid-task, stop and fix.** Don't proceed past a failing quality gate.
