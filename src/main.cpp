#include "rendercheck/doctor.h"
#include "rendercheck/run.h"
#include "rendercheck/version.h"

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
        "  rendercheck run [test]          execute renderer tests\n"
        "  rendercheck version             print version\n"
        "  rendercheck help                show this help\n\n"
        "Planned:\n"
        "  rendercheck diff                compare captured frames\n"
        "  rendercheck approve             accept a new baseline\n";
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
        "cwd = \".\"\n\n"
        "[validation]\n"
        "vulkan = true\n"
        "fail_on_warning = false\n\n"
        "# Add one or more tests to run the project command with test-specific arguments.\n"
        "# [[test]]\n"
        "# name = \"triangle\"\n"
        "# args = \"--scene tests/triangle.scene --headless\"\n";

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

    if (command == "run") {
        const std::string_view filter = argc >= 3 ? std::string_view(argv[2]) : std::string_view{};
        return rendercheck::run_tests(filter);
    }

    std::cerr << "unknown command: " << command << "\n\n";
    print_help();
    return 2;
}
