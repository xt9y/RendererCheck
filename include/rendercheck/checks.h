#pragma once

#include "rendercheck/config.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace rendercheck {

struct ValidationResult {
    std::size_t errors = 0;
    std::size_t warnings = 0;
    std::size_t vuids = 0;
    bool passed = true;
};

struct PerformanceResult {
    std::size_t gpu_samples = 0;
    double gpu_average_ms = 0.0;
    double gpu_max_ms = 0.0;
    bool gpu_missing = false;
    bool process_over_budget = false;
    bool gpu_over_budget = false;
    bool passed = true;
};

struct TestReport {
    std::string name;
    bool passed = false;
    double process_ms = 0.0;
    bool validation_checked = false;
    std::size_t validation_errors = 0;
    std::size_t validation_warnings = 0;
    std::size_t validation_vuids = 0;
    std::size_t gpu_samples = 0;
    double gpu_average_ms = 0.0;
    double gpu_max_ms = 0.0;
    bool visual_checked = false;
    double changed_percent = 0.0;
    std::string detail;
};

std::filesystem::path metrics_path(std::string_view name);
std::filesystem::path validation_path(std::string_view name);
PerformanceConfig performance_config(const Config& config, const TestConfig* test);

bool prepare_child_checks(std::string_view test_name,
                          const std::filesystem::path& output_dir,
                          const ValidationConfig& validation);

ValidationResult analyze_validation(std::string_view test_name, const ValidationConfig& config);
PerformanceResult analyze_performance(std::string_view test_name,
                                      double process_ms,
                                      const PerformanceConfig& config);
void write_reports(const std::vector<TestReport>& reports, std::size_t passed, std::size_t failed);

} // namespace rendercheck
