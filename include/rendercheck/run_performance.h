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
    bool gating = true;
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

inline bool run_performance_metric_is_gating(std::string_view name,
                                             bool has_native_timing)
{
    (void)has_native_timing;
    return name != "process_ms";
}

inline constexpr double RUN_PERFORMANCE_ABSOLUTE_FLOOR_MS = 0.25;

/* Run-performance captures contain several sub-millisecond bookkeeping
 * metrics. A large percentage swing there can be only a few microseconds and
 * should not fail a rendering run. A regression must exceed both the normal
 * relative threshold and the absolute timing floor. */
inline bool run_performance_exceeds_regression_floor(
        double current_ms,
        double baseline_ms,
        double regression_percent,
        double absolute_floor_ms = RUN_PERFORMANCE_ABSOLUTE_FLOOR_MS)
{
    if (current_ms <= baseline_ms + absolute_floor_ms)
    {
        return false;
    }

    if (baseline_ms <= 0.0)
    {
        return current_ms > absolute_floor_ms;
    }

    return current_ms > baseline_ms * (1.0 + regression_percent / 100.0);
}

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
