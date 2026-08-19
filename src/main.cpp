#include "rendercheck/doctor.h"
#include "rendercheck/run.h"
#include "rendercheck/version.h"
#include "rendercheck/visual.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>

namespace fs = std::filesystem;

namespace {

void print_help() {
    std::cout <<
        "RendererCheck — graphics CI for native renderers\n\n"
        "Usage:\n"
        "  renderercheck init                create rendercheck.toml\n"
        "  renderercheck doctor [--verbose]  inspect the local graphics environment\n"
        "  renderercheck run [test]          execute renderer tests and checks\n"
        "  renderercheck diff [test]         compare the latest capture with its baseline\n"
        "  renderercheck approve [test]      accept the latest capture as baseline\n"
        "  renderercheck version             print version\n"
        "  renderercheck help                show this help\n";
}

void clear_run_reports() {
    std::error_code ec;
    fs::create_directories(".rendercheck", ec);
    ec.clear();
    fs::remove(".rendercheck/report.md", ec);
    ec.clear();
    fs::remove(".rendercheck/results.json", ec);
}

int init_project() {
    const fs::path config = "rendercheck.toml";
    if (fs::exists(config)) { std::cerr << "rendercheck.toml already exists\n"; return 1; }
    std::ofstream out(config);
    if (!out) { std::cerr << "could not create rendercheck.toml\n"; return 1; }
    out <<
        "[project]\n"
        "name = \"renderer\"\n"
        "command = \"./build/app\"\n"
        "cwd = \".\"\n"
        "baseline_dir = \"rendercheck/baselines\"\n"
        "headless = \"auto\"\n"
        "renderer = \"auto\"\n"
        "timeout_ms = 30000.0\n\n"
        "[validation]\n"
        "# Enable this only for Vulkan projects.\n"
        "vulkan = false\n"
        "fail_on_error = true\n"
        "fail_on_warning = false\n\n"
        "[performance]\n"
        "# max_gpu_ms = 16.67\n"
        "# max_process_ms = 1000.0\n\n"
        "# [[test]]\n"
        "# name = \"triangle\"\n"
        "# args = \"--scene tests/triangle.scene\"\n"
        "# capture = true\n"
        "# warmup_frames = 30\n"
        "# pixel_threshold = 0\n"
        "# max_changed_percent = 0.0\n"
        "# max_gpu_ms = 16.67\n"
        "# timeout_ms = 30000.0\n";
    std::cout << "created rendercheck.toml\n";
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) { print_help(); return 0; }
    const std::string_view command = argv[1];
    if (command == "help" || command == "--help" || command == "-h") { print_help(); return 0; }
    if (command == "version" || command == "--version" || command == "-v") {
        std::cout << "RendererCheck " << RENDERCHECK_VERSION << '\n'; return 0;
    }
    if (command == "doctor") {
        if (argc > 3 || (argc == 3 && std::string_view(argv[2]) != "--verbose")) {
            std::cerr << "renderercheck: doctor accepts only --verbose\n"; return 2;
        }
        return rendercheck::run_doctor(argc == 3);
    }
    if (command == "init") {
        if (argc != 2) { std::cerr << "renderercheck: init takes no arguments\n"; return 2; }
        return init_project();
    }
    if (argc > 3) { std::cerr << "renderercheck: too many arguments\n"; return 2; }
    const std::string_view filter = argc == 3 ? std::string_view(argv[2]) : std::string_view{};
    if (command == "run") {
        clear_run_reports();
        return rendercheck::run_tests(filter);
    }
    if (command == "diff") return rendercheck::diff_captures(filter);
    if (command == "approve") return rendercheck::approve_captures(filter);
    std::cerr << "unknown command: " << command << "\n\n";
    print_help();
    return 2;
}
