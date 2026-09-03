#include "rendercheck/config.h"

#include <cerrno>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_set>

namespace rendercheck {
namespace {

std::string trim(std::string_view value) {
    std::size_t first = 0;
    while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first]))) ++first;
    std::size_t last = value.size();
    while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1]))) --last;
    return std::string(value.substr(first, last - first));
}

std::string strip_comment(std::string_view line) {
    bool quoted = false;
    bool escaped = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (escaped) { escaped = false; continue; }
        if (c == '\\' && quoted) { escaped = true; continue; }
        if (c == '"') { quoted = !quoted; continue; }
        if (c == '#' && !quoted) return std::string(line.substr(0, i));
    }
    return std::string(line);
}

bool parse_string(std::string_view value, std::string& out) {
    const std::string v = trim(value);
    if (v.size() < 2 || v.front() != '"' || v.back() != '"') return false;
    out.clear();
    out.reserve(v.size() - 2);
    bool escaped = false;
    for (std::size_t i = 1; i + 1 < v.size(); ++i) {
        const char c = v[i];
        if (escaped) {
            switch (c) {
                case 'n': out.push_back('\n'); break;
                case 't': out.push_back('\t'); break;
                case 'r': out.push_back('\r'); break;
                case '\\': out.push_back('\\'); break;
                case '"': out.push_back('"'); break;
                default: return false;
            }
            escaped = false;
        } else if (c == '\\') {
            escaped = true;
        } else if (c == '"') {
            return false;
        } else {
            out.push_back(c);
        }
    }
    return !escaped;
}

bool parse_bool(std::string_view value, bool& out) {
    const std::string v = trim(value);
    if (v == "true") { out = true; return true; }
    if (v == "false") { out = false; return true; }
    return false;
}

bool parse_uint(std::string_view value, std::uint32_t& out) {
    const std::string v = trim(value);
    if (v.empty() || v.front() == '-') return false;
    char* end = nullptr;
    errno = 0;
    const unsigned long parsed = std::strtoul(v.c_str(), &end, 10);
    if (errno != 0 || end != v.c_str() + v.size() || parsed > std::numeric_limits<std::uint32_t>::max()) return false;
    out = static_cast<std::uint32_t>(parsed);
    return true;
}

bool parse_double(std::string_view value, double& out) {
    const std::string v = trim(value);
    if (v.empty()) return false;
    char* end = nullptr;
    errno = 0;
    const double parsed = std::strtod(v.c_str(), &end);
    if (errno != 0 || end != v.c_str() + v.size() || !std::isfinite(parsed)) return false;
    out = parsed;
    return true;
}

bool parse_nonnegative_double(const std::string& key,
                              std::string_view value,
                              double& out,
                              std::string& error,
                              const std::filesystem::path& path,
                              std::size_t line_number) {
    if (!parse_double(value, out) || out < 0.0) {
        error = path.string() + ':' + std::to_string(line_number) + ": expected non-negative number for " + key;
        return false;
    }
    return true;
}

bool parse_visual_key(const std::string& key,
                      std::string_view value,
                      VisualConfig& visual,
                      std::string& error,
                      const std::filesystem::path& path,
                      std::size_t line_number) {
    auto fail = [&](std::string_view expectation) {
        error = path.string() + ':' + std::to_string(line_number) + ": expected " +
                std::string(expectation) + " for " + key;
        return false;
    };
    if (key == "capture") {
        if (!parse_bool(value, visual.capture)) return fail("true or false");
        return true;
    }
    if (key == "baseline") {
        std::string baseline;
        if (!parse_string(value, baseline)) return fail("quoted string");
        visual.baseline = baseline;
        return true;
    }
    if (key == "pixel_threshold") {
        if (!parse_uint(value, visual.pixel_threshold) || visual.pixel_threshold > 255) return fail("integer from 0 to 255");
        return true;
    }
    if (key == "max_changed_percent") {
        if (!parse_double(value, visual.max_changed_percent) || visual.max_changed_percent < 0.0 || visual.max_changed_percent > 100.0)
            return fail("number from 0 to 100");
        return true;
    }
    if (key == "warmup_frames") {
        if (!parse_uint(value, visual.warmup_frames)) return fail("non-negative integer");
        return true;
    }
    return false;
}

