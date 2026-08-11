#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace rendercheck {

struct ProjectConfig {
    std::string name = "renderer";
    std::string command;
    std::filesystem::path cwd = ".";
};

struct TestConfig {
    std::string name;
    std::string command;
    std::string args;
    std::filesystem::path cwd;
    bool enabled = true;
};

struct Config {
    ProjectConfig project;
    std::vector<TestConfig> tests;
};

bool load_config(const std::filesystem::path& path, Config& config, std::string& error);

} // namespace rendercheck
