#include "rendercheck/checks.h"
#include "rendercheck/run_performance.h"

#include <cassert>
#include <filesystem>
#include <string>
#include <vector>

int main()
{
    namespace fs = std::filesystem;
    using rendercheck::MetricSummary;

    assert(rendercheck::run_performance_metric_is_gating("cpu_render_ms", true));
    assert(!rendercheck::run_performance_metric_is_gating("process_ms", true));
    assert(!rendercheck::run_performance_metric_is_gating("process_ms", false));

    const fs::path old_cwd = fs::current_path();
    const fs::path root = fs::temp_directory_path() / "renderercheck-run-performance-contract";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root);
    fs::current_path(root);

    MetricSummary cpu;
    cpu.name = "cpu_render_ms";
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

    bool saw_cpu = false;
    bool saw_process = false;
    for (const auto& comparison : missing.comparisons)
    {
        if (comparison.name == "cpu_render_ms")
        {
            saw_cpu = true;
            assert(comparison.gating);
            assert(comparison.baseline_missing);
        }
        else if (comparison.name == "process_ms")
        {
            saw_process = true;
            assert(!comparison.gating);
            assert(comparison.baseline_missing);
        }
    }
    assert(saw_cpu && saw_process);

    std::size_t approved = 0;
    assert(rendercheck::approve_run_performance("FinalScene", approved, error));
    assert(approved == 2);
    assert(fs::exists(rendercheck::run_performance_baseline_path()));

    const auto same = rendercheck::evaluate_run_performance("FinalScene", 1000.0, metrics, 15.0);
    assert(same.passed);
    assert(!same.baseline_missing);

    const auto missing_native_now = rendercheck::evaluate_run_performance("FinalScene", 1000.0, {}, 15.0);
    assert(!missing_native_now.passed);
    bool reported_missing_native = false;
    for (const auto& failure : missing_native_now.failures)
    {
        if (failure.find("cpu_render_ms") != std::string::npos
                && failure.find("metric missing") != std::string::npos)
        {
            reported_missing_native = true;
        }
    }
    assert(reported_missing_native);

    const auto slower_process = rendercheck::evaluate_run_performance("FinalScene", 1400.0, metrics, 15.0);
    assert(slower_process.passed);
    bool saw_diagnostic_process = false;
    for (const auto& comparison : slower_process.comparisons)
    {
        if (comparison.name == "process_ms")
        {
            assert(!comparison.gating);
            assert(!comparison.regressed);
            assert(comparison.median_delta_percent > 15.0);
            assert(comparison.p95_delta_percent > 15.0);
            saw_diagnostic_process = true;
        }
    }
    assert(saw_diagnostic_process);

    /* Large relative p95 movement below the tail-noise floor remains accepted
     * when the median is still inside its stricter floor. */
    MetricSummary scheduler_noise_cpu = cpu;
    scheduler_noise_cpu.median = 0.200;
    scheduler_noise_cpu.p95 = 0.450;
    const std::vector<MetricSummary> scheduler_noise_metrics = {scheduler_noise_cpu};
    const auto scheduler_noise = rendercheck::evaluate_run_performance(
            "FinalScene",
            1000.0,
            scheduler_noise_metrics,
            15.0
        );
    assert(scheduler_noise.passed);

    /* A p95-only change beyond 0.50 ms still gates even if the median does not. */
    MetricSummary regressed_cpu = cpu;
    regressed_cpu.median = 0.200;
    regressed_cpu.p95 = 0.600;
    const std::vector<MetricSummary> regressed_metrics = {regressed_cpu};
    const auto regressed = rendercheck::evaluate_run_performance("FinalScene", 1000.0, regressed_metrics, 15.0);
    assert(!regressed.passed);
    bool saw_regression = false;
    for (const auto& comparison : regressed.comparisons)
    {
        if (comparison.name == "cpu_render_ms")
        {
            saw_regression = comparison.regressed;
            assert(comparison.gating);
            assert(comparison.median_delta_percent > 15.0);
            assert(comparison.p95_delta_percent > 15.0);
        }
    }
    assert(saw_regression);

    const auto process_only = rendercheck::evaluate_run_performance("ProcessOnly", 1000.0, {}, 15.0);
    assert(process_only.passed);
    assert(!process_only.baseline_missing);
    assert(process_only.comparisons.size() == 1);
    assert(process_only.comparisons.front().name == "process_ms");
    assert(!process_only.comparisons.front().gating);
    assert(process_only.comparisons.front().baseline_missing);

    assert(rendercheck::save_run_performance_latest("FinalScene", 0.0, {}, false, error));
    approved = 0;
    assert(!rendercheck::approve_run_performance("FinalScene", approved, error));

    fs::current_path(old_cwd);
    fs::remove_all(root, ec);
    return 0;
}
