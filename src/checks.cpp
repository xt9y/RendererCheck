#include "rendercheck/checks.h"
#include "rendercheck/version.h"
#include "rendercheck/visual.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <string_view>

#if defined(__APPLE__) || defined(__linux__)
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace rendercheck {
namespace {

void append_csv_env(const char* name, const char* value) {
#if defined(__APPLE__) || defined(__linux__)
    const char* current = std::getenv(name);
    if (!current || !*current) { ::setenv(name, value, 1); return; }
    const std::string existing(current);
    std::stringstream stream(existing);
    std::string item;
    while (std::getline(stream, item, ',')) if (item == value) return;
    const std::string combined = existing + ',' + value;
    ::setenv(name, combined.c_str(), 1);
#else
    (void)name; (void)value;
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

bool metric_name_valid(std::string_view name) {
    if (name.empty()) return false;
    for (const unsigned char c : name) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
        if (!ok) return false;
    }
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
    for (const unsigned char c : value) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    std::ostringstream hex;
                    hex << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<unsigned>(c);
                    out += hex.str();
                } else out += static_cast<char>(c);
                break;
        }
    }
    return out;
}

std::string timing_label(const TestReport& report) {
    if (report.gpu_samples == 0) return "—";
    std::ostringstream out;
    out << std::fixed << std::setprecision(2)
        << (report.renderer_mode == "software" ? "software max " : "GPU max ")
        << report.gpu_max_ms << " ms (" << report.gpu_samples << ')';
    return out.str();
}

std::string validation_label(const TestReport& report) {
    if (!report.validation_checked) return "—";
    if (!report.validation_available) return "unavailable";
    if (report.validation_errors == 0 && report.validation_warnings == 0 && report.validation_vuids == 0) return "clean";
    std::ostringstream out;
    out << report.validation_errors << "E/" << report.validation_warnings << "W, " << report.validation_vuids << " VUID";
    return out.str();
}

std::string build_markdown_report(const std::vector<TestReport>& reports, std::size_t passed, std::size_t failed) {
    std::ostringstream out;
    out << "# RendererCheck report\n\n"
        << "RendererCheck " << RENDERCHECK_VERSION << " — **" << passed << " passed, " << failed << " failed**\n\n"
        << "| Test | Status | Process | Renderer | Timing | Validation | Visual |\n"
        << "|---|---:|---:|---:|---:|---:|---:|\n";
    for (const auto& report : reports) {
        out << "| " << markdown_escape(report.name)
            << " | " << (report.passed ? "PASS" : "FAIL")
            << " | " << std::fixed << std::setprecision(2) << report.process_ms << " ms";
        if (report.timed_out) out << " timeout";
        out << " | " << report.renderer_mode
            << " | " << timing_label(report)
            << " | " << validation_label(report);
        if (report.visual_checked) out << " | " << std::fixed << std::setprecision(3) << report.changed_percent << "% changed";
        else out << " | —";
        out << " |\n";
    }
    out << "\n";
    for (const auto& report : reports) {
        if (report.failures.empty()) continue;
        out << "## " << markdown_escape(report.name) << " failures\n\n";
        for (const auto& failure : report.failures) out << "- " << markdown_escape(failure) << '\n';
        if (!report.stdout_path.empty()) out << "- stdout: `" << report.stdout_path << "`\n";
        if (!report.stderr_path.empty()) out << "- stderr: `" << report.stderr_path << "`\n";
        if (!report.diff_png_path.empty()) out << "- diff preview: `" << report.diff_png_path << "`\n";
        out << '\n';
    }
    return out.str();
}

void write_json_string(std::ostream& out, std::string_view value) { out << '"' << json_escape(value) << '"'; }

void append_validation_layer_log(std::string_view test_name) {
    const fs::path layer_log = capture_output_dir(test_name) / "validation-layer.log";
    std::ifstream in(layer_log, std::ios::binary);
    if (!in) return;
    std::ofstream out(validation_path(test_name), std::ios::binary | std::ios::app);
    if (!out) return;
    out << in.rdbuf();
}

} // namespace

