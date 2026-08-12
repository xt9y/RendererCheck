#include "rendercheck/config.h"

#include <cerrno>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <string>
#include <string_view>

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
        if (escaped) {
            escaped = false;
            continue;
        }
        if (c == '\\' && quoted) {
            escaped = true;
            continue;
        }
        if (c == '"') {
            quoted = !quoted;
            continue;
        }
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
    if (v == "true") {
        out = true;
        return true;
    }
    if (v == "false") {
        out = false;
        return true;
    }
    return false;
}

bool parse_uint(std::string_view value, std::uint32_t& out) {
    const std::string v = trim(value);
    if (v.empty() || v.front() == '-') return false;

    char* end = nullptr;
    errno = 0;
    const unsigned long parsed = std::strtoul(v.c_str(), &end, 10);
    if (errno != 0 || end != v.c_str() + v.size() || parsed > std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }
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
        error = path.string() + ':' + std::to_string(line_number) +
                ": expected non-negative number for " + key;
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
        if (!parse_uint(value, visual.pixel_threshold) || visual.pixel_threshold > 255) {
            return fail("integer from 0 to 255");
        }
        return true;
    }
    if (key == "max_changed_percent") {
        if (!parse_double(value, visual.max_changed_percent) ||
            visual.max_changed_percent < 0.0 || visual.max_changed_percent > 100.0) {
            return fail("number from 0 to 100");
        }
        return true;
    }
    return false;
}

} // namespace

bool load_config(const std::filesystem::path& path, Config& config, std::string& error) {
    config = {};

    std::ifstream in(path);
    if (!in) {
        error = "could not open " + path.string();
        return false;
    }

    enum class Section { None, Project, Validation, Performance, Test, Other };
    Section section = Section::None;
    TestConfig* current_test = nullptr;

    std::string raw;
    std::size_t line_number = 0;

    while (std::getline(in, raw)) {
        ++line_number;
        const std::string line = trim(strip_comment(raw));
        if (line.empty()) continue;

        if (line == "[project]") {
            section = Section::Project;
            current_test = nullptr;
            continue;
        }
        if (line == "[validation]") {
            section = Section::Validation;
            current_test = nullptr;
            continue;
        }
        if (line == "[performance]") {
            section = Section::Performance;
            current_test = nullptr;
            continue;
        }
        if (line == "[[test]]") {
            config.tests.emplace_back();
            current_test = &config.tests.back();
            section = Section::Test;
            continue;
        }
        if (line.front() == '[' && line.back() == ']') {
            section = Section::Other;
            current_test = nullptr;
            continue;
        }

        const std::size_t eq = line.find('=');
        if (eq == std::string::npos) {
            error = path.string() + ':' + std::to_string(line_number) + ": expected key = value";
            return false;
        }

        const std::string key = trim(std::string_view(line).substr(0, eq));
        const std::string_view value = std::string_view(line).substr(eq + 1);

        auto string_value = [&](std::string& destination) {
            if (!parse_string(value, destination)) {
                error = path.string() + ':' + std::to_string(line_number) + ": expected quoted string for " + key;
                return false;
            }
            return true;
        };

        if (section == Section::Project) {
            if (key == "name") {
                if (!string_value(config.project.name)) return false;
            } else if (key == "command") {
                if (!string_value(config.project.command)) return false;
            } else if (key == "cwd") {
                std::string cwd;
                if (!string_value(cwd)) return false;
                config.project.cwd = cwd;
            } else if (key == "baseline_dir") {
                std::string baseline_dir;
                if (!string_value(baseline_dir)) return false;
                config.project.baseline_dir = baseline_dir;
            } else if (key == "capture" || key == "baseline" || key == "pixel_threshold" ||
                       key == "max_changed_percent") {
                if (!parse_visual_key(key, value, config.project.visual, error, path, line_number)) return false;
            }
        } else if (section == Section::Validation) {
            if (key == "vulkan") {
                if (!parse_bool(value, config.validation.vulkan)) {
                    error = path.string() + ':' + std::to_string(line_number) + ": expected true or false for vulkan";
                    return false;
                }
            } else if (key == "fail_on_warning") {
                if (!parse_bool(value, config.validation.fail_on_warning)) {
                    error = path.string() + ':' + std::to_string(line_number) + ": expected true or false for fail_on_warning";
                    return false;
                }
            } else if (key == "fail_on_error") {
                if (!parse_bool(value, config.validation.fail_on_error)) {
                    error = path.string() + ':' + std::to_string(line_number) + ": expected true or false for fail_on_error";
                    return false;
                }
            }
        } else if (section == Section::Performance) {
            if (key == "max_gpu_ms") {
                if (!parse_nonnegative_double(key, value, config.performance.max_gpu_ms, error, path, line_number)) return false;
            } else if (key == "max_process_ms") {
                if (!parse_nonnegative_double(key, value, config.performance.max_process_ms, error, path, line_number)) return false;
            }
        } else if (section == Section::Test && current_test) {
            if (key == "name") {
                if (!string_value(current_test->name)) return false;
            } else if (key == "command") {
                if (!string_value(current_test->command)) return false;
            } else if (key == "args") {
                if (!string_value(current_test->args)) return false;
            } else if (key == "cwd") {
                std::string cwd;
                if (!string_value(cwd)) return false;
                current_test->cwd = cwd;
            } else if (key == "enabled") {
                if (!parse_bool(value, current_test->enabled)) {
                    error = path.string() + ':' + std::to_string(line_number) + ": expected true or false for enabled";
                    return false;
                }
            } else if (key == "capture" || key == "baseline" || key == "pixel_threshold" ||
                       key == "max_changed_percent") {
                if (!parse_visual_key(key, value, current_test->visual, error, path, line_number)) return false;
            } else if (key == "max_gpu_ms") {
                if (!parse_nonnegative_double(key, value, current_test->performance.max_gpu_ms, error, path, line_number)) return false;
                current_test->has_max_gpu_ms = true;
            } else if (key == "max_process_ms") {
                if (!parse_nonnegative_double(key, value, current_test->performance.max_process_ms, error, path, line_number)) return false;
                current_test->has_max_process_ms = true;
            }
        }
    }

    if (config.project.command.empty()) {
        error = path.string() + ": [project].command is required";
        return false;
    }

    for (std::size_t i = 0; i < config.tests.size(); ++i) {
        if (config.tests[i].name.empty()) {
            error = path.string() + ": test " + std::to_string(i + 1) + " is missing name";
            return false;
        }
    }

    return true;
}

} // namespace rendercheck
