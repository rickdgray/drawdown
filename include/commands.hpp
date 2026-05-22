//=======================================================================
// Copyright Rick Gray 2026.
// Distributed under the MIT License.
// (See accompanying file LICENSE or copy at
//  http://opensource.org/licenses/MIT)
//
// Public entry points for each CLI command. Each command's schema and
// implementation live in their own translation unit (src/cmd_<name>.cpp).
//=======================================================================
#pragma once

#include <string>
#include <vector>

namespace swr::cmd {

int dynamic_dollar(const std::vector<std::string>& args);
int dynamic_success(const std::vector<std::string>& args);
int constant_dollar(const std::vector<std::string>& args);
int constant_percent(const std::vector<std::string>& args);
int coast(const std::vector<std::string>& args);
int accumulate(const std::vector<std::string>& args);

} // namespace swr::cmd