fs::path metrics_path(std::string_view name) { return capture_output_dir(name) / "metrics.txt"; }
fs::path validation_path(std::string_view name) { return capture_output_dir(name) / "validation.log"; }
fs::path stdout_path(std::string_view name) { return capture_output_dir(name) / "stdout.log"; }
fs::path stderr_path(std::string_view name) { return capture_output_dir(name) / "stderr.log"; }

PerformanceConfig performance_config(const Config& config, const TestConfig* test) {
    PerformanceConfig resolved = config.performance;
    if (!test) return resolved;
    if (test->has_max_gpu_ms) resolved.max_gpu_ms = test->performance.max_gpu_ms;
    if (test->has_max_process_ms) resolved.max_process_ms = test->performance.max_process_ms;
    return resolved;
}

double timeout_config(const Config& config, const TestConfig* test) {
    return test && test->has_timeout_ms ? test->timeout_ms : config.project.timeout_ms;
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

    // Validation layers may retain their own logging destination instead of writing
    // through the child's redirected stderr. Force the layer's LOG_MSG action into
    // a per-test file, then merge it into validation.log before analysis.
    const std::string layer_log = fs::absolute(output_dir / "validation-layer.log").string();
    ::setenv("VK_LAYER_DEBUG_ACTION", "VK_DBG_LAYER_ACTION_LOG_MSG", 1);
    ::setenv("VK_LAYER_REPORT_FLAGS", "error,warn", 1);
    ::setenv("VK_LAYER_LOG_FILENAME", layer_log.c_str(), 1);
    (void)test_name;
    return true;
#else
    (void)test_name; (void)output_dir; (void)validation;
    return false;
#endif
}

ValidationResult analyze_validation(std::string_view test_name, const ValidationConfig& config) {
    ValidationResult result;
    if (!config.vulkan) return result;
    append_validation_layer_log(test_name);
    std::ifstream in(validation_path(test_name));
    std::string line;
    while (std::getline(in, line)) {
        const std::string lower = lower_copy(line);
        if (lower.find("validation error") != std::string::npos ||
            lower.find("(error / spec)") != std::string::npos) ++result.errors;
        else if (lower.find("validation warning") != std::string::npos ||
                 lower.find("(warning / spec)") != std::string::npos) ++result.warnings;
        std::size_t pos = 0;
        while ((pos = line.find("VUID-", pos)) != std::string::npos) { ++result.vuids; pos += 5; }
    }
    result.passed = (!config.fail_on_error || result.errors == 0) && (!config.fail_on_warning || result.warnings == 0);
    return result;
}

PerformanceResult analyze_performance(std::string_view test_name,
                                      double process_ms,
                                      const PerformanceConfig& config,
                                      bool software_renderer) {
    PerformanceResult result;
    std::ifstream in(metrics_path(test_name));
    std::map<std::string, std::vector<double>> values;
    std::string line;
    while (std::getline(in, line)) {
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string name = line.substr(0, eq);
        if (!metric_name_valid(name)) continue;
        double value = 0.0;
        if (!parse_metric_value(std::string_view(line).substr(eq + 1), value)) continue;
        values[name].push_back(value);
    }
    for (const auto& [name, samples] : values) {
        MetricSummary summary;
        summary.name = name;
        summary.samples = samples.size();
        double sum = 0.0;
        for (const double value : samples) { sum += value; summary.maximum = std::max(summary.maximum, value); }
        if (!samples.empty()) summary.average = sum / static_cast<double>(samples.size());
        result.metrics.push_back(summary);
        if (name == "gpu_ms") {
            result.gpu_samples = summary.samples;
            result.gpu_average_ms = summary.average;
            result.gpu_max_ms = summary.maximum;
        }
    }
    if (config.max_process_ms > 0.0 && process_ms > config.max_process_ms) {
        result.process_over_budget = true;
        result.passed = false;
    }
    if (config.max_gpu_ms > 0.0 && !software_renderer) {
        if (result.gpu_samples == 0) { result.gpu_missing = true; result.passed = false; }
        else if (result.gpu_max_ms > config.max_gpu_ms) { result.gpu_over_budget = true; result.passed = false; }
    }
    return result;
}

