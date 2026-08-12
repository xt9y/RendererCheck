#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace rendercheck {

struct VisualConfig {
    bool capture = false;
    std::filesystem::path baseline;
    std::uint32_t pixel_threshold = 0;
    double max_changed_percent = 0.0;
};

struct ValidationConfig {
    bool vulkan = false;
    bool fail_on_warning = false;
    bool fail_on_error = true;
};

struct PerformanceConfig {
    double max_gpu_ms = 0.0;
    double max_process_ms = 0.0;
};

struct ProjectConfig {
    std::string name = "renderer";
    std::string command;
    std::filesystem::path cwd = ".";
    std::filesystem::path baseline_dir = "rendercheck/baselines";
    VisualConfig visual;
};

struct TestConfig {
    std::string name;
    std::string command;
    std::string args;
    std::filesystem::path cwd;
    bool enabled = true;
    VisualConfig visual;
    PerformanceConfig performance;
    bool has_max_gpu_ms = false;
    bool has_max_process_ms = false;
};

struct Config {
    ProjectConfig project;
    ValidationConfig validation;
    PerformanceConfig performance;
    std::vector<TestConfig> tests;
};

bool load_config(const std::filesystem::path& path, Config& config, std::string& error);

} // namespace rendercheck
