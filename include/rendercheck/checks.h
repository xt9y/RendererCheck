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

struct MetricSummary {
    std::string name;
    std::size_t samples = 0;
    double minimum = 0.0;
    double average = 0.0;
    double median = 0.0;
    double p95 = 0.0;
    double maximum = 0.0;
};

struct PerformanceResult {
    std::size_t gpu_samples = 0;
    double gpu_average_ms = 0.0;
    double gpu_max_ms = 0.0;
    bool gpu_missing = false;
    bool process_over_budget = false;
    bool gpu_over_budget = false;
    bool passed = true;
    std::vector<MetricSummary> metrics;
};

struct TestReport {
    std::string name;
    bool passed = false;
    int exit_code = 0;
    int signal = 0;
    bool timed_out = false;
    double process_ms = 0.0;
    std::string renderer_mode = "driver_default";
    std::string headless_backend = "none";

    bool validation_checked = false;
    bool validation_available = true;
    std::size_t validation_errors = 0;
    std::size_t validation_warnings = 0;
    std::size_t validation_vuids = 0;

    std::size_t gpu_samples = 0;
    double gpu_average_ms = 0.0;
    double gpu_max_ms = 0.0;
    std::vector<MetricSummary> metrics;

    bool visual_checked = false;
    std::uint32_t capture_width = 0;
    std::uint32_t capture_height = 0;
    std::size_t changed_pixels = 0;
    std::size_t total_pixels = 0;
    double changed_percent = 0.0;
    double rmse = 0.0;
    unsigned max_channel_delta = 0;

    std::string actual_path;
    std::string baseline_path;
    std::string diff_path;
    std::string actual_png_path;
    std::string baseline_png_path;
    std::string diff_png_path;
    std::string stdout_path;
    std::string stderr_path;
    std::vector<std::string> failures;
};

std::filesystem::path metrics_path(std::string_view name);
std::filesystem::path validation_path(std::string_view name);
std::filesystem::path stdout_path(std::string_view name);
std::filesystem::path stderr_path(std::string_view name);
PerformanceConfig performance_config(const Config& config, const TestConfig* test);
double timeout_config(const Config& config, const TestConfig* test);
std::vector<MetricSummary> summarize_metrics_file(const std::filesystem::path& path);

bool prepare_child_checks(std::string_view test_name,
                          const std::filesystem::path& output_dir,
                          const ValidationConfig& validation);

ValidationResult analyze_validation(std::string_view test_name, const ValidationConfig& config);
PerformanceResult analyze_performance(std::string_view test_name,
                                      double process_ms,
                                      const PerformanceConfig& config,
                                      bool software_renderer);
void write_reports(const std::vector<TestReport>& reports, std::size_t passed, std::size_t failed);

} // namespace rendercheck
