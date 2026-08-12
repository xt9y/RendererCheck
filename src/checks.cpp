#include "rendercheck/checks.h"
#include "rendercheck/visual.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>

#if defined(__APPLE__) || defined(__linux__)
#include <fcntl.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace rendercheck {
namespace {

void append_csv_env(const char* name, const char* value) {
#if defined(__APPLE__) || defined(__linux__)
    const char* current = std::getenv(name);
    if (!current || !*current) {
        ::setenv(name, value, 1);
        return;
    }
    const std::string existing(current);
    if (existing.find(value) != std::string::npos) return;
    const std::string combined = existing + ',' + value;
    ::setenv(name, combined.c_str(), 1);
#else
    (void)name;
    (void)value;
#endif
}

std::string lower_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        if (c >= 'A' && c <= 'Z') return static_cast<char>(c - 'A' + 'a');
        return static_cast<char>(c);
    });
    return value;
}

bool parse_metric_value(std::string_view text, double& value) {
    const std::string raw(text);
    char* end = nullptr;
    errno = 0;
    const double parsed = std::strtod(raw.c_str(), &end);
    if (errno != 0 || end != raw.c_str() + raw.size() || !std::isfinite(parsed) || parsed < 0.0) return false;
    value = parsed;
    return true;
}

std::string markdown_escape(std::string value) {
    std::string out;
    out.reserve(value.size());
    for (const char c : value) {
        if (c == '|') out += "\\|";
        else if (c == '\n' || c == '\r') out += ' ';
        else out += c;
    }
    return out;
}

std::string json_escape(std::string_view value) {
    std::string out;
    for (const char c : value) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out += c; break;
        }
    }
    return out;
}

std::string build_markdown_report(const std::vector<TestReport>& reports,
                                  std::size_t passed,
                                  std::size_t failed) {
    std::ostringstream out;
    out << "# RendererCheck report\n\n"
        << "**" << passed << " passed, " << failed << " failed**\n\n"
        << "| Test | Status | Process | GPU | Validation | Visual |\n"
        << "|---|---:|---:|---:|---:|---:|\n";

    for (const auto& report : reports) {
        out << "| " << markdown_escape(report.name)
            << " | " << (report.passed ? "PASS" : "FAIL")
            << " | " << std::fixed << std::setprecision(2) << report.process_ms << " ms";
        if (report.gpu_samples != 0) out << " | max " << report.gpu_max_ms << " ms (" << report.gpu_samples << ")";
        else out << " | —";
        if (!report.validation_checked) out << " | —";
        else if (report.validation_errors != 0 || report.validation_warnings != 0 || report.validation_vuids != 0) {
            out << " | " << report.validation_errors << "E/" << report.validation_warnings
                << "W, " << report.validation_vuids << " VUID";
        } else out << " | clean";
        if (report.visual_checked) out << " | " << report.changed_percent << "% changed";
        else out << " | —";
        out << " |\n";
    }
    return out.str();
}

} // namespace

fs::path metrics_path(std::string_view name) {
    return capture_output_dir(name) / "metrics.txt";
}

fs::path validation_path(std::string_view name) {
    return capture_output_dir(name) / "validation.log";
}

PerformanceConfig performance_config(const Config& config, const TestConfig* test) {
    PerformanceConfig resolved = config.performance;
    if (!test) return resolved;
    if (test->has_max_gpu_ms) resolved.max_gpu_ms = test->performance.max_gpu_ms;
    if (test->has_max_process_ms) resolved.max_process_ms = test->performance.max_process_ms;
    return resolved;
}