void write_reports(const std::vector<TestReport>& reports, std::size_t passed, std::size_t failed) {
    std::error_code ec;
    fs::create_directories(".rendercheck", ec);
    if (ec) return;
    const std::string markdown = build_markdown_report(reports, passed, failed);
    { std::ofstream out(".rendercheck/report.md", std::ios::trunc); if (out) out << markdown; }
    {
        std::ofstream out(".rendercheck/results.json", std::ios::trunc);
        if (out) {
            out << "{\n  \"schema_version\": 1,\n  \"renderercheck_version\": ";
            write_json_string(out, RENDERCHECK_VERSION);
            out << ",\n  \"passed\": " << passed << ",\n  \"failed\": " << failed << ",\n  \"tests\": [\n";
            for (std::size_t i = 0; i < reports.size(); ++i) {
                const auto& r = reports[i];
                out << "    {\n      \"name\": "; write_json_string(out, r.name);
                out << ",\n      \"passed\": " << (r.passed ? "true" : "false")
                    << ",\n      \"exit_code\": " << r.exit_code
                    << ",\n      \"signal\": " << r.signal
                    << ",\n      \"timed_out\": " << (r.timed_out ? "true" : "false")
                    << ",\n      \"process_ms\": " << std::fixed << std::setprecision(3) << r.process_ms
                    << ",\n      \"renderer_mode\": "; write_json_string(out, r.renderer_mode);
                out << ",\n      \"headless_backend\": "; write_json_string(out, r.headless_backend);
                const std::string timing_kind = r.gpu_samples == 0 ? "none" : (r.renderer_mode == "software" ? "software_render" : "gpu");
                out << ",\n      \"timing_kind\": "; write_json_string(out, timing_kind);
                out << ",\n      \"gpu_samples\": " << r.gpu_samples
                    << ",\n      \"gpu_average_ms\": " << r.gpu_average_ms
                    << ",\n      \"gpu_max_ms\": " << r.gpu_max_ms
                    << ",\n      \"validation\": {\"checked\": " << (r.validation_checked ? "true" : "false")
                    << ", \"available\": " << (r.validation_available ? "true" : "false")
                    << ", \"errors\": " << r.validation_errors
                    << ", \"warnings\": " << r.validation_warnings
                    << ", \"vuids\": " << r.validation_vuids << "}"
                    << ",\n      \"visual\": {\"checked\": " << (r.visual_checked ? "true" : "false")
                    << ", \"width\": " << r.capture_width
                    << ", \"height\": " << r.capture_height
                    << ", \"changed_pixels\": " << r.changed_pixels
                    << ", \"total_pixels\": " << r.total_pixels
                    << ", \"changed_percent\": " << r.changed_percent
                    << ", \"rmse\": " << r.rmse
                    << ", \"max_channel_delta\": " << r.max_channel_delta << "}"
                    << ",\n      \"paths\": {\"actual\": "; write_json_string(out, r.actual_path);
                out << ", \"baseline\": "; write_json_string(out, r.baseline_path);
                out << ", \"diff\": "; write_json_string(out, r.diff_path);
                out << ", \"actual_png\": "; write_json_string(out, r.actual_png_path);
                out << ", \"baseline_png\": "; write_json_string(out, r.baseline_png_path);
                out << ", \"diff_png\": "; write_json_string(out, r.diff_png_path);
                out << ", \"stdout\": "; write_json_string(out, r.stdout_path);
                out << ", \"stderr\": "; write_json_string(out, r.stderr_path);
                out << "},\n      \"metrics\": [";
                for (std::size_t m = 0; m < r.metrics.size(); ++m) {
                    if (m) out << ',';
                    out << "{\"name\":"; write_json_string(out, r.metrics[m].name);
                    out << ",\"samples\":" << r.metrics[m].samples
                        << ",\"average\":" << r.metrics[m].average
                        << ",\"maximum\":" << r.metrics[m].maximum << '}';
                }
                out << "],\n      \"failures\": [";
                for (std::size_t f = 0; f < r.failures.size(); ++f) {
                    if (f) out << ',';
                    write_json_string(out, r.failures[f]);
                }
                out << "]\n    }";
                if (i + 1 != reports.size()) out << ',';
                out << '\n';
            }
            out << "  ]\n}\n";
        }
    }
    const char* summary = std::getenv("GITHUB_STEP_SUMMARY");
    if (summary && *summary) { std::ofstream out(summary, std::ios::app); if (out) out << '\n' << markdown; }
}

} // namespace rendercheck
