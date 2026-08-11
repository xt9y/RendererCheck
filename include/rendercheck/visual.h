#pragma once

#include "rendercheck/config.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace rendercheck {

struct VisualResult {
    std::filesystem::path actual_path;
    std::filesystem::path baseline_path;
    std::filesystem::path diff_path;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::size_t changed_pixels = 0;
    std::size_t total_pixels = 0;
    double changed_percent = 0.0;
    double rmse = 0.0;
    std::uint8_t max_channel_delta = 0;
    bool baseline_missing = false;
    bool passed = false;
};

std::string safe_test_name(std::string_view name);
std::filesystem::path capture_output_dir(std::string_view name);
std::filesystem::path capture_path(std::string_view name);
std::filesystem::path baseline_path(const Config& config, std::string_view name, const TestConfig* test);
const VisualConfig& visual_config(const Config& config, const TestConfig* test);

bool evaluate_capture(const Config& config,
                      std::string_view name,
                      const TestConfig* test,
                      VisualResult& result,
                      std::string& error);

int diff_captures(std::string_view filter);
int approve_captures(std::string_view filter);

} // namespace rendercheck
