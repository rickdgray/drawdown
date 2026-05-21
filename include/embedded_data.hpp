//=======================================================================
// Copyright Rick Gray 2026.
// Distributed under the MIT License.
// (See accompanying file LICENSE or copy at
//  http://opensource.org/licenses/MIT)
//
// Historical CSV data embedded into the binary at compile time via #embed.
// Lookup by series name returns a span over the raw CSV bytes; the loader
// parses these the same way it parses on-disk CSV files.
//=======================================================================
#pragma once

#include <span>
#include <string_view>
#include <optional>
#include <cstddef>

namespace swr::embedded {

// Returns a span over the embedded CSV bytes for the named series, or
// std::nullopt if no embedded series matches that name. The bytes are
// exactly the on-disk CSV content (same format the file-based loader parses).
std::optional<std::span<const char>> find(std::string_view name);

} // namespace swr::embedded
