#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace rendercheck {

struct MetricSummary;

struct RunPerformanceComparison {
    std::string name;
    std::size_t samples = 0;
    double current_median = 0.0;
    double current_p95 = 0.0;
    bool baseline_missing = false;
    double baseline_median = 0.0;
    double baseline_p95 = 0.0;
    double median_delta_percent = 0.0;
    double p95_delta_percent = 0.0;
    bool regressed = false;
};

struct RunPerformanceResult {
    bool passed = true;
    bool baseline_missing = false;
    std::vector<RunPerformanceComparison> comparisons;
    std::vector<std::string> failures;
};

std::filesystem::path run_performance_baseline_path();

bool save_run_performance_latest(std::string_view test_name,
                                 double process_ms,
                                 const std::vector<MetricSummary>& metrics,
                                 bool valid_process,
                                 std::string& error);

RunPerformanceResult evaluate_run_performance(std::string_view test_name,
                                              double process_ms,
                                              const std::vector<MetricSummary>& metrics,
                                              double regression_percent);

bool approve_run_performance(std::string_view test_name,
                             std::size_t& approved_metrics,
                             std::string& error);

} // namespace rendercheck