bool is_visual_key(const std::string& key) {
    return key == "capture" || key == "baseline" || key == "pixel_threshold" ||
           key == "max_changed_percent" || key == "warmup_frames";
}

bool parse_headless(std::string_view value, HeadlessMode& mode) {
    std::string v;
    if (!parse_string(value, v)) return false;
    if (v == "auto") mode = HeadlessMode::Auto;
    else if (v == "xvfb") mode = HeadlessMode::Xvfb;
    else if (v == "none") mode = HeadlessMode::None;
    else return false;
    return true;
}

bool parse_renderer(std::string_view value, RendererMode& mode) {
    std::string v;
    if (!parse_string(value, v)) return false;
    if (v == "auto") mode = RendererMode::Auto;
    else if (v == "hardware") mode = RendererMode::Hardware;
    else if (v == "software") mode = RendererMode::Software;
    else return false;
    return true;
}

bool parse_min_samples(const std::string& key,
                       std::string_view value,
                       std::uint32_t& out,
                       std::string& error,
                       const std::filesystem::path& path,
                       std::size_t line_number) {
    if (!parse_uint(value, out) || out == 0) {
        error = path.string() + ':' + std::to_string(line_number) + ": expected positive integer for " + key;
        return false;
    }
    return true;
}

} // namespace

const char* headless_mode_name(HeadlessMode mode) {
    switch (mode) {
        case HeadlessMode::Auto: return "auto";
        case HeadlessMode::Xvfb: return "xvfb";
        case HeadlessMode::None: return "none";
    }
    return "auto";
}

const char* renderer_mode_name(RendererMode mode) {
    switch (mode) {
        case RendererMode::Auto: return "auto";
        case RendererMode::Hardware: return "hardware";
        case RendererMode::Software: return "software";
    }
    return "auto";
}

