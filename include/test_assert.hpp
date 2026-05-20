//=======================================================================
// Copyright Rick Gray 2026.
// Distributed under the MIT License.
// (See accompanying file LICENSE or copy at
//  http://opensource.org/licenses/MIT)
//
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
