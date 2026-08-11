#include "rendercheck/run.h"
#include "rendercheck/config.h"
#include "rendercheck/visual.h"

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#if defined(__APPLE__) || defined(__linux__)
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace rendercheck {
namespace {

struct RunResult {
    int exit_code = 1;
    double milliseconds = 0.0;
};

RunResult run_process(const std::string& command,
                      const fs::path& cwd,
                      std::string_view test_name,
                      const fs::path& output_dir,
                      bool capture_enabled) {
    RunResult result;
    const auto started = std::chrono::steady_clock::now();

#if defined(__APPLE__) || defined(__linux__)
    const pid_t pid = fork();
    if (pid < 0) {
        std::cerr << "  [fail] could not fork renderer process\n";
        return result;
    }

    if (pid == 0) {
        const std::string test(test_name);
        const std::string output = fs::absolute(output_dir).string();

        ::setenv("RENDERCHECK", "1", 1);
        ::setenv("RENDERCHECK_TEST", test.c_str(), 1);
        ::setenv("RENDERCHECK_OUTPUT_DIR", output.c_str(), 1);

        if (capture_enabled) {
            const std::string capture = fs::absolute(rendercheck::capture_path(test_name)).string();
            ::setenv("RENDERCHECK_CAPTURE_PATH", capture.c_str(), 1);
            ::setenv("RENDERCHECK_CAPTURE_FORMAT", "ppm-rgb8", 1);
        } else {
            ::unsetenv("RENDERCHECK_CAPTURE_PATH");
            ::unsetenv("RENDERCHECK_CAPTURE_FORMAT");
        }

        if (::chdir(cwd.c_str()) != 0) {
            std::perror("rendercheck: chdir");
            _exit(126);
        }

        execl("/bin/sh", "sh", "-c", command.c_str(), static_cast<char*>(nullptr));
        std::perror("rendercheck: exec");
        _exit(127);
    }

    int status = 0;
    for (;;) {
        const pid_t waited = waitpid(pid, &status, 0);
        if (waited == pid) break;
        if (waited < 0 && errno == EINTR) continue;
        std::perror("rendercheck: waitpid");
        return result;
    }

    if (WIFEXITED(status)) {
        result.exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        result.exit_code = 128 + WTERMSIG(status);
    }
#else
    (void)command;
    (void)cwd;
    (void)test_name;
    (void)output_dir;
    (void)capture_enabled;
    std::cerr << "  [fail] process execution is unsupported on this platform\n";
#endif

    const auto finished = std::chrono::steady_clock::now();
    result.milliseconds = std::chrono::duration<double, std::milli>(finished - started).count();
    return result;
}

std::string build_command(const Config& config, const TestConfig* test) {
    std::string command = test && !test->command.empty() ? test->command : config.project.command;
    if (test && !test->args.empty()) {
        command += ' ';
        command += test->args;
    }
    return command;
}

fs::path working_directory(const Config& config, const TestConfig* test) {
    if (test && !test->cwd.empty()) return test->cwd;
    return config.project.cwd;
}

} // namespace

int run_tests(std::string_view filter) {
    Config config;
    std::string error;
    if (!load_config("rendercheck.toml", config, error)) {
        std::cerr << "rendercheck: " << error << '\n';
        return 2;
    }

    struct SelectedTest {
        std::string name;
        const TestConfig* test;
    };

    std::vector<SelectedTest> selected;
    if (config.tests.empty()) {
        const std::string name = config.project.name.empty() ? "project" : config.project.name;
        if (filter.empty() || filter == name) selected.push_back({name, nullptr});
    } else {
        for (const auto& test : config.tests) {
            if (!test.enabled) continue;
            if (!filter.empty() && filter != test.name) continue;
            selected.push_back({test.name, &test});
        }
    }

    if (selected.empty()) {
        if (!filter.empty()) std::cerr << "rendercheck: no enabled test named '" << filter << "'\n";
        else std::cerr << "rendercheck: no enabled tests\n";
        return 2;
    }

    std::cout << "RendererCheck run\n\n";

    std::size_t passed = 0;
    std::size_t failed = 0;

    for (const auto& selected_test : selected) {
        const fs::path output_dir = capture_output_dir(selected_test.name);
        std::error_code ec;
        fs::create_directories(output_dir, ec);
        if (ec) {
            std::cerr << selected_test.name << "\n  [fail] could not create " << output_dir.string() << "\n\n";
            ++failed;
            continue;
        }

        const VisualConfig& visual = visual_config(config, selected_test.test);
        if (visual.capture) {
            fs::remove(capture_path(selected_test.name), ec);
            ec.clear();
            fs::remove(output_dir / "diff.ppm", ec);
        }

        const std::string command = build_command(config, selected_test.test);
        const fs::path cwd = working_directory(config, selected_test.test);

        std::cout << selected_test.name << '\n';
        std::cout << "  command: " << command << '\n';

        const RunResult result = run_process(command, cwd, selected_test.name, output_dir, visual.capture);
        if (result.exit_code != 0) {
            std::cout << "  [fail] process exited " << result.exit_code
                      << " (" << static_cast<long long>(result.milliseconds) << " ms)\n\n";
            ++failed;
            continue;
        }

        std::cout << "  [ok] process (" << static_cast<long long>(result.milliseconds) << " ms)\n";

        if (!visual.capture) {
            std::cout << '\n';
            ++passed;
            continue;
        }

        VisualResult visual_result;
        if (!evaluate_capture(config, selected_test.name, selected_test.test, visual_result, error)) {
            if (visual_result.baseline_missing) {
                std::cout << "  [fail] " << error << '\n'
                          << "  actual: " << visual_result.actual_path.string() << '\n'
                          << "  approve: rendercheck approve " << selected_test.name << "\n\n";
            } else {
                std::cout << "  [fail] image: " << error << "\n\n";
            }
            ++failed;
            continue;
        }

        std::cout << std::fixed << std::setprecision(3)
                  << "  capture: " << visual_result.width << 'x' << visual_result.height << " PPM\n"
                  << "  changed: " << visual_result.changed_percent << "% ("
                  << visual_result.changed_pixels << '/' << visual_result.total_pixels << " pixels)\n"
                  << "  rmse: " << visual_result.rmse << '\n';

        if (!visual_result.passed) {
            std::cout << "  [fail] visual regression\n"
                      << "  diff: " << visual_result.diff_path.string() << "\n\n";
            ++failed;
            continue;
        }

        std::cout << "  [ok] image matches baseline\n\n";
        ++passed;
    }

    std::cout << "Summary: " << passed << " passed, " << failed << " failed\n";
    return failed == 0 ? 0 : 1;
}

} // namespace rendercheck
