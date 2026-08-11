#include "rendercheck/config.h"

#include <cctype>
#include <fstream>
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

} // namespace

bool load_config(const std::filesystem::path& path, Config& config, std::string& error) {
    std::ifstream in(path);
    if (!in) {
        error = "could not open " + path.string();
        return false;
    }

    enum class Section { None, Project, Test, Other };
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
