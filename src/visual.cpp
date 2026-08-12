#include "rendercheck/visual.h"
#include "rendercheck/image.h"

#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace rendercheck {
namespace {

struct SelectedTest {
    std::string name;
    const TestConfig* test = nullptr;
};

std::vector<SelectedTest> select_visual_tests(const Config& config, std::string_view filter) {
    std::vector<SelectedTest> selected;

    if (config.tests.empty()) {
        const std::string name = config.project.name.empty() ? "project" : config.project.name;
        if ((filter.empty() || filter == name) && config.project.visual.capture) {
            selected.push_back({name, nullptr});
        }
        return selected;
    }

    for (const auto& test : config.tests) {
        if (!test.enabled || !test.visual.capture) continue;
        if (!filter.empty() && filter != test.name) continue;
        selected.push_back({test.name, &test});
    }
    return selected;
}

void print_metrics(const VisualResult& result) {
    std::cout << std::fixed << std::setprecision(3)
              << "  changed: " << result.changed_percent << "% ("
              << result.changed_pixels << '/' << result.total_pixels << " pixels)\n"
              << "  rmse: " << result.rmse << "\n"
              << "  max channel delta: " << static_cast<unsigned>(result.max_channel_delta) << "\n";
}

std::uint32_t fnv1a(std::string_view value) {
    std::uint32_t hash = 2166136261u;
    for (const unsigned char c : value) {
        hash ^= c;
        hash *= 16777619u;
    }
    return hash;
}

} // namespace

std::string safe_test_name(std::string_view name) {
    if (name.empty()) return "project";

    std::string result;
    result.reserve(name.size() + 9);
    bool changed = false;

    for (const char c : name) {
        const bool safe = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                          (c >= '0' && c <= '9') || c == '-' || c == '_';
        result.push_back(safe ? c : '_');
        changed = changed || !safe;
    }

    if (changed) {
        std::ostringstream suffix;
        suffix << '-' << std::hex << std::setw(8) << std::setfill('0') << fnv1a(name);
        result += suffix.str();
    }

    return result;
}

fs::path capture_output_dir(std::string_view name) {
    return fs::path(".rendercheck") / safe_test_name(name);
}

fs::path capture_path(std::string_view name) {
    return capture_output_dir(name) / "actual.ppm";
}

const VisualConfig& visual_config(const Config& config, const TestConfig* test) {
    return test ? test->visual : config.project.visual;
}

fs::path baseline_path(const Config& config, std::string_view name, const TestConfig* test) {
    const VisualConfig& visual = visual_config(config, test);
    if (!visual.baseline.empty()) return visual.baseline;
    return config.project.baseline_dir / (safe_test_name(name) + ".ppm");
}

bool evaluate_capture(const Config& config,
                      std::string_view name,
                      const TestConfig* test,
                      VisualResult& result,
                      std::string& error) {
    result = {};
    result.actual_path = capture_path(name);
    result.baseline_path = baseline_path(config, name, test);
    result.diff_path = capture_output_dir(name) / "diff.ppm";

    const VisualConfig& visual = visual_config(config, test);
    if (!visual.capture) {
        error = "visual capture is disabled";
        return false;
    }

    Image actual;
    if (!load_ppm(result.actual_path, actual, error)) return false;
    result.width = actual.width;
    result.height = actual.height;

    if (!fs::exists(result.baseline_path)) {
        result.baseline_missing = true;
        error = "baseline missing: " + result.baseline_path.string();
        return false;
    }

    Image baseline;
    if (!load_ppm(result.baseline_path, baseline, error)) return false;

    ImageDiff metrics;
    Image diff;
    if (!compare_images(baseline,
                        actual,
                        static_cast<std::uint8_t>(visual.pixel_threshold),
                        metrics,
                        diff,
                        error)) {
        return false;
    }

    result.changed_pixels = metrics.changed_pixels;
    result.total_pixels = metrics.total_pixels;
    result.changed_percent = metrics.changed_percent;
    result.rmse = metrics.rmse;
    result.max_channel_delta = metrics.max_channel_delta;
    result.passed = result.changed_percent <= visual.max_changed_percent;

    std::error_code ec;
    fs::remove(result.diff_path, ec);
    if (!result.passed && result.changed_pixels != 0) {
        if (!save_ppm(result.diff_path, diff, error)) return false;
    }

    return true;
}

int diff_captures(std::string_view filter) {
    Config config;
    std::string error;
    if (!load_config("rendercheck.toml", config, error)) {
        std::cerr << "rendercheck: " << error << '\n';
        return 2;
    }

    const auto selected = select_visual_tests(config, filter);
    if (selected.empty()) {
        if (!filter.empty()) std::cerr << "rendercheck: no enabled capture test named '" << filter << "'\n";
        else std::cerr << "rendercheck: no enabled visual capture tests\n";
        return 2;
    }

    std::cout << "RendererCheck diff\n\n";
    std::size_t passed = 0;
    std::size_t failed = 0;

    for (const auto& selected_test : selected) {
        std::cout << selected_test.name << '\n';
        VisualResult result;
        if (!evaluate_capture(config, selected_test.name, selected_test.test, result, error)) {
            std::cout << "  [fail] " << error << "\n\n";
            ++failed;
            continue;
        }

        print_metrics(result);
        if (result.passed) {
            std::cout << "  [ok] image matches threshold\n\n";
            ++passed;
        } else {
            std::cout << "  [fail] visual regression\n"
                      << "  diff: " << result.diff_path.string() << "\n\n";
            ++failed;
        }
    }

    std::cout << "Summary: " << passed << " passed, " << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}

int approve_captures(std::string_view filter) {
    Config config;
    std::string error;
    if (!load_config("rendercheck.toml", config, error)) {
        std::cerr << "rendercheck: " << error << '\n';
        return 2;
    }

    const auto selected = select_visual_tests(config, filter);
    if (selected.empty()) {
        if (!filter.empty()) std::cerr << "rendercheck: no enabled capture test named '" << filter << "'\n";
        else std::cerr << "rendercheck: no enabled visual capture tests\n";
        return 2;
    }

    std::cout << "RendererCheck approve\n\n";
    std::size_t approved = 0;
    std::size_t failed = 0;

    for (const auto& selected_test : selected) {
        const fs::path actual = capture_path(selected_test.name);
        const fs::path baseline = baseline_path(config, selected_test.name, selected_test.test);

        Image image;
        if (!load_ppm(actual, image, error)) {
            std::cout << selected_test.name << "\n  [fail] " << error << "\n\n";
            ++failed;
            continue;
        }

        if (!save_ppm(baseline, image, error)) {
            std::cout << selected_test.name << "\n  [fail] " << error << "\n\n";
            ++failed;
            continue;
        }

        std::cout << selected_test.name << "\n"
                  << "  [ok] approved " << image.width << 'x' << image.height << " baseline\n"
                  << "  baseline: " << baseline.string() << "\n\n";
        ++approved;
    }

    std::cout << "Summary: " << approved << " approved, " << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}

} // namespace rendercheck
