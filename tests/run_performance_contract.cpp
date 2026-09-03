#include "rendercheck/run_performance.h"

#include <cassert>
#include <filesystem>
#include <string>
#include <vector>

int main()
{
    namespace fs = std::filesystem;
    using rendercheck::MetricSummary;

    const fs::path old_cwd = fs::current_path();
    const fs::path root = fs::temp_directory_path() / "renderercheck-run-performance-contract";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root);
    fs::current_path(root);

    MetricSummary cpu;
    cpu.name = "cpu_frame_ms";
    cpu.samples = 100;
    cpu.median = 0.020;
    cpu.p95 = 0.030;

    MetricSummary non_timing;
    non_timing.name = "draw_calls";
    non_timing.samples = 100;
    non_timing.median = 7.0;
    non_timing.p95 = 7.0;

    std::string error;
    const std::vector<MetricSummary> metrics = {cpu, non_timing};
    assert(rendercheck::save_run_performance_latest("FinalScene", 1000.0, metrics, true, error));

    const auto missing = rendercheck::evaluate_run_performance("FinalScene", 1000.0, metrics, 15.0);
    assert(!missing.passed);
    assert(missing.baseline_missing);
    assert(missing.comparisons.size() == 2);
    assert(missing.comparisons[0].name == "cpu_frame_ms" || missing.comparisons[1].name == "cpu_frame_ms");
    assert(missing.comparisons[0].name == "process_ms" || missing.comparisons[1].name == "process_ms");

    std::size_t approved = 0;
    assert(rendercheck::approve_run_performance("FinalScene", approved, error));
    assert(approved == 2);
    assert(fs::exists(rendercheck::run_performance_baseline_path()));

    const auto same = rendercheck::evaluate_run_performance("FinalScene", 1000.0, metrics, 15.0);
    assert(same.passed);
    assert(!same.baseline_missing);

    MetricSummary regressed_cpu = cpu;
    regressed_cpu.median = 0.025;
    regressed_cpu.p95 = 0.040;
    const std::vector<MetricSummary> regressed_metrics = {regressed_cpu};
    const auto regressed = rendercheck::evaluate_run_performance("FinalScene", 1000.0, regressed_metrics, 15.0);
    assert(!regressed.passed);
    bool saw_regression = false;
    for (const auto& comparison : regressed.comparisons)
    {
        if (comparison.name == "cpu_frame_ms")
        {
            saw_regression = comparison.regressed;
            assert(comparison.median_delta_percent > 15.0);
            assert(comparison.p95_delta_percent > 15.0);
        }
    }
    assert(saw_regression);

    assert(rendercheck::save_run_performance_latest("FinalScene", 0.0, {}, false, error));
    approved = 0;
    assert(!rendercheck::approve_run_performance("FinalScene", approved, error));

    fs::current_path(old_cwd);
    fs::remove_all(root, ec);
    return 0;
}
