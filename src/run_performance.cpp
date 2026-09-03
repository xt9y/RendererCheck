#include "rendercheck/run_performance.h"

#include "rendercheck/checks.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <utility>

namespace fs = std::filesystem;

namespace rendercheck {
namespace {

struct Value {
    std::size_t samples = 0;
    double median = 0.0;
    double p95 = 0.0;
};

using Key = std::pair<std::string, std::string>;
using ValueMap = std::map<Key, Value>;

fs::path root_path()
{
    return fs::path(".rendercheck") / "run-performance";
}

fs::path latest_path()
{
    return root_path() / "latest.tsv";
}

bool timing_metric(std::string_view name)
{
    return name.size() >= 3u && name.ends_with("_ms");
}

ValueMap load_values(const fs::path& path, bool with_samples)
{
    ValueMap values;
    std::ifstream in(path);
    std::string line;

    while (std::getline(in, line))
    {
        if (line.empty() || line.front() == '#')
        {
            continue;
        }

        std::istringstream row(line);
        std::string test_name;
        std::string metric_name;
        Value value;

        bool parsed = false;
        if (with_samples)
        {
            parsed = static_cast<bool>(
                    row >> std::quoted(test_name)
                        >> std::quoted(metric_name)
                        >> value.samples
                        >> value.median
                        >> value.p95
                );
        }
        else
        {
            parsed = static_cast<bool>(
                    row >> std::quoted(test_name)
                        >> std::quoted(metric_name)
                        >> value.median
                        >> value.p95
                );
        }

        if (!parsed
                || !std::isfinite(value.median)
                || !std::isfinite(value.p95)
                || value.median < 0.0
                || value.p95 < 0.0)
        {
            continue;
        }

        values[{test_name, metric_name}] = value;
    }

    return values;
}

bool write_values(const fs::path& path,
                  const ValueMap& values,
                  bool with_samples,
                  std::string& error)
{
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    if (ec)
    {
        error = "could not create " + path.parent_path().string();
        return false;
    }

    std::ofstream out(path, std::ios::trunc);
    if (!out)
    {
        error = "could not write " + path.string();
        return false;
    }

    out << (with_samples
            ? "# RendererCheck run performance latest v1\n"
            : "# RendererCheck run performance baseline v1\n");
    out << std::setprecision(17);

    for (const auto& [key, value] : values)
    {
        out << std::quoted(key.first) << '\t'
            << std::quoted(key.second) << '\t';

        if (with_samples)
        {
            out << value.samples << '\t';
        }

        out << value.median << '\t'
            << value.p95 << '\n';
    }

    if (!out)
    {
        error = "could not finish writing " + path.string();
        return false;
    }

    return true;
}

void erase_test(ValueMap& values, std::string_view test_name)
{
    for (auto it = values.begin(); it != values.end();)
    {
        if (it->first.first == test_name)
        {
            it = values.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

ValueMap current_values(std::string_view test_name,
                        double process_ms,
                        const std::vector<MetricSummary>& metrics)
{
    ValueMap values;
    const std::string test(test_name);

    if (std::isfinite(process_ms) && process_ms >= 0.0)
    {
        values[{test, "process_ms"}] = {1u, process_ms, process_ms};
    }

    for (const auto& metric : metrics)
    {
        if (!timing_metric(metric.name)
                || metric.name == "process_ms"
                || metric.samples == 0u)
        {
            continue;
        }

        values[{test, metric.name}] = {
            metric.samples,
            metric.median,
            metric.p95
        };
    }

    return values;
}

bool has_native_timing(const ValueMap& values,
                       std::string_view test_name)
{
    for (const auto& [key, value] : values)
    {
        (void)value;
        if (key.first == test_name && key.second != "process_ms")
        {
            return true;
        }
    }

    return false;
}

double delta_percent(double current, double baseline)
{
    if (baseline > 0.0)
    {
        return (current / baseline - 1.0) * 100.0;
    }

    return current <= 0.0 ? 0.0 : 1000000.0;
}

} // namespace

fs::path run_performance_baseline_path()
{
    return root_path() / "baseline.tsv";
}

bool save_run_performance_latest(std::string_view test_name,
                                 double process_ms,
                                 const std::vector<MetricSummary>& metrics,
                                 bool valid_process,
                                 std::string& error)
{
    ValueMap latest = load_values(latest_path(), true);
    erase_test(latest, test_name);

    if (valid_process)
    {
        const ValueMap current = current_values(test_name, process_ms, metrics);
        latest.insert(current.begin(), current.end());
    }

    return write_values(latest_path(), latest, true, error);
}

RunPerformanceResult evaluate_run_performance(std::string_view test_name,
                                              double process_ms,
                                              const std::vector<MetricSummary>& metrics,
                                              double regression_percent)
{
    RunPerformanceResult result;
    const ValueMap current = current_values(test_name, process_ms, metrics);
    const ValueMap baselines = load_values(run_performance_baseline_path(), false);
    const double factor = 1.0 + regression_percent / 100.0;
    const bool current_native_timing = has_native_timing(current, test_name);
    const bool baseline_native_timing = has_native_timing(baselines, test_name);
    const bool native_timing_expected = current_native_timing || baseline_native_timing;

    for (const auto& [key, value] : current)
    {
        RunPerformanceComparison comparison;
        comparison.name = key.second;
        comparison.samples = value.samples;
        comparison.current_median = value.median;
        comparison.current_p95 = value.p95;
        comparison.gating = run_performance_metric_is_gating(
                comparison.name,
                native_timing_expected
            );

        const auto baseline_it = baselines.find(key);
        if (baseline_it == baselines.end())
        {
            comparison.baseline_missing = true;
            if (comparison.gating)
            {
                result.baseline_missing = true;
                result.passed = false;
                result.failures.push_back(
                        comparison.name + " run-performance baseline missing"
                    );
            }
            result.comparisons.push_back(comparison);
            continue;
        }

        const Value& baseline = baseline_it->second;
        comparison.baseline_median = baseline.median;
        comparison.baseline_p95 = baseline.p95;
        comparison.median_delta_percent = delta_percent(value.median, baseline.median);
        comparison.p95_delta_percent = delta_percent(value.p95, baseline.p95);
        const bool raw_regression =
            value.median > baseline.median * factor
            || value.p95 > baseline.p95 * factor;
        comparison.regressed = comparison.gating && raw_regression;

        if (comparison.regressed)
        {
            result.passed = false;
            result.failures.push_back(
                    comparison.name + " run-performance regression"
                );
        }

        result.comparisons.push_back(comparison);
    }

    if (baseline_native_timing)
    {
        for (const auto& [key, value] : baselines)
        {
            (void)value;
            if (key.first != test_name || key.second == "process_ms")
            {
                continue;
            }

            if (current.find(key) == current.end())
            {
                result.passed = false;
                result.failures.push_back(
                        key.second + " run-performance metric missing"
                    );
            }
        }
    }

    return result;
}

bool approve_run_performance(std::string_view test_name,
                             std::size_t& approved_metrics,
                             std::string& error)
{
    approved_metrics = 0u;
    const ValueMap latest = load_values(latest_path(), true);
    ValueMap baselines = load_values(run_performance_baseline_path(), false);
    erase_test(baselines, test_name);

    for (const auto& [key, value] : latest)
    {
        if (key.first != test_name)
        {
            continue;
        }

        baselines[key] = {0u, value.median, value.p95};
        ++approved_metrics;
    }

    if (approved_metrics == 0u)
    {
        error = "no valid run-performance metrics available for " + std::string(test_name);
        return false;
    }

    return write_values(run_performance_baseline_path(), baselines, false, error);
}

} // namespace rendercheck
