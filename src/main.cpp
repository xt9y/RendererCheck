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
        "  rendercheck init                create rendercheck.toml\n"
        "  rendercheck doctor [--verbose]  inspect the local graphics environment\n"
        "  rendercheck run [test]          execute renderer tests and checks\n"
        "  rendercheck diff [test]         compare the latest capture with its baseline\n"
        "  rendercheck approve [test]      accept the latest capture as baseline\n"
        "  rendercheck version             print version\n"
        "  rendercheck help                show this help\n";
}

int init_project() {
    const fs::path config = "rendercheck.toml";
    if (fs::exists(config)) {
        std::cerr << "rendercheck.toml already exists\n";
        return 1;
    }

    std::ofstream out(config);
    if (!out) {
        std::cerr << "could not create rendercheck.toml\n";
        return 1;
    }

    out <<
        "[project]\n"
        "name = \"renderer\"\n"
        "command = \"./build/app\"\n"
        "cwd = \".\"\n"
        "baseline_dir = \"rendercheck/baselines\"\n\n"
        "[validation]\n"
        "vulkan = true\n"
        "fail_on_error = true\n"
        "fail_on_warning = false\n\n"
        "[performance]\n"
        "# max_gpu_ms = 16.67\n"
        "# max_process_ms = 1000.0\n\n"
        "# Add one or more tests to run the project command with test-specific arguments.\n"
        "# Set capture = true when the renderer writes RENDERCHECK_CAPTURE_PATH.\n"
        "# Report GPU timings with rendercheck_gpu_ms() from <rendercheck/metrics.h>.\n"
        "# [[test]]\n"
        "# name = \"triangle\"\n"
        "# args = \"--scene tests/triangle.scene --headless\"\n"
        "# capture = true\n"
        "# pixel_threshold = 0\n"
        "# max_changed_percent = 0.0\n"
        "# max_gpu_ms = 16.67\n";

    std::cout << "created rendercheck.toml\n";
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        print_help();
        return 0;
    }

    const std::string_view command = argv[1];

    if (command == "help" || command == "--help" || command == "-h") {
        print_help();
        return 0;
    }

    if (command == "version" || command == "--version" || command == "-v") {
        std::cout << "RendererCheck " << RENDERCHECK_VERSION << '\n';
        return 0;
    }

    if (command == "doctor") {
        const bool verbose = argc >= 3 && std::string_view(argv[2]) == "--verbose";
        return rendercheck::run_doctor(verbose);
    }

    if (command == "init") return init_project();

    const std::string_view filter = argc >= 3 ? std::string_view(argv[2]) : std::string_view{};

    if (command == "run") return rendercheck::run_tests(filter);
    if (command == "diff") return rendercheck::diff_captures(filter);
    if (command == "approve") return rendercheck::approve_captures(filter);

    std::cerr << "unknown command: " << command << "\n\n";
    print_help();
    return 2;
}
