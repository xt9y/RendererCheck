#pragma once

#include <string_view>

namespace rendercheck {

int run_perf(std::string_view filter, bool approve);

} // namespace rendercheck