bool prepare_child_checks(std::string_view test_name,
                          const fs::path& output_dir,
                          const ValidationConfig& validation) {
#if defined(__APPLE__) || defined(__linux__)
    const std::string metrics = fs::absolute(output_dir / "metrics.txt").string();
    ::setenv("RENDERCHECK_METRICS_PATH", metrics.c_str(), 1);

    if (!validation.vulkan) {
        ::unsetenv("RENDERCHECK_VULKAN_VALIDATION");
        return true;
    }

    ::setenv("RENDERCHECK_VULKAN_VALIDATION", "1", 1);
    append_csv_env("VK_LOADER_LAYERS_ENABLE", "VK_LAYER_KHRONOS_validation");
    if (!std::getenv("VK_INSTANCE_LAYERS")) ::setenv("VK_INSTANCE_LAYERS", "VK_LAYER_KHRONOS_validation", 1);
    if (!std::getenv("VK_LOADER_DEBUG")) ::setenv("VK_LOADER_DEBUG", "error,warn", 1);

    const std::string log = fs::absolute(validation_path(test_name)).string();
    const int fd = ::open(log.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return false;
    const bool ok = ::dup2(fd, STDERR_FILENO) >= 0;
    ::close(fd);
    return ok;
#else
    (void)test_name;
    (void)output_dir;
    (void)validation;
    return false;
#endif
}

ValidationResult analyze_validation(std::string_view test_name, const ValidationConfig& config) {
    ValidationResult result;
    if (!config.vulkan) return result;

    std::ifstream in(validation_path(test_name));
    std::string line;
    while (std::getline(in, line)) {
        std::cerr << line << '\n';
        const std::string lower = lower_copy(line);
        if (lower.find("validation error") != std::string::npos) ++result.errors;
        else if (lower.find("validation warning") != std::string::npos) ++result.warnings;
        std::size_t pos = 0;
        while ((pos = line.find("VUID-", pos)) != std::string::npos) {
            ++result.vuids;
            pos += 5;
        }
    }
    result.passed = (!config.fail_on_error || result.errors == 0) &&
                    (!config.fail_on_warning || result.warnings == 0);
    return result;
}

PerformanceResult analyze_performance(std::string_view test_name,
                                      double process_ms,
                                      const PerformanceConfig& config) {
    PerformanceResult result;
    std::ifstream in(metrics_path(test_name));
    std::string line;
    double sum = 0.0;
    while (std::getline(in, line)) {
        constexpr std::string_view prefix = "gpu_ms=";
        if (line.rfind(prefix, 0) != 0) continue;
        double value = 0.0;
        if (!parse_metric_value(std::string_view(line).substr(prefix.size()), value)) continue;
        ++result.gpu_samples;
        sum += value;
        result.gpu_max_ms = std::max(result.gpu_max_ms, value);
    }
    if (result.gpu_samples != 0) result.gpu_average_ms = sum / static_cast<double>(result.gpu_samples);
    if (config.max_process_ms > 0.0 && process_ms > config.max_process_ms) {
        result.process_over_budget = true;
        result.passed = false;
    }
    if (config.max_gpu_ms > 0.0) {
        if (result.gpu_samples == 0) {
            result.gpu_missing = true;
            result.passed = false;
        } else if (result.gpu_max_ms > config.max_gpu_ms) {
            result.gpu_over_budget = true;
            result.passed = false;
        }
    }
    return result;
}

void write_reports(const std::vector<TestReport>& reports, std::size_t passed, std::size_t failed) {
    std::error_code ec;
    fs::create_directories(".rendercheck", ec);
    if (ec) return;

    const std::string markdown = build_markdown_report(reports, passed, failed);
    { std::ofstream out(".rendercheck/report.md"); if (out) out << markdown; }
    {
        std::ofstream out(".rendercheck/results.json");
        if (out) {
            out << "{\n  \"passed\": " << passed << ",\n  \"failed\": " << failed << ",\n  \"tests\": [\n";
            for (std::size_t i = 0; i < reports.size(); ++i) {
                const auto& r = reports[i];
                out << "    {\"name\": \"" << json_escape(r.name)
                    << "\", \"passed\": " << (r.passed ? "true" : "false")
                    << ", \"process_ms\": " << std::fixed << std::setprecision(3) << r.process_ms
                    << ", \"gpu_samples\": " << r.gpu_samples
                    << ", \"gpu_average_ms\": " << r.gpu_average_ms
                    << ", \"gpu_max_ms\": " << r.gpu_max_ms
                    << ", \"validation_checked\": " << (r.validation_checked ? "true" : "false")
                    << ", \"validation_errors\": " << r.validation_errors
                    << ", \"validation_warnings\": " << r.validation_warnings
                    << ", \"validation_vuids\": " << r.validation_vuids
                    << ", \"visual_checked\": " << (r.visual_checked ? "true" : "false")
                    << ", \"changed_percent\": " << r.changed_percent
                    << ", \"detail\": \"" << json_escape(r.detail) << "\"}";
                if (i + 1 != reports.size()) out << ',';
                out << '\n';
            }
            out << "  ]\n}\n";
        }
    }

    const char* summary = std::getenv("GITHUB_STEP_SUMMARY");
    if (summary && *summary) {
        std::ofstream out(summary, std::ios::app);
        if (out) out << '\n' << markdown;
    }
}

} // namespace rendercheck