bool load_config(const std::filesystem::path& path, Config& config, std::string& error) {
    config = {};
    std::ifstream in(path);
    if (!in) { error = "could not open " + path.string(); return false; }

    enum class Section { None, Project, Validation, Performance, Test, Perf };
    Section section = Section::None;
    TestConfig* current_test = nullptr;
    PerfCaseConfig* current_perf = nullptr;
    std::unordered_set<std::string> current_keys;
    bool seen_project = false;
    bool seen_validation = false;
    bool seen_performance = false;

    std::string raw;
    std::size_t line_number = 0;
    while (std::getline(in, raw)) {
        ++line_number;
        const std::string line = trim(strip_comment(raw));
        if (line.empty()) continue;

        if (line.front() == '[') {
            current_keys.clear();
            current_test = nullptr;
            current_perf = nullptr;
            if (line == "[project]") {
                if (seen_project) { error = path.string() + ':' + std::to_string(line_number) + ": duplicate [project] section"; return false; }
                seen_project = true; section = Section::Project; continue;
            }
            if (line == "[validation]") {
                if (seen_validation) { error = path.string() + ':' + std::to_string(line_number) + ": duplicate [validation] section"; return false; }
                seen_validation = true; section = Section::Validation; continue;
            }
            if (line == "[performance]") {
                if (seen_performance) { error = path.string() + ':' + std::to_string(line_number) + ": duplicate [performance] section"; return false; }
                seen_performance = true; section = Section::Performance; continue;
            }
            if (line == "[[test]]") {
                config.tests.emplace_back();
                current_test = &config.tests.back();
                section = Section::Test;
                continue;
            }
            if (line == "[[perf]]") {
                config.perf_cases.emplace_back();
                current_perf = &config.perf_cases.back();
                section = Section::Perf;
                continue;
            }
            error = path.string() + ':' + std::to_string(line_number) + ": unknown section " + line;
            return false;
        }

        if (section == Section::None) {
            error = path.string() + ':' + std::to_string(line_number) + ": key outside a section";
            return false;
        }

        const std::size_t eq = line.find('=');
        if (eq == std::string::npos) {
            error = path.string() + ':' + std::to_string(line_number) + ": expected key = value";
            return false;
        }
        const std::string key = trim(std::string_view(line).substr(0, eq));
        const std::string_view value = std::string_view(line).substr(eq + 1);
        if (key.empty()) {
            error = path.string() + ':' + std::to_string(line_number) + ": empty key";
            return false;
        }
        if (!current_keys.insert(key).second) {
            error = path.string() + ':' + std::to_string(line_number) + ": duplicate key " + key;
            return false;
        }

        auto string_value = [&](std::string& destination) {
            if (!parse_string(value, destination)) {
                error = path.string() + ':' + std::to_string(line_number) + ": expected quoted string for " + key;
                return false;
            }
            return true;
        };

        bool recognized = true;
        if (section == Section::Project) {
            if (key == "name") { if (!string_value(config.project.name)) return false; }
            else if (key == "command") { if (!string_value(config.project.command)) return false; }
            else if (key == "cwd") { std::string v; if (!string_value(v)) return false; config.project.cwd = v; }
            else if (key == "baseline_dir") { std::string v; if (!string_value(v)) return false; config.project.baseline_dir = v; }
            else if (key == "headless") {
                if (!parse_headless(value, config.project.headless)) {
                    error = path.string() + ':' + std::to_string(line_number) + ": expected \"auto\", \"xvfb\", or \"none\" for headless";
                    return false;
                }
            } else if (key == "renderer") {
                if (!parse_renderer(value, config.project.renderer)) {
                    error = path.string() + ':' + std::to_string(line_number) + ": expected \"auto\", \"hardware\", or \"software\" for renderer";
                    return false;
                }
            } else if (key == "timeout_ms") {
                if (!parse_nonnegative_double(key, value, config.project.timeout_ms, error, path, line_number)) return false;
            } else if (is_visual_key(key)) {
                if (!parse_visual_key(key, value, config.project.visual, error, path, line_number)) return false;
            } else recognized = false;
        } else if (section == Section::Validation) {
            if (key == "vulkan") {
                if (!parse_bool(value, config.validation.vulkan)) {
                    error = path.string() + ':' + std::to_string(line_number) + ": expected true or false for vulkan"; return false;
                }
            } else if (key == "fail_on_warning") {
                if (!parse_bool(value, config.validation.fail_on_warning)) {
                    error = path.string() + ':' + std::to_string(line_number) + ": expected true or false for fail_on_warning"; return false;
                }
            } else if (key == "fail_on_error") {
                if (!parse_bool(value, config.validation.fail_on_error)) {
                    error = path.string() + ':' + std::to_string(line_number) + ": expected true or false for fail_on_error"; return false;
                }
            } else recognized = false;
        } else if (section == Section::Performance) {
            if (key == "max_gpu_ms") {
                if (!parse_nonnegative_double(key, value, config.performance.max_gpu_ms, error, path, line_number)) return false;
            } else if (key == "max_process_ms") {
                if (!parse_nonnegative_double(key, value, config.performance.max_process_ms, error, path, line_number)) return false;
            } else if (key == "warmup_ms") {
                if (!parse_nonnegative_double(key, value, config.performance.warmup_ms, error, path, line_number)) return false;
            } else if (key == "sample_ms") {
                if (!parse_nonnegative_double(key, value, config.performance.sample_ms, error, path, line_number) || config.performance.sample_ms <= 0.0) {
                    if (error.empty()) error = path.string() + ':' + std::to_string(line_number) + ": expected positive number for sample_ms";
                    return false;
                }
            } else if (key == "regression_percent") {
                if (!parse_nonnegative_double(key, value, config.performance.regression_percent, error, path, line_number)) return false;
            } else if (key == "min_samples") {
                if (!parse_min_samples(key, value, config.performance.min_samples, error, path, line_number)) return false;
            } else recognized = false;
        } else if (section == Section::Test && current_test) {
            if (key == "name") { if (!string_value(current_test->name)) return false; }
            else if (key == "command") { if (!string_value(current_test->command)) return false; }
            else if (key == "args") { if (!string_value(current_test->args)) return false; }
            else if (key == "cwd") { std::string v; if (!string_value(v)) return false; current_test->cwd = v; }
            else if (key == "enabled") {
                if (!parse_bool(value, current_test->enabled)) {
                    error = path.string() + ':' + std::to_string(line_number) + ": expected true or false for enabled"; return false;
                }
            } else if (is_visual_key(key)) {
                if (!parse_visual_key(key, value, current_test->visual, error, path, line_number)) return false;
                if (key == "capture") current_test->has_capture = true;
                else if (key == "baseline") current_test->has_baseline = true;
                else if (key == "pixel_threshold") current_test->has_pixel_threshold = true;
                else if (key == "max_changed_percent") current_test->has_max_changed_percent = true;
                else if (key == "warmup_frames") current_test->has_warmup_frames = true;
            } else if (key == "max_gpu_ms") {
                if (!parse_nonnegative_double(key, value, current_test->performance.max_gpu_ms, error, path, line_number)) return false;
                current_test->has_max_gpu_ms = true;
            } else if (key == "max_process_ms") {
                if (!parse_nonnegative_double(key, value, current_test->performance.max_process_ms, error, path, line_number)) return false;
                current_test->has_max_process_ms = true;
            } else if (key == "timeout_ms") {
                if (!parse_nonnegative_double(key, value, current_test->timeout_ms, error, path, line_number)) return false;
                current_test->has_timeout_ms = true;
            } else recognized = false;
        } else if (section == Section::Perf && current_perf) {
            if (key == "name") { if (!string_value(current_perf->name)) return false; }
            else if (key == "command") { if (!string_value(current_perf->command)) return false; }
            else if (key == "args") { if (!string_value(current_perf->args)) return false; }
            else if (key == "env") { if (!string_value(current_perf->env)) return false; }
            else if (key == "cwd") { std::string v; if (!string_value(v)) return false; current_perf->cwd = v; }
            else if (key == "enabled") {
                if (!parse_bool(value, current_perf->enabled)) {
                    error = path.string() + ':' + std::to_string(line_number) + ": expected true or false for enabled"; return false;
                }
            } else if (key == "warmup_ms") {
                if (!parse_nonnegative_double(key, value, current_perf->warmup_ms, error, path, line_number)) return false;
                current_perf->has_warmup_ms = true;
            } else if (key == "sample_ms") {
                if (!parse_nonnegative_double(key, value, current_perf->sample_ms, error, path, line_number) || current_perf->sample_ms <= 0.0) {
                    if (error.empty()) error = path.string() + ':' + std::to_string(line_number) + ": expected positive number for sample_ms";
                    return false;
                }
                current_perf->has_sample_ms = true;
            } else if (key == "regression_percent") {
                if (!parse_nonnegative_double(key, value, current_perf->regression_percent, error, path, line_number)) return false;
                current_perf->has_regression_percent = true;
            } else if (key == "min_samples") {
                if (!parse_min_samples(key, value, current_perf->min_samples, error, path, line_number)) return false;
                current_perf->has_min_samples = true;
            } else if (key == "timeout_ms") {
                if (!parse_nonnegative_double(key, value, current_perf->timeout_ms, error, path, line_number)) return false;
                current_perf->has_timeout_ms = true;
            } else recognized = false;
        }

        if (!recognized) {
            error = path.string() + ':' + std::to_string(line_number) + ": unknown key " + key;
            return false;
        }
    }

    if (!seen_project) { error = path.string() + ": missing [project] section"; return false; }
    if (config.project.command.empty()) { error = path.string() + ": [project].command is required"; return false; }

    std::unordered_set<std::string> names;
    for (std::size_t i = 0; i < config.tests.size(); ++i) {
        if (config.tests[i].name.empty()) {
            error = path.string() + ": test " + std::to_string(i + 1) + " is missing name";
            return false;
        }
        if (!names.insert(config.tests[i].name).second) {
            error = path.string() + ": duplicate test name \"" + config.tests[i].name + "\"";
            return false;
        }
    }

    names.clear();
    for (std::size_t i = 0; i < config.perf_cases.size(); ++i) {
        if (config.perf_cases[i].name.empty()) {
            error = path.string() + ": perf case " + std::to_string(i + 1) + " is missing name";
            return false;
        }
        if (!names.insert(config.perf_cases[i].name).second) {
            error = path.string() + ": duplicate perf case name \"" + config.perf_cases[i].name + "\"";
            return false;
        }
    }
    return true;
}

} // namespace rendercheck
