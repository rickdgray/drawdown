//=======================================================================
// Copyright Rick Gray 2026.
// Distributed under the MIT License.
// (See accompanying file LICENSE or copy at
//  http://opensource.org/licenses/MIT)
//
// Historical CSV data embedded into the binary via C++26 #embed.
//=======================================================================
#include "embedded_data.hpp"

#include <array>
#include <string_view>

namespace swr::embedded {

namespace {

constexpr char us_stocks_csv[] = {
    #embed "../stock-data/us_stocks.csv"
};
constexpr char us_bonds_csv[] = {
    #embed "../stock-data/us_bonds.csv"
};
constexpr char us_inflation_csv[] = {
    #embed "../stock-data/us_inflation.csv"
};
constexpr char ex_us_stocks_csv[] = {
    #embed "../stock-data/ex_us_stocks.csv"
};
constexpr char gold_csv[] = {
    #embed "../stock-data/gold.csv"
};
constexpr char commodities_csv[] = {
    #embed "../stock-data/commodities.csv"
};
constexpr char cash_csv[] = {
    #embed "../stock-data/cash.csv"
};

struct series_entry {
    std::string_view name;
    std::span<const char> bytes;
};

const std::array<series_entry, 7> series_table = {{
    {"us_stocks",     {us_stocks_csv,     sizeof(us_stocks_csv)}},
    {"us_bonds",      {us_bonds_csv,      sizeof(us_bonds_csv)}},
    {"us_inflation",  {us_inflation_csv,  sizeof(us_inflation_csv)}},
    {"ex_us_stocks",  {ex_us_stocks_csv,  sizeof(ex_us_stocks_csv)}},
    {"gold",          {gold_csv,          sizeof(gold_csv)}},
    {"commodities",   {commodities_csv,   sizeof(commodities_csv)}},
    {"cash",          {cash_csv,          sizeof(cash_csv)}},
}};

} // namespace

std::optional<std::span<const char>> find(std::string_view name) {
    for (auto& e : series_table) {
        if (e.name == name) return e.bytes;
    }
    return std::nullopt;
}

} // namespace swr::embedded
