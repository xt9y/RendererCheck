#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace rendercheck {

enum class HeadlessMode { Auto, Xvfb, None };
enum class RendererMode { Auto, Hardware, Software };

struct VisualConfig {
    bool capture = false;
    std::filesystem::path baseline;
    std::uint32_t pixel_threshold = 0;
    double max_changed_percent = 0.0;
    std::uint32_t warmup_frames = 0;
};

struct ValidationConfig {
    bool vulkan = false;
    bool fail_on_warning = false;
    bool fail_on_error = true;
};

struct PerformanceConfig {
    double max_gpu_ms = 0.0;
    double max_process_ms = 0.0;
    double warmup_ms = 1000.0;
    double sample_ms = 3000.0;
    double regression_percent = 15.0;
    std::uint32_t min_samples = 3;
};

struct ProjectConfig {
    std::string name = "renderer";
    std::string command;
    std::filesystem::path cwd = ".";
    std::filesystem::path baseline_dir = "rendercheck/baselines";
    VisualConfig visual;
    HeadlessMode headless = HeadlessMode::Auto;
    RendererMode renderer = RendererMode::Auto;
    double timeout_ms = 30000.0;
};

struct TestConfig {
    std::string name;
    std::string command;
    std::string args;
    std::filesystem::path cwd;
    bool enabled = true;

    VisualConfig visual;
    bool has_capture = false;
    bool has_baseline = false;
    bool has_pixel_threshold = false;
    bool has_max_changed_percent = false;
    bool has_warmup_frames = false;

    PerformanceConfig performance;
    bool has_max_gpu_ms = false;
    bool has_max_process_ms = false;

    double timeout_ms = 0.0;
    bool has_timeout_ms = false;
};

struct PerfCaseConfig {
    std::string name;
    std::string command;
    std::string args;
    std::string env;
    std::filesystem::path cwd;
    bool enabled = true;

    double warmup_ms = 0.0;
    double sample_ms = 0.0;
    double regression_percent = 0.0;
    std::uint32_t min_samples = 0;
    double timeout_ms = 0.0;

    bool has_warmup_ms = false;
    bool has_sample_ms = false;
    bool has_regression_percent = false;
    bool has_min_samples = false;
    bool has_timeout_ms = false;
};

struct Config {
    ProjectConfig project;
    ValidationConfig validation;
    PerformanceConfig performance;
    std::vector<TestConfig> tests;
    std::vector<PerfCaseConfig> perf_cases;
};

bool load_config(const std::filesystem::path& path, Config& config, std::string& error);
const char* headless_mode_name(HeadlessMode mode);
const char* renderer_mode_name(RendererMode mode);

} // namespace rendercheck
