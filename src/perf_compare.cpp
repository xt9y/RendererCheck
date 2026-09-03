#include "rendercheck/perf_compare.h"

#include "rendercheck/checks.h"
#include "rendercheck/config.h"
#include "rendercheck/visual.h"

#include <cctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace rendercheck {
namespace {

enum class BvhVariant {
    Linear,
    Bvh,
};

struct ParsedBvhCase {
    std::size_t stress = 0;
    BvhVariant variant = BvhVariant::Linear;
};

struct BvhMeasurement {
    bool present = false;
    double direct_ms = 0.0;
    double lumen_trace_ms = 0.0;

    double ray_work_ms() const {
        return direct_ms + lumen_trace_ms;
    }
};

struct BvhPair {
    std::size_t stress = 0;
    BvhMeasurement linear;
    BvhMeasurement bvh;
};

std::optional<ParsedBvhCase> parse_bvh_case(std::string_view name) {
    if (!name.starts_with("BVH")) return std::nullopt;

    std::size_t pos = 3;
    if (pos >= name.size() || !std::isdigit(static_cast<unsigned char>(name[pos]))) {
        return std::nullopt;
    }

    std::size_t stress = 0;
    while (pos < name.size() && std::isdigit(static_cast<unsigned char>(name[pos]))) {
        stress = stress * 10u + static_cast<std::size_t>(name[pos] - '0');
        ++pos;
    }

    if (pos >= name.size() || name[pos] != '-') return std::nullopt;
    ++pos;

    const std::string_view suffix = name.substr(pos);
    if (suffix == "linear") return ParsedBvhCase{stress, BvhVariant::Linear};
    if (suffix == "bvh") return ParsedBvhCase{stress, BvhVariant::Bvh};
    return std::nullopt;
}

std::optional<double> metric_median(const std::vector<MetricSummary>& metrics,
                                    std::string_view name) {
    for (const auto& metric : metrics) {
        if (metric.name == name && metric.samples > 0) return metric.median;
    }
    return std::nullopt;
}

std::optional<BvhMeasurement> load_measurement(std::string_view case_name) {
    const fs::path metrics = fs::path(".rendercheck") / "performance" / "runs" /
        safe_test_name(case_name) / "metrics.txt";
    const std::vector<MetricSummary> summaries = summarize_metrics_file(metrics);
    const std::optional<double> direct = metric_median(summaries, "direct_ms");
    const std::optional<double> lumen = metric_median(summaries, "lumen_trace_ms");
    if (!direct || !lumen) return std::nullopt;

    BvhMeasurement measurement;
    measurement.present = true;
    measurement.direct_ms = *direct;
    measurement.lumen_trace_ms = *lumen;
    return measurement;
}

const char* winner_name(const BvhPair& pair) {
    return pair.bvh.ray_work_ms() < pair.linear.ray_work_ms() ? "bvh" : "linear";
}

std::optional<std::size_t> recommended_crossover(const std::vector<BvhPair>& pairs) {
    for (std::size_t i = 0; i < pairs.size(); ++i) {
        if (winner_name(pairs[i]) != std::string_view("bvh")) continue;

        bool remains_bvh = true;
        for (std::size_t j = i + 1; j < pairs.size(); ++j) {
            if (winner_name(pairs[j]) != std::string_view("bvh")) {
                remains_bvh = false;
                break;
            }
        }
        if (remains_bvh) return pairs[i].stress;
    }
    return std::nullopt;
}

void print_console(const std::vector<BvhPair>& pairs,
                   std::optional<std::size_t> crossover) {
    std::cout << "\nBVH crossover\n"
              << "  stress   linear-ray   bvh-ray   winner\n";
    for (const auto& pair : pairs) {
        std::cout << "  " << std::left << std::setw(8) << pair.stress
                  << std::right << std::fixed << std::setprecision(3)
                  << std::setw(8) << pair.linear.ray_work_ms() << " ms   "
                  << std::setw(7) << pair.bvh.ray_work_ms() << " ms   "
                  << winner_name(pair) << '\n';
    }
    if (crossover) std::cout << "Recommended crossover: " << *crossover << '\n';
    else std::cout << "Recommended crossover: none in measured range\n";
}

void append_markdown(const std::vector<BvhPair>& pairs,
                     std::optional<std::size_t> crossover) {
    std::ofstream out(fs::path(".rendercheck") / "performance" / "report.md", std::ios::app);
    if (!out) return;

    out << "\n## BVH crossover\n\n"
        << "| Stress primitives | Linear direct | BVH direct | Linear Lumen trace | BVH Lumen trace | Linear ray work | BVH ray work | Winner |\n"
        << "|---:|---:|---:|---:|---:|---:|---:|---|\n";
    for (const auto& pair : pairs) {
        out << "| " << pair.stress
            << " | " << std::fixed << std::setprecision(3) << pair.linear.direct_ms
            << " | " << pair.bvh.direct_ms
            << " | " << pair.linear.lumen_trace_ms
            << " | " << pair.bvh.lumen_trace_ms
            << " | " << pair.linear.ray_work_ms()
            << " | " << pair.bvh.ray_work_ms()
            << " | " << winner_name(pair) << " |\n";
    }
    if (crossover) out << "\nRecommended crossover: " << *crossover << "\n";
    else out << "\nRecommended crossover: none in measured range\n";
}

void write_machine_readable(const std::vector<BvhPair>& pairs,
                            std::optional<std::size_t> crossover) {
    std::ofstream out(fs::path(".rendercheck") / "performance" / "bvh-crossover.tsv",
                      std::ios::trunc);
    if (!out) return;

    out << "stress\tlinear_direct_ms\tbvh_direct_ms\tlinear_lumen_trace_ms\tbvh_lumen_trace_ms\tlinear_ray_work_ms\tbvh_ray_work_ms\twinner\n";
    for (const auto& pair : pairs) {
        out << pair.stress << '\t'
            << std::setprecision(17) << pair.linear.direct_ms << '\t'
            << pair.bvh.direct_ms << '\t'
            << pair.linear.lumen_trace_ms << '\t'
            << pair.bvh.lumen_trace_ms << '\t'
            << pair.linear.ray_work_ms() << '\t'
            << pair.bvh.ray_work_ms() << '\t'
            << winner_name(pair) << '\n';
    }
    if (crossover) out << "recommended_crossover\t" << *crossover << '\n';
    else out << "recommended_crossover\tnone\n";
}

} // namespace

void report_perf_comparisons() {
    Config config;
    std::string error;
    if (!load_config("rendercheck.toml", config, error)) return;

    std::map<std::size_t, BvhPair> grouped;
    for (const auto& perf_case : config.perf_cases) {
        if (!perf_case.enabled) continue;
        const auto parsed = parse_bvh_case(perf_case.name);
        if (!parsed) continue;
        const auto measurement = load_measurement(perf_case.name);
        if (!measurement) continue;

        BvhPair& pair = grouped[parsed->stress];
        pair.stress = parsed->stress;
        if (parsed->variant == BvhVariant::Linear) pair.linear = *measurement;
        else pair.bvh = *measurement;
    }

    std::vector<BvhPair> pairs;
    pairs.reserve(grouped.size());
    for (const auto& [stress, pair] : grouped) {
        (void)stress;
        if (pair.linear.present && pair.bvh.present) pairs.push_back(pair);
    }
    if (pairs.empty()) return;

    const auto crossover = recommended_crossover(pairs);
    print_console(pairs, crossover);
    append_markdown(pairs, crossover);
    write_machine_readable(pairs, crossover);
}

} // namespace rendercheck
